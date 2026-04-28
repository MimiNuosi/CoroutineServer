#pragma once
#include <iostream>
#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include "const.h"
#include <queue>
#include <mutex>
#include <memory>
#include "MsgNode.h"

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::strand;
namespace this_coro = boost::asio::this_coro;

class CServer;

class CSession:public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	~CSession();
	tcp::socket& GetSocket();
	std::string& GetUuid();
	void Start();
	void Close();
	void Send(const char* msg, short max_len, short msgid);
	void Send(std::string msg, short msgid);
	void HandleWrite(const boost::system::error_code& error);

private:
	boost::asio::io_context& _io_context;
	CServer* _server;
	tcp::socket _socket;
	std::string _uuid;
	bool _b_stop;
	std::mutex _mutex;
	std::queue<std::shared_ptr<SendNode>> _send_que;
	std::shared_ptr<RecvNode> _recv_msg_node;
	std::shared_ptr<MsgNode> _recv_head_node;

};

class LogicNode {
	friend class LogicSystem;
public:
	LogicNode(std::shared_ptr<CSession>, std::shared_ptr<RecvNode>);
private:
	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recvnode;
};