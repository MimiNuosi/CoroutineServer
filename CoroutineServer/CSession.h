#pragma once
/// @file  CSession.h
/// @brief Per-connection session with C++20 coroutine-based read loop
///        and thread-safe send queue.

#include <iostream>
#include <cstdint>
#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include "const.h"
#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include "MsgNode.h"

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::strand;
namespace this_coro = boost::asio::this_coro;

class CServer;

/// @class CSession
/// @brief Represents a single client connection.
/// @details Read loop runs as a boost::asio coroutine (co_spawn).
///          Send queue is thread-safe via _mutex. _b_stop is atomic
///          to prevent double-close. Login uid is protected by _login_mutex
///          because both the coroutine and LogicSystem worker may access it.
class CSession:public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	~CSession();

	tcp::socket& GetSocket();
	std::string& GetUuid();

	int64_t GetLoginUid();                           ///< thread-safe copy (0 = not logged in)
	void SetLoginUid(int64_t uid);                   ///< thread-safe write

	/// @brief Spawn the coroutine read loop (detached).
	void Start();

	/// @brief Close socket and clean up user state. Idempotent.
	void Close();

	/// @brief Enqueue a raw message for async write.
	void Send(const char* msg, short max_len, short msgid);

	/// @brief Convenience overload: string -> Send(const char*, len, msgid).
	void Send(std::string msg, short msgid);

	/// @brief Completion handler for async_write; drains _send_que.
	void HandleWrite(const boost::system::error_code& error);

private:
	boost::asio::io_context& _io_context;
	CServer* _server;
	tcp::socket _socket;
	std::string _uuid;
	int64_t _login_uid = 0;                          ///< 0 means not logged in
	std::mutex _login_mutex;                         ///< protects _login_uid (LogicSystem worker thread + io_context coroutine)
	std::atomic<bool> _b_stop;                       ///< CAS-guard for Close()
	std::mutex _mutex;                               ///< protects _send_que
	std::queue<std::shared_ptr<SendNode>> _send_que;
	std::shared_ptr<RecvNode> _recv_msg_node;
	std::shared_ptr<MsgNode> _recv_head_node;
};

/// @class LogicNode
/// @brief Message node passed through LogicSystem queue.
///        Binds a received RecvNode with its source CSession.
class LogicNode {
	friend class LogicSystem;
public:
	LogicNode(std::shared_ptr<CSession>, std::shared_ptr<RecvNode>);
private:
	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recvnode;
};
