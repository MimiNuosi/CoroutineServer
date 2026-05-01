#pragma once
/// @file  LogicSystem.h
/// @brief Business logic dispatch with a dedicated worker thread.
///        Receives LogicNode messages via PostMsgToQue() and dispatches
///        to registered callbacks based on message ID.

#include <queue>
#include <thread>
#include "CSession.h"
#include <map>
#include <functional>
#include "const.h"
#include <nlohmann/json.hpp>

/// @brief Callback signature: (session, msg_id, msg_data_json_string)
typedef std::function <void(std::shared_ptr<CSession>
	, const short& msg_id, const std::string& msg_data)> FunCallBack;

/// @class LogicSystem
/// @brief Singleton message dispatcher with thread-safe input queue.
/// @details On construction, starts a worker thread to process LogicNode
///          messages from the queue. Callbacks are registered for each
///          message ID in RegisterCallBack(). Destructor signals the
///          worker thread to stop and joins it.
class LogicSystem
{
public:
	~LogicSystem();

	/// @brief Thread-safe enqueue of a LogicNode. Drops if queue is full.
	void PostMsgToQue(std::shared_ptr<LogicNode> msg);

	static LogicSystem& GetInstance();
	LogicSystem(const LogicSystem&) = delete;
	LogicSystem& operator=(const LogicSystem&) = delete;
private:
	LogicSystem();

	/// @brief Worker thread main loop: wait for messages, dispatch.
	void DealMsg();

	/// @brief Register all message ID -> handler mappings.
	void RegisterCallBack();

	// ---- Callback implementations ----
	void HelloWorldCallBack(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	void RegisterRequest(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	void LoginRequest(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	void GetFriendListRequest(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	void AddFriendRequest(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	void ChatMessageRequest(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	void GetOfflineMessageRequest(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);

	std::thread _worker_thread;
	std::queue<std::shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FunCallBack> _fun_callbacks;
};
