# CoroutineServer

基于 **Boost.Asio** 和 **C++20** 的高并发协程 IM（即时通讯）TCP 服务器。

---

## 项目简介

CoroutineServer 是一个演示性质的 IM 服务器，使用 C++20 协程（`co_await`、`co_spawn`）处理并发客户端连接。支持以下功能：

- 用户注册与登录（密码哈希存储）
- 好友在线时实时聊天消息推送
- 好友离线时消息存储，上线后自动拉取
- 好友列表管理（双向好友关系）
- 多端登录处理（重登时踢掉旧会话）
- 连接异常断开时在线列表自动清理，客户端重连后恢复登录态

服务器使用自定义二进制协议：**4 字节头部**（2 字节 msg_id + 2 字节 body_len，网络字节序）+ **JSON 正文**（通过 nlohmann-json 解析）。

---

## 技术栈

| 组件               | 用途                                                       |
|--------------------|------------------------------------------------------------|
| **C++20**          | 协程、lambda、结构化绑定、`std::atomic`                    |
| **Boost.Asio 1.90** | 异步 I/O、TCP 套接字、`co_spawn`、`awaitable`              |
| **Boost.Uuid**     | 生成唯一会话标识                                           |
| **SQLite3**        | 嵌入式数据库（WAL 模式，线程安全访问）                     |
| **nlohmann-json** | IM 协议的 JSON 序列化/反序列化                             |
| **MSVC 2022**      | Visual Studio 2022 C++ 编译器                              |
| **vcpkg**          | C++ 包管理器                                               |

---

## 环境要求

| 要求           | 版本/说明                               |
|----------------|-----------------------------------------|
| **操作系统**   | Windows 10 / 11（64 位）                |
| **IDE**        | Visual Studio 2022（任意版本）          |
| **编译器**     | MSVC 19.x+，启用 `/std:c++20`           |
| **vcpkg**      | 最新版，已与 VS 2022 集成               |
| **依赖库**     | boost-asio、boost-uuid、nlohmann-json、sqlite3 |

安装依赖前，确保 vcpkg 已集成到 Visual Studio：

```powershell
vcpkg integrate install
```

---

## 编译步骤

### 方式一：命令行（推荐）

打开 **VS 2022 开发者命令提示符**，执行：

```batch
cd d:\Boost_asio\CoroutineServer
build.bat Debug
```

编译完成后，可执行文件会被拷贝到项目根目录：

```
CoroutineServer.exe
TestClient.exe
```

编译 Release 版本：

```batch
build.bat Release
```

### 方式二：Visual Studio IDE

1. 在 Visual Studio 2022 中打开 `CoroutineServer.sln`
2. 选择 **Debug** 或 **Release** 配置，平台选 **x64**
3. 右键解决方案 → **生成解决方案**（或按 `Ctrl+Shift+B`）
4. 输出目录：
   - 服务器：`CoroutineServer\x64\Debug\CoroutineServer.exe`
   - 客户端：`TestClient\x64\Debug\TestClient.exe`

---

## 运行方法

### 1. 启动服务器

```batch
CoroutineServer.exe
```

正常启动日志：

```
[HH:MM:SS] [LogicSystem] worker thread started
[HH:MM:SS] [DBManager] database im.db opened
[HH:MM:SS] [DBManager] database tables initialized
[HH:MM:SS] [AsioIOServicePool] started with N threads
[HH:MM:SS] [CServer] server started, listening on port 8080
[HH:MM:SS] [CServer] accept loop started
[HH:MM:SS] [Main] server running, press Ctrl+C to stop
```

首次运行时，数据库文件 `im.db` 会在工作目录中自动创建。

### 2. 运行测试客户端（另开终端）

```batch
TestClient.exe
```

客户端命令一览：

| 命令                      | 说明                           |
|---------------------------|--------------------------------|
| `/register <用户名> <密码>` | 注册新用户                     |
| `/login <用户名> <密码>`    | 登录                           |
| `/friends`                | 获取好友列表                    |
| `/friend add <uid>`       | 添加好友（按用户 ID）           |
| `/chat <uid> <消息内容>`    | 给好友发送聊天消息              |
| `/offline`                | 手动拉取离线消息                |
| `/reconnect`              | 触发断线后重连                  |
| `/help`                   | 显示可用命令                    |
| `/quit`                   | 退出客户端                      |

### 3. 快速测试流程

**终端 1（Alice）：**
```
/register alice 123456      → [成功] uid=1 register success
/login alice 123456         → [成功] uid=1 login success
```

**终端 2（Bob）：**
```
/register bob 123456        → [成功] uid=2 register success
/login bob 123456           → [成功] uid=2 login success
```

**互加好友：**
```
# Alice端：       /friend add 2  → [成功] friend added
# Bob端：         /friend add 1  → [成功] friend added
```

**实时聊天：**
```
# Alice端：       /chat 2 你好Bob！  → [成功] msg_id=1
# Bob端收到：     [Push-Chat] from=1, content: 你好Bob！
```

**离线消息测试：**
- 关闭 Bob 的客户端（Ctrl+C），从 Alice 端发消息
- 重新打开 Bob 的客户端并登录 → 自动推送离线消息

**重连测试：**
```
/reconnect  → 自动重连并恢复登录状态
```

完整的四项场景集成测试计划详见 [TEST_PLAN.md](TEST_PLAN.md)。

---

## 调试方法

### Visual Studio 调试

1. 在 VS 2022 中打开 `CoroutineServer.sln`
2. 右键 **CoroutineServer** → **属性** → **调试**
   - 将 **工作目录** 设置为 `$(SolutionDir)`
3. 在关键位置设置断点后按 F5 启动

**推荐断点位置：**

| 文件               | 行号  | 用途                              |
|--------------------|-------|-----------------------------------|
| `LogicSystem.cpp`  | ~62   | `DealMsg()` 工作线程循环入口      |
| `LogicSystem.cpp`  | ~186  | `LoginRequest` 登录处理           |
| `LogicSystem.cpp`  | ~372  | `ChatMessageRequest` 聊天处理     |
| `CSession.cpp`     | ~66   | `Start()` 协程 I/O 循环           |
| `UserManager.cpp`  | ~20   | `AddOnlineUser` 用户上线          |
| `UserManager.cpp`  | ~30   | `RemoveOnlineUser` 用户下线       |

### 关注服务器日志中的关键事件

| 场景                             | 预期日志                                                |
|----------------------------------|---------------------------------------------------------|
| 客户端连接                       | `[CServer] new connection uuid=xxx total sessions=N`    |
| 重登（踢掉旧会话）               | `[LogicSystem] re-login detected uid=N, closing...`    |
| 实时消息送达                     | `[LogicSystem] realtime push msg_id=N`                 |
| 接收者离线，消息入库             | `[LogicSystem] recipient offline, msg stored`           |
| 登录时推送离线消息               | `[LogicSystem] push N offline msgs on login`            |
| 客户端断线                       | `[UserManager] user offline: uid=N`                     |

---

## 项目结构

```
CoroutineServer/
├── CoroutineServer/              # 服务器项目
│   ├── CoroutineServer.cpp       # main() 入口，信号处理
│   ├── CServer.h / CServer.cpp   # TCP 接收器，会话生命周期管理
│   ├── CSession.h / CSession.cpp # 单客户端会话：I/O 协程、发送队列、登录状态
│   ├── AsioIOServicePool.h/.cpp  # io_context 线程池（轮询调度）
│   ├── LogicSystem.h / .cpp      # 业务逻辑引擎：生产者-消费者工作线程
│   ├── MsgNode.h / MsgNode.cpp   # 协议消息抽象（vector<char> RAII 缓冲区）
│   ├── DBManager.h / DBManager.cpp # SQLite3 数据库：用户、好友、消息的 CRUD
│   ├── UserManager.h / .cpp      # 在线用户管理（int64_t → weak_ptr<CSession>）
│   ├── Timestamp.h               # 公共日志时间戳辅助工具（inline 函数）
│   └── const.h                   # 共享常量、消息 ID 枚举、输入验证限制
│
├── TestClient/                   # 测试客户端项目
│   └── TestClient.cpp            # 交互式命令行客户端，支持重连
│
├── TEST_PLAN.md                  # 手动集成测试计划（4 个场景）
├── vcpkg.json                    # vcpkg 清单（boost-asio, boost-uuid, nlohmann-json, sqlite3）
├── build.bat                     # 命令行编译脚本
├── run_server.bat                # 服务器启动脚本
├── README.md                     # 英文版 README
└── README_zh.md                  # 本文件（中文版 README）
```

### 核心文件说明

| 文件                          | 职责                                                                                    |
|-------------------------------|-----------------------------------------------------------------------------------------|
| `CoroutineServer.cpp`         | 程序入口。初始化 IO 线程池，创建服务器实例，处理 Ctrl+C 优雅关闭。                      |
| `CServer.h/.cpp`              | TCP 接收器服务器。接受新连接并管理活跃会话映射表。                                      |
| `CSession.h/.cpp`             | 单连接会话。运行 C++20 协程读取协议头+正文，通过发送队列链式异步写入。使用 `std::mutex` 保护登录 UID 的线程安全访问。 |
| `AsioIOServicePool.h/.cpp`    | IO 线程池。创建 N 个 `io_context` 实例（N=硬件并发数），每个在独立线程中运行。通过 `std::atomic` 实现线程安全的轮询分发。 |
| `LogicSystem.h/.cpp`          | 业务逻辑引擎。单线程消费者从队列取消息并分发给回调：注册、登录、添加好友、获取好友列表、聊天消息、获取离线消息。 |
| `MsgNode.h/.cpp`              | 通信协议基类。`MsgNode` 持有 `std::vector<char>` 缓冲区（RAII，无内存泄漏）。`SendNode` 将 msg_id+长度+数据序列化为网络字节序。 |
| `DBManager.h/.cpp`            | SQLite3 持久化层。所有公开方法均被互斥锁保护。使用 RAII `StmtGuard` 管理 sqlite3_stmt 生命周期。WAL 模式提升并发读性能。 |
| `UserManager.h/.cpp`          | 在线用户注册表。`unordered_map<int64_t, weak_ptr<CSession>>` 自动清理已失效的会话。互斥锁保护线程安全。 |
| `const.h`                     | 消息 ID 枚举（1001 HelloWorld，2001-2014 IM 协议）、缓冲区/队列上限、以及输入验证限制（用户名 2-32 字符，密码 3-64 字符，消息最大 2048 字符）。 |
| `Timestamp.h`                 | 公共内联 `Timestamp()` 函数，返回 `[HH:MM:SS.mmm]` 格式的时间戳，供所有服务器模块统一使用。消除多处重复定义。 |
| `TestClient.cpp`              | 交互式命令行测试客户端，包含命令解析、异步接收线程、同步响应匹配、以及带凭据缓存的断线自动重连。 |

---

## 通信协议

服务器与客户端之间使用自定义二进制协议：

```
+--------+--------+-------------------+
| msg_id | length |    JSON 正文      |
| (2 字节)| (2 字节)|  (变长)           |
+--------+--------+-------------------+
| <---- 网络字节序（大端）----->        |
```

- **msg_id**：2 字节无符号短整型（大端），对应 `MSG_IDS` 枚举值
- **length**：2 字节无符号短整型（大端），JSON 正文的字节长度
- **body**：UTF-8 JSON 字符串（nlohmann-json 格式）

消息 ID 对照表：

| ID   | 名称                     | 方向                   |
|------|-------------------------|------------------------|
| 1001 | MSG_HELLO_WORLD         | 回显测试               |
| 2001 | MSG_REGISTER_REQ        | 客户端 → 服务器        |
| 2002 | MSG_REGISTER_RSP        | 服务器 → 客户端        |
| 2003 | MSG_LOGIN_REQ           | 客户端 → 服务器        |
| 2004 | MSG_LOGIN_RSP           | 服务器 → 客户端        |
| 2005 | MSG_GET_FRIEND_LIST_REQ  | 客户端 → 服务器        |
| 2006 | MSG_GET_FRIEND_LIST_RSP  | 服务器 → 客户端        |
| 2007 | MSG_ADD_FRIEND_REQ      | 客户端 → 服务器        |
| 2008 | MSG_ADD_FRIEND_RSP      | 服务器 → 客户端        |
| 2009 | MSG_FRIEND_REQUEST_PUSH  | 服务器 → 客户端（推送） |
| 2010 | MSG_SEND_CHAT_REQ       | 客户端 → 服务器        |
| 2011 | MSG_SEND_CHAT_RSP       | 服务器 → 客户端        |
| 2012 | MSG_CHAT_MSG_PUSH       | 服务器 → 客户端（推送） |
| 2013 | MSG_OFFLINE_MSG_REQ     | 客户端 → 服务器        |
| 2014 | MSG_OFFLINE_MSG_RSP     | 服务器 → 客户端        |

---

## 常见问题排查

### 编译报错：找不到头文件

确保 vcpkg 已集成到 Visual Studio：

```powershell
vcpkg integrate install
```

然后重新生成解决方案（VS 可能需要重启以加载 vcpkg 路径）。

### 找不到 MSBuild

请从 **VS 2022 开发者命令提示符**中运行 `build.bat`，而非普通命令提示符。

### 服务器启动失败："cannot open im.db database"

- 确保工作目录可写入（建议使用项目根目录）
- 如果 `im.db` 由其他版本创建或已损坏，删除后重新运行即可
- 检查杀毒软件是否阻止了 SQLite 文件操作

### 连接被拒绝（TestClient 连接失败）

- 确认服务器正在运行并监听 8080 端口
- 服务器日志应显示 `listening on port 8080`
- 检查 Windows 防火墙 — 允许 `CoroutineServer.exe` 通过 8080 端口

### 重连功能不正常

TestClient 使用 `std::unique_ptr<boost::asio::io_context>` 实现重连。请确保使用的是最新代码 — Boost.Asio 1.70 以上版本删除了 `io_context` 的移动构造函数，必须通过 `unique_ptr` 来替换 `io_context` 实例。

### 服务器关机时崩溃

始终使用 **Ctrl+C**（SIGINT）优雅地停止服务器。这会触发所有 `io_context` 实例、工作线程和 SQLite 连接的正确清理。`AsioIOServicePool` 析构函数现在会调用 `Stop()` 安全 join 所有线程（防止正常退出时调用 `std::terminate()`）。直接强制结束进程可能使 `im.db` 处于不一致状态（不过 WAL 模式可以缓解此问题）。

### 数据库文件（im.db）持续增长

SQLite 不会自动清理空间。要缩小数据库文件：

```batch
sqlite3 im.db "VACUUM;"
```

---

## 架构说明

### 线程模型

| 组件                  | 线程数                        |
|-----------------------|-------------------------------|
| `AsioIOServicePool`   | N 个工作线程（N = CPU 核心数）。析构函数调用 Stop() 安全 join 所有线程（防止 std::terminate()）。 |
| `LogicSystem`         | 1 个专用工作线程              |
| `TestClient`          | 1 个接收线程 + 1 个主线程     |

### 并发安全

- `DBManager`：所有公开方法使用 `std::mutex`（单个 SQLite 连接，串行访问）
- `UserManager`：`_online_users` 映射操作使用 `std::mutex` 保护
- `CSession::_login_uid`：使用 `std::mutex`（LogicSystem 工作线程写入，io_context 协程读取）
- `CSession::_send_que`：使用 `std::mutex`（LogicSystem 写入，异步写链消费）
- `LogicSystem::_msg_que`：使用 `std::mutex` + `std::condition_variable`（生产者-消费者）
- `CServer::_sessions`：使用 `std::mutex`（接收时插入，关闭/清理时擦除）
- `CSession::_b_stop`：使用 `std::atomic<bool>` + `compare_exchange_strong` 实现幂等的 `Close()`

### 内存管理

- `CSession` 继承 `std::enable_shared_from_this` — 会话生命周期由 `CServer::_sessions` 映射表及未完成的异步操作共同管理
- `UserManager` 存储 `std::weak_ptr<CSession>` 以避免延长会话生命周期
- `MsgNode` 缓冲区使用 `std::vector<char>`（RAII）— 无需手动 new/delete，异常安全
- 数据库语句对象使用 `StmtGuard` RAII 包装器 — 保证在作用域退出时调用 `sqlite3_finalize()`
- TestClient 使用 `std::unique_ptr<boost::asio::io_context>` 支持重连（`io_context` 不可移动）

---

## 许可证

本项目为演示/教学代码，可自由使用。
