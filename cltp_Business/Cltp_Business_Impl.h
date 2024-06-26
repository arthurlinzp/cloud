#pragma once

#include <vector>
#include <thread>

#include "../public/TrCltCommLib.h"

#include "DlpULog/DlpULog.h"
#pragma comment(lib, "DlpULog.lib")

#include "CltDispatcher.h"

// 服务器状态
#ifndef SERVER_STATUS
#define SERVER_STATUS
enum _tagSERVER_STATUS
{
	enSVR_LOGIN_FAILED = 0x00,	// 未登录/登录失败
	enSVR_LOGIN_SUCCESS = 0x01,	// 登录成功
};
#endif

//extern enum _tagIngeractiveType;
//{
//	INTERACTIVE_OTHER = 0,
//	INTERACTIVE_TERM_LOGIN,
//	INTERACTIVE_USER_LOGIN,
//	INTERACTIVE_STRATEGY,
//	INTERACTIVE_LOG_UPLOAD,
//	INTERACTIVE_COUNT
//};

typedef struct _tagSvrPerformance
{
	_tagSvrPerformance()
	{
		finish_count = 0;
	}
	int finish_count;	// 完成的数量
	std::chrono::steady_clock::time_point start_time;	// 发起的时间
	std::chrono::steady_clock::time_point finish_time;	// 完成的时间
}SvrPerformance;

// 与服务器的交互信息
typedef struct _tagInteractiveInfoEx
{
	_tagInteractiveInfoEx()
	{
		ingeractiveType = INTERACTIVE_OTHER;
		send_data_size = 0;
		recv_data_size = 0;
		business_count = 0;
		success = false;
		pkg_total = 0;
		pkg_index = 0;
		term_id = 0;
		memset(term_guid, 0x00, sizeof(term_guid));
	}
	uint8_t ingeractiveType;										// 交互类型：_tagIngeractiveType
	std::chrono::steady_clock::time_point start_time;				// 发起的时间
	std::chrono::steady_clock::time_point finish_time;				// 完成的时间
	uint32_t send_data_size;										// 发送的数据大小
	uint32_t recv_data_size;										// 接收的数据大小
	uint32_t business_count;										// 业务数据条数
	bool success;													// 是否成功
	uint8_t pkg_total;												//总帧数
	uint8_t pkg_index;												//当前帧数
	unsigned int term_id;											// 模拟终端id
	char term_guid[32 + 1];											// 终端guid
}INTERACTIVE_INFO_EX;

// 统计信息
typedef struct _tagStatistics
{
	uint64_t send_data_size;		// 发送的数据大小
	uint64_t recv_data_size;		// 接收的数据大小
	uint32_t total_count;			// 总交互数
	uint32_t success_count;			// 交互成功的数量
	uint32_t login_count;			// 登录成功的数量
	std::chrono::steady_clock::time_point start_time;				// 第一次发起请求的时间
	std::chrono::steady_clock::time_point finish_time;				// 最后一次收到响应的时间
}STATISTICS;


typedef std::function<void(const STATISTICS * statistics, const char * text)> CBFN_reflashStatistics;

class CCltp_Business_Impl
{
public:
	CCltp_Business_Impl();
	~CCltp_Business_Impl();
public:
	static inline CCltp_Business_Impl* instance() { return &s_cltpLOGIN_Impl; }
private:
	static CCltp_Business_Impl s_cltpLOGIN_Impl;
public:
	int init(STRESS_TEST_INFO & test_info, CBFN_reflashStatistics cbfn);
	int applyPara(STRESS_TEST_INFO & test_info);
	int sendMsg(SEND_TEST_INFO &send_info);
	void release();

	void InitLog();
	void HexStrToByte(const char* source, int sourceLen, unsigned char* dest);

	int getInteractiveType(uint32_t protocol_num);
private:
	void addInteractiveInfo(const INTERACTIVE_INFO & interactive);
	void statusReport(uint32_t term_index, uint8_t buss_type, bool status);
	bool isAllConnected();
	void calcInteractionInfo(std::map<uint32_t, INTERACTIVE_INFO_EX> & map_interactive);
	void getTotalInteractionInfo(STATISTICS * statistics);
	void statueMonitorThread();

private:
	bool m_init;

	std::mutex m_mutex;

	bool threadEnd;
	std::condition_variable m_cond_var;
	std::thread m_thread_statue_monitor;

	static CTrCltCommLib m_trcltcomm_lib;

	static LOG_HANDLE m_log_handle;
	static LOG_HANDLE m_log_handle_performance;

	CCltDispatcher m_cltDispatcher[CLT_DISPATCH_CONUNT];

	CBFN_reflashStatistics m_cbfn_reflashStatistics;

	std::map<uint32_t, INTERACTIVE_INFO_EX> m_map_clt_info;
	std::map<uint32_t, INTERACTIVE_INFO_EX> m_map_svr_info;

	// 完成统计的交互信息
	STATISTICS m_interaction_finish[INTERACTIVE_COUNT];

	bool * m_term_login_status;		// 终端登录状态数组
	bool * m_user_login_status;		// 操作员登录状态数组

	SvrPerformance login_performance;
	SvrPerformance stg_performance;
};

