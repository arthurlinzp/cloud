#include "CltDispatcher.h"
#include <random>

std::mutex  CCltDispatcher::sg_mutex_login;
std::mutex  CCltDispatcher::sg_mutex_stg;
std::mutex  CCltDispatcher::sg_mutex_log;
std::mutex  CCltDispatcher::sg_mutex_detect;

int CCltDispatcher::sg_disconn_count;
int CCltDispatcher::sg_stg_count;
int CCltDispatcher::sg_log_count;

CCltDispatcher::CCltDispatcher()
{
	threadEnd = true;
	m_dispatch_index = 0;
	m_all_term_login_successed = false;
}


CCltDispatcher::~CCltDispatcher()
{
}

void CCltDispatcher::InitLog()
{
	CHAR   szModuleDir[MAX_PATH] = { 0 };
	CHAR   szDriver[MAX_PATH] = { 0 };
	CHAR   szDir[MAX_PATH] = { 0 };
	CHAR   szFName[MAX_PATH] = { 0 };
	CHAR   szExt[MAX_PATH] = { 0 };
	CHAR   szConfigDir[MAX_PATH] = { 0 };

	::GetModuleFileNameA(NULL, szModuleDir, sizeof(szModuleDir));
	::_splitpath_s(szModuleDir, szDriver, sizeof(szDriver), szDir, sizeof(szDir), szFName, sizeof(szFName), szExt, sizeof(szExt));
	::_makepath_s(szModuleDir, sizeof(szModuleDir), szDriver, szDir, NULL, NULL);

	// 加载日志配置
	::sprintf_s(szConfigDir, sizeof(szConfigDir),
		"%sconfig\\log_TrCltComm.properties", szModuleDir);
	dlplog_load_config(szConfigDir);

	dlplog_init("TrCltComm");

	m_log_handle = dlplog_open("TrCltComm"); 
	m_log_handle_login = dlplog_open_v2("login", "TrCltComm");
	m_log_handle_stg = dlplog_open_v2("strategy", "TrCltComm");
	m_log_handle_log_upload = dlplog_open_v2("log_upload", "TrCltComm");

	return;
}

bool CCltDispatcher::isAllConnected()
{
	for (int i = 0; i < m_vec_business.size(); i++)
	{
		if (m_vec_business[i]->getDataServerConnState() != enBCS_Connected)
		{
			return false;
		}
	}
	return true;
}

bool CCltDispatcher::init(int dispatch_index)
{
	InitLog();
	m_dispatch_index = dispatch_index;
	threadEnd = false;
	return true;
}

void CCltDispatcher::start()
{
	bConnectService = false;
	// 启动终端，发起登录
	for (int i = 0; i < m_vec_business.size(); i++)
	{
		m_vec_business[i]->start();
	}
	
	// 开始线程处理
	if (CTestConfig::instance().login_time > 0)
	{
		m_login_thread = std::thread(&CCltDispatcher::onLoginThread, this);
	}

	if (CTestConfig::instance().stg_time > 0)
	{
		m_stg_thread = std::thread(&CCltDispatcher::onStgThread, this);
	}

	return ;
}

void CCltDispatcher::release()
{
	threadEnd = true;

	m_login_cond_var.notify_all();
	if (m_login_thread.joinable())
	{
		m_login_thread.join();
	}
	m_stg_cond_var.notify_all();
	if (m_stg_thread.joinable())
	{
		m_stg_thread.join();
	}
	m_log_cond_var.notify_all();
	if (m_log_thread.joinable())
	{
		m_log_thread.join();
	}
	m_detect_cond_var.notify_all();
	if (m_detect_thread.joinable())
	{
		m_detect_thread.join();
	}

	for (auto & cltBusiness : m_vec_business)
	{
		delete cltBusiness;
		cltBusiness = nullptr;
	}
	m_vec_business.clear();

	m_all_term_login_successed = false;
	bConnectService = false;
}

bool CCltDispatcher::addCltBusiness(CCltBusiness * cltBusiness)
{
	m_vec_business.push_back(cltBusiness);
	return true;
}

//发送协议
void CCltDispatcher::sendMsg()
{
	//连接服务器
	if (!bConnectService)
	{
		for (int i = 0; i < m_vec_business.size(); i++)
		{
			m_vec_business[i]->connectService();
		}
		for (int i = 0; i < m_vec_business.size(); i++)
		{
			m_vec_business[i]->connectService();
		}
		bConnectService = true;
	}
	std::string strSendMsg;
	getSendFile(strSendMsg);
	for (int i = 0; i < m_vec_business.size(); i++)
	{
		m_vec_business[i]->beginsend2detector(strSendMsg);
	}
}

void CCltDispatcher::getSendFile(std::string &SendFile)
{
	std::string content_str_p = "";
	const size_t WR_SIZE = 1024 * 1024;

	FILE* stream_rd;
	size_t num_read = 0;
	long long offset = 0;

	char *buf = new char[WR_SIZE + 1];
	std::string filepath = CTestConfig::instance().send_info.file_path;
	if (0 == fopen_s(&stream_rd, filepath.c_str(), "rb")) {
		while (!feof(stream_rd)) {
			//读
			num_read = fread(buf, sizeof(char), WR_SIZE, stream_rd);
			if (0 == num_read) break;
			offset += num_read;//文件偏移
			_fseeki64(stream_rd, offset, SEEK_SET);
			buf[num_read] = '\0';
			content_str_p.append(buf);
		}
		fclose(stream_rd);
	}
	SendFile = content_str_p;
	delete[]buf;
}

/*******************************************************************************
*   函数名：checkLoginState
* 功能简介：检查分配到该线程的终端是否全部登录到服务器
*     参数：
*   返回值：true全部登录成功，false未全部登录成功
*******************************************************************************/
bool CCltDispatcher::isAllLoginSuccessed()
{
	int term_count = m_vec_business.size();

	for (int i = 0; i < term_count; i++)
	{
		if (!m_vec_business[i]->isLoginSuccessed())
		{
			return false;
		}
	}

	return true;
}

void CCltDispatcher::doStgReqForAllTerm()
{
	m_all_term_login_successed = true;
	m_login_cond_var.notify_all();
	m_stg_cond_var.notify_all();
}

// 检查连接及登录状态的线程
void CCltDispatcher::onLoginThread()
{
	std::random_device sd;//生成random_device对象sd做种子
	std::minstd_rand linearRan(sd());//使用种子初始化linear_congruential_engine对象
	std::uniform_int_distribution<int>dis1(1, 100);// 随机数范围 [1,100]

	while (true)
	{
		std::unique_lock<std::mutex> lock_chk(sg_mutex_login);
		m_login_cond_var.wait_for(lock_chk, std::chrono::seconds(CTestConfig::instance().login_time));
		lock_chk.unlock();

		if (threadEnd)
		{
			return;
		}

		//if (!m_all_term_login_successed)
		//{
		//	continue;
		//}

		if (CTestConfig::instance().use_prob & 0x01)
		{
			for (int i = 0; i < m_vec_business.size(); i++)
			{
				if (m_vec_business[i]->checkLoginState())
				{
					// 登录成功
					if (dis1(linearRan) <= CTestConfig::instance().disconn_count)
					{
						// 断开重连
						m_vec_business[i]->setDataServerReconnect();
						dlplog_debug(m_log_handle_login, "term_id[%d] reconnect", m_vec_business[i]->getTermId());
					}
				}
			}
		}
		//
		else
		{
			lock_chk.lock();
			if (sg_disconn_count <= 0)
			{
				sg_disconn_count = CTestConfig::instance().disconn_count;
			}

			lock_chk.unlock();

			for (int i = 0; i < m_vec_business.size(); i++)
			{
				if (threadEnd) return;
				if (m_vec_business[i]->checkLoginState())
				{
					// 登录成功
					lock_chk.lock();
					if (sg_disconn_count > 0)
					{
						--sg_disconn_count;
						lock_chk.unlock();
						// 设置断开连接
						m_vec_business[i]->setDataServerReconnect();
						dlplog_debug(m_log_handle_login, "term_id[%d] reconnect", m_vec_business[i]->getTermId());
					}
					else
					{
						lock_chk.unlock();
					}
				}
			}
		}
	}
}

// 定时发起策略下载请求的线程
void CCltDispatcher::onStgThread()
{
	std::random_device sd;//生成random_device对象sd做种子
	std::minstd_rand linearRan(sd());//使用种子初始化linear_congruential_engine对象
	std::uniform_int_distribution<int>dis1(1, 100);// 随机数范围 [1,100]

	bool first_time = true;
	while (true)
	{
		std::unique_lock<std::mutex> lock_chk(sg_mutex_stg);
		m_stg_cond_var.wait_for(lock_chk, std::chrono::seconds(CTestConfig::instance().stg_time));
		lock_chk.unlock();

		if (threadEnd)
		{
			return;
		}

		if (first_time)
		{
			if (2 == CTestConfig::instance().stg_req_condition)
			{
				if (m_all_term_login_successed)
				{
					// 已设置为等待所有终端都登录成功之后再发起策略下载
					// 所有终端已经登录完成
					// 让所有终端发起策略下载
					for (int i = 0; i < m_vec_business.size(); i++)
					{
						m_vec_business[i]->requestStrategy();
						dlplog_debug(m_log_handle_stg, "term_id[%d] request strategy", m_vec_business[i]->getTermId());
					}
					first_time = false;
				}
				continue;
			}
			else
			{
				first_time = false;
			}
		}


		// 按终端数量或概念随机发起策略下载
		if (CTestConfig::instance().use_prob & 0x02)
		{
			for (int i = 0; i < m_vec_business.size(); i++)
			{
				if (dis1(linearRan) <= CTestConfig::instance().stg_count)
				{
					m_vec_business[i]->requestStrategy();
					dlplog_debug(m_log_handle_stg, "term_id[%d] request strategy", m_vec_business[i]->getTermId());
				}
			}
		}
		else
		{
			lock_chk.lock();
			if (sg_stg_count <= 0)
			{
				sg_stg_count = CTestConfig::instance().stg_count;
			}
			lock_chk.unlock();

			for (int i = 0; i < m_vec_business.size(); i++)
			{
				lock_chk.lock();
				if (sg_stg_count > 0)
				{
					--sg_stg_count;
					lock_chk.unlock();
					m_vec_business[i]->requestStrategy();
					dlplog_debug(m_log_handle_stg, "term_id[%d] request strategy", m_vec_business[i]->getTermId());
				}
				else
				{
					lock_chk.unlock();
				}
			}
		}
	}
}
