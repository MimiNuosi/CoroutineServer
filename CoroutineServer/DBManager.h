#pragma once
/// @file  DBManager.h
/// @brief SQLite database manager for user accounts, friend relationships,
///        and chat message persistence. Thread-safe via internal mutex.

#include <sqlite3.h>
#include <string>
#include <vector>
#include <utility>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

/// @class DBManager
/// @brief Singleton managing all SQLite operations.
/// @details Uses RAII StmtGuard to auto-finalize prepared statements.
///          All public methods acquire _mutex for thread safety.
class DBManager
{
public:
	static DBManager& GetInstance();

	DBManager(const DBManager&) = delete;
	DBManager& operator=(const DBManager&) = delete;

	/// @brief Register a new user with hashed password.
	/// @return true on success, false if username exists or DB error.
	bool RegisterUser(const std::string& username, const std::string& password);

	/// @brief Authenticate a user by username and password.
	/// @return (true, uid) on success; (false, 0) on failure.
	std::pair<bool, int64_t> LoginUser(const std::string& username, const std::string& password);

	/// @brief Add a bidirectional friend relationship (uid <-> friend_uid).
	/// @return true on success, false if already friends or users not found.
	bool AddFriend(int64_t uid, int64_t friend_uid);

	/// @brief Get the friend list for a uid.
	/// @return Vector of friend UIDs (empty if uid invalid).
	std::vector<int64_t> GetFriendList(int64_t uid);

	/// @brief Save a chat message with timestamp.
	/// @return message ID on success; -1 on failure.
	int64_t SaveMessage(int64_t from_uid, int64_t to_uid,
		const std::string& content, const std::string& timestamp);

	/// @brief Get undelivered offline messages for uid, then mark them delivered=1.
	/// @return JSON vector of message objects.
	std::vector<nlohmann::json> GetOfflineMessages(int64_t uid);

private:
	DBManager();
	~DBManager();

	/// @brief RAII wrapper for sqlite3_stmt - guarantees finalize on scope exit.
	struct StmtGuard {
		sqlite3_stmt* stmt;
		explicit StmtGuard(sqlite3_stmt* s = nullptr) : stmt(s) {}
		~StmtGuard() { if (stmt) sqlite3_finalize(stmt); }
		operator sqlite3_stmt* () const { return stmt; }
		StmtGuard(const StmtGuard&) = delete;
		StmtGuard& operator=(const StmtGuard&) = delete;
	};

	/// @brief Create database tables if they don't exist.
	void CreateTables();

	/// @brief Execute a SQL statement with no result set (for DDL).
	bool ExecuteSQL(const std::string& sql);

	/// @brief Simple salted password hash using std::hash.
	std::string SimpleHash(const std::string& input);

	sqlite3* _db;
	std::mutex _mutex;          ///< Serializes all DB access
};
