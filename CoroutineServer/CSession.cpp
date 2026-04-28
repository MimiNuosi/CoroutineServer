#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
	:_io_context(io_context),_server(server),_socket(io_context),_b_stop(false)
{
	boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
	_uuid = boost::uuids::to_string(a_uuid);
	_recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN);
}

CSession::~CSession()
{
	try
	{
		std::cout << "CSession destruct" << "\n";
		Close();
	}
	catch (const std::exception& e)
	{
		std::cout << "Exception is" << e.what() << "\n";
	}
}

tcp::socket& CSession::GetSocket()
{
	return _socket;
}

std::string& CSession::GetUuid()
{
	return _uuid;
}

void CSession::Start()
{
	auto self = shared_from_this();
	co_spawn(_io_context, [=]()->awaitable<void> {
		try
		{
			for (; !_b_stop;) {
				_recv_head_node->Clear();
				std::size_t size = co_await boost::asio::async_read(_socket, boost::asio::buffer(_recv_head_node->_data,
					HEAD_TOTAL_LEN), use_awaitable);

				if (size == 0) {
					std::cout << "receivce peer closed" << "\n";
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				short msg_id = 0;
				memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
				msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
				std::cout << "msg_id is " << msg_id << "\n";
				if (msg_id > MAX_LENGTH) {
					std::cout << "invalid msg id :" << msg_id << "\n";
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				short msg_len = 0;
				memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
				msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
				std::cout << "msg_len is " << msg_len << "\n";
				if (msg_len > MAX_LENGTH) {
					std::cout << "invalid msg len " << msg_len << "\n";
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
				size = co_await boost::asio::async_read(_socket, boost::asio::buffer(_recv_msg_node->_data,
					_recv_msg_node->_total_len), use_awaitable);
				if (size == 0) {
					std::cout << "receive peer closed" << "\n";
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
				_recv_msg_node->_cur_len = _recv_msg_node->_total_len;
				std::cout << "receive msg is " << _recv_msg_node->_data << "\n";
				LogicSystem::GetInstance().PostMsgToQue(std::make_shared<LogicNode>(self,_recv_msg_node));
			}
		}
		catch (const std::exception& e)
		{
			std::cout << "Exception :" << e.what() << "\n";
			Close();
			_server->ClearSession(_uuid);
		}
		}, detached);
}

void CSession::Close() {
	_socket.close();
	_b_stop = true;
}

void CSession::Send(const char* msg, short max_len, short msgid)
{
	std::unique_lock<std::mutex> lock(_mutex);
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session :" << _uuid << " send que is fulled :" << MAX_SENDQUE << std::endl;
		return;
	}

	// 关键：记录入队前队列是否为空
	bool write_in_progress = !_send_que.empty();
	_send_que.push(std::make_shared<SendNode>(msg, max_len, msgid));

	// 如果之前已经有写操作在进行，直接返回，由 HandleWrite 后续处理
	if (write_in_progress) {
		return;
	}

	// 之前队列为空，现在需要手动启动第一次异步写
	auto msgnode = _send_que.front();
	lock.unlock();  // 注意：启动异步写前解锁，避免死锁
	auto self = shared_from_this();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		[self](const boost::system::error_code& error, std::size_t) {
			self->HandleWrite(error);
		});
}
void CSession::Send(std::string msg, short msgid)
{
	Send(msg.c_str(), msg.length(), msgid);
}

void CSession::HandleWrite(const boost::system::error_code& error)
{
	try
	{
		if (!error) {
			std::unique_lock<std::mutex> lock(_mutex);
			_send_que.pop();
			if (!_send_que.empty()) {
				auto msgnode = _send_que.front();
				lock.unlock();
				auto self = shared_from_this();
				boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
					[self](const boost::system::error_code& error, std::size_t) {
						self->HandleWrite(error);
					});
			}
		}
		else {
			std::cout << "handle write failed :" << error.what() << "\n";
			Close();
			_server->ClearSession(_uuid);
		}
	}
	catch (const std::exception& e)
	{
		std::cout << "Exception :" << e.what() << "\n";
		Close();
		_server->ClearSession(_uuid);
	}
}
