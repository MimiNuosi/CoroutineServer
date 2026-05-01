#include "UserManager.h"
#include "CSession.h"
#include "Timestamp.h"
#include <iostream>

UserManager& UserManager::GetInstance()
{
	static UserManager instance;
	return instance;
}

void UserManager::AddOnlineUser(int64_t uid, std::shared_ptr<CSession> session)
{
	if (uid <= 0 || !session)
	{
		std::cerr << Timestamp() << " [UserManager] AddOnlineUser failed: uid=" << uid << std::endl;
		return;
	}

	std::lock_guard<std::mutex> lock(_mutex);
	_online_users[uid] = session;
	std::cout << Timestamp() << " [UserManager] user online: uid=" << uid
		<< " online_count=" << _online_users.size() << std::endl;
}

void UserManager::RemoveOnlineUser(int64_t uid)
{
	if (uid <= 0) return;

	std::lock_guard<std::mutex> lock(_mutex);
	auto it = _online_users.find(uid);
	if (it != _online_users.end())
	{
		_online_users.erase(it);
		std::cout << Timestamp() << " [UserManager] user offline: uid=" << uid
			<< " online_count=" << _online_users.size() << std::endl;
	}
	else
	{
		std::cout << Timestamp() << " [UserManager] RemoveOnlineUser: uid=" << uid << " was not online" << std::endl;
	}
}

std::shared_ptr<CSession> UserManager::GetSessionByUid(int64_t uid)
{
	if (uid <= 0) return nullptr;

	std::lock_guard<std::mutex> lock(_mutex);
	auto it = _online_users.find(uid);
	if (it == _online_users.end()) return nullptr;

	std::shared_ptr<CSession> session = it->second.lock();
	if (!session)
	{
		// Clean up expired weak_ptr to prevent accumulation
		_online_users.erase(it);
		std::cout << Timestamp() << " [UserManager] GetSessionByUid: uid=" << uid << " session expired, cleaned" << std::endl;
	}

	return session;
}
