#include <iostream>
#include "CServer.h"
#include <csignal>
#include <thread>
#include <mutex>
#include "AsioIOServicePool.h"
#include "Timestamp.h"

int main()
{
	try
	{
		std::cout << Timestamp() << " [Main] CoroutineServer starting..."
			<< " threads=" << std::thread::hardware_concurrency() << std::endl;
		auto& pool = AsioIOServicePool::GetInstance();
		boost::asio::io_context io_context;
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&io_context, &pool](auto error_code, auto signal_number) {
			try {
				std::cout << Timestamp() << " [Main] received signal #" << signal_number
					<< " (" << (error_code ? error_code.message() : "ok")
					<< "), shutting down..." << std::endl;
				io_context.stop();
				pool.Stop();
			}
			catch (const std::exception& e) {
				std::cerr << Timestamp() << " [Main] signal handler exception: " << e.what() << std::endl;
			}
		});
		CServer server(io_context, 8080);
		std::cout << Timestamp() << " [Main] server running, press Ctrl+C to stop" << std::endl;
		io_context.run();
		std::cout << Timestamp() << " [Main] server stopped" << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cerr << Timestamp() << " [Main] fatal exception: " << e.what() << "\n";
		// Ensure pool is stopped even on early failure (destructor handles this safely)
		return 1;
	}
	return 0;
}
