#pragma once
/// @file const.h
/// @brief Protocol constants, queue limits, message IDs, and input validation bounds.
///        Shared between CoroutineServer and TestClient.

// ============ Protocol constants ============
#define MAX_LENGTH      (1024 * 2)     // max single message body length in bytes
#define HEAD_TOTAL_LEN  4              // header total length: msg_id(2) + body_len(2)
#define HEAD_ID_LEN     2              // message ID field length
#define HEAD_DATA_LEN   2              // body length field length

// ============ Queue limits ============
#define MAX_RECVQUE     10000          // max pending messages in LogicSystem queue
#define MAX_SENDQUE     1000           // max pending messages per-session send queue

// ============ Input validation limits ============
#define MIN_USERNAME_LEN  2            // minimum username length
#define MAX_USERNAME_LEN  32           // maximum username length
#define MIN_PASSWORD_LEN  3            // minimum password length
#define MAX_PASSWORD_LEN  64           // maximum password length
#define MAX_MESSAGE_LEN   2048         // maximum chat message content length

// ============ Message ID range ============
// valid range: [MSG_HELLO_WORLD, 9999]
// unregistered msg_id within this range is rejected by CSession coroutine

enum MSG_IDS
{
	MSG_HELLO_WORLD = 1001,

	// IM system message IDs (2001-2999)
	// Register
	MSG_REGISTER_REQ        = 2001,  // register request
	MSG_REGISTER_RSP        = 2002,  // register response

	// Login
	MSG_LOGIN_REQ           = 2003,  // login request
	MSG_LOGIN_RSP           = 2004,  // login response

	// Friend list
	MSG_GET_FRIEND_LIST_REQ = 2005,  // get friend list request
	MSG_GET_FRIEND_LIST_RSP = 2006,  // get friend list response

	// Add friend
	MSG_ADD_FRIEND_REQ      = 2007,  // add friend request
	MSG_ADD_FRIEND_RSP      = 2008,  // add friend response
	MSG_FRIEND_REQUEST_PUSH = 2009,  // server push friend request

	// Chat message
	MSG_SEND_CHAT_REQ       = 2010,  // send chat message request
	MSG_SEND_CHAT_RSP       = 2011,  // send chat message response
	MSG_CHAT_MSG_PUSH       = 2012,  // server push chat message

	// Offline message
	MSG_OFFLINE_MSG_REQ     = 2013,  // pull offline messages request
	MSG_OFFLINE_MSG_RSP     = 2014,  // pull offline messages response
};
