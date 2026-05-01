#pragma once
/// @file  UserManager.h
/// @brief Online user session tracker using weak_ptr to avoid circular ownership.

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <mutex>

class CSession;

/// @class UserManager
/// @brief Maps uid -> weak_ptr<CSession> for online user tracking.
/// @details Uses weak_ptr to avoid extending CSession lifetime.
///          Expired weak_ptrs are cleaned up lazily in GetSessionByUid().
class UserManager
{
public:
	static UserManager& GetInstance();

	UserManager(const UserManager&) = delete;
	UserManager& operator=(const UserManager&) = delete;

	/// @brief Register a user as online after successful login.
	void AddOnlineUser(int64_t uid, std::shared_ptr<CSession> session);

	/// @brief Remove a user from the online map (called on disconnect).
	void RemoveOnlineUser(int64_t uid);

	/// @brief Get the CSession for an online user.
	/// @return shared_ptr to session, or nullptr if offline/expired.
	///         Expired entries are cleaned up on access.
	std::shared_ptr<CSession> GetSessionByUid(int64_t uid);

private:
	UserManager() = default;
	~UserManager() = default;

	std::unordered_map<int64_t, std::weak_ptr<CSession>> _online_users;
	std::mutex _mutex;          ///< Protects _online_users
};
