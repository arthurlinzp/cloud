#include "stdafx.h"
#include "Cltp_Business_Impl.h"

#include "../public/IniFileOperator/IniFileOperator.h"
#include "TestConfig.h"

#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#include <WS2tcpip.h>
#include <cinttypes>

CCltp_Business_Impl CCltp_Business_Impl::s_cltpLOGIN_Impl;
CTrCltCommLib  CCltp_Business_Impl::m_trcltcomm_lib;
LOG_HANDLE CCltp_Business_Impl::m_log_handle;
LOG_HANDLE CCltp_Business_Impl::m_log_handle_performance;

CCltp_Business_Impl::CCltp_Business_Impl()
{
	m_init = false;
	threadEnd = true;
	m_cbfn_reflashStatistics = nullptr;

	memset(&m_interaction_finish, 0x00, sizeof(m_interaction_finish));

	m_term_login_status =NULL;
	m_user_login_status = NULL;
}


CCltp_Business_Impl::~CCltp_Business_Impl()
{
}

/*******************************************************************************
*   函数名：initMulti
* 功能简介：初始化多个模拟终端
*     参数：test_info，压测信息结构体
*   返回值：
*******************************************************************************/
int CCltp_Business_Impl::init(STRESS_TEST_INFO & test_info, CBFN_reflashStatistics cbfn)
{
	if (m_init)
	{
		return -1;
	}

	// 设置压测参数
	CTestConfig::instance().setConfig(test_info);

	InitLog();

	int ret = 0;

	m_trcltcomm_lib.loadLib();

	// 初始化通信组件
	ret = m_trcltcomm_lib.initStartMulti(CTestConfig::instance().term_count);
	if (ret != 0)
	{
		return ret;
	}

	m_cbfn_reflashStatistics = cbfn;

	//创建终端、用户状态数组
	m_term_login_status = new bool[CTestConfig::instance().term_count];
	m_user_login_status = new bool[CTestConfig::instance().term_count];
	memset(m_term_login_status, 0x00, CTestConfig::instance().term_count);
	memset(m_user_login_status, 0x00, CTestConfig::instance().term_count);

	CBFN_addInteractiveInfo addInteractiveInfo = 
		std::bind(&CCltp_Business_Impl::addInteractiveInfo, this,
		std::placeholders::_1);
	CBFN_statusReport statusReport =
		std::bind(&CCltp_Business_Impl::statusReport, this,
			std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	// 连接服务器之后，只要不调用disconnServer接口，底层维护连接状态（断线自动重连）
	for (int term_index = 0; term_index < CTestConfig::instance().term_count; term_index++)
	{
		CCltBusiness * business = new CCltBusiness;
		if (0 == business->init(term_index, addInteractiveInfo, statusReport))
		{
			m_cltDispatcher[term_index % CLT_DISPATCH_CONUNT].addCltBusiness(business);
		}
	}

	// 模拟终端需要全部都的连接上服务器
	if (!isAllConnected())
	{
		return -1;
	}

	// 初始化4个模拟终端处理线程
	for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
	{
		m_cltDispatcher[i].init(i);
	}
	for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
	{
		m_cltDispatcher[i].start();
	}

	threadEnd = false;
	m_thread_statue_monitor = std::thread(&CCltp_Business_Impl::statueMonitorThread, this);

	m_init = true;
	return 0;
}

/*******************************************************************************
*   函数名：applyMulti
* 功能简介：实时应用压测参数
*     参数：
*   返回值：
*******************************************************************************/
int CCltp_Business_Impl::applyPara(STRESS_TEST_INFO & test_info)
{
	if (!m_init)
	{
		return -1;
	}
	dlplog_debug(m_log_handle, "applyMulti status");

	CTestConfig::instance().setConfig(test_info);

	return 0;
}

//发送协议
int CCltp_Business_Impl::sendMsg(SEND_TEST_INFO &send_info)
{
	dlplog_debug(m_log_handle, "sendMsg begin");
	CTestConfig::instance().setSendInfo(send_info);
	for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
	{
		m_cltDispatcher[i].sendMsg();
	}
	return 0;
}

void CCltp_Business_Impl::release()
{
	if (!m_init)
	{
		return;
	}
	m_init = false;

	threadEnd = true;
	m_cond_var.notify_all();
	if (m_thread_statue_monitor.joinable())
	{
		m_thread_statue_monitor.join();
	}

	for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
	{
		m_cltDispatcher[i].release();
	}

	m_trcltcomm_lib.stopRelease();
	m_trcltcomm_lib.freeLib();

	m_map_clt_info.clear();
	m_map_svr_info.clear();

	memset(&m_interaction_finish, 0x00, sizeof(m_interaction_finish));

	if (NULL != m_term_login_status)
	{
		delete[] m_term_login_status;
	}
	if (NULL != m_user_login_status)
	{
		delete[] m_user_login_status;
	}

	std::chrono::steady_clock::time_point this_time = std::chrono::steady_clock::now();
	login_performance.finish_count = 0;
	login_performance.start_time = this_time;
	login_performance.finish_time = this_time;
	stg_performance.finish_count = 0;
	stg_performance.start_time = this_time;
	stg_performance.finish_time = this_time;
}

void CCltp_Business_Impl::InitLog()
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
	m_log_handle_performance = dlplog_open_v2("performance", "TrCltComm");

	return;
}

//十六进制字符串转换为字节流  
void CCltp_Business_Impl::HexStrToByte(const char* source, int sourceLen, unsigned char* dest)
{
	int i = 0, j = 0;
	unsigned char highByte, lowByte;

	for (i = 0; i < sourceLen; i += 2)
	{
		highByte = toupper(source[i]);
		lowByte = toupper(source[i + 1]);

		if (highByte > 0x39)
			highByte -= 0x37;
		else
			highByte -= 0x30;

		if (lowByte > 0x39)
			lowByte -= 0x37;
		else
			lowByte -= 0x30;

		dest[j++] = (highByte << 4) | lowByte;
		if (j >= sourceLen)
		{
			break;
		}
	}
	return;
}

// 获取交互类型
int CCltp_Business_Impl::getInteractiveType(uint32_t protocol_num)
{
	int interactive_type = INTERACTIVE_OTHER;
	switch (protocol_num)
	{
	case 0x010101:
	case 0x010181:
		interactive_type = INTERACTIVE_TERM_LOGIN;
		break;
	case 0x010301:
	case 0x010381:
		interactive_type = INTERACTIVE_USER_LOGIN;
		break;
	case 0x020101:
	case 0x020181:
		interactive_type = INTERACTIVE_STRATEGY;
		break;
	case 0x030201:
	case 0x030281:
		interactive_type = INTERACTIVE_LOG_UPLOAD;
		break;
	}
	return interactive_type;
}

/*******************************************************************************
*   函数名：
* 功能简介：添加交互信息，在发送或接收协议时都需要进行统计
*     参数：
*   返回值：
*******************************************************************************/
void CCltp_Business_Impl::addInteractiveInfo(const INTERACTIVE_INFO & interactive)
{
	uint8_t ingeractiveType = getInteractiveType(interactive.protocol_num);
	if (ingeractiveType == INTERACTIVE_OTHER)
	{
		return;
	}

	std::unique_lock<std::mutex> lock_chk(m_mutex);
	if (interactive.sender)
	{
		// 终端发送的数据
		if (interactive.protocol_num & 0x80)
		{
			// 该数据为终端向服务器发送的响应，找到对应的请求信息，以便计算交互过程使用的时间
			if (m_map_svr_info.find(interactive.msg_sn) != m_map_svr_info.end())
			{
				// 合并请求和响应数据
				if (0 == m_map_svr_info[interactive.msg_sn].finish_time.time_since_epoch().count())		//第一次接收响应
				{
					m_map_svr_info[interactive.msg_sn].pkg_total = interactive.pkg_total;
					m_map_svr_info[interactive.msg_sn].pkg_index = 1;
				}
				else																					//已添加过响应
				{
					m_map_svr_info[interactive.msg_sn].pkg_index++;
				}
				// 合并请求和响应数据
				m_map_svr_info[interactive.msg_sn].finish_time = interactive.deal_time;
				m_map_svr_info[interactive.msg_sn].send_data_size += interactive.data_size;
				m_map_svr_info[interactive.msg_sn].success |= interactive.success;
				m_map_svr_info[interactive.msg_sn].term_id = interactive.term_id;
				memcpy(m_map_svr_info[interactive.msg_sn].term_guid, interactive.term_guid, strlen(interactive.term_guid));

				SYSTEMTIME sys_time;
				GetLocalTime(&sys_time);
				dlplog_debug(m_log_handle, "Recv success, [%4d/%02d/%02d %02d:%02d:%02d.%03d] protocol_num[%x],msg_sn[%d],term_id[%d]",
					sys_time.wYear, sys_time.wMonth, sys_time.wDay, sys_time.wHour, sys_time.wMinute, sys_time.wSecond, sys_time.wMilliseconds,
					interactive.protocol_num, interactive.msg_sn, interactive.term_id);
			}
			else
			{
				// 没有找到请求信息，不应该执行到这里
				dlplog_error(m_log_handle, "this respond msg[%d] can't find the currect request. term_id[%d], term_guid[%s] 1", interactive.msg_sn, interactive.term_id, interactive.term_guid);
			}
		}
		else
		{
			// 该数据为终端发送的请求信息，需要保存到map中，以便收到响应时能找到对应的请求数据
			std::map<uint32_t, INTERACTIVE_INFO_EX>::iterator it = m_map_clt_info.find(interactive.msg_sn);
			if (it == m_map_clt_info.end())				//第一次添加
			{
				INTERACTIVE_INFO_EX interactive_info_ex;
				interactive_info_ex.ingeractiveType = getInteractiveType(interactive.protocol_num);
				interactive_info_ex.start_time = interactive.deal_time;
				/*interactive_info_ex.finish_time = 0;*/
				interactive_info_ex.send_data_size = interactive.data_size;
				interactive_info_ex.recv_data_size = 0;
				interactive_info_ex.business_count = interactive.business_count;
				interactive_info_ex.success = interactive.success;
				interactive_info_ex.pkg_total = interactive.pkg_total;
				interactive_info_ex.pkg_index = 1;
				interactive_info_ex.term_id = interactive.term_id;
				memcpy(interactive_info_ex.term_guid, interactive.term_guid, strlen(interactive.term_guid));
				m_map_clt_info.insert(std::make_pair(interactive.msg_sn, interactive_info_ex));
			}
			else										//已添加过
			{
				it->second.send_data_size += interactive.data_size;
				it->second.business_count += interactive.business_count;
				it->second.success |= interactive.success;
				it->second.pkg_index++;
			}
			SYSTEMTIME sys_time;
			GetLocalTime(&sys_time);
			dlplog_debug(m_log_handle, "Sent success, [%4d/%02d/%02d %02d:%02d:%02d.%03d] protocol_num[%x],msg_sn[%d],term_id[%d]",
				sys_time.wYear, sys_time.wMonth, sys_time.wDay, sys_time.wHour, sys_time.wMinute, sys_time.wSecond, sys_time.wMilliseconds,
				interactive.protocol_num, interactive.msg_sn, interactive.term_id);
		}
	}
	else
	{
		// 终端接收的数据
		if (interactive.protocol_num & 0x80)
		{
			// 该数据为响应信息，找到对应的请求信息，以便计算交互过程使用的时间
			if (m_map_clt_info.find(interactive.msg_sn) != m_map_clt_info.end())
			{
				if (0 == m_map_clt_info[interactive.msg_sn].finish_time.time_since_epoch().count())				//第一次接收响应
				{
					m_map_clt_info[interactive.msg_sn].pkg_total = interactive.pkg_total;
					m_map_clt_info[interactive.msg_sn].pkg_index = 1;
				}
				else																							//已添加过响应
				{
					m_map_clt_info[interactive.msg_sn].pkg_index++;
				}
				// 合并请求和响应数据
				m_map_clt_info[interactive.msg_sn].finish_time = interactive.deal_time;
				m_map_clt_info[interactive.msg_sn].recv_data_size += interactive.data_size;
				m_map_clt_info[interactive.msg_sn].success |= interactive.success;
				m_map_clt_info[interactive.msg_sn].business_count += interactive.business_count;
				m_map_clt_info[interactive.msg_sn].term_id = interactive.term_id;
				memcpy(m_map_clt_info[interactive.msg_sn].term_guid, interactive.term_guid, strlen(interactive.term_guid));

				SYSTEMTIME sys_time;
				GetLocalTime(&sys_time);
				dlplog_debug(m_log_handle, "Recv success, [%4d/%02d/%02d %02d:%02d:%02d.%03d] protocol_num[%x],msg_sn[%d],term_id[%d]",
					sys_time.wYear, sys_time.wMonth, sys_time.wDay, sys_time.wHour, sys_time.wMinute, sys_time.wSecond, sys_time.wMilliseconds,
					interactive.protocol_num, interactive.msg_sn, interactive.term_id);
			}
			else
			{
				// 没有找到请求信息，不应该执行到这里
				dlplog_error(m_log_handle, "this respond msg[%d] can't find the currect request. term_id[%d], term_guid[%s] 2", interactive.msg_sn, interactive.term_id, interactive.term_guid);
			}
		}
		else
		{
			// 该数据为服务器发送的请求，需要保存到map中，以便收到响应时能找到对应的请求数据
			std::map<uint32_t, INTERACTIVE_INFO_EX>::iterator it = m_map_svr_info.find(interactive.msg_sn);
			if (it == m_map_svr_info.end())				//第一次添加
			{
				INTERACTIVE_INFO_EX interactive_info_ex;
				interactive_info_ex.ingeractiveType = getInteractiveType(interactive.protocol_num);
				interactive_info_ex.start_time = interactive.deal_time;
				/*interactive_info_ex.finish_time = 0;*/
				interactive_info_ex.send_data_size = 0;
				interactive_info_ex.recv_data_size = interactive.data_size;
				interactive_info_ex.business_count = interactive.business_count;
				interactive_info_ex.success = interactive.success;
				interactive_info_ex.pkg_total = interactive.pkg_total;
				interactive_info_ex.pkg_index = 1;
				interactive_info_ex.term_id = interactive.term_id;
				memcpy(interactive_info_ex.term_guid, interactive.term_guid, strlen(interactive.term_guid));
				m_map_svr_info.insert(std::make_pair(interactive.msg_sn, interactive_info_ex));
			}
			else										//已添加过
			{
				it->second.send_data_size += interactive.data_size;
				it->second.business_count += interactive.business_count;
				it->second.success |= interactive.success;
				it->second.pkg_index++;
			}
			SYSTEMTIME sys_time;
			GetLocalTime(&sys_time);
			dlplog_debug(m_log_handle, "Sent success, [%4d/%02d/%02d %02d:%02d:%02d.%03d] protocol_num[%x],msg_sn[%d],term_id[%d]",
				sys_time.wYear, sys_time.wMonth, sys_time.wDay, sys_time.wHour, sys_time.wMinute, sys_time.wSecond, sys_time.wMilliseconds, 
				interactive.protocol_num,interactive.msg_sn, interactive.term_id);
		}
	}
	if (0 == login_performance.finish_count && interactive.protocol_num == 0x010101)
	{
		// 记录第一个终端登录请求的时间
		login_performance.start_time = interactive.deal_time;
	}

	if (interactive.protocol_num == 0x010381)
	{
		login_performance.finish_count++;
		if (CTestConfig::instance().term_count == login_performance.finish_count)
		{
			// 记录最后一个操作员登录完成的时间
			login_performance.finish_time = interactive.deal_time;
		}
	}


	if (0 == stg_performance.finish_count && interactive.protocol_num == 0x020101)
	{
		// 记录第一个策略下载请求的时间
		stg_performance.start_time = interactive.deal_time;
	}

	if (interactive.protocol_num == 0x020181)
	{
		stg_performance.finish_count++;
		if (CTestConfig::instance().term_count == stg_performance.finish_count)
		{
			// 记录最后一个策略下载完成的时间
			stg_performance.finish_time = interactive.deal_time;
		}
	}
}

void CCltp_Business_Impl::statusReport(uint32_t term_index, uint8_t buss_type, bool status)
{
	if (buss_type == INTERACTIVE_TERM_LOGIN)
	{
		if (NULL != m_term_login_status)
		{
			std::unique_lock<std::mutex> lock_chk(m_mutex);
			m_term_login_status[term_index] = status;
		}
	}
	else if (buss_type == INTERACTIVE_USER_LOGIN)
	{
		if (NULL != m_user_login_status)
		{
			std::unique_lock<std::mutex> lock_chk(m_mutex);
			m_user_login_status[term_index] = status;
		}
	}
}

bool CCltp_Business_Impl::isAllConnected()
{
	bool ret = false;
	int try_time = 10;
	while (try_time--)
	{
		bool all_connected = true;
		for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
		{
			if (!m_cltDispatcher[i].isAllConnected())
			{
				all_connected = false;
				break;
			}
		}
		if (all_connected)
		{
			dlplog_info(m_log_handle, "[%s] all terminal is connected to server", __FUNCTION__);
			ret = true;
			break;
		}
		else
		{
			dlplog_debug(m_log_handle, "[%s] wait ", __FUNCTION__);
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}

	if (0 == try_time)
	{
		dlplog_warn(m_log_handle, "[%s] wait for all connected too long, continue", __FUNCTION__);
	}
	return ret;
}

void CCltp_Business_Impl::calcInteractionInfo(std::map<uint32_t, INTERACTIVE_INFO_EX> & map_interactive)
{
	// 统计未合并的信息：将map中已经完成的交互进行统计
	for (std::map<uint32_t, INTERACTIVE_INFO_EX>::iterator iter = map_interactive.begin(); iter != map_interactive.end();)
	{
		const INTERACTIVE_INFO_EX & interactive_ex = iter->second;
		const uint8_t & ingeractiveType = interactive_ex.ingeractiveType;

		//总帧数等于当前帧数且finish_time>0的成员，合并统计
		if ((interactive_ex.pkg_index >= interactive_ex.pkg_total) &&
			interactive_ex.finish_time.time_since_epoch().count() > 0)
		{
			m_interaction_finish[ingeractiveType].send_data_size += interactive_ex.send_data_size;
			m_interaction_finish[ingeractiveType].recv_data_size += interactive_ex.recv_data_size;
			m_interaction_finish[ingeractiveType].total_count++;

			if (interactive_ex.success)
			{
				m_interaction_finish[ingeractiveType].success_count++;
			}

			if (m_interaction_finish[ingeractiveType].start_time.time_since_epoch().count() == 0 ||
				m_interaction_finish[ingeractiveType].start_time > interactive_ex.start_time)
			{
				m_interaction_finish[ingeractiveType].start_time = interactive_ex.start_time;
			}


			if (m_interaction_finish[ingeractiveType].finish_time < interactive_ex.finish_time)
			{
				m_interaction_finish[ingeractiveType].finish_time = interactive_ex.finish_time;
			}

			map_interactive.erase(iter++);
		}
		else
		{
			iter++;
		}
	}
}


void CCltp_Business_Impl::getTotalInteractionInfo(STATISTICS * statistics)
{
	int term_login_num = 0, user_login_num = 0;
	if (NULL != m_term_login_status && NULL != m_user_login_status)
	{
		for (int j = 0; j < CTestConfig::instance().term_count; j++)
		{
			if (m_term_login_status[j] == true)
			{
				term_login_num++;
			}
			if (m_user_login_status[j] == true)
			{
				user_login_num++;
			}
		}
	}

	for (int i=0; i<INTERACTIVE_COUNT; i++)
	{
		statistics[i].send_data_size = m_interaction_finish[i].send_data_size;
		statistics[i].recv_data_size = m_interaction_finish[i].recv_data_size;
		statistics[i].total_count = m_interaction_finish[i].total_count;
		statistics[i].success_count = m_interaction_finish[i].success_count;
		statistics[i].start_time = m_interaction_finish[i].start_time;
		statistics[i].finish_time = m_interaction_finish[i].finish_time; 
		
		if (i == INTERACTIVE_TERM_LOGIN)
		{
			statistics[i].login_count = term_login_num;
		}
		else if (i == INTERACTIVE_USER_LOGIN)
		{
			statistics[i].login_count = user_login_num;
		}
	}

	std::map<uint32_t, INTERACTIVE_INFO_EX>::iterator iter;
	for (iter = m_map_clt_info.begin(); iter != m_map_clt_info.end(); iter++)
	{
		const INTERACTIVE_INFO_EX & interactive_ex = iter->second;
		const uint8_t & ingeractiveType = interactive_ex.ingeractiveType;

		statistics[ingeractiveType].send_data_size += interactive_ex.send_data_size;
		statistics[ingeractiveType].recv_data_size += interactive_ex.recv_data_size;
		statistics[ingeractiveType].total_count++;
	}

	for (iter = m_map_svr_info.begin(); iter != m_map_svr_info.end(); iter++)
	{
		const INTERACTIVE_INFO_EX & interactive_ex = iter->second;
		const uint8_t & ingeractiveType = interactive_ex.ingeractiveType;

		statistics[ingeractiveType].send_data_size += interactive_ex.send_data_size;
		statistics[ingeractiveType].recv_data_size += interactive_ex.recv_data_size;
		statistics[ingeractiveType].total_count++;
	}
}
/*******************************************************************************
*   函数名：statueMonitorThread
* 功能简介：状态管理线程，定时获取各个模拟终端的运行状态
*     参数：
*   返回值：
*******************************************************************************/
void CCltp_Business_Impl::statueMonitorThread()
{
	dlplog_debug(m_log_handle_performance, "login start");
	bool first_time = true;
	while (true)
	{
		std::unique_lock<std::mutex> lock_chk(m_mutex);
		m_cond_var.wait_for(lock_chk, std::chrono::seconds(3));
		lock_chk.unlock();
		if (threadEnd)
		{
			return;
		}

		if (first_time)
		{
			if (2 == CTestConfig::instance().stg_req_condition)
			{
				bool all_login_success = true;
				for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
				{
					all_login_success &= m_cltDispatcher[i].isAllLoginSuccessed();
				}

				if (all_login_success)
				{
					// 第一次全部都登录成功
					first_time = false;
					dlplog_debug(m_log_handle_performance, "all login success");

					for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
					{
						// 通知进行策略请求
						m_cltDispatcher[i].doStgReqForAllTerm();
					}
				}
			}
			else
			{
				first_time = false;
			}
		}

		STATISTICS statistics_total[INTERACTIVE_COUNT];
		memset(&statistics_total, 0x00, sizeof(statistics_total));

		// 统计未合并的数据
		lock_chk.lock();
		calcInteractionInfo(m_map_clt_info);
		calcInteractionInfo(m_map_svr_info);
		getTotalInteractionInfo(statistics_total);
		lock_chk.unlock();


		std::map<uint32_t, uint32_t> map_svr_id_to_clt_count;
		for (int i = 0; i < CLT_DISPATCH_CONUNT; i++)
		{
			// 通知进行策略请求
			for (auto clt : m_cltDispatcher[i].m_vec_business)
			{
				int svr_id = clt->getDevSvrId();
				if (svr_id > 0)
				{
					map_svr_id_to_clt_count[svr_id]++;
				}
			}
		}


		// 3.刷新界面信息
		if (m_cbfn_reflashStatistics != nullptr)
		{
			char text[4096] = {};
			uint64_t used_time = 
				std::chrono::duration_cast<std::chrono::milliseconds>(
					statistics_total[INTERACTIVE_STRATEGY].finish_time - 
					statistics_total[INTERACTIVE_TERM_LOGIN].start_time).count();
			snprintf(text, sizeof(text), "综合使用时间%" PRIu64"ms\r\n", used_time);

			for (auto iter : map_svr_id_to_clt_count)
			{
				char temp[256] = {};
				snprintf(temp, sizeof(temp), "[%d]:%d\r\n", iter.first, iter.second);
				strcat(text, temp);
			}
			strcat(text, "\r\n");

			m_cbfn_reflashStatistics(statistics_total, text);
		}
		// 4.打印统计信息到日志
		//dlplog_debug(m_log_handle_performance, "the statics info:");
		//for (int i = 0; i < INTERACTIVE_COUNT; i++)
		//{
		//	switch (i)
		//	{
		//	case INTERACTIVE_OTHER:
		//		dlplog_debug_raw(m_log_handle_performance, "other data:\n");
		//		break;
		//	case INTERACTIVE_TERM_LOGIN:
		//		dlplog_debug_raw(m_log_handle_performance, "term login:\n");
		//		break;
		//	case INTERACTIVE_USER_LOGIN:
		//		dlplog_debug_raw(m_log_handle_performance, "user login:\n");
		//		break;
		//	case INTERACTIVE_STRATEGY:
		//		dlplog_debug_raw(m_log_handle_performance, "strategy request:\n");
		//		break;
		//	case INTERACTIVE_LOG_UPLOAD:
		//		dlplog_debug_raw(m_log_handle_performance, "log upload:\n");
		//		break;
		//	}
		//	dlplog_debug_raw(m_log_handle_performance, "    used_time:%u\n", statistics_total[i].used_time);
		//	dlplog_debug_raw(m_log_handle_performance, "    send_data_size:%llu\n", statistics_total[i].send_data_size);
		//	dlplog_debug_raw(m_log_handle_performance, "    recv_data_size:%llu\n", statistics_total[i].recv_data_size);
		//	dlplog_debug_raw(m_log_handle_performance, "    business_count:%u\n", statistics_total[i].business_count);
		//	dlplog_debug_raw(m_log_handle_performance, "    total_count:%u\n", statistics_total[i].total_count);
		//	dlplog_debug_raw(m_log_handle_performance, "    success_count:%u\n", statistics_total[i].success_count);
		//
		//	if (i != INTERACTIVE_LOG_UPLOAD)
		//	{
		//		dlplog_debug_raw(m_log_handle_performance, "    used_total_time:%u\n", statistics_total[i].used_total_time);
		//		dlplog_debug_raw(m_log_handle_performance, "    req_time:%u\n", statistics_total[i].req_time);
		//		dlplog_debug_raw(m_log_handle_performance, "    rsp_time:%u\n", statistics_total[i].rsp_time);
		//	}
		//	
		//	dlplog_debug_raw(m_log_handle_performance, "    max_time:%u\n", statistics_total[i].max_time);
		//	dlplog_debug_raw(m_log_handle_performance, "    min_time:%u\n", statistics_total[i].min_time);
		//}
		//dlplog_debug(m_log_handle_performance, "\n");
	}
}

