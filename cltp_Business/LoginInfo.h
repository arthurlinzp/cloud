#pragma once
#include <atomic>

class CLoginInfo
{
public:
	~CLoginInfo();
	static CLoginInfo & instance();
private:
	CLoginInfo();
	static CLoginInfo sg_singleton;
public:

	//std::atomic_uint32_t connected_count;
	std::atomic_uint32_t term_login_count;
	std::atomic_uint32_t oper_login_count;
};

