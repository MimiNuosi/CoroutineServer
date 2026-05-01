#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"
#include "UserManager.h"
#include "Timestamp.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
	:_io_context(io_context),_server(server),_socket(io_context),_b_stop(false)
{
	boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
	_uuid = boost::uuids::to_string(a_uuid);
	_recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN);
	std::cout << Timestamp() << " [CSession] new session uuid=" << _uuid << std::endl;
}

CSession::~CSession()
{
	try
	{
		std::cout << Timestamp() << " [CSession] ~CSession uuid=" << _uuid << std::endl;
		Close();
	}
	catch (const std::exception& e)
	{
		std::cout << Timestamp() << " [CSession] ~CSession exception: " << e.what() << std::endl;
	}
}

tcp::socket& CSession::GetSocket() { return _socket; }

std::string& CSession::GetUuid() { return _uuid; }

int64_t CSession::GetLoginUid() {
	std::lock_guard<std::mutex> lock(_login_mutex);
	return _login_uid;
}

void CSession::SetLoginUid(int64_t uid) {
	std::lock_guard<std::mutex> lock(_login_mutex);
	_login_uid = uid;
	std::cout << Timestamp() << " [CSession] bound uid=" << uid
		<< " session_uuid=" << _uuid << std::endl;
}

void CSession::Start()
{
	auto self = shared_from_this();
	co_spawn(_io_context, [=]()->awaitable<void> {
		try
		{
			for (; !_b_stop;) {
				_recv_head_node->Clear();
				std::size_t size = co_await boost::asio::async_read(_socket,
					boost::asio::buffer(_recv_head_node->_data.data(), HEAD_TOTAL_LEN),
					use_awaitable);

				if (size == 0) {
					std::cout << Timestamp() << " [CSession] peer closed uuid=" << _uuid << std::endl;
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				short msg_id = 0;
				memcpy(&msg_id, _recv_head_node->_data.data(), HEAD_ID_LEN);
				msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
				std::cout << Timestamp() << " [CSession] recv msg_id=" << msg_id << " uuid=" << _uuid << std::endl;

				if (msg_id < MSG_HELLO_WORLD || msg_id > 9999) {
					std::cerr << Timestamp() << " [CSession] invalid msg_id " << msg_id << " uuid=" << _uuid << std::endl;
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				short msg_len = 0;
				memcpy(&msg_len, _recv_head_node->_data.data() + HEAD_ID_LEN, HEAD_DATA_LEN);
				msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
				std::cout << Timestamp() << " [CSession] recv msg_len=" << msg_len << " uuid=" << _uuid << std::endl;

				if (msg_len > MAX_LENGTH) {
					std::cerr << Timestamp() << " [CSession] body too long " << msg_len << " uuid=" << _uuid << std::endl;
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
				size = co_await boost::asio::async_read(_socket,
					boost::asio::buffer(_recv_msg_node->_data.data(), _recv_msg_node->_total_len),
					use_awaitable);
				if (size == 0) {
					std::cout << Timestamp() << " [CSession] peer closed during body uuid=" << _uuid << std::endl;
					Close();
					_server->ClearSession(_uuid);
					co_return;
				}

				_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
				_recv_msg_node->_cur_len = _recv_msg_node->_total_len;
				std::cout << Timestamp() << " [CSession] recv body=" << _recv_msg_node->_data.data() << " uuid=" << _uuid << std::endl;
				LogicSystem::GetInstance().PostMsgToQue(std::make_shared<LogicNode>(self, _recv_msg_node));
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << Timestamp() << " [CSession] coroutine exception: " << e.what() << " uuid=" << _uuid << std::endl;
			Close();
			_server->ClearSession(_uuid);
		}
		}, detached);
}

void CSession::Close() {
	bool expected = false;
	if (!_b_stop.compare_exchange_strong(expected, true)) {
		return;
	}

	std::cout << Timestamp() << " [CSession] Close begin uuid=" << _uuid << std::endl;

	boost::system::error_code ec;
	_socket.close(ec);
	if (ec) {
		std::cerr << Timestamp() << " [CSession] socket.close error: " << ec.message() << " uuid=" << _uuid << std::endl;
	}

	{
		int64_t uid_copy = 0;
		{
			std::lock_guard<std::mutex> lock(_login_mutex);
			if (_login_uid != 0) {
				uid_copy = _login_uid;
				_login_uid = 0;
			}
		}
		if (uid_copy != 0) {
			UserManager::GetInstance().RemoveOnlineUser(uid_copy);
		}
	}

	std::cout << Timestamp() << " [CSession] Close done uuid=" << _uuid << std::endl;
}

void CSession::Send(const char* msg, short max_len, short msgid)
{
	// Validate: refuse to send on a closing/closed session
	if (_b_stop.load(std::memory_order_acquire)) {
		std::cerr << Timestamp() << " [CSession] Send on closing session, drop msgid=" << msgid << " uuid=" << _uuid << std::endl;
		return;
	}

	if (max_len <= 0 || max_len > static_cast<short>(MAX_LENGTH)) {
		std::cerr << Timestamp() << " [CSession] Send invalid len=" << max_len << " msgid=" << msgid << " uuid=" << _uuid << std::endl;
		return;
	}

	std::unique_lock<std::mutex> lock(_mutex);
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		std::cerr << Timestamp() << " [CSession] send queue full, drop msgid=" << msgid << " uuid=" << _uuid << std::endl;
		return;
	}

	bool write_in_progress = !_send_que.empty();
	_send_que.push(std::make_shared<SendNode>(msg, max_len, msgid));

	if (write_in_progress) {
		return;
	}

	auto msgnode = _send_que.front();
	lock.unlock();

	// shared_from_this may throw if not managed by shared_ptr; wrap defensively
	std::shared_ptr<CSession> self;
	try {
		self = shared_from_this();
	}
	catch (const std::bad_weak_ptr& e) {
		std::cerr << Timestamp() << " [CSession] shared_from_this failed: " << e.what()
			<< " uuid=" << _uuid << std::endl;
		return;
	}

	boost::asio::async_write(_socket,
		boost::asio::buffer(msgnode->_data.data(), msgnode->_total_len),
		[self](const boost::system::error_code& error, std::size_t) {
			self->HandleWrite(error);
		});
}

void CSession::Send(std::string msg, short msgid)
{
	short len = static_cast<short>(msg.length());
	Send(msg.c_str(), len, msgid);
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
				boost::asio::async_write(_socket,
					boost::asio::buffer(msgnode->_data.data(), msgnode->_total_len),
					[self](const boost::system::error_code& error, std::size_t) {
						self->HandleWrite(error);
					});
			}
		}
		else {
			std::cerr << Timestamp() << " [CSession] write failed: " << error.message() << " uuid=" << _uuid << std::endl;
			Close();
			_server->ClearSession(_uuid);
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [CSession] HandleWrite exception: " << e.what() << " uuid=" << _uuid << std::endl;
		Close();
		_server->ClearSession(_uuid);
	}
}
