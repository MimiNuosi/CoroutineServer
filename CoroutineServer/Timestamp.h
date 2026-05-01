#pragma once
/// @file  Timestamp.h
/// @brief Common timestamp helper: all server modules share this single definition.
///        Replaces the duplicated static Timestamp() in each .cpp file.
/// @note  Uses localtime_s (Windows-safe), returns "[HH:MM:SS]" with milliseconds.

#include <chrono>
#include <ctime>
#include <sstream>
#include <string>

inline std::string Timestamp()
{
	auto now = std::chrono::system_clock::now();
	auto t = std::chrono::system_clock::to_time_t(now);

	// milliseconds for finer-grained logs
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;

	char buf[32] = {};
	struct tm tm_info{};
	localtime_s(&tm_info, &t);
	std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_info);

	std::ostringstream oss;
	oss << "[" << buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
	return oss.str();
}
