// cltp_Business.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "cltp_Business.h"

CLTP_BUSINESS_API int WINAPI cltp_init(const char * ip, int port)
{
	return -1;
}

CLTP_BUSINESS_API void WINAPI cltp_release()
{
	return CCltp_Business_Impl::instance()->release();
}

CLTP_BUSINESS_API int WINAPI cltp_initMulti(STRESS_TEST_INFO & test_info, CBFN_reflashStatistics cbfn)
{
	return  CCltp_Business_Impl::instance()->init(test_info, cbfn);
}

CLTP_BUSINESS_API int WINAPI cltp_applyMulti(STRESS_TEST_INFO & test_info)
{
	return CCltp_Business_Impl::instance()->applyPara(test_info);
}

CLTP_BUSINESS_API int WINAPI cltp_sendMulti(SEND_TEST_INFO &send_info)
{
	return CCltp_Business_Impl::instance()->sendMsg(send_info);
}
