#include "TestConfig.h"
#include <string>

CTestConfig CTestConfig::sg_singleton;

CTestConfig::CTestConfig()
{
}


CTestConfig::~CTestConfig()
{
}

CTestConfig & CTestConfig::instance()
{
	return sg_singleton;
}

void CTestConfig::setConfig(STRESS_TEST_INFO & stress_test_info)
{
	client_num = stress_test_info.client_num;					// 压测工具编号
	strncpy_s(svr_address, stress_test_info.svr_address, sizeof(svr_address) - 1);		// 服务器地址
	svr_port = stress_test_info.svr_port;						// 服务器端口
	term_type = stress_test_info.term_type;
	term_count = stress_test_info.term_count;					// 终端数量
	disconn_count = stress_test_info.disconn_count;				// 断线重连数量
	stg_count = stress_test_info.stg_count;						// 策略下载数量
	use_prob = stress_test_info.use_prob;						// 是否使用概率，0x01断线重连 0x02策略下载
	login_time = stress_test_info.login_time;					// 连接与登录定时检查时间
	stg_time = stress_test_info.stg_time;						// 定时发起策略下载时间
	term_oper_login = stress_test_info.term_oper_login;			// 是否终端和操作员登录：0x01终端登录 0x02操作员登录
	stg_req_condition = stress_test_info.stg_req_condition;		// 策略请求条件：0不请求策略，1登录后立即请求策略，2所有终端都登录完成后再请求策略
	stg_req_range = stress_test_info.stg_req_range;				// 策略请求范围：0x01基础策略 0x02终端策略 0x04操作员策略 
	respond_stg_notice = stress_test_info.respond_stg_notice;	// 响应服务器的策略更新通知
	enable_log_upload = stress_test_info.enable_log_upload;		// 响应服务器的日志上传请求
	log_content_num = stress_test_info.log_content_num;			// 日志内容条数
}
void CTestConfig::setSendInfo(SEND_TEST_INFO &send_test_info)
{
	send_info = send_test_info;
}