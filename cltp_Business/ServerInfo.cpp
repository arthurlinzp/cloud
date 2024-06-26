#include "ServerInfo.h"
#include <string>
#include "../public/trclt_comm_defs.h"


CServerInfo::CServerInfo()
{
	memset(address_, 0x00, sizeof(address_));
	port_ = 0;
	type_ = miOtherDevice;
	id_ = 0;
	is_logined = false;
}

CServerInfo::~CServerInfo()
{
}
