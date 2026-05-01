/// @file TestClient.cpp
/// @brief Interactive IM test client for CoroutineServer.
///        Supports register, login, friend add, chat, offline messages.
///        Uses nlohmann/json for protocol serialization.

#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <sstream>
#include <atomic>
#include <nlohmann/json.hpp>
#include <windows.h>

using namespace std;
using namespace boost::asio::ip;

// ============ Protocol constants ============
const int MAX_LENGTH   = 1024 * 2;
const int HEAD_ID_LEN  = 2;
const int HEAD_DATA_LEN = 2;
const int HEAD_TOTAL   = 4;

// ============ Input validation limits ============
const int MIN_USERNAME_LEN = 2;
const int MAX_USERNAME_LEN = 32;
const int MIN_PASSWORD_LEN = 3;
const int MAX_PASSWORD_LEN = 64;

// ============ Message IDs (must match server const.h) ============
enum {
	MSG_REGISTER_REQ        = 2001,
	MSG_REGISTER_RSP        = 2002,
	MSG_LOGIN_REQ           = 2003,
	MSG_LOGIN_RSP           = 2004,
	MSG_GET_FRIEND_LIST_REQ = 2005,
	MSG_GET_FRIEND_LIST_RSP = 2006,
	MSG_ADD_FRIEND_REQ      = 2007,
	MSG_ADD_FRIEND_RSP      = 2008,
	MSG_FRIEND_REQUEST_PUSH = 2009,
	MSG_SEND_CHAT_REQ       = 2010,
	MSG_SEND_CHAT_RSP       = 2011,
	MSG_CHAT_MSG_PUSH       = 2012,
	MSG_OFFLINE_MSG_REQ     = 2013,
	MSG_OFFLINE_MSG_RSP     = 2014,
};

struct ReceivedMsg {
	short      msg_id;
	nlohmann::json json;
};

// ============ Global state ============
mutex              g_recv_mutex;
mutex              g_cout_mutex;
condition_variable g_recv_cv;
queue<ReceivedMsg> g_recv_queue;
atomic<bool>       g_stop_recv{ false };
atomic<bool>       g_connected{ false };

string g_last_username;
string g_last_password;
atomic<bool>       g_logged_in{ false };

// ============ Input validation helpers ============
static bool ValidateUsername(const string& user) {
	if (user.length() < MIN_USERNAME_LEN || user.length() > MAX_USERNAME_LEN) {
		lock_guard<mutex> lock(g_cout_mutex);
		cerr << "[Client] username must be " << MIN_USERNAME_LEN << "-"
			<< MAX_USERNAME_LEN << " chars" << endl;
		return false;
	}
	return true;
}

static bool ValidatePassword(const string& pass) {
	if (pass.length() < MIN_PASSWORD_LEN || pass.length() > MAX_PASSWORD_LEN) {
		lock_guard<mutex> lock(g_cout_mutex);
		cerr << "[Client] password must be " << MIN_PASSWORD_LEN << "-"
			<< MAX_PASSWORD_LEN << " chars" << endl;
		return false;
	}
	return true;
}

// ============ Send ============
bool SendMsg(tcp::socket& sock, short msg_id, const nlohmann::json& root)
{
	try {
		string json_str = root.dump();
		size_t json_len = json_str.length();
		if (json_len > MAX_LENGTH) {
			{
				lock_guard<mutex> lock(g_cout_mutex);
				cerr << "[Client] JSON too long: " << json_len << endl;
			}
			return false;
		}

		std::vector<char> send_data(MAX_LENGTH + HEAD_TOTAL, 0);
		short msg_id_net   = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
		short json_len_net = boost::asio::detail::socket_ops::host_to_network_short((short)json_len);

		memcpy(send_data.data(), &msg_id_net, HEAD_ID_LEN);
		memcpy(send_data.data() + HEAD_ID_LEN, &json_len_net, HEAD_DATA_LEN);
		memcpy(send_data.data() + HEAD_TOTAL, json_str.c_str(), json_len);

		boost::asio::write(sock, boost::asio::buffer(send_data.data(), json_len + HEAD_TOTAL));
		return true;
	}
	catch (const exception& e) {
		{
			lock_guard<mutex> lock(g_cout_mutex);
			cerr << "[Client] send failed: " << e.what() << endl;
		}
		g_connected = false;
		g_logged_in = false;
		return false;
	}
}

// ============ Recv ============
bool RecvOneMessage(tcp::socket& sock, short& out_msg_id, string& out_body)
{
	try {
		char header[HEAD_TOTAL] = { 0 };
		size_t n = boost::asio::read(sock, boost::asio::buffer(header, HEAD_TOTAL));
		if (n == 0) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] server disconnected" << endl; }
			return false;
		}

		short msg_id = 0;
		memcpy(&msg_id, header, HEAD_ID_LEN);
		msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);

		short body_len = 0;
		memcpy(&body_len, header + HEAD_ID_LEN, HEAD_DATA_LEN);
		body_len = boost::asio::detail::socket_ops::network_to_host_short(body_len);

		if (body_len <= 0 || body_len > MAX_LENGTH) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] invalid body_len: " << body_len << endl; }
			return false;
		}

		std::vector<char> body(MAX_LENGTH + 1, 0);
		n = boost::asio::read(sock, boost::asio::buffer(body.data(), body_len));
		if (n == 0) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] server disconnected" << endl; }
			return false;
		}
		body[body_len] = '\0';

		out_msg_id = msg_id;
		out_body   = string(body.data(), body_len);
		return true;
	}
	catch (const exception& e) {
		{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] recv exception: " << e.what() << endl; }
		return false;
	}
}

// ============ Recv thread ============
void RecvThreadFunc(tcp::socket& sock)
{
	while (!g_stop_recv) {
		short  msg_id = 0;
		string body;

		if (!RecvOneMessage(sock, msg_id, body)) {
			g_connected = false;
			g_logged_in = false;
			g_recv_cv.notify_all();
			break;
		}

		// Robust JSON parse with graceful fallback
		nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
		if (json.is_discarded()) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] JSON parse failed, msg_id=" << msg_id << endl; }
			continue;
		}

		// Safe field access: use .value() instead of .get<>() for optional fields
		try {
			if (msg_id == MSG_CHAT_MSG_PUSH) {
				lock_guard<mutex> lock(g_cout_mutex);
				cout << "\n[Push-Chat] from=" << json.value("from_uid", 0)
					<< " msg_id=" << json.value("msg_id", 0)
					<< "\n  content: " << json.value("content", "(empty)")
					<< "\n  time: " << json.value("timestamp", "(unknown)")
					<< "\n> " << flush;
			}
			else if (msg_id == MSG_FRIEND_REQUEST_PUSH) {
				lock_guard<mutex> lock(g_cout_mutex);
				cout << "\n[Push-Friend] from=" << json.value("from_uid", 0)
					<< " msg: " << json.value("msg", "(empty)")
					<< "\n> " << flush;
			}
			else if (msg_id == MSG_OFFLINE_MSG_RSP && json.contains("messages")) {
				lock_guard<mutex> lock(g_cout_mutex);
				cout << "\n[Auto-Offline] " << json.value("count", 0) << " msgs:\n";
				const nlohmann::json& arr = json["messages"];
				for (size_t i = 0; i < arr.size(); ++i) {
					const nlohmann::json& m = arr[i];
					cout << "  [" << m.value("msg_id", 0) << "] from=" << m.value("from_uid", 0)
						<< " | " << m.value("timestamp", "(unknown)")
						<< "\n    " << m.value("content", "(empty)") << "\n";
				}
				cout << "> " << flush;
			}
			else {
				lock_guard<mutex> lock(g_recv_mutex);
				g_recv_queue.push({ msg_id, json });
				g_recv_cv.notify_one();
			}
		}
		catch (const exception& e) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] push display error: " << e.what() << endl; }
		}
	}
}

// ============ Wait for response ============
nlohmann::json WaitForResponse(short expected_rsp_id, int timeout_sec = 5)
{
	unique_lock<mutex> lock(g_recv_mutex);
	chrono::milliseconds timeout(timeout_sec * 1000);

	while (true) {
		queue<ReceivedMsg> temp;
		bool found = false;
		nlohmann::json result;

		while (!g_recv_queue.empty()) {
			ReceivedMsg msg = std::move(g_recv_queue.front());
			g_recv_queue.pop();
			if (msg.msg_id == expected_rsp_id) {
				result = std::move(msg.json);
				found = true;
			}
			else {
				temp.push(std::move(msg));
			}
		}
		g_recv_queue = std::move(temp);

		if (found) return result;

		if (!g_connected) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] disconnected" << endl; }
			return nlohmann::json();
		}

		auto status = g_recv_cv.wait_for(lock, timeout);
		if (status == cv_status::timeout) {
			{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] timeout (msg_id=" << expected_rsp_id << ")" << endl; }
			return nlohmann::json();
		}
	}
}

// ============ Print response ============
void PrintResponse(const nlohmann::json& rsp)
{
	if (rsp.is_null()) return;

	lock_guard<mutex> lock(g_cout_mutex);

	int status = rsp.value("status", -999);
	cout << (status == 0 ? "[OK] " : "[FAIL] ");

	if (rsp.contains("uid") && rsp["uid"].get<int64_t>() != 0) {
		cout << "uid=" << rsp["uid"].get<int64_t>() << " ";
	}
	if (rsp.contains("msg")) {
		cout << rsp["msg"].get<std::string>() << " ";
	}
	if (rsp.contains("offline_count") && rsp["offline_count"].get<int>() > 0) {
		cout << "offline=" << rsp["offline_count"].get<int>() << " ";
	}
	if (rsp.contains("friends")) {
		cout << "\n  friends: ";
		const nlohmann::json& arr = rsp["friends"];
		for (size_t i = 0; i < arr.size(); ++i)
			cout << arr[i].get<int64_t>() << (i + 1 < arr.size() ? ", " : "");
	}
	if (rsp.contains("messages")) {
		cout << "\n  offline (" << rsp["count"].get<int>() << "):\n";
		const nlohmann::json& arr = rsp["messages"];
		for (size_t i = 0; i < arr.size(); ++i) {
			const nlohmann::json& m = arr[i];
			cout << "    [" << m["msg_id"].get<int64_t>() << "] from=" << m["from_uid"].get<int64_t>()
				<< " | " << m["timestamp"].get<std::string>()
				<< "\n      " << m["content"].get<std::string>() << "\n";
		}
	}
	if (rsp.contains("msg_id")) {
		cout << "msg_id=" << rsp["msg_id"].get<int64_t>() << " ";
	}
	cout << endl;
}

// ============ Help ============
void PrintHelp()
{
	lock_guard<mutex> lock(g_cout_mutex);
	cout << R"(Commands:
  /register <user> <pass>
  /login <user> <pass>
  /friends
  /friend add <uid>
  /chat <uid> <message>
  /offline
  /reconnect
  /help
  /quit
)";
}

// ============ Do login (for both initial and re-login) ============
bool DoLogin(tcp::socket& sock)
{
	if (g_last_username.empty()) {
		{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] no cached credentials, use /login first" << endl; }
		return false;
	}

	nlohmann::json req;
	req["username"] = g_last_username;
	req["password"] = g_last_password;

	if (!SendMsg(sock, MSG_LOGIN_REQ, req)) return false;
	nlohmann::json rsp = WaitForResponse(MSG_LOGIN_RSP);
	if (rsp.is_null()) return false;

	PrintResponse(rsp);
	if (rsp.value("status", -1) == 0) {
		g_logged_in = true;
		{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] re-login ok uid=" << rsp["uid"].get<int64_t>() << endl; }
		return true;
	}
	return false;
}

// ============ Try connect ============
bool TryConnect(boost::asio::io_context& ioc, tcp::socket& sock)
{
	boost::system::error_code ec;
	tcp::endpoint remote_ep(boost::asio::ip::make_address("127.0.0.1"), 8080);
	sock.connect(remote_ep, ec);
	if (ec) {
		{ lock_guard<mutex> lock(g_cout_mutex); cerr << "[Client] connect failed: " << ec.message() << endl; }
		return false;
	}
	g_connected = true;
	{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] connected to 127.0.0.1:8080" << endl; }
	return true;
}

// ============ Main ============
int main()
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	{ lock_guard<mutex> lock(g_cout_mutex); cout << "=== Boost.Asio IM Test Client ===" << endl; }
	{ lock_guard<mutex> lock(g_cout_mutex); cout << "Type /help for commands" << endl; }

	try {
		auto ioc = std::make_unique<boost::asio::io_context>(1);
		auto sock = std::make_shared<tcp::socket>(*ioc);

		if (!TryConnect(*ioc, *sock)) return 1;

		thread recv_thread([sock]() { RecvThreadFunc(*sock); });

		string line;
		while (true) {
			if (!g_connected) {
				{ lock_guard<mutex> lock(g_cout_mutex); cout << "\n[Client] connection lost!" << endl; }

				g_stop_recv = true;
				g_recv_cv.notify_all();
				boost::system::error_code ec;
				sock->close(ec);
				if (recv_thread.joinable()) recv_thread.join();
				recv_thread = thread();

				{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] type /reconnect or /quit" << endl; }
				if (!getline(cin, line)) break;

				istringstream iss(line);
				string cmd;
				iss >> cmd;

				if (cmd == "/reconnect" || cmd == "/quit") {
					if (cmd == "/quit") break;

					g_stop_recv = false;
					g_connected = false;
					g_logged_in = false;
					{
						lock_guard<mutex> lock(g_recv_mutex);
						while (!g_recv_queue.empty()) g_recv_queue.pop();
					}

					{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] reconnecting..." << endl; }
					sock.reset();
					ioc = std::make_unique<boost::asio::io_context>(1);
					sock = std::make_shared<tcp::socket>(*ioc);

					if (!TryConnect(*ioc, *sock)) return 1;

					{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] restoring login state..." << endl; }
					if (!DoLogin(*sock)) {
						{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] re-login failed, use /login manually" << endl; }
					}

					recv_thread = thread([sock]() { RecvThreadFunc(*sock); });
					continue;
				}
				else {
					break;
				}
			}

			{ lock_guard<mutex> lock(g_cout_mutex);
			cout << "> " << flush; }
			if (!getline(cin, line)) break;
			if (line.empty()) continue;

			istringstream iss(line);
			string cmd;
			iss >> cmd;

			// ----- /register -----
			if (cmd == "/register") {
				string user, pass;
				if (!(iss >> user >> pass)) { { lock_guard<mutex> lock(g_cout_mutex); cerr << "Usage: /register <user> <pass>" << endl; } continue; }
				if (!ValidateUsername(user) || !ValidatePassword(pass)) continue;
				nlohmann::json req;
				req["username"] = user;
				req["password"] = pass;
				if (!SendMsg(*sock, MSG_REGISTER_REQ, req)) continue;
				PrintResponse(WaitForResponse(MSG_REGISTER_RSP));
			}

			// ----- /login -----
			else if (cmd == "/login") {
				string user, pass;
				if (!(iss >> user >> pass)) { { lock_guard<mutex> lock(g_cout_mutex); cerr << "Usage: /login <user> <pass>" << endl; } continue; }
				if (!ValidateUsername(user) || !ValidatePassword(pass)) continue;
				nlohmann::json req;
				req["username"] = user;
				req["password"] = pass;
				if (!SendMsg(*sock, MSG_LOGIN_REQ, req)) continue;
				nlohmann::json rsp = WaitForResponse(MSG_LOGIN_RSP);
				PrintResponse(rsp);
				if (rsp.value("status", -1) == 0) {
					g_logged_in = true;
					g_last_username = user;
					g_last_password = pass;
				}
			}

			// ----- /friends -----
			else if (cmd == "/friends") {
				nlohmann::json req;
				if (!SendMsg(*sock, MSG_GET_FRIEND_LIST_REQ, req)) continue;
				PrintResponse(WaitForResponse(MSG_GET_FRIEND_LIST_RSP));
			}

			// ----- /friend add -----
			else if (cmd == "/friend") {
				string sub;
				if (!(iss >> sub)) { { lock_guard<mutex> lock(g_cout_mutex); cerr << "Usage: /friend add <uid>" << endl; } continue; }
				if (sub != "add") { { lock_guard<mutex> lock(g_cout_mutex); cerr << "Unknown: /friend " << sub << endl; } continue; }
				int64_t friend_uid;
				if (!(iss >> friend_uid)) { { lock_guard<mutex> lock(g_cout_mutex); cerr << "Usage: /friend add <uid>" << endl; } continue; }
				nlohmann::json req;
				req["friend_uid"] = static_cast<int64_t>(friend_uid);
				if (!SendMsg(*sock, MSG_ADD_FRIEND_REQ, req)) continue;
				PrintResponse(WaitForResponse(MSG_ADD_FRIEND_RSP));
			}

			// ----- /chat -----
			else if (cmd == "/chat") {
				int64_t to_uid;
				if (!(iss >> to_uid)) { { lock_guard<mutex> lock(g_cout_mutex); cerr << "Usage: /chat <uid> <message>" << endl; } continue; }
				string content;
				getline(iss, content);
				size_t start = content.find_first_not_of(" \t");
				if (start != string::npos) content = content.substr(start);
				if (content.empty()) { { lock_guard<mutex> lock(g_cout_mutex); cerr << "message empty" << endl; } continue; }
				nlohmann::json req;
				req["to_uid"]  = static_cast<int64_t>(to_uid);
				req["content"] = content;
				if (!SendMsg(*sock, MSG_SEND_CHAT_REQ, req)) continue;
				PrintResponse(WaitForResponse(MSG_SEND_CHAT_RSP));
			}

			// ----- /offline -----
			else if (cmd == "/offline") {
				nlohmann::json req;
				if (!SendMsg(*sock, MSG_OFFLINE_MSG_REQ, req)) continue;
				PrintResponse(WaitForResponse(MSG_OFFLINE_MSG_RSP));
			}

			// ----- /reconnect -----
			else if (cmd == "/reconnect") {
				{ lock_guard<mutex> lock(g_cout_mutex); cout << "[Client] manual reconnect..." << endl; }
				g_connected = false;
				continue;
			}

			// ----- /help -----
			else if (cmd == "/help") {
				PrintHelp();
			}

			// ----- /quit -----
			else if (cmd == "/quit") {
				break;
			}

			else {
				{ lock_guard<mutex> lock(g_cout_mutex); cerr << "Unknown: " << cmd << ". Type /help" << endl; }
			}
		}

		g_stop_recv = true;
		g_connected = false;
		g_logged_in = false;
		g_recv_cv.notify_all();
		boost::system::error_code ec;
		sock->close(ec);
		if (recv_thread.joinable()) recv_thread.join();
		{ lock_guard<mutex> lock(g_cout_mutex); cout << "client exited" << endl; }
	}
	catch (const exception& e) {
		{ lock_guard<mutex> lock(g_cout_mutex); cerr << "fatal: " << e.what() << endl; }
		return 1;
	}
	return 0;
}
