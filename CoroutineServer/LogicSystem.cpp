#include "LogicSystem.h"
#include "DBManager.h"
#include "UserManager.h"
#include "Timestamp.h"
#include <iostream>

// ---- Error response helper ----
static std::string ErrorResponse(int code, const std::string& msg) {
	nlohmann::json rsp;
	rsp["status"] = code;
	rsp["msg"] = msg;
	return rsp.dump();
}

// ---- Credential validation helper ----
static bool ValidateCredentials(const std::string& username, const std::string& password) {
	if (username.empty() || password.empty()) return false;
	if (username.length() < MIN_USERNAME_LEN || username.length() > MAX_USERNAME_LEN) return false;
	if (password.length() < MIN_PASSWORD_LEN || password.length() > MAX_PASSWORD_LEN) return false;
	return true;
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg)
{
	std::unique_lock<std::mutex> unique_lk(_mutex);
	if (_msg_que.size() >= MAX_RECVQUE) {
		unique_lk.unlock();
		std::cerr << Timestamp() << " [LogicSystem] queue full (MAX_RECVQUE="
			<< MAX_RECVQUE << "), drop msg_id=" << msg->_recvnode->_msg_id << std::endl;
		return;
	}

	_msg_que.push(msg);
	if (_msg_que.size() == 1) {
		_consume.notify_one();
	}
}

LogicSystem::LogicSystem() :_b_stop(false) {
	RegisterCallBack();
	_worker_thread = std::thread(&LogicSystem::DealMsg, this);
	std::cout << Timestamp() << " [LogicSystem] worker thread started" << std::endl;
}

LogicSystem::~LogicSystem() {
	_b_stop = true;
	_consume.notify_one();
	_worker_thread.join();
	std::cout << Timestamp() << " [LogicSystem] worker thread stopped" << std::endl;
}

void LogicSystem::DealMsg()
{
	for (;;) {
		std::shared_ptr<LogicNode> msg_node;
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_consume.wait(lock, [this]() {
				return !_msg_que.empty() || _b_stop;
				});

			if (_b_stop && _msg_que.empty()) {
				break;
			}

			msg_node = _msg_que.front();
			_msg_que.pop();
		}

		std::cout << Timestamp() << " [LogicSystem] DealMsg msg_id="
			<< msg_node->_recvnode->_msg_id << std::endl;

		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			std::cerr << Timestamp() << " [LogicSystem] unregistered msg_id: "
				<< msg_node->_recvnode->_msg_id << std::endl;
			continue;
		}

		try {
			call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
				std::string(msg_node->_recvnode->_data.data(), msg_node->_recvnode->_cur_len));
		}
		catch (const std::exception& e) {
			std::cerr << Timestamp() << " [LogicSystem] callback exception: " << e.what()
				<< " msg_id=" << msg_node->_recvnode->_msg_id << std::endl;
		}
	}
}

void LogicSystem::RegisterCallBack()
{
	_fun_callbacks[MSG_HELLO_WORLD] = [this](auto session, short msg_id, std::string msg_data) {
		this->HelloWorldCallBack(session, msg_id, msg_data);
		};
	_fun_callbacks[MSG_REGISTER_REQ] = [this](auto session, short msg_id, std::string msg_data) {
		this->RegisterRequest(session, msg_id, msg_data);
		};
	_fun_callbacks[MSG_LOGIN_REQ] = [this](auto session, short msg_id, std::string msg_data) {
		this->LoginRequest(session, msg_id, msg_data);
		};
	_fun_callbacks[MSG_GET_FRIEND_LIST_REQ] = [this](auto session, short msg_id, std::string msg_data) {
		this->GetFriendListRequest(session, msg_id, msg_data);
		};
	_fun_callbacks[MSG_ADD_FRIEND_REQ] = [this](auto session, short msg_id, std::string msg_data) {
		this->AddFriendRequest(session, msg_id, msg_data);
		};
	_fun_callbacks[MSG_SEND_CHAT_REQ] = [this](auto session, short msg_id, std::string msg_data) {
		this->ChatMessageRequest(session, msg_id, msg_data);
		};
	_fun_callbacks[MSG_OFFLINE_MSG_REQ] = [this](auto session, short msg_id, std::string msg_data) {
		this->GetOfflineMessageRequest(session, msg_id, msg_data);
		};
}

void LogicSystem::HelloWorldCallBack(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	try {
		nlohmann::json root = nlohmann::json::parse(msg_data, nullptr, false);
		if (root.is_discarded()) {
			std::cerr << Timestamp() << " [LogicSystem] HelloWorld JSON parse failed" << std::endl;
			return;
		}
		std::cout << Timestamp() << " [LogicSystem] HelloWorld id=" << root["id"].get<int>()
			<< " data=" << root["data"].get<std::string>() << std::endl;
		root["data"] = "server has received msg, msg data is " + root["data"].get<std::string>();
		session->Send(root.dump(), root["id"].get<int>());
	}
	catch (const std::exception& e) {
		std::cerr << Timestamp() << " [LogicSystem] HelloWorld exception: " << e.what() << std::endl;
	}
}

LogicSystem& LogicSystem::GetInstance() {
	static LogicSystem instance;
	return instance;
}

// ============ Register ============
void LogicSystem::RegisterRequest(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	try
	{
		nlohmann::json root = nlohmann::json::parse(msg_data, nullptr, false);
		if (root.is_discarded())
		{
			std::cerr << Timestamp() << " [LogicSystem] RegisterRequest JSON parse failed" << std::endl;
			session->Send(ErrorResponse(-1, "invalid json"), MSG_REGISTER_RSP);
			return;
		}

		// Validate required fields exist
		if (!root.contains("username") || !root.contains("password")) {
			std::cerr << Timestamp() << " [LogicSystem] RegisterRequest: missing fields" << std::endl;
			session->Send(ErrorResponse(-1, "missing username or password"), MSG_REGISTER_RSP);
			return;
		}

		std::string username = root["username"].get<std::string>();
		std::string password = root["password"].get<std::string>();

		if (!ValidateCredentials(username, password)) {
			std::cerr << Timestamp() << " [LogicSystem] RegisterRequest: invalid credentials (len: user="
				<< username.length() << " pass=" << password.length() << ")" << std::endl;
			std::ostringstream oss;
			oss << "username " << MIN_USERNAME_LEN << "-" << MAX_USERNAME_LEN
				<< " chars, password " << MIN_PASSWORD_LEN << "-" << MAX_PASSWORD_LEN << " chars";
			session->Send(ErrorResponse(-1, oss.str()), MSG_REGISTER_RSP);
			return;
		}

		std::cout << Timestamp() << " [LogicSystem] RegisterRequest: username=" << username << std::endl;
		bool ok = DBManager::GetInstance().RegisterUser(username, password);

		nlohmann::json rsp;
		rsp["status"] = ok ? 0 : -1;
		rsp["msg"] = ok ? "register success" : "register failed (username may exist)";
		session->Send(rsp.dump(), MSG_REGISTER_RSP);
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [LogicSystem] RegisterRequest exception: " << e.what() << std::endl;
		try { session->Send(ErrorResponse(-1, std::string("exception: ") + e.what()), MSG_REGISTER_RSP); }
		catch (...) {}
	}
}

// ============ Login ============
void LogicSystem::LoginRequest(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	try
	{
		nlohmann::json root = nlohmann::json::parse(msg_data, nullptr, false);
		if (root.is_discarded())
		{
			std::cerr << Timestamp() << " [LogicSystem] LoginRequest JSON parse failed" << std::endl;
			session->Send(ErrorResponse(-1, "invalid json"), MSG_LOGIN_RSP);
			return;
		}

		if (!root.contains("username") || !root.contains("password")) {
			std::cerr << Timestamp() << " [LogicSystem] LoginRequest: missing fields" << std::endl;
			session->Send(ErrorResponse(-1, "missing username or password"), MSG_LOGIN_RSP);
			return;
		}

		std::string username = root["username"].get<std::string>();
		std::string password = root["password"].get<std::string>();

		if (!ValidateCredentials(username, password)) {
			std::cerr << Timestamp() << " [LogicSystem] LoginRequest: invalid credentials" << std::endl;
			session->Send(ErrorResponse(-1, "invalid credentials"), MSG_LOGIN_RSP);
			return;
		}

		std::cout << Timestamp() << " [LogicSystem] LoginRequest: username=" << username << std::endl;
		auto [ok, uid] = DBManager::GetInstance().LoginUser(username, password);

		nlohmann::json rsp;
		if (ok)
		{
			// Re-login: close old session if user already online
			auto old_session = UserManager::GetInstance().GetSessionByUid(uid);
			if (old_session) {
				std::cout << Timestamp() << " [LogicSystem] re-login detected uid=" << uid
					<< ", closing old session" << std::endl;
				old_session->Close();
			}

			session->SetLoginUid(uid);
			UserManager::GetInstance().AddOnlineUser(uid, session);

			rsp["status"] = 0;
			rsp["uid"] = static_cast<int64_t>(uid);
			rsp["msg"] = "login success";

			// Auto-push offline messages
			auto offline_msgs = DBManager::GetInstance().GetOfflineMessages(uid);
			rsp["offline_count"] = static_cast<int>(offline_msgs.size());
			if (!offline_msgs.empty()) {
				nlohmann::json offline_rsp;
				offline_rsp["status"] = 0;
				offline_rsp["msg"] = "offline messages";
				nlohmann::json arr = nlohmann::json::array();
				for (const auto& m : offline_msgs) {
					nlohmann::json item;
					item["msg_id"]    = m["msg_id"];
					item["from_uid"]  = m["from_uid"];
					item["content"]   = m["content"];
					item["timestamp"] = m["timestamp"];
					arr.push_back(item);
				}
				offline_rsp["messages"] = arr;
				offline_rsp["count"] = static_cast<int>(offline_msgs.size());
				std::cout << Timestamp() << " [LogicSystem] push " << offline_msgs.size()
					<< " offline msgs on login uid=" << uid << std::endl;
				session->Send(offline_rsp.dump(), MSG_OFFLINE_MSG_RSP);
			}
		}
		else
		{
			rsp["status"] = -1;
			rsp["uid"] = 0;
			rsp["msg"] = "login failed (wrong username or password)";
		}
		session->Send(rsp.dump(), MSG_LOGIN_RSP);
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [LogicSystem] LoginRequest exception: " << e.what() << std::endl;
		try { session->Send(ErrorResponse(-1, std::string("exception: ") + e.what()), MSG_LOGIN_RSP); }
		catch (...) {}
	}
}

// ============ Get Friend List ============
void LogicSystem::GetFriendListRequest(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	try
	{
		int64_t uid = session->GetLoginUid();
		if (uid == 0) {
			std::cerr << Timestamp() << " [LogicSystem] GetFriendListRequest: not logged in" << std::endl;
			session->Send(ErrorResponse(-1, "not logged in"), MSG_GET_FRIEND_LIST_RSP);
			return;
		}

		std::cout << Timestamp() << " [LogicSystem] GetFriendListRequest: uid=" << uid << std::endl;
		std::vector<int64_t> friend_list = DBManager::GetInstance().GetFriendList(uid);

		nlohmann::json rsp;
		rsp["status"] = 0;
		rsp["msg"] = "ok";
		nlohmann::json arr = nlohmann::json::array();
		for (int64_t fid : friend_list) {
			arr.push_back(static_cast<int64_t>(fid));
		}
		rsp["friends"] = arr;
		session->Send(rsp.dump(), MSG_GET_FRIEND_LIST_RSP);
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [LogicSystem] GetFriendListRequest exception: " << e.what() << std::endl;
		try { session->Send(ErrorResponse(-1, std::string("exception: ") + e.what()), MSG_GET_FRIEND_LIST_RSP); }
		catch (...) {}
	}
}

// ============ Add Friend ============
void LogicSystem::AddFriendRequest(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	try
	{
		nlohmann::json root = nlohmann::json::parse(msg_data, nullptr, false);
		if (root.is_discarded()) {
			std::cerr << Timestamp() << " [LogicSystem] AddFriendRequest JSON parse failed" << std::endl;
			session->Send(ErrorResponse(-1, "invalid json"), MSG_ADD_FRIEND_RSP);
			return;
		}

		if (!root.contains("friend_uid")) {
			std::cerr << Timestamp() << " [LogicSystem] AddFriendRequest: missing friend_uid" << std::endl;
			session->Send(ErrorResponse(-1, "missing friend_uid"), MSG_ADD_FRIEND_RSP);
			return;
		}

		int64_t uid = session->GetLoginUid();
		if (uid == 0) {
			std::cerr << Timestamp() << " [LogicSystem] AddFriendRequest: not logged in" << std::endl;
			session->Send(ErrorResponse(-1, "not logged in"), MSG_ADD_FRIEND_RSP);
			return;
		}

		int64_t friend_uid = root["friend_uid"].get<int64_t>();
		if (friend_uid <= 0) {
			std::cerr << Timestamp() << " [LogicSystem] AddFriendRequest: invalid friend_uid" << std::endl;
			session->Send(ErrorResponse(-1, "friend_uid invalid"), MSG_ADD_FRIEND_RSP);
			return;
		}

		std::cout << Timestamp() << " [LogicSystem] AddFriendRequest: uid=" << uid
			<< " friend_uid=" << friend_uid << std::endl;

		if (uid == friend_uid) {
			std::cerr << Timestamp() << " [LogicSystem] AddFriendRequest: cannot add self" << std::endl;
			session->Send(ErrorResponse(-1, "cannot add yourself"), MSG_ADD_FRIEND_RSP);
			return;
		}

		bool ok = DBManager::GetInstance().AddFriend(uid, friend_uid);

		nlohmann::json rsp;
		if (ok)
		{
			auto friend_session = UserManager::GetInstance().GetSessionByUid(friend_uid);
			if (friend_session) {
				nlohmann::json push;
				push["from_uid"] = static_cast<int64_t>(uid);
				push["msg"] = "you have a new friend request";
				std::cout << Timestamp() << " [LogicSystem] push friend request to uid=" << friend_uid << std::endl;
				friend_session->Send(push.dump(), MSG_FRIEND_REQUEST_PUSH);
			}
			rsp["status"] = 0;
			rsp["msg"] = "friend added";
		}
		else
		{
			rsp["status"] = -1;
			rsp["msg"] = "add friend failed (already friends or user not found)";
		}
		session->Send(rsp.dump(), MSG_ADD_FRIEND_RSP);
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [LogicSystem] AddFriendRequest exception: " << e.what() << std::endl;
		try { session->Send(ErrorResponse(-1, std::string("exception: ") + e.what()), MSG_ADD_FRIEND_RSP); }
		catch (...) {}
	}
}

// ============ Chat Message ============
void LogicSystem::ChatMessageRequest(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	try
	{
		nlohmann::json root = nlohmann::json::parse(msg_data, nullptr, false);
		if (root.is_discarded()) {
			std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest JSON parse failed" << std::endl;
			session->Send(ErrorResponse(-1, "invalid json"), MSG_SEND_CHAT_RSP);
			return;
		}

		if (!root.contains("to_uid") || !root.contains("content")) {
			std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest: missing fields" << std::endl;
			session->Send(ErrorResponse(-1, "missing to_uid or content"), MSG_SEND_CHAT_RSP);
			return;
		}

		int64_t from_uid = session->GetLoginUid();
		if (from_uid == 0) {
			std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest: sender not logged in" << std::endl;
			session->Send(ErrorResponse(-1, "sender not logged in"), MSG_SEND_CHAT_RSP);
			return;
		}

		int64_t to_uid = root["to_uid"].get<int64_t>();
		std::string content = root["content"].get<std::string>();

		if (to_uid <= 0) {
			std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest: invalid to_uid" << std::endl;
			session->Send(ErrorResponse(-1, "invalid to_uid"), MSG_SEND_CHAT_RSP);
			return;
		}

		if (content.empty()) {
			std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest: content empty" << std::endl;
			session->Send(ErrorResponse(-1, "content empty"), MSG_SEND_CHAT_RSP);
			return;
		}

		if (content.length() > MAX_MESSAGE_LEN) {
			std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest: content too long ("
				<< content.length() << " > " << MAX_MESSAGE_LEN << ")" << std::endl;
			session->Send(ErrorResponse(-1, "content too long, max " + std::to_string(MAX_MESSAGE_LEN) + " chars"), MSG_SEND_CHAT_RSP);
			return;
		}

		std::cout << Timestamp() << " [LogicSystem] ChatMessageRequest: from=" << from_uid
			<< " to=" << to_uid << " content=" << content << std::endl;

		// Generate timestamp (thread-safe: strftime with local buffer, no ctime)
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		char time_buf[32];
		struct tm tm_info;
		localtime_s(&tm_info, &t);
		std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
		std::string timestamp(time_buf);

		int64_t saved_msg_id = DBManager::GetInstance().SaveMessage(from_uid, to_uid, content, timestamp);

		if (saved_msg_id >= 0)
		{
			auto to_session = UserManager::GetInstance().GetSessionByUid(to_uid);
			if (to_session) {
				nlohmann::json push;
				push["msg_id"]    = static_cast<int64_t>(saved_msg_id);
				push["from_uid"]  = static_cast<int64_t>(from_uid);
				push["to_uid"]    = static_cast<int64_t>(to_uid);
				push["content"]   = content;
				push["timestamp"] = timestamp;
				std::cout << Timestamp() << " [LogicSystem] realtime push msg_id=" << saved_msg_id << std::endl;
				to_session->Send(push.dump(), MSG_CHAT_MSG_PUSH);
			}
			else {
				std::cout << Timestamp() << " [LogicSystem] recipient offline, msg stored msg_id=" << saved_msg_id << std::endl;
			}
		}

		nlohmann::json rsp;
		rsp["status"] = (saved_msg_id >= 0) ? 0 : -1;
		rsp["msg_id"] = static_cast<int64_t>(saved_msg_id);
		rsp["msg"] = (saved_msg_id >= 0) ? "ok" : "save failed";
		session->Send(rsp.dump(), MSG_SEND_CHAT_RSP);
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [LogicSystem] ChatMessageRequest exception: " << e.what() << std::endl;
		try { session->Send(ErrorResponse(-1, std::string("exception: ") + e.what()), MSG_SEND_CHAT_RSP); }
		catch (...) {}
	}
}

// ============ Get Offline Messages ============
void LogicSystem::GetOfflineMessageRequest(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	try
	{
		int64_t uid = session->GetLoginUid();
		if (uid == 0) {
			std::cerr << Timestamp() << " [LogicSystem] GetOfflineMessageRequest: not logged in" << std::endl;
			session->Send(ErrorResponse(-1, "not logged in"), MSG_OFFLINE_MSG_RSP);
			return;
		}

		std::cout << Timestamp() << " [LogicSystem] GetOfflineMessageRequest: uid=" << uid << std::endl;
		std::vector<nlohmann::json> offline_msgs = DBManager::GetInstance().GetOfflineMessages(uid);

		nlohmann::json rsp;
		rsp["status"] = 0;
		rsp["msg"] = "ok";
		nlohmann::json arr = nlohmann::json::array();
		for (const auto& m : offline_msgs) {
			nlohmann::json item;
			item["msg_id"]    = m["msg_id"];
			item["from_uid"]  = m["from_uid"];
			item["content"]   = m["content"];
			item["timestamp"] = m["timestamp"];
			arr.push_back(item);
		}
		rsp["messages"] = arr;
		rsp["count"] = static_cast<int>(offline_msgs.size());

		std::cout << Timestamp() << " [LogicSystem] GetOfflineMessageRequest: returned "
			<< offline_msgs.size() << " msgs" << std::endl;
		session->Send(rsp.dump(), MSG_OFFLINE_MSG_RSP);
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [LogicSystem] GetOfflineMessageRequest exception: " << e.what() << std::endl;
		try { session->Send(ErrorResponse(-1, std::string("exception: ") + e.what()), MSG_OFFLINE_MSG_RSP); }
		catch (...) {}
	}
}
