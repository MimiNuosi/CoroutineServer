#pragma once
/// @file  CServer.h
/// @brief TCP acceptor that manages client session lifecycle.

#include <memory>
#include <map>
#include <mutex>
#include <boost/asio.hpp>
#include "CSession.h"
using boost::asio::ip::tcp;

/// @class CServer
/// @brief Accepts incoming TCP connections and catalogs active sessions.
/// @note  _sessions map is protected by _mutex. ClearSession() is called
///        from CSession coroutine and must be thread-safe.
class CServer
{
public:
	/// @param io_context The io_context to use for the acceptor.
	/// @param port       TCP port to listen on.
	CServer(boost::asio::io_context& io_context, short port);
	~CServer();

	/// @brief Remove a session from the active map (called on disconnect).
	void ClearSession(std::string uuid);

private:
	/// @brief Callback for async_accept; starts session and registers it.
	void HandleAccept(std::shared_ptr<CSession>  new_session, const boost::system::error_code error);

	/// @brief Post a new async_accept operation (re-arms the acceptor).
	void StartAccept();

	boost::asio::io_context& _io_context;
	short _port;
	tcp::acceptor _acceptor;
	std::map<std::string, std::shared_ptr<CSession>>_sessions;
	std::mutex _mutex;                          ///< Protects _sessions
};
