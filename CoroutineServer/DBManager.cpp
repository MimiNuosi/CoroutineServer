#include "DBManager.h"
#include "Timestamp.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <ctime>

// ============ Constructor / Destructor ============

DBManager::DBManager() : _db(nullptr)
{
	int rc = sqlite3_open("im.db", &_db);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] failed to open database: " << sqlite3_errmsg(_db) << std::endl;
		sqlite3_close(_db);
		_db = nullptr;
		throw std::runtime_error("cannot open im.db database");
	}

	std::cout << Timestamp() << " [DBManager] database im.db opened" << std::endl;

	ExecuteSQL("PRAGMA journal_mode=WAL;");
	ExecuteSQL("PRAGMA foreign_keys=ON;");

	CreateTables();
	std::cout << Timestamp() << " [DBManager] database tables initialized" << std::endl;
}

DBManager::~DBManager()
{
	if (_db)
	{
		sqlite3_close(_db);
		_db = nullptr;
		std::cout << Timestamp() << " [DBManager] database connection closed" << std::endl;
	}
}

DBManager& DBManager::GetInstance()
{
	static DBManager instance;
	return instance;
}

// ============ Internal Helpers ============

void DBManager::CreateTables()
{
	const char* sql_users = R"(
		CREATE TABLE IF NOT EXISTS users (
			uid           INTEGER PRIMARY KEY AUTOINCREMENT,
			username      TEXT    NOT NULL UNIQUE,
			password_hash TEXT    NOT NULL,
			created_at    TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
		);
	)";
	if (!ExecuteSQL(sql_users))
		throw std::runtime_error("failed to create users table");

	const char* sql_friends = R"(
		CREATE TABLE IF NOT EXISTS friends (
			id         INTEGER PRIMARY KEY AUTOINCREMENT,
			uid        INTEGER NOT NULL,
			friend_uid INTEGER NOT NULL,
			created_at TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
			UNIQUE(uid, friend_uid)
		);
	)";
	if (!ExecuteSQL(sql_friends))
		throw std::runtime_error("failed to create friends table");

	ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_friends_uid ON friends(uid);");
	ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_friends_friend_uid ON friends(friend_uid);");

	const char* sql_messages = R"(
		CREATE TABLE IF NOT EXISTS messages (
			msg_id    INTEGER PRIMARY KEY AUTOINCREMENT,
			from_uid  INTEGER NOT NULL,
			to_uid    INTEGER NOT NULL,
			content   TEXT    NOT NULL,
			timestamp TEXT    NOT NULL,
			msg_type  INTEGER NOT NULL DEFAULT 0,
			delivered INTEGER NOT NULL DEFAULT 0
		);
	)";
	if (!ExecuteSQL(sql_messages))
		throw std::runtime_error("failed to create messages table");

	ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_messages_to_delivered ON messages(to_uid, delivered);");
}

bool DBManager::ExecuteSQL(const std::string& sql)
{
	char* err_msg = nullptr;
	int rc = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &err_msg);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] SQL execution failed: " << err_msg << std::endl;
		std::cerr << Timestamp() << " [DBManager] SQL: " << sql << std::endl;
		sqlite3_free(err_msg);
		return false;
	}
	return true;
}

std::string DBManager::SimpleHash(const std::string& input)
{
	// Guard: empty input produces a deterministic hash, but we log a warning
	if (input.empty()) {
		std::cerr << Timestamp() << " [DBManager] SimpleHash called with empty input" << std::endl;
	}

	static const std::string salt = "CoroutineServer_IM_SALT_2024";
	std::string salted = salt + input;
	std::hash<std::string> hasher;
	size_t hash_val = hasher(salted);
	std::ostringstream oss;
	oss << std::hex << std::setfill('0') << std::setw(16) << hash_val;
	return oss.str();
}

// ============ User Management ============

bool DBManager::RegisterUser(const std::string& username, const std::string& password)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (username.empty() || password.empty())
	{
		std::cerr << Timestamp() << " [DBManager] register failed: username or password empty" << std::endl;
		return false;
	}

	const char* sql_check = "SELECT uid FROM users WHERE username = ?;";
	StmtGuard stmt_check;
	int rc = sqlite3_prepare_v2(_db, sql_check, -1, &stmt_check.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare query failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	sqlite3_bind_text(stmt_check, 1, username.c_str(), -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt_check);
	if (rc == SQLITE_ROW)
	{
		std::cerr << Timestamp() << " [DBManager] register failed: username \"" << username << "\" already exists" << std::endl;
		return false;
	}

	std::string hash = SimpleHash(password);
	const char* sql_insert = "INSERT INTO users (username, password_hash) VALUES (?, ?);";
	StmtGuard stmt_insert;
	rc = sqlite3_prepare_v2(_db, sql_insert, -1, &stmt_insert.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare insert failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	sqlite3_bind_text(stmt_insert, 1, username.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_insert, 2, hash.c_str(), -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt_insert);

	if (rc != SQLITE_DONE)
	{
		std::cerr << Timestamp() << " [DBManager] insert user failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	std::cout << Timestamp() << " [DBManager] user registered: " << username
		<< " (uid=" << sqlite3_last_insert_rowid(_db) << ")" << std::endl;
	return true;
}

std::pair<bool, int64_t> DBManager::LoginUser(const std::string& username, const std::string& password)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (username.empty() || password.empty())
	{
		std::cerr << Timestamp() << " [DBManager] login failed: username or password empty" << std::endl;
		return { false, 0 };
	}

	const char* sql = "SELECT uid, password_hash FROM users WHERE username = ?;";
	StmtGuard stmt;
	int rc = sqlite3_prepare_v2(_db, sql, -1, &stmt.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare query failed: " << sqlite3_errmsg(_db) << std::endl;
		return { false, 0 };
	}

	sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW)
	{
		std::cerr << Timestamp() << " [DBManager] login failed: username \"" << username << "\" not found" << std::endl;
		return { false, 0 };
	}

	int64_t uid = sqlite3_column_int64(stmt, 0);
	const char* stored_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
	std::string input_hash = SimpleHash(password);

	if (stored_hash && input_hash == std::string(stored_hash))
	{
		std::cout << Timestamp() << " [DBManager] user logged in: " << username << " (uid=" << uid << ")" << std::endl;
		return { true, uid };
	}

	std::cerr << Timestamp() << " [DBManager] login failed: wrong password (username=\"" << username << "\")" << std::endl;
	return { false, 0 };
}

// ============ Friends Management ============

bool DBManager::AddFriend(int64_t uid, int64_t friend_uid)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (uid <= 0 || friend_uid <= 0)
	{
		std::cerr << Timestamp() << " [DBManager] add friend failed: invalid uid" << std::endl;
		return false;
	}

	if (uid == friend_uid)
	{
		std::cerr << Timestamp() << " [DBManager] add friend failed: cannot add yourself" << std::endl;
		return false;
	}

	// check if both users exist
	const char* sql_check = "SELECT COUNT(*) FROM users WHERE uid = ? OR uid = ?;";
	StmtGuard stmt_check;
	int rc = sqlite3_prepare_v2(_db, sql_check, -1, &stmt_check.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	sqlite3_bind_int64(stmt_check, 1, uid);
	sqlite3_bind_int64(stmt_check, 2, friend_uid);
	rc = sqlite3_step(stmt_check);
	int count = (rc == SQLITE_ROW) ? sqlite3_column_int(stmt_check, 0) : 0;

	if (count < 2)
	{
		std::cerr << Timestamp() << " [DBManager] add friend failed: user not found" << std::endl;
		return false;
	}

	// check if already friends
	const char* sql_exist = R"(
		SELECT id FROM friends
		WHERE (uid = ? AND friend_uid = ?) OR (uid = ? AND friend_uid = ?);
	)";
	StmtGuard stmt_exist;
	rc = sqlite3_prepare_v2(_db, sql_exist, -1, &stmt_exist.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	sqlite3_bind_int64(stmt_exist, 1, uid);
	sqlite3_bind_int64(stmt_exist, 2, friend_uid);
	sqlite3_bind_int64(stmt_exist, 3, friend_uid);
	sqlite3_bind_int64(stmt_exist, 4, uid);
	rc = sqlite3_step(stmt_exist);

	if (rc == SQLITE_ROW)
	{
		std::cerr << Timestamp() << " [DBManager] add friend failed: already friends" << std::endl;
		return false;
	}

	const char* sql_insert = "INSERT INTO friends (uid, friend_uid) VALUES (?, ?);";
	StmtGuard stmt_insert;
	rc = sqlite3_prepare_v2(_db, sql_insert, -1, &stmt_insert.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare insert failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	sqlite3_bind_int64(stmt_insert, 1, uid);
	sqlite3_bind_int64(stmt_insert, 2, friend_uid);
	rc = sqlite3_step(stmt_insert);

	if (rc != SQLITE_DONE)
	{
		std::cerr << Timestamp() << " [DBManager] add friend failed: " << sqlite3_errmsg(_db) << std::endl;
		return false;
	}

	std::cout << Timestamp() << " [DBManager] friend added: uid=" << uid << " <-> friend_uid=" << friend_uid << std::endl;
	return true;
}

std::vector<int64_t> DBManager::GetFriendList(int64_t uid)
{
	std::lock_guard<std::mutex> lock(_mutex);

	std::vector<int64_t> result;
	if (uid <= 0)
	{
		std::cerr << Timestamp() << " [DBManager] get friend list failed: invalid uid (" << uid << ")" << std::endl;
		return result;
	}

	const char* sql = R"(
		SELECT friend_uid AS friend_id FROM friends WHERE uid = ?
		UNION
		SELECT uid AS friend_id FROM friends WHERE friend_uid = ?;
	)";

	StmtGuard stmt;
	int rc = sqlite3_prepare_v2(_db, sql, -1, &stmt.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare query failed: " << sqlite3_errmsg(_db) << std::endl;
		return result;
	}

	sqlite3_bind_int64(stmt, 1, uid);
	sqlite3_bind_int64(stmt, 2, uid);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		result.push_back(sqlite3_column_int64(stmt, 0));
	}

	std::cout << Timestamp() << " [DBManager] friend list: uid=" << uid << ", count=" << result.size() << std::endl;
	return result;
}

// ============ Message Management ============

int64_t DBManager::SaveMessage(int64_t from_uid, int64_t to_uid,
	const std::string& content, const std::string& timestamp)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (from_uid <= 0 || to_uid <= 0 || content.empty())
	{
		std::cerr << Timestamp() << " [DBManager] save message failed: invalid params" << std::endl;
		return -1;
	}

	const char* sql = R"(
		INSERT INTO messages (from_uid, to_uid, content, timestamp)
		VALUES (?, ?, ?, ?);
	)";

	StmtGuard stmt;
	int rc = sqlite3_prepare_v2(_db, sql, -1, &stmt.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare insert failed: " << sqlite3_errmsg(_db) << std::endl;
		return -1;
	}

	sqlite3_bind_int64(stmt, 1, from_uid);
	sqlite3_bind_int64(stmt, 2, to_uid);
	sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, timestamp.c_str(), -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE)
	{
		std::cerr << Timestamp() << " [DBManager] save message failed: " << sqlite3_errmsg(_db) << std::endl;
		return -1;
	}

	int64_t msg_id = sqlite3_last_insert_rowid(_db);

	std::cout << Timestamp() << " [DBManager] message saved: msg_id=" << msg_id
		<< " from=" << from_uid << " to=" << to_uid << std::endl;
	return msg_id;
}

std::vector<nlohmann::json> DBManager::GetOfflineMessages(int64_t uid)
{
	std::lock_guard<std::mutex> lock(_mutex);

	std::vector<nlohmann::json> result;
	if (uid <= 0)
	{
		std::cerr << Timestamp() << " [DBManager] get offline messages failed: invalid uid (" << uid << ")" << std::endl;
		return result;
	}

	const char* sql_select = R"(
		SELECT msg_id, from_uid, to_uid, content, timestamp, msg_type
		FROM messages
		WHERE to_uid = ? AND delivered = 0
		ORDER BY msg_id ASC;
	)";

	StmtGuard stmt_select;
	int rc = sqlite3_prepare_v2(_db, sql_select, -1, &stmt_select.stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		std::cerr << Timestamp() << " [DBManager] prepare query failed: " << sqlite3_errmsg(_db) << std::endl;
		return result;
	}

	sqlite3_bind_int64(stmt_select, 1, uid);

	while ((rc = sqlite3_step(stmt_select)) == SQLITE_ROW)
	{
		nlohmann::json msg;
		msg["msg_id"]   = static_cast<int64_t>(sqlite3_column_int64(stmt_select, 0));
		msg["from_uid"] = static_cast<int64_t>(sqlite3_column_int64(stmt_select, 1));
		msg["to_uid"]   = static_cast<int64_t>(sqlite3_column_int64(stmt_select, 2));
		msg["content"]  = reinterpret_cast<const char*>(sqlite3_column_text(stmt_select, 3));
		msg["timestamp"]= reinterpret_cast<const char*>(sqlite3_column_text(stmt_select, 4));
		msg["msg_type"] = sqlite3_column_int(stmt_select, 5);
		result.push_back(msg);
	}

	if (!result.empty())
	{
		const char* sql_update = R"(
			UPDATE messages SET delivered = 1
			WHERE to_uid = ? AND delivered = 0;
		)";

		StmtGuard stmt_update;
		rc = sqlite3_prepare_v2(_db, sql_update, -1, &stmt_update.stmt, nullptr);
		if (rc == SQLITE_OK)
		{
			sqlite3_bind_int64(stmt_update, 1, uid);
			rc = sqlite3_step(stmt_update);
			if (rc != SQLITE_DONE)
			{
				std::cerr << Timestamp() << " [DBManager] mark offline messages delivered failed: " << sqlite3_errmsg(_db) << std::endl;
			}
		}
	}

	std::cout << Timestamp() << " [DBManager] offline messages: uid=" << uid << ", count=" << result.size() << std::endl;
	return result;
}
