#pragma once
#include <queue>
#include <thread>
#include "CSession.h"
#include <map>
#include <functional>
#include "const.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

typedef std::function <void(std::shared_ptr<CSession>
	, const short& msg_id, const std::string& msg_data)> FunCallBack;
class LogicSystem
{
public:
	~LogicSystem();
	void PostMsgToQue(std::shared_ptr<LogicNode> msg);
	static LogicSystem& GetInstance();
	LogicSystem(const LogicSystem&) = delete;
	LogicSystem& operator=(const LogicSystem&) = delete;
private:
	LogicSystem();
	void DealMsg();
	void RegisterCallBack();
	void HelloWorldCallBack(std::shared_ptr<CSession>
		, const short& msg_id, const std::string& msg_data);
	std::thread _worker_thread;
	std::queue<std::shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FunCallBack> _fun_callbacks;
};

