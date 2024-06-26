#pragma once

struct STRESS_TEST_INFO
{
	int client_num;				// 压测工具编号
	char svr_address[256];		// 采集服务器地址
	int svr_port;				// 采集服务器端口
	int term_type;				// 终端类型
	int term_count;				// 终端数量
	int disconn_count;			// 断线重连数量
	int stg_count;				// 策略下载数量
	int use_prob;				// 是否使用概率，0x01断线重连 0x02策略下载
	int login_time;				// 连接与登录定时检查时间
	int stg_time;				// 定时发起策略下载时间
	int term_oper_login;		// 是否终端和操作员登录：0x01终端登录 0x02操作员登录
	int stg_req_condition;		// 策略请求条件：0不请求策略，1登录后立即请求策略，2所有终端都登录完成后再请求策略
	int stg_req_range;			// 策略请求范围：0x01基础策略 0x02终端策略 0x04操作员策略 
	bool respond_stg_notice;	// 响应服务器的策略更新通知
	bool enable_log_upload;		// 响应服务器的日志上传请求
	int log_content_num;		// 日志内容条数
};

struct SEND_TEST_INFO
{
	int stg_id;					// 策略id
	char file_path[1024];		// 待检测文件路径
	char des_address[256];		// 目标服务器地址
	int des_port;				// 目标服务器端口
};


class CTestConfig
{
public:
	~CTestConfig();
	static CTestConfig & instance();
	void setConfig(STRESS_TEST_INFO & stress_test_info);
	void setSendInfo(SEND_TEST_INFO &send_test_info);
private:
	CTestConfig();
	static CTestConfig sg_singleton;
public:
	int client_num;				// 压测工具编号
	char svr_address[256];		// 服务器地址
	int svr_port;				// 服务器端口
	int term_type;				// 终端类型：0windows，1linux,2mac
	int term_count;				// 终端数量
	int disconn_count;			// 断线重连数量
	int stg_count;				// 策略下载数量
	int use_prob;				// 是否使用概率，0x01断线重连 0x02策略下载 0x04日志上报
	int login_time;				// 连接与登录定时检查时间
	int stg_time;				// 定时发起策略下载时间
	int term_oper_login;		// 是否终端和操作员登录：0x01终端登录 0x02操作员登录
	int stg_req_condition;		// 策略请求条件：0不请求策略，1登录后立即请求策略，2所有终端都登录完成后再请求策略
	int stg_req_range;			// 策略请求范围：0x01基础策略 0x02终端策略 0x04操作员策略 
	bool respond_stg_notice;	// 响应服务器的策略更新通知
	bool enable_log_upload;		// 响应服务器的日志上传请求
	int log_content_num;		// 日志内容条数
	SEND_TEST_INFO send_info;
};

