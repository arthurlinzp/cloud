#pragma once
#include <mutex>
#include <thread>
#include <vector>

#include "CltBusiness.h"

#include "DlpULog/DlpULog.h"
#pragma comment(lib, "DlpULog.lib")

#define CLT_DISPATCH_CONUNT 8

class CCltDispatcher
{
public:
	CCltDispatcher();
	~CCltDispatcher();
	bool isAllConnected();
	bool init(int dispatch_index);
	void start();
	void sendMsg();
	void release();
	bool addCltBusiness(CCltBusiness * cltBusiness);
	bool isAllLoginSuccessed();
	void doStgReqForAllTerm();

	void getSendFile(std::string &SendFile);
private:
	void InitLog();
	void onLoginThread();
	void onStgThread();
private:
	LOG_HANDLE m_log_handle;
	LOG_HANDLE m_log_handle_login;
	LOG_HANDLE m_log_handle_stg;
	LOG_HANDLE m_log_handle_log_upload;

	static std::mutex sg_mutex_login;
	static std::mutex sg_mutex_stg;
	static std::mutex sg_mutex_log;
	static std::mutex sg_mutex_detect;

	std::condition_variable m_login_cond_var;
	std::condition_variable m_stg_cond_var;
	std::condition_variable m_log_cond_var;
	std::condition_variable m_detect_cond_var;

	static int sg_disconn_count;
	static int sg_stg_count;
	static int sg_log_count;

	bool threadEnd;

	std::thread m_login_thread;
	std::thread m_stg_thread;
	std::thread m_log_thread;
	std::thread m_detect_thread;

	bool m_all_term_login_successed;

	int m_dispatch_index;
public:
	std::vector<CCltBusiness *> m_vec_business;

	bool bConnectService;
};

