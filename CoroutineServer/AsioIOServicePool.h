#pragma once
#include <boost/asio.hpp>
#include <vector>

class AsioIOServicePool {
public:
	using IOService = boost::asio::io_context;
	using Work = boost::asio::executor_work_guard<IOService::executor_type>;
	using WorkPtr = std::unique_ptr<Work>;
	~AsioIOServicePool();
	AsioIOServicePool(const AsioIOServicePool&) = delete;
	AsioIOServicePool& operator =(const AsioIOServicePool&) = delete;
	boost::asio::io_context& GetIOService();
	void Stop();
	static AsioIOServicePool& GetInstance();
private:
	AsioIOServicePool(std::size_t size = std::thread::hardware_concurrency());
	std::vector<IOService> _ioService;
	std::vector<WorkPtr> _works;
	std::vector<std::thread> _threads;
	std::size_t _nextIOService;
};