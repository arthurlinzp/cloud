#pragma once
#include <stdint.h>
class CServerInfo
{
public:
	CServerInfo();
	~CServerInfo();
public:
	char address_[256];		// 服务器地址
	int port_;				// 服务器端口
	uint32_t type_;			// 服务器类型
	uint32_t id_;			// 服务器id
	bool is_logined;		// 是否已登录
};

