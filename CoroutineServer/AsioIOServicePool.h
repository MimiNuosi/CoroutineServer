#pragma once
/// @file  AsioIOServicePool.h
/// @brief Thread pool of boost::asio::io_context instances.
///        Distributes connections via atomic round-robin for load balancing.

#include <boost/asio.hpp>
#include <vector>
#include <atomic>

/// @class AsioIOServicePool
/// @brief Singleton pool managing N io_context + N worker threads.
/// @note  Destructor calls Stop() to safely join all threads before
///        implicit member destruction (avoids std::terminate()).
class AsioIOServicePool {
public:
	using IOService = boost::asio::io_context;
	using Work = boost::asio::executor_work_guard<IOService::executor_type>;
	using WorkPtr = std::unique_ptr<Work>;

	~AsioIOServicePool();
	AsioIOServicePool(const AsioIOServicePool&) = delete;
	AsioIOServicePool& operator =(const AsioIOServicePool&) = delete;

	/// @brief Get next io_context in round-robin order (thread-safe).
	boost::asio::io_context& GetIOService();

	/// @brief Gracefully stop all io_context workers and join threads.
	void Stop();

	/// @brief Get the global singleton instance.
	static AsioIOServicePool& GetInstance();
private:
	/// @param size Number of worker threads (default: hardware_concurrency).
	AsioIOServicePool(std::size_t size = std::thread::hardware_concurrency());

	std::vector<IOService> _ioService;
	std::vector<WorkPtr> _works;
	std::vector<std::thread> _threads;
	std::atomic<std::size_t> _nextIOService;
};
