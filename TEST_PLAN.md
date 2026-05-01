# Integration Test Plan — CoroutineServer

## Test Environment

| Item | Value |
|------|-------|
| Server | `CoroutineServer.exe` (port 8080) |
| Client | `TestClient.exe` (Windows CLI) |
| DB | `im.db` (SQLite, auto-created) |
| Protocol | Binary header (4B) + JSON body |

**Preparation:** Delete `im.db` before each test round to start clean.

---

## Test 1: Concurrent Registration and Login

### 1.1 Concurrent Registration (same username)
**Purpose:** Verify DB handles collision under concurrent access.

| Step | Client A | Client B | Expected |
|------|----------|----------|----------|
| 1 | `/register alice pass1` | — | `[成功] register success` |
| 2 | — | `/register alice pass2` | `[失败] register failed (username may exist)` |

### 1.2 Concurrent Login (same user, multi-device)
**Purpose:** Verify re-login kicks old session.

| Step | Client A | Client B | Expected |
|------|----------|----------|----------|
| 1 | `/login alice pass1` | — | A gets `uid=X` |
| 2 | — | `/login alice pass1` | B gets `uid=X`; server log shows "检测到重复登录, 关闭旧会话" |
| 3 | `/friends` (A) | — | Client A detects disconnect (`g_connected=false`) |

### 1.3 Re-login after disconnect
**Purpose:** Verify `/reconnect` restores login state + pulls offline messages.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Register: alice + bob | Both success |
| 2 | Login alice | uid=1 |
| 3 | Login bob | uid=2 |
| 4 | alice `/friend add 2` | success |
| 5 | bob `/friend add 1` | success |
| 6 | Kill bob client | Bob disconnects |
| 7 | alice `/chat 2 hello bob offline` | `[成功] msg saved` |
| 8 | Re-run bob client, `/login bob pass` | Login success, `offline_count=1`; auto-pushed message appears |
| 9 | (or use `/reconnect`) | Same result: offline message auto-delivered |

---

## Test 2: Real-time Chat (Friend Online)

### 2.1 Two online users chat
**Purpose:** Verify push delivery when recipient is online.

| Step | Client A (alice) | Client B (bob) | Expected |
|------|-------------------|------------------|----------|
| 1 | `/register alice a` | `/register bob b` | Both success |
| 2 | `/login alice a` | `/login bob b` | uid=1, uid=2 |
| 3 | `/friend add 2` | — | success |
| 4 | — | `/friend add 1` | success |
| 5 | `/chat 2 Hi Bob!` | — | A gets `[成功]`; B sees `[推送-聊天] from=1` |
| 6 | — | `/chat 1 Hi Alice!` | B gets `[成功]`; A sees `[推送-聊天] from=2` |

### 2.2 Multiple messages in queue
| Step | Action | Expected |
|------|--------|----------|
| 1 | A sends 10 rapid messages to B | All queued, all delivered in order |

### 2.3 Invalid recipient
| Step | Action | Expected |
|------|--------|----------|
| 1 | `/chat 999 nobody` | `[失败] invalid params` or DB error |

---

## Test 3: Offline Message Storage and Retrieval

### 3.1 Message stored when friend offline
**Purpose:** Verify messages persist and are delivered on next login.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Setup: alice(1) + bob(2) friends | — |
| 2 | Bob offline, alice sends `/chat 2 hello while offline` | msg saved, `delivered=0` |
| 3 | Alice sends `/chat 2 second message` | msg saved |
| 4 | Bob logs in | Login response shows `offline_count=2`; auto-pushed offline messages printed |
| 5 | (or Bob uses `/offline`) | Gets 2 messages; server marks `delivered=1` |

### 3.2 Pull offline after all consumed
| Step | Action | Expected |
|------|--------|----------|
| 1 | Bob `/offline` | `[成功] 0 条` (no more offline) |

### 3.3 Repeat login does not re-deliver
| Step | Action | Expected |
|------|--------|----------|
| 1 | Re-run bob client, `/login bob b` | `offline_count=0` (messages already marked delivered) |

---

## Test 4: Connection Loss and Auto Cleanup

### 4.1 Client crash → Online list cleanup
**Purpose:** Verify `UserManager` removes session and `CServer` clears session map.

| Step | Action | Expected Server Behavior |
|------|--------|--------------------------|
| 1 | Alice + Bob online, friends | — |
| 2 | Kill Alice client (Ctrl+C / task kill) | Server log: "对端关闭连接" → `[CSession] Close 开始` → `[UserManager] 用户下线: uid=X` → `[CServer] 清除会话` |
| 3 | Bob `/chat 1 are you there?` | `[成功] msg saved` (recipient offline) |

### 4.2 Server crash → Client detects disconnect
| Step | Action | Expected |
|------|--------|----------|
| 1 | Alice logged in | Normal operation |
| 2 | Kill server | Client shows "接收异常" / "已断线" |
| 3 | Client enters reconnect prompt | Prompts `/reconnect` or `/quit` |
| 4 | Restart server | — |
| 5 | Client `/reconnect` | Reconnects, auto-re-login with stored credentials, offline messages delivered |

### 4.3 Graceful logout (/quit) → Cleanup
| Step | Action | Expected |
|------|--------|----------|
| 1 | Alice `/login alice a` | success |
| 2 | Alice `/quit` | Client sends FIN, server detects disconnect, cleanup triggered |

---

## Test 5: Concurrency and Stability

### 5.1 High message throughput
**Purpose:** Verify no crashes under sustained load.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Setup: 2 friends online | — |
| 2 | Send 100 messages rapidly | All delivered, no drops, no crashes |
| 3 | Check DB: `SELECT COUNT(*) FROM messages` | 100 rows |

### 5.2 Send queue overflow protection
| Step | Action | Expected |
|------|--------|----------|
| 1 | Server under load, send queue fills | Server drops messages with log "发送队列已满, 丢弃消息" |

### 5.3 Logic queue overflow protection
| Step | Action | Expected |
|------|--------|----------|
| 1 | Flood server with >10000 messages rapid | Server drops messages with log "消息队列已满(MAX_RECVQUE=10000), 丢弃消息" |

---

## Bug Fixes Verified by These Tests

| Bug | Fixed? | Verified by |
|-----|--------|-------------|
| `MsgNode` raw `new[]`/`delete[]` leak on exception | Yes (now `std::vector<char>`) | Any message path |
| `_login_uid` data race (LogicSystem ↔ io_context threads) | Yes (added `_login_mutex`) | Test 1.2 (re-login) |
| `msg_id > MAX_LENGTH` semantic validation bug | Yes (now `msg_id < 1000 or > 9999`) | Test 2.3 (invalid recipient) |
| `DBManager` no mutex, stmt leaks on early return | Yes (added `_mutex` + RAII `StmtGuard`) | Test 3.1 (concurrent DB ops) |
| `LogicSystem::PostMsgToQue` unbounded queue | Yes (enforced `MAX_RECVQUE`) | Test 5.3 |
| No error response on JSON parse failure | Yes (all handlers now send error rsp) | Test 1.1 (malformed input) |
| No offline message auto-delivery on re-login | Yes (pushes on successful login) | Test 3.1 |
| No re-login old-session cleanup | Yes (closes old session on duplicate login) | Test 1.2 |
| No re-login recovery in test client | Yes (reconnect + auto-re-login) | Test 4.2 |

---

## Running the Full Test Suite (Manual Steps)

1. **Start server:** `CoroutineServer.exe`
2. **Open 2+ client terminals:** `TestClient.exe` in each
3. **Run Test 1:** Registration + login sequence
4. **Run Test 2:** Friend add + real-time chat
5. **Run Test 3:** Offline message flow (close one client, send messages, reopen)
6. **Run Test 4:** Kill processes (server and client) + verify recovery
7. **Run Test 5:** Rapid send + verify DB consistency

## Server Log Monitoring

All components now output timestamped logs in the format:
```
[HH:MM:SS] [Component] message details
```
Key log markers to watch:
- `[CSession] Close 开始` — Session cleanup begins
- `[UserManager] 用户下线` — User removed from online map
- `[CServer] 清除会话` — Session removed from server map
- `[LogicSystem] 检测到重复登录` — Re-login triggered
- `[LogicSystem] 登录后推送 N 条离线消息` — Offline messages auto-delivered
- `[DBManager] 消息已保存` — Message persisted
- `[LogicSystem] 消息队列已满` — Queue overflow protection active
