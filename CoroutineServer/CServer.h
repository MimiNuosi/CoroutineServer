#pragma once
#include <memory>
#include <map>
#include <mutex>
#include <boost/asio.hpp>
#include "CSession.h"
using boost::asio::ip::tcp;

class CServer
{
public:
	CServer(boost::asio::io_context& io_context, short port);
	~CServer();
	void ClearSession(std::string uuid);

private:
	void HandleAccept(std::shared_ptr<CSession>  new_session, const boost::system::error_code error);
	void StartAccept();
	boost::asio::io_context& _io_context;
	short _port;
	tcp::acceptor _acceptor;
	std::map<std::string, std::shared_ptr<CSession>>_sessions;
	std::mutex _mutex;
};

