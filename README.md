# CoroutineServer

High-concurrency coroutine-based IM (Instant Messaging) TCP server built with **Boost.Asio** and **C++20**.

---

## Project Overview

CoroutineServer is a demonstration IM server that handles concurrent client connections using C++20 coroutines (`co_await`, `co_spawn`). It supports:

- User registration and login with hashed passwords
- Real-time chat message delivery (push to online recipients)
- Offline message storage and automatic retrieval on login
- Friend list management with bidirectional relationships
- Multi-device login handling (old session kicked on re-login)
- Graceful session cleanup on connection loss

The server uses a custom binary protocol: **4-byte header** (2B msg_id + 2B body_len in network byte order) + **JSON body** (via nlohmann-json).

---

## Tech Stack

| Component         | Usage                                                  |
|-------------------|--------------------------------------------------------|
| **C++20**         | Coroutines, lambdas, structured bindings, `std::atomic` |
| **Boost.Asio 1.90** | Async I/O, TCP sockets, `co_spawn`, `awaitable`        |
| **Boost.Uuid**    | Unique session ID generation                           |
| **SQLite3**       | Embedded database (WAL mode, thread-safe access)       |
| **nlohmann-json** | JSON serialization/deserialization for IM protocol     |
| **MSVC 2022**     | Visual Studio 2022 C++ compiler                        |
| **vcpkg**         | C++ package manager                                    |

---

## Environment Requirements

| Requirement  | Version / Notes                        |
|--------------|----------------------------------------|
| **OS**       | Windows 10 / 11 (64-bit)               |
| **IDE**      | Visual Studio 2022 (any edition)       |
| **Compiler** | MSVC 19.x+ with `/std:c++20` enabled   |
| **vcpkg**    | Latest, integrated with VS 2022        |
| **Dependencies** | boost-asio, boost-uuid, nlohmann-json, sqlite3 |

Ensure vcpkg is integrated with Visual Studio:

```powershell
vcpkg integrate install
```

---

## Build Steps

### Option 1: Command Line (Recommended)

Open **Developer Command Prompt for VS 2022**, then:

```batch
cd d:\Boost_asio\CoroutineServer
build.bat Debug
```

This compiles both the server and client. After a successful build, the executables are copied to the project root directory:

```
CoroutineServer.exe
TestClient.exe
```

To build in Release mode:

```batch
build.bat Release
```

### Option 2: Visual Studio IDE

1. Open `CoroutineServer.sln` in Visual Studio 2022
2. Select **Debug** or **Release** configuration and **x64** platform
3. Right-click the solution → **Build Solution** (or press `Ctrl+Shift+B`)
4. The output binaries will be in:
   - Server: `CoroutineServer\x64\Debug\CoroutineServer.exe`
   - Client: `TestClient\x64\Debug\TestClient.exe`

---

## How to Run

### 1. Start the Server

```batch
CoroutineServer.exe
```

Expected startup log:

```
[HH:MM:SS] [LogicSystem] worker thread started
[HH:MM:SS] [DBManager] database im.db opened
[HH:MM:SS] [DBManager] database tables initialized
[HH:MM:SS] [AsioIOServicePool] started with N threads
[HH:MM:SS] [CServer] server started, listening on port 8080
[HH:MM:SS] [CServer] accept loop started
[HH:MM:SS] [Main] server running, press Ctrl+C to stop
```

The database file `im.db` is auto-created in the working directory on first run.

### 2. Run the Test Client (in separate terminal(s))

```batch
TestClient.exe
```

Client commands:

| Command                  | Description                        |
|--------------------------|------------------------------------|
| `/register <user> <pass>` | Register a new user               |
| `/login <user> <pass>`    | Login with credentials             |
| `/friends`                | Get your friend list               |
| `/friend add <uid>`       | Add a friend by user ID            |
| `/chat <uid> <message>`   | Send a chat message to a friend    |
| `/offline`                | Manually pull offline messages     |
| `/reconnect`              | Trigger disconnect and reconnect    |
| `/help`                   | Show available commands            |
| `/quit`                   | Exit the client                    |

### 3. Quick Test Walkthrough

**Terminal 1 (Alice):**
```
/register alice 123456      → [OK] uid=1 register success
/login alice 123456         → [OK] uid=1 login success
```

**Terminal 2 (Bob):**
```
/register bob 123456        → [OK] uid=2 register success
/login bob 123456           → [OK] uid=2 login success
```

**Mutual friendship:**
```
# Alice:       /friend add 2  → [OK] friend added
# Bob:         /friend add 1  → [OK] friend added
```

**Real-time chat:**
```
# Alice:       /chat 2 Hello Bob!  → [OK] msg_id=1
# Bob sees:    [Push-Chat] from=1, content: Hello Bob!
```

**Offline message test:**
- Close Bob's client (Ctrl+C), send messages from Alice
- Reopen Bob's client → auto-pushes offline messages on login

**Reconnection test:**
```
/reconnect  → auto reconnects and restores login state
```

For a full integration test plan with all 4 scenarios, see [TEST_PLAN.md](TEST_PLAN.md).

---

## Debugging

### Debug with Visual Studio

1. Open `CoroutineServer.sln` in VS 2022
2. Right-click **CoroutineServer** → **Properties** → **Debugging**
   - Set **Working Directory** to `$(SolutionDir)`
3. Set breakpoints at key locations and press F5

**Recommended breakpoints:**

| File               | Line  | Purpose                           |
|--------------------|-------|-----------------------------------|
| `LogicSystem.cpp`  | ~62   | `DealMsg()` worker loop entry     |
| `LogicSystem.cpp`  | ~186  | `LoginRequest` handler             |
| `LogicSystem.cpp`  | ~372  | `ChatMessageRequest` handler       |
| `CSession.cpp`     | ~66   | `Start()` coroutine I/O loop       |
| `UserManager.cpp`  | ~20   | `AddOnlineUser`                   |
| `UserManager.cpp`  | ~30   | `RemoveOnlineUser`                |

### Monitor Key Events in Server Logs

Watch these patterns during testing:

| Scenario                         | Expected Log                                          |
|----------------------------------|-------------------------------------------------------|
| Client connects                  | `[CServer] new connection uuid=xxx total sessions=N`  |
| Re-login (kicks old session)     | `[LogicSystem] re-login detected uid=N, closing...`  |
| Real-time message delivered      | `[LogicSystem] realtime push msg_id=N`               |
| Recipient offline, msg stored    | `[LogicSystem] recipient offline, msg stored`         |
| Offline messages pushed on login | `[LogicSystem] push N offline msgs on login`          |
| Client disconnects               | `[UserManager] user offline: uid=N`                   |

---

## Project Structure

```
CoroutineServer/
├── CoroutineServer/              # Server project
│   ├── CoroutineServer.cpp       # main() - entry point, signal handling
│   ├── CServer.h / CServer.cpp   # TCP acceptor, session lifecycle management
│   ├── CSession.h / CSession.cpp # Per-client session: I/O coroutine, send queue, login state
│   ├── AsioIOServicePool.h/.cpp  # Thread pool of io_context instances (round-robin)
│   ├── LogicSystem.h / .cpp      # Business logic engine: producer-consumer worker thread
│   ├── MsgNode.h / MsgNode.cpp   # Protocol message abstraction (vector<char> RAII buffer)
│   ├── DBManager.h / DBManager.cpp # SQLite3 database: users, friends, messages CRUD
│   ├── UserManager.h / .cpp      # Online user tracking (int64_t -> weak_ptr<CSession>)
│   ├── Timestamp.h               # Common logging timestamp helper (inline)
│   └── const.h                   # Shared constants, message ID enum, input validation limits
│
├── TestClient/                   # Test client project
│   └── TestClient.cpp            # Interactive CLI client with reconnect support
│
├── TEST_PLAN.md                  # Manual integration test plan (4 scenarios)
├── vcpkg.json                    # vcpkg manifest (boost-asio, boost-uuid, nlohmann-json, sqlite3)
├── build.bat                     # Command-line build script
├── run_server.bat                # Server launcher script
└── README.md                     # This file
```

### Core File Descriptions

| File                          | Role                                                                                    |
|-------------------------------|-----------------------------------------------------------------------------------------|
| `CoroutineServer.cpp`         | Application entry point. Initializes IO pool, creates server, handles Ctrl+C shutdown. |
| `CServer.h/.cpp`              | TCP acceptor server. Accepts connections and manages active session map.               |
| `CSession.h/.cpp`             | Per-connection session. Runs a C++20 coroutine to read protocol headers+bodies. Chains async writes via a send queue. Thread-safe login UID with `std::mutex`. |
| `AsioIOServicePool.h/.cpp`    | IO thread pool. Creates N `io_context` instances (N=hardware_concurrency) and runs each in its own thread. Thread-safe round-robin distribution via `std::atomic`. |
| `LogicSystem.h/.cpp`          | Business logic engine. Single-threaded consumer dequeues messages and dispatches to callbacks: Register, Login, AddFriend, GetFriendList, ChatMessage, GetOfflineMessages. |
| `MsgNode.h/.cpp`              | Wire protocol base classes. `MsgNode` holds a `std::vector<char>` buffer (RAII, no memory leaks). `SendNode` serializes msg_id+len+data into network byte order. |
| `DBManager.h/.cpp`            | SQLite3 persistence layer. All public methods mutex-protected. Uses RAII `StmtGuard` for sqlite3_stmt lifecycle. WAL mode for concurrent read performance. |
| `UserManager.h/.cpp`          | Online user registry. `unordered_map<int64_t, weak_ptr<CSession>>` auto-expires dead sessions. Mutex-protected for thread safety. |
| `const.h`                     | Message ID enum (1001 HelloWorld, 2001-2014 IM protocol), buffer/queue limits, and input validation limits (username 2-32 chars, password 3-64 chars, message max 2048 chars). |
| `Timestamp.h`                 | Shared inline `Timestamp()` returning `[HH:MM:SS.mmm]` for consistent logging across all server modules. Eliminates duplicated definitions. |
| `TestClient.cpp`              | Interactive CLI test client with command parsing, async recv thread, sync response matching, and auto-reconnect with credential caching. |

---

## Protocol

Custom binary protocol used between server and client:

```
+--------+--------+-------------------+
| msg_id | length |    JSON body      |
| (2 B)  | (2 B)  |  (variable)       |
+--------+--------+-------------------+
| <---- network byte order ---->       |
```

- **msg_id**: 2-byte unsigned short (big-endian), maps to `MSG_IDS` enum values
- **length**: 2-byte unsigned short (big-endian), length of JSON body
- **body**: UTF-8 JSON string (nlohmann-json format)

Message IDs:

| ID   | Name                    | Direction             |
|------|-------------------------|-----------------------|
| 1001 | MSG_HELLO_WORLD         | Echo test             |
| 2001 | MSG_REGISTER_REQ        | Client → Server       |
| 2002 | MSG_REGISTER_RSP        | Server → Client       |
| 2003 | MSG_LOGIN_REQ           | Client → Server       |
| 2004 | MSG_LOGIN_RSP           | Server → Client       |
| 2005 | MSG_GET_FRIEND_LIST_REQ  | Client → Server       |
| 2006 | MSG_GET_FRIEND_LIST_RSP  | Server → Client       |
| 2007 | MSG_ADD_FRIEND_REQ      | Client → Server       |
| 2008 | MSG_ADD_FRIEND_RSP      | Server → Client       |
| 2009 | MSG_FRIEND_REQUEST_PUSH  | Server → Client (push) |
| 2010 | MSG_SEND_CHAT_REQ       | Client → Server       |
| 2011 | MSG_SEND_CHAT_RSP       | Server → Client       |
| 2012 | MSG_CHAT_MSG_PUSH       | Server → Client (push) |
| 2013 | MSG_OFFLINE_MSG_REQ     | Client → Server       |
| 2014 | MSG_OFFLINE_MSG_RSP     | Server → Client       |

---

## Troubleshooting

### Build fails: cannot open include file

Ensure vcpkg is integrated with Visual Studio:

```powershell
vcpkg integrate install
```

Then rebuild the solution (VS may need a restart to pick up vcpkg paths).

### MSBuild not found

Run `build.bat` from a **Developer Command Prompt for VS 2022**, not a regular command prompt.

### Server fails with "cannot open im.db database"

- Ensure the working directory is writable (the project root is recommended)
- Delete `im.db` if it was created by a different version or is corrupted
- Check for anti-virus software blocking SQLite file operations

### Connection refused (TestClient)

- Verify the server is running and listening on port 8080
- Server log should show `listening on port 8080`
- Check Windows Firewall — allow `CoroutineServer.exe` on port 8080

### Reconnection does not work

The TestClient uses `std::unique_ptr<boost::asio::io_context>` for reconnect support. Ensure you are built with the latest code that handles `io_context` via `unique_ptr` rather than move semantics (Boost.Asio 1.70+ deleted the move constructor).

### Server crashes on shutdown

Always use **Ctrl+C** (SIGINT) to stop the server gracefully. This triggers proper cleanup of all `io_context` instances, worker threads, and the SQLite connection. The `AsioIOServicePool` destructor now calls `Stop()` to safely join all threads (prevents `std::terminate()` on normal exit). Killing the process abruptly may leave `im.db` in an inconsistent state (though WAL mode mitigates this).

### Database file (im.db) keeps growing

SQLite does not auto-vacuum. To shrink the database:

```batch
sqlite3 im.db "VACUUM;"
```

---

## Architecture Notes

### Thread Model

| Component            | Thread(s)                        |
|----------------------|----------------------------------|
| `AsioIOServicePool`  | N worker threads (N = CPU cores). Destructor calls Stop() to safely join threads (avoids std::terminate()). |
| `LogicSystem`        | 1 dedicated worker thread        |
| `TestClient`         | 1 recv thread + 1 main thread    |

### Concurrency Safety

- `DBManager`: `std::mutex` on all public methods (single SQLite connection, serialized access)
- `UserManager`: `std::mutex` on `_online_users` map operations
- `CSession::_login_uid`: `std::mutex` (written by LogicSystem worker, read by io_context coroutine)
- `CSession::_send_que`: `std::mutex` (written by LogicSystem, consumed by async write chain)
- `LogicSystem::_msg_que`: `std::mutex` + `std::condition_variable` (producer-consumer)
- `CServer::_sessions`: `std::mutex` (inserted on accept, erased on close/session cleanup)
- `CSession::_b_stop`: `std::atomic<bool>` with `compare_exchange_strong` for idempotent `Close()`

### Memory Management

- `CSession` uses `std::enable_shared_from_this` — session lifetime is managed by `CServer::_sessions` map and outstanding async operations
- `UserManager` stores `std::weak_ptr<CSession>` to avoid extending session lifetime
- `MsgNode` buffer uses `std::vector<char>` (RAII) — no manual new/delete, safe on exception
- DB statement objects use `StmtGuard` RAII wrapper — guaranteed `sqlite3_finalize()` on scope exit
- TestClient uses `std::unique_ptr<boost::asio::io_context>` to support reconnection (io_context is non-movable)

---
```

---

## License

This project is a demonstration / educational codebase. Use freely.
