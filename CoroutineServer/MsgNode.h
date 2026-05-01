#pragma once
/// @file  MsgNode.h
/// @brief Protocol message buffer classes with RAII memory management.

#include <string>
#include <vector>
#include <cstring>
#include "const.h"
#include <iostream>
#include <boost/asio.hpp>
using boost::asio::ip::tcp;

/// @class MsgNode
/// @brief Base class for protocol message buffers.
///        Uses std::vector<char> for RAII memory management (no manual new/delete).
class MsgNode {
public:
	MsgNode(short max_len)
		:_total_len(max_len), _cur_len(0), _data(max_len + 1, 0) {
	}

	~MsgNode() = default;

	/// @brief Zero out the data buffer and reset current length.
	void Clear() {
		::memset(_data.data(), 0, _total_len);
		_cur_len = 0;
	}

	short _cur_len;            ///< Bytes currently consumed
	short _total_len;          ///< Total allocated buffer size
	std::vector<char> _data;   ///< RAII vector replaces raw new[]/delete[], prevents memory leaks
};

/// @class RecvNode
/// @brief Message node for received data, carries the parsed message ID.
class RecvNode :public MsgNode {
public:
	RecvNode(short max_len, short msg_id);
	short _msg_id;             ///< Protocol message ID
};

/// @class SendNode
/// @brief Message node for outgoing data.
///        Constructor writes the complete message (header + body) into _data.
class SendNode :public MsgNode {
public:
	SendNode(const char* msg, short max_len, short msg_id) ;
	short _msg_id;             ///< Protocol message ID
};
