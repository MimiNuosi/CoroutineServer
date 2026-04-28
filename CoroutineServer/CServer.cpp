#include "CServer.h"
#include "AsioIOServicePool.h"

CServer::CServer(boost::asio::io_context& io_context, short port)
	:_io_context(io_context),_port(port),_acceptor(AsioIOServicePool::GetInstance().GetIOService(),tcp::endpoint(tcp::v4(),port))
{

	StartAccept();
}

CServer::~CServer()
{

}

void CServer::ClearSession(std::string uuid)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_sessions.erase(uuid);
}

void CServer::HandleAccept(std::shared_ptr<CSession>  new_session, const boost::system::error_code error)
{
	if (!error) {
		new_session->Start();
		std::lock_guard<std::mutex> lock(_mutex);
		_sessions.insert({ new_session->GetUuid(), new_session});
	}
	else {
		std::cout << "session accept faile :" << error.what() << "\n";
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