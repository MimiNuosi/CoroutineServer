#include "CServer.h"
#include "AsioIOServicePool.h"
#include "Timestamp.h"
#include <iostream>

CServer::CServer(boost::asio::io_context& io_context, short port)
	:_io_context(io_context),_port(port),_acceptor(AsioIOServicePool::GetInstance().GetIOService(),tcp::endpoint(tcp::v4(),port))
{
	std::cout << Timestamp() << " [CServer] server started, listening on port " << port << std::endl;
	StartAccept();
}

CServer::~CServer()
{
	std::cout << Timestamp() << " [CServer] server destructor, active sessions=" << _sessions.size() << std::endl;
	// _acceptor is automatically closed by its destructor.
	// Remaining sessions will be cleaned up by CSession destructors.
}

void CServer::ClearSession(std::string uuid)
{
	std::lock_guard<std::mutex> lock(_mutex);
	auto it = _sessions.find(uuid);
	if (it != _sessions.end()) {
		std::cout << Timestamp() << " [CServer] clear session uuid=" << uuid
			<< " remaining sessions=" << _sessions.size() - 1 << std::endl;
		_sessions.erase(it);
	}
}

void CServer::HandleAccept(std::shared_ptr<CSession>  new_session, const boost::system::error_code error)
{
	try {
		if (!error) {
			new_session->Start();
			std::lock_guard<std::mutex> lock(_mutex);
			_sessions.insert({ new_session->GetUuid(), new_session});
			std::cout << Timestamp() << " [CServer] new connection uuid=" << new_session->GetUuid()
				<< " total sessions=" << _sessions.size() << std::endl;
		}
		else if (error != boost::asio::error::operation_aborted) {
			// operation_aborted is expected when acceptor is closed (server shutdown)
			std::cerr << Timestamp() << " [CServer] accept failed: " << error.message() << "\n";
		}
	}
	catch (const std::exception& e) {
		std::cerr << Timestamp() << " [CServer] HandleAccept exception: " << e.what() << std::endl;
	}
	StartAccept();
}

void CServer::StartAccept()
{
	auto& io_context = AsioIOServicePool::GetInstance().GetIOService();
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(),
		boost::asio::bind_executor(io_context,
			[this, new_session](const boost::system::error_code error) {
				this->HandleAccept(new_session, error);
			}));
}

LogicNode::LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode)
	:_session(session), _recvnode(recvnode) {
}
