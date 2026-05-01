#include "AsioIOServicePool.h"
#include "Timestamp.h"
#include <iostream>

AsioIOServicePool::AsioIOServicePool(std::size_t size)
	:_ioService(size), _works(size) ,_nextIOService(0){
	for (std::size_t i = 0; i < size; i++) {
		_works[i] = std::make_unique<Work>(boost::asio::make_work_guard(_ioService[i]));
	}
	for (std::size_t i = 0; i < size; i++) {
		_threads.emplace_back([this, i]() {
			try {
				_ioService[i].run();
			}
			catch (const std::exception& e) {
				std::cerr << Timestamp() << " [AsioIOServicePool] io_context[" << i
					<< "] run() exception: " << e.what() << std::endl;
			}
			catch (...) {
				std::cerr << Timestamp() << " [AsioIOServicePool] io_context[" << i
					<< "] run() unknown fatal exception" << std::endl;
			}
		});
	}
}

AsioIOServicePool::~AsioIOServicePool()
{
	// CRITICAL: Must call Stop() before implicit member destruction.
	// If threads are still joinable when std::thread destructor runs,
	// std::terminate() is called (C++ standard). Stop() resets works
	// and joins all threads gracefully.
	std::cout << Timestamp() << " [AsioIOServicePool] destructor begin" << std::endl;
	try {
		Stop();
	}
	catch (const std::exception& e) {
		std::cerr << Timestamp() << " [AsioIOServicePool] Stop() exception: " << e.what() << std::endl;
	}
	catch (...) {
		std::cerr << Timestamp() << " [AsioIOServicePool] Stop() unknown exception" << std::endl;
	}
}

AsioIOServicePool::IOService& AsioIOServicePool::GetIOService()
{
	// atomic increment + modulo, thread-safe round-robin
	std::size_t idx = _nextIOService.fetch_add(1, std::memory_order_relaxed);
	idx %= _ioService.size();
	return _ioService[idx];
}

void AsioIOServicePool::Stop()
{
	for (auto& work : _works) {
		work.reset();
	}

	for (auto& t : _threads) {
		t.join();
	}
}

AsioIOServicePool& AsioIOServicePool::GetInstance()
{
	static AsioIOServicePool instance;
	return instance;
}
