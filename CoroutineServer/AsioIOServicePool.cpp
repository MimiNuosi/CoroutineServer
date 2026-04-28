#include "AsioIOServicePool.h"
#include <iostream>


AsioIOServicePool::AsioIOServicePool(std::size_t size)
	:_ioService(size), _works(size) ,_nextIOService(0){
	for (std::size_t i = 0; i < size; i++) {
		_works[i] = std::make_unique<Work>(boost::asio::make_work_guard(_ioService[i]));
	}
	for (std::size_t i = 0; i < size; i++) {
		_threads.emplace_back([this, i]() {
			_ioService[i].run();
			});
	}
}

AsioIOServicePool::~AsioIOServicePool()
{
	std::cout << "AsioIOServicePool destruct" << "\n";
}

AsioIOServicePool::IOService& AsioIOServicePool::GetIOService()
{
	auto& service = _ioService[_nextIOService++];
	if (_nextIOService == _ioService.size()) {
		_nextIOService = 0;
	}
	return service;
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
