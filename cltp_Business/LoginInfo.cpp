#include "LoginInfo.h"

CLoginInfo CLoginInfo::sg_singleton;

CLoginInfo::CLoginInfo()
{
	//connected_count = 0;
	term_login_count = 0;
	oper_login_count = 0;
}


CLoginInfo::~CLoginInfo()
{
}

CLoginInfo & CLoginInfo::instance()
{
	return sg_singleton;
}
