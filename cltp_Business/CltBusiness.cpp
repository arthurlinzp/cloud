#include "stdafx.h"
#include "CltBusiness.h"

#include "../public/IniFileOperator/IniFileOperator.h"
#include "LoginInfo.h"
#include <random>
#include <functional>

#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#include <WS2tcpip.h>

#include "trfclib/trfclib_inc.h"

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

#include <codecvt>

uint8_t CCltBusiness::LOGIN_MODE_ID = 0x01;
uint8_t CCltBusiness::STG_MODE_ID = 0x02;
uint8_t CCltBusiness::LOG_MODE_ID = 0x03;

int CCltBusiness::CLT_INDEX_LEN = 2;
int CCltBusiness::TERM_INDEX_LEN = 4;

CCltBusiness::CCltBusiness()
{
	m_log_handle = 0;
	m_log_handle_login = 0;
	m_log_handle_stg = 0;

	m_had_send_login = false;

	m_term_index = 0;			// 模拟终端序号
	m_local_clt = nullptr;		// 模拟终端通信对象
	m_term_id = 0;				// 模拟终端id
	m_longin_state = 0;			// 终端登录状态
	m_uer_login_state = 0;		// 用户登录状态

	m_term_nickname = "";
	m_login_mode = 0;			// 用户登录模式，从终端登录响应获取到，用于用户登录
	m_user_id = 0;				// 用户Id，从终端登录响应获取到，用于用户登录

	memset(m_term_guid, 0x00, sizeof(m_term_guid));
	memset(m_term_mcode, 0x00, sizeof(m_term_guid));
	memset(m_term_dpt_name, 0x00, sizeof(m_term_guid));
	memset(m_term_user_name, 0x00, sizeof(m_term_guid));
	memset(m_term_cpt_name, 0x00, sizeof(m_term_guid));
	strncpy(m_term_ver, "3.01.230101.SC", sizeof(m_term_ver) - 1);

	m_statusReport = NULL;
	m_addInteractiveInfo = NULL;

	long num_of_procs = 2;
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	num_of_procs = sysinfo.dwNumberOfProcessors;
	threadPool_ = new CThreadPool(num_of_procs);
}


CCltBusiness::~CCltBusiness()
{
	release();
}

int CCltBusiness::init(int term_index, CBFN_addInteractiveInfo addInteractiveInfo, CBFN_statusReport statusReport)
{
	initMap();
	initLog();

	m_trcltcomm_lib.loadLib();

	m_term_index = term_index;
	m_term_id = 0;
	m_longin_state = 0;
	m_uer_login_state = 0;
	m_local_clt = reinterpret_cast<ILocalClt *>(m_trcltcomm_lib.getLocalClt(term_index));
	if (nullptr == m_local_clt)
	{
		return -1;
	}
	initTermInfo();

	m_local_clt_callback.setLocalCltInstance(this);
	m_local_clt_callback.setHandleProtocolFunc(&CCltBusiness::handleProtocol);
	m_local_clt->registerHandler(&m_local_clt_callback);

	m_addInteractiveInfo = addInteractiveInfo;
	m_statusReport = statusReport;

	//端口和服务器类型可以由界面输入，终端去连接多个服务器
	CServerInfo * data_server = new CServerInfo;
	strncpy(data_server->address_, CTestConfig::instance().svr_address, sizeof(data_server->address_) - 1);
	data_server->port_ = CTestConfig::instance().svr_port;
	data_server->type_ = miDataServer;

	m_server_info_map[data_server->type_] = data_server;
	m_local_clt->connNewServer(data_server->address_, data_server->port_, data_server->type_);

	return 0;
}

void CCltBusiness::connectService()
{
	SVR_CONN_INFO svr_conn_info;
	m_local_clt->queryConnInfoByType(miAdvAlgoContentAwareServer, svr_conn_info);
	if (svr_conn_info.buss_conn_state != enBCS_Connected && svr_conn_info.buss_conn_state !=enBCS_ReConnected)
	{
		std::map<uint32_t, CServerInfo *>::iterator iter =
			m_server_info_map.find(miAdvAlgoContentAwareServer);
		if (iter == m_server_info_map.end())
		{
			CServerInfo * detector_server = new CServerInfo;
			strncpy(detector_server->address_, CTestConfig::instance().send_info.des_address, sizeof(detector_server->address_) - 1);
			detector_server->port_ = CTestConfig::instance().send_info.des_port;
			detector_server->type_ = miAdvAlgoContentAwareServer;
			m_server_info_map[detector_server->type_] = detector_server;
		}

		bool ret=m_local_clt->connNewServer(m_server_info_map[miAdvAlgoContentAwareServer]->address_,
			m_server_info_map[miAdvAlgoContentAwareServer]->port_,
			m_server_info_map[miAdvAlgoContentAwareServer]->type_);
		dlplog_info(m_log_handle, "[%s] connNewServer <%d>", __FUNCTION__, ret);
	}
}
// 启动模拟终端工作，其实就是开始登录
void CCltBusiness::start()
{
	SVR_CONN_INFO svr_conn_info;
	m_local_clt->queryConnInfoByType(miDataServer, svr_conn_info);

	if (svr_conn_info.buss_conn_state == enBCS_Connected)
	{
		loginRequest0101();
	}
}

void CCltBusiness::release()
{
	for (auto server_info : m_server_info_map)
	{
		m_local_clt->disconnServerByType(server_info.first);
		delete server_info.second;
	}
	m_server_info_map.clear();

	m_term_index = 0;			// 模拟终端序号
	m_local_clt = nullptr;		// 模拟终端通信对象
	m_term_id = 0;				// 模拟终端id
	m_longin_state = 0;			// 终端登录状态
	m_uer_login_state = 0;

	m_term_nickname = "";
	m_login_mode = 0;			// 用户登录模式，从终端登录响应获取到，用于用户登录
	m_user_id = 0;				// 用户Id，从终端登录响应获取到，用于用户登录

	for (auto & network : m_vec_network)
	{
		network->vec_ip.clear();
		delete network;
		network = nullptr;
	}
	m_vec_network.clear();

	m_addInteractiveInfo = NULL;
	m_statusReport = NULL;
	threadPool_->release();
}

void CCltBusiness::setDataServerReconnect()
{
	if (m_longin_state == enSVR_LOGIN_SUCCESS)
	{
		//断开连接，则登如成功状态失效
		m_statusReport(m_term_index, INTERACTIVE_TERM_LOGIN, false);
		m_statusReport(m_term_index, INTERACTIVE_USER_LOGIN, false);

		// 已连接到服务器，随机断开连接
		dlplog_info(m_log_handle, "disconnect server], term_index[%d] term_id[%d]",
			m_term_index, m_term_id);
		m_local_clt->disconnServerByType(miDataServer);
		// 连接状态异常，设置登录状态失败
		m_had_send_login = false;
		m_longin_state = enSVR_LOGIN_FAILED;
		m_uer_login_state = enSVR_LOGIN_FAILED;

		std::map<uint32_t, CServerInfo *>::iterator iter =
			m_server_info_map.find(miDataServer);
		if (iter != m_server_info_map.end())
		{
			iter->second->is_logined = false;
			// 重新连接服务器
			m_local_clt->connNewServer(iter->second->address_, iter->second->port_, iter->second->type_);
		}

	}
}

int CCltBusiness::getDataServerConnState()
{
	if (nullptr != m_local_clt)
	{
		SVR_CONN_INFO svr_conn_info;
		m_local_clt->queryConnInfoByType(miDataServer, svr_conn_info);
		return svr_conn_info.buss_conn_state;
	}
	else
	{
		return enBCS_Close;
	}
}

bool CCltBusiness::isLoginSuccessed()
{
	if (CTestConfig::instance().term_oper_login & 0x01)
	{
		if (m_longin_state != enSVR_LOGIN_SUCCESS)
		{
			return false;
		}

		if (CTestConfig::instance().term_oper_login & 0x02)
		{
			if (m_uer_login_state != enSVR_LOGIN_SUCCESS)
			{
				// 终端登录成功，操作员登录失败
				return false;
			}
			else
			{
				// 终端登录成功，操作员登录成功
				return true;
			}
		}
		else
		{
			// 终端登录成功，操作员不需要登录
			return true;
		}
	}
	else
	{
		// 终端登录失败
		return false;
	}
}

bool CCltBusiness::checkLoginState()
{
	// 维护登录状态
	if (nullptr == m_local_clt)
	{
		return false;
	}
	SVR_CONN_INFO svr_conn_info;
	m_local_clt->queryConnInfoByType(miDataServer, svr_conn_info);

	dlplog_debug(m_log_handle_login, "term_index[%d] login state[%d]", m_term_index, svr_conn_info.buss_conn_state);

	if (svr_conn_info.buss_conn_state == enBCS_ReConnected)
	{
		// 将重新连接状态恢复到正常连接，发起登录请求
		m_local_clt->setReconnStateNormal(svr_conn_info);
		m_longin_state = enSVR_LOGIN_FAILED;
		m_uer_login_state = enSVR_LOGIN_FAILED;
		loginRequest0101();
	}
	else if (svr_conn_info.buss_conn_state == enBCS_Connected)
	{
		int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
		m_trcltcomm_lib.getServerTime(year, month, day, hour, minute, second);

		if (m_longin_state != enSVR_LOGIN_SUCCESS && m_had_send_login == false)
		{
			// 通信链路已连接但未登录或登录失败，发送登录协议
			loginRequest0101();
		}
	}
	else
	{
		// 连接状态异常，设置登录状态失败
		m_longin_state = enSVR_LOGIN_FAILED;
		m_uer_login_state = enSVR_LOGIN_FAILED;
	}
	return m_longin_state == enSVR_LOGIN_SUCCESS ? true : false;
}

bool CCltBusiness::requestStrategy()
{
	if (m_longin_state == enSVR_LOGIN_SUCCESS)
	{
		// 已登录到服务器，随机发起策略下载请求
		strategyRequest0101(CTestConfig::instance().stg_req_range);
		return true;
	}
	return false;
}

int CCltBusiness::getTermId()
{
	return m_term_id;
}

int CCltBusiness::getDevSvrId()
{
	auto iter = m_server_info_map.find(miDataServer);
	if (iter != m_server_info_map.end() && iter->second->is_logined)
	{
		return iter->second->id_;
	}
	return 0;
}

void CCltBusiness::initMap()
{
	m_log_table_name[LOGTABLE_PrintMonitor] = "PrintMonitor";
	m_log_table_name[LOGTABLE_ChatTextLog] = "ChatTextLog";
	m_log_table_name[LOGTABLE_CdBurnLog] = "CdBurnLog";
	m_log_table_name[LOGTABLE_BurnFileLog] = "BurnFileLog";
	m_log_table_name[LOGTABLE_FileOps] = "FileOps";
	m_log_table_name[LOGTABLE_OpAlarmMsg] = "OpAlarmMsg";
	m_log_table_name[LOGTABLE_Urls] = "Urls";
	m_log_table_name[LOGTABLE_Logs] = "Logs";
	m_log_table_name[LOGTABLE_SystemLog] = "SystemLog";
	m_log_table_name[LOGTABLE_LoginLog] = "LoginLog";
	m_log_table_name[LOGTABLE_DisclosureLog] = "DisclosureLog";
	m_log_table_name[LOGTABLE_CurTaskInfo] = "CurTaskInfo";
	m_log_table_name[LOGTABLE_forumdata] = "forumdata";
	m_log_table_name[LOGTABLE_Mails] = "Mails";
	m_log_table_name[LOGTABLE_ScreenShotLog] = "ScreenShotLog";
	m_log_table_name[LOGTABLE_ChatImagetLog] = "ChatImagetLog";
	m_log_table_name[LOGTABLE_ChatFiletLog] = "ChatFiletLog";
	m_log_table_name[LOGTABLE_UserInfoChange] = "UserInfoChange";
	m_log_table_name[LOGTABLE_GroupInfoChange] = "GroupInfoChange";
	m_log_table_name[LOGTABLE_UsbEventLog] = "UsbEventLog";
	m_log_table_name[LOGTABLE_BluetoothConnectChange] = "BluetoothConnectChange";
	m_log_table_name[LOGTABLE_BlueToothFileLog] = "BlueToothFileLog";


	m_generate_record_func[LOGTABLE_PrintMonitor] =
		std::bind(&CCltBusiness::GeneratePrintMonitorRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_ChatTextLog] =
		std::bind(&CCltBusiness::GenerateChatTextLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_CdBurnLog] =
		std::bind(&CCltBusiness::GenerateCdBurnLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_BurnFileLog] =
		std::bind(&CCltBusiness::GenerateBurnFileLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_FileOps] =
		std::bind(&CCltBusiness::GenerateFileOpsRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_OpAlarmMsg] =
		std::bind(&CCltBusiness::GenerateOpAlarmMsgRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_Urls] =
		std::bind(&CCltBusiness::GenerateUrlsRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_Logs] =
		std::bind(&CCltBusiness::GenerateLogsRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_SystemLog] =
		std::bind(&CCltBusiness::GenerateSystemLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_LoginLog] =
		std::bind(&CCltBusiness::GenerateLoginLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_DisclosureLog] =
		std::bind(&CCltBusiness::GenerateDisclosureLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_CurTaskInfo] =
		std::bind(&CCltBusiness::GenerateCurTaskInfoRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_forumdata] =
		std::bind(&CCltBusiness::GenerateforumdataRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_Mails] =
		std::bind(&CCltBusiness::GenerateMailsRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_ScreenShotLog] =
		std::bind(&CCltBusiness::GenerateScreenShotLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_ChatImagetLog] =
		std::bind(&CCltBusiness::GenerateChatImagetLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_ChatFiletLog] =
		std::bind(&CCltBusiness::GenerateChatFiletLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_UserInfoChange] =
		std::bind(&CCltBusiness::GenerateUserInfoChangeRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_GroupInfoChange] =
		std::bind(&CCltBusiness::GenerateGroupInfoChangeRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_UsbEventLog] =
		std::bind(&CCltBusiness::GenerateUsbEventLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_BluetoothConnectChange] =
		std::bind(&CCltBusiness::GenerateBluetoothConnectChangeRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);
	m_generate_record_func[LOGTABLE_BlueToothFileLog] =
		std::bind(&CCltBusiness::GenerateBlueToothFileLogRecord, this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4,
			std::placeholders::_5
		);

	for (int i=0; i<LOGTABLE_COUNT; ++i)
	{
		m_map_log_type_to_max_id[i] = 0;
	}
}

void CCltBusiness::initLog()
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

	return;
}

void CCltBusiness::initTermInfo()
{
	char szIndex[7] = {};
	snprintf(szIndex, sizeof(szIndex), "%02d%04d", CTestConfig::instance().client_num, m_term_index);

	CSimpleIniA ini;
	ini.SetUnicode(true);
	GetSimpleIniObj(ini, "clt_comm_tool.ini");

	strncpy(m_term_guid, ini.GetValue("clt_info", "guid"), sizeof(m_term_guid) - 1);
	strncpy(m_term_guid + strlen(m_term_guid) - TERM_INDEX_LEN - CLT_INDEX_LEN, szIndex, sizeof(szIndex) - 1);

	strncpy(m_term_mcode, ini.GetValue("clt_info", "MCode"), sizeof(m_term_mcode) - 1);
	strncpy(m_term_mcode + strlen(m_term_mcode) - TERM_INDEX_LEN - CLT_INDEX_LEN, szIndex, sizeof(m_term_mcode) - 1);

	strncpy(m_term_dpt_name, ini.GetValue("clt_info", "dpt_name"), sizeof(m_term_dpt_name) - 1);

	strncpy(m_term_user_name, ini.GetValue("clt_info", "user_name"), sizeof(m_term_user_name) - 1);
	strncat(m_term_user_name, szIndex, strlen(szIndex));

	strncpy(m_term_cpt_name, ini.GetValue("clt_info", "cpt_name"), sizeof(m_term_cpt_name) - 1);
	strncat(m_term_cpt_name, szIndex, strlen(szIndex));

    strncpy(m_term_ver, ini.GetValue("clt_info", "term_ver"), sizeof(m_term_ver) - 1);

	int network_count = ini.GetLongValue("clt_info", "network_count");

	char szSectionName[256] = {};
	char szKeyName[256] = {};
	char szMac[13] = {};
	char szIp[256] = {};
	NETWORKINFO * network = nullptr;
	for (int i = 0; i < network_count; i++)
	{
		network = new NETWORKINFO;
		snprintf(szSectionName, 255, "clt_net_%d", i+1);
		strncpy(szMac, ini.GetValue(szSectionName, "mac"), sizeof(szMac) - 1);
		strncpy(szMac + sizeof(szMac) - TERM_INDEX_LEN - CLT_INDEX_LEN - 1, szIndex, sizeof(szIndex));
		
		network->mac = szMac;

		network->ip_count = ini.GetLongValue(szSectionName, "ip_count");

		for (int j = 0; j < network->ip_count; j++)
		{
			snprintf(szKeyName, 255, "ip_%d", j+1);
			strncpy(szIp, ini.GetValue(szSectionName, szKeyName), sizeof(szIp) - 1);
			network->vec_ip.push_back(szIp);
		}

		m_vec_network.push_back(network);
	}
}

//十六进制字符串转换为字节流  
void CCltBusiness::HexStrToByte(const char* source, int sourceLen, unsigned char* dest)
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

// 终端登录请求
int CCltBusiness::loginRequest0101()
{
	if (CTestConfig::instance().term_oper_login & 0x01)
	{
		// 需要登录
	}
	else
	{
		// 指定不登陆
		return -1;
	}

	m_had_send_login = true;

	char szTermIndex[5] = {};
	snprintf(szTermIndex, sizeof(szTermIndex), "%04d", m_term_index);
	uint32_t str_len = 0;
	uint8_t data[2048] = {};

	uint8_t network_count = m_vec_network.size();	// 网卡个数

	tr::proto::CProtoWriter writer(data, sizeof(data));
	writer.writeUInt8(0);				// 0二进制结构，1json格式
	writer.writeUInt16(1);				// 策略版本号
	writer.writeUInt8(CTestConfig::instance().term_type);// 终端类型
	writer.writeUInt32(0);				// 设备id
	writer.writeString(m_term_guid, sizeof(m_term_guid) - 1, 50);
	writer.writeString(m_term_mcode, sizeof(m_term_mcode) - 1, 50);
	writer.writeUInt8(0);				// 服务器负载均衡
	writer.writeUInt64(0);
	writer.writeString(m_term_ver, sizeof(m_term_ver) - 1, 20);		// 终端版本
	str_len = strlen(m_term_dpt_name);
	writer.writeUInt32(str_len);
	writer.writeString(m_term_dpt_name, str_len, str_len);
	str_len = strlen(m_term_user_name);
	writer.writeUInt32(str_len);
	writer.writeString(m_term_user_name, str_len, str_len);
	str_len = strlen(m_term_cpt_name);
	writer.writeUInt32(str_len);
	writer.writeString(m_term_cpt_name, str_len, str_len);
	writer.writeUInt8(network_count);	// 	网卡个数

	char szSectionName[256] = {};
	char szMac[13] = {};
	uint8_t		mac[6] = {};				// mac地址
	uint8_t		ip_count = 0;				// ip个数
	for (int i = 0; i < network_count; i++)
	{
		snprintf(szSectionName, 255, "clt_net_%d", i);
		strncpy(szMac, m_vec_network[i]->mac.c_str(), sizeof(szMac) - 1);
		strncpy(szMac + sizeof(szMac) - TERM_INDEX_LEN - 1, szTermIndex, sizeof(szTermIndex));

		HexStrToByte(szMac, strlen(szMac), mac);
		writer.writeBlock(mac, sizeof(mac));

		ip_count = m_vec_network[i]->ip_count;
		writer.writeUInt8(ip_count);

		for (int j = 0; j < ip_count; j++)
		{
            if (m_vec_network[i]->vec_ip[j].find(':') != std::string::npos)
            {
                // ipv6
                struct in6_addr s; // IPv6地址结构体
                inet_pton(AF_INET6, m_vec_network[i]->vec_ip[j].c_str(), (void *)&s);
                // Word[0]~Word[5]的内容使用原始内容
                s.u.Word[6] = CTestConfig::instance().client_num;  // 倒数第三和第四字节保存client_num
                s.u.Word[7] = m_term_index;     // 最后2个字节保存m_term_index
                writer.writeUInt8(1);
                writer.writeBlock(s.u.Byte, 16);
            }
            else
            {
                // ipv4
                struct in_addr s; // IPv4地址结构体
                inet_pton(AF_INET, m_vec_network[i]->vec_ip[j].c_str(), (void *)&s);
                uint32_t host_ip = s.S_un.S_addr;
                host_ip = ntohl(host_ip);
                host_ip &= 0xFF000000;// 保留原始IP的第一个字节
                host_ip |= ((uint8_t)CTestConfig::instance().client_num) << 16; // 第二个字节保存client_num
                host_ip += m_term_index;// 第四个字节保存m_term_index，允许使用到第三个字节
                writer.writeUInt8(0);
                writer.writeUInt32(host_ip);
            }
		}
	}

	SYSTEMTIME sys_time;
	GetLocalTime(&sys_time);
	dlplog_info(m_log_handle_login, "[%s] [%4d/%02d/%02d %02d:%02d:%02d.%03d] term_index[%d]", __FUNCTION__,
		sys_time.wYear, sys_time.wMonth, sys_time.wDay, sys_time.wHour, sys_time.wMinute, sys_time.wSecond, sys_time.wMilliseconds, m_term_index);

	// 登录协议发送时，设备类型或设备id应填0
	unsigned int msg_sn = 0;
	int ret = m_local_clt->sendDataToServerByType(miDataServer, data, writer.getLen(), LOGIN_MODE_ID, 0x0101, msg_sn);

	if (0 == ret)		//ret=0 成功 -1失败
	{
		INTERACTIVE_INFO interactive;

		interactive.success = true;
		interactive.sender = true;
		interactive.protocol_num = (LOGIN_MODE_ID << 16) + 0x0101;
		interactive.msg_sn = msg_sn;
		interactive.data_size = writer.getLen();
		interactive.business_count = 1;
		interactive.deal_time = std::chrono::steady_clock::now();
		interactive.pkg_total = 1;
		strncpy(interactive.term_guid, m_term_guid,sizeof(interactive.term_guid));
		interactive.term_id = m_term_id;
		if (NULL != m_addInteractiveInfo)
		{
			m_addInteractiveInfo(interactive);
		}
	}
	else
	{
		dlplog_warn(m_log_handle_login, "[%s] term_index[%d] send login proto failed[%d]", __FUNCTION__, m_term_index, ret);
	}
	
	return ret;
}

bool CCltBusiness::parseDevSvrList(std::string & str_json)
{
	dlplog_info(m_log_handle_login, "dev_server info: [%s]",
		str_json.c_str());

	uint32_t cur_server_id = 0;
	uint32_t cur_term_id = 0;

	rapidjson::Document dom;
	if (!dom.Parse(str_json.c_str()).HasParseError())
	{
		if (dom.HasMember("dataServerId") && dom["dataServerId"].IsInt())
		{
			cur_server_id = dom["dataServerId"].GetInt();
		}
		if (dom.HasMember("devId") && dom["devId"].IsInt())
		{
			cur_term_id = dom["devId"].GetInt();
		}

		if (dom.HasMember("dataServer") && dom["dataServer"].IsArray())
		{
			const rapidjson::Value& arr = dom["dataServer"];
			for (int i = 0; i < arr.Size(); ++i)
			{
				const rapidjson::Value& object = arr[i];
				if (object.IsObject())
				{
					std::shared_ptr<DSvrInfo> dsvr_info = std::make_shared<DSvrInfo>();
					memset(dsvr_info.get(), 0x00, sizeof(DSvrInfo));

					if (object.HasMember("priority") && object["priority"].IsInt())
					{
						dsvr_info->priority = object["priority"].GetInt();
					}
					if (object.HasMember("serverId") && object["serverId"].IsInt())
					{
						dsvr_info->dsvr_id = object["serverId"].GetInt();
					}
					if (object.HasMember("intranetIp") && object["intranetIp"].IsString())
					{
						strncpy(dsvr_info->intranet_ip,
							object["intranetIp"].GetString(),
							sizeof(dsvr_info->intranet_ip) - 1);
					}
					if (object.HasMember("intranetPort") && object["intranetPort"].IsInt())
					{
						dsvr_info->intranet_port = object["intranetPort"].GetInt();
					}
					if (object.HasMember("internetIp") && object["internetIp"].IsString())
					{
						strncpy(dsvr_info->internet_ip,
							object["internetIp"].GetString(),
							sizeof(dsvr_info->intranet_ip) - 1);
					}
					if (object.HasMember("internetPort") && object["internetPort"].IsInt())
					{
						dsvr_info->internet_port = object["internetPort"].GetInt();
					}

					if (m_map_dsvr_info[dsvr_info->priority])
					{
						if (m_map_dsvr_info[dsvr_info->priority]->dsvr_id == dsvr_info->dsvr_id)
						{
							dsvr_info->tried = m_map_dsvr_info[dsvr_info->priority]->tried;
						}
						m_map_dsvr_info.erase(dsvr_info->priority);
					}
					m_map_dsvr_info[dsvr_info->priority] = dsvr_info;
				}
			}
		}

		for (int i = 3; i >= 0; --i)
		{
			if (m_map_dsvr_info[i] && m_map_dsvr_info[i]->tried == false)
			{
				if (cur_server_id == m_map_dsvr_info[i]->dsvr_id)
				{
					// 与当前登录的采集一致，不需要切换
					m_map_dsvr_info[i]->tried = true;
					break;
				}

				// 需要切换采集

				// 当前登录的采集失败
				m_had_send_login = false;
				m_longin_state = enSVR_LOGIN_FAILED;
				std::map<uint32_t, CServerInfo *>::iterator iter =
					m_server_info_map.find(miDataServer);
				if (iter != m_server_info_map.end())
				{
					iter->second->is_logined = false;
				}

				dlplog_info(m_log_handle_login, "terminal change dev_server[%d]",
					m_map_dsvr_info[i]->dsvr_id);

				m_local_clt->disconnServerByType(miDataServer);

				strncpy(m_server_info_map[miDataServer]->address_,
					m_map_dsvr_info[i]->intranet_ip, sizeof(m_server_info_map[miDataServer]->address_) - 1);
				m_server_info_map[miDataServer]->port_ = m_map_dsvr_info[i]->intranet_port;
				m_server_info_map[miDataServer]->id_ = m_map_dsvr_info[i]->dsvr_id;

				m_local_clt->connNewServer(m_server_info_map[miDataServer]->address_,
					m_server_info_map[miDataServer]->port_, miDataServer);
				/*
				uint32_t try_tmies = 0;
				SVR_CONN_INFO svr_conn_info;
				while (svr_conn_info.buss_conn_state != enBCS_Connected)
				{
					Sleep(100);
					m_local_clt->queryConnInfoByType(miDataServer, svr_conn_info);
					if (++try_tmies > 5)
					{
						break;
					}
				}

				if (svr_conn_info.buss_conn_state == enBCS_Connected)
				{
					loginRequest0101();
				}
				else
				{
					dlplog_info(m_log_handle_login, "terminal change dev_server failed");
				}*/
				return false;
			}
		}

	}
	return true;
}

int CCltBusiness::loginRespond0181(
	const unsigned char* msg_body,	// 消息体数据
	unsigned int msg_body_len,		// 消息体长度
	RECV_EXTRA * recv_extra
)
{
	uint8_t		proto_type = 0;			// 0二进制结构，1json格式
	uint16_t	proto_ver = 0;			// 协议版本
	uint8_t		status = 0;				// 登录状态
	uint32_t	str_len = 0;
	uint32_t	group_id = 0;			// 分组Id
	char		guid[50 + 1];			// 终端guid
	uint32_t	svr_id = 0;				// 服务器设备id
	uint32_t	svr_dev_type = 0;		// 服务器设备类型

	tr::proto::CProtoReader reader(msg_body, msg_body_len);

	reader.readUInt8(&proto_type);
	reader.readUInt16(&proto_ver);
	reader.readUInt8(&status);

	// 0x00：登录失败
	// 0x01：登录成功
	// 0x02：登录失败，终端版本过低
	// 0x03：禁止新用户接入
	// 0x10：审批中
	// 0x11：审批失败，拒绝接入
	// 0x12：审批接入失败，删除终端
	// 0x20：服务器负载超过限定值，拒绝接入

	if (0x01 == status)
	{
		// 登录成功
		reader.readUInt32(&m_term_id);
		reader.readUInt32(&str_len);
		reader.readVarStringObj(m_term_nickname, str_len);
		reader.readUInt32(&group_id);
		reader.readString(guid, sizeof(guid) - 1, sizeof(guid) - 1);
		reader.readUInt32(&svr_id);
		reader.readUInt32(&svr_dev_type);
		reader.readUInt8(&m_login_mode);
		reader.readUInt32(&m_user_id);

		m_longin_state = enSVR_LOGIN_SUCCESS;

		std::map<uint32_t, CServerInfo *>::iterator iter =
			m_server_info_map.find(miDataServer);
		if (iter != m_server_info_map.end())
		{
			iter->second->id_ = svr_id;
			if (0x01 == status)
			{
				iter->second->is_logined = true;
			}
		}
		m_local_clt->setConnInfo(recv_extra->conn_uid, m_term_id, svr_id, svr_dev_type);
		
		// 跳过中间协议
		reader.skip(32);
		uint8_t main_key_len = 0;
		reader.readUInt8(&main_key_len);
		reader.skip(main_key_len);
		reader.skip(20 + 15);

		uint32_t json_len = 0;
		std::string str_json;
		reader.readUInt32(&json_len);
		if (json_len > 0)
		{
			reader.readVarStringObj(str_json, json_len);
			
			// 立即切换采集服务器--真实终端不会立即切换采集服务器
			//if (!parseDevSvrList(str_json))
			//{
			//	// 当前采集服务器登录失败
			//	status = 0x20;
			//}
			//else
			//{
			//	dlplog_info(m_log_handle_login, "terminal login success : term_index[%d] term_id[%d]",
			//		m_term_index, m_term_id);
			//}
		}

        dlplog_info(m_log_handle_login, "terminal login success : term_index[%d] term_id[%d]",
            m_term_index, m_term_id);
	}
	else if (0x20 == status)
    {
        dlplog_warn(m_log_handle_login, "terminal login failed : term_index[%d] errcode[%d]",
            m_term_index, status);

		uint32_t json_len = 0;
		std::string str_json;
		reader.readUInt32(&json_len);
		if (json_len > 0)
		{
			reader.readVarStringObj(str_json, json_len);

			parseDevSvrList(str_json);
		}
	}
	else
	{
		dlplog_warn(m_log_handle_login, "terminal login failed : term_index[%d] errcode[%d]",
			m_term_index, status);
	}

	if (NULL != m_statusReport)
	{
		m_statusReport(m_term_index, INTERACTIVE_TERM_LOGIN, 1 == status ? true : false);
	}

	INTERACTIVE_INFO interactive;

	interactive.success = true;
	interactive.sender = false;
	interactive.protocol_num = (LOGIN_MODE_ID << 16) + 0x0181;
	interactive.msg_sn = recv_extra->msg_sn;
	interactive.data_size = msg_body_len;
	interactive.business_count = 0;
	interactive.deal_time = std::chrono::steady_clock::now();
	interactive.pkg_total = 1;
	strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
	interactive.term_id = m_term_id;

	if (NULL != m_addInteractiveInfo)
	{
		m_addInteractiveInfo(interactive);
	}

	return status;
}

int CCltBusiness::userLoginReq0301()
{
	uint32_t	str_len = 0;
	uint32_t	dev_id = 0;					// 设备Id
	uint32_t	usr_id = 0;					// 用户Id
	char		user_name[64 + 1] = {};		// 用户名
	char		password[64 + 1] = {};		// 用户登录密码
	uint8_t		usb_active = 0;				// usb实时监控  0不启用  1启用
	char		usb_key[50 + 1] = {};		//
	uint8_t		login_mode = 0;				// 登录模式 0账号密码 1自动登录 2域登录
	char		sid[100 + 1] = {};			// 域用户的sid

	uint8_t data[2048] = {};
	tr::proto::CProtoWriter writer(data, sizeof(data));
	writer.writeUInt8(0);			// 0二进制结构，1json格式
	writer.writeUInt16(0);			// 协议版本
	writer.writeUInt32(m_term_id);
	writer.writeUInt32(m_user_id);
	str_len = strlen("test_usr_name");
	writer.writeUInt32(str_len);
	writer.writeString("test_usr_name", str_len, str_len);
	writer.writeString("test_password_md5", sizeof("test_password_md5") - 1, 64);
	writer.writeString("password_rc4", sizeof("password_rc4") - 1, 64);
	writer.writeUInt8(usb_active);
	writer.writeString(usb_key, sizeof(usb_key) - 1, 50);
	writer.writeUInt8(m_login_mode);
	writer.writeString(sid, sizeof(sid) - 1, 100);

	// 登录协议发送时，设备类型或设备id应填0
	unsigned int msg_sn = 0;
	if (nullptr == m_local_clt)
	{
		return -1;
	}
	int ret = m_local_clt->sendDataToServerByType(miDataServer, data, writer.getLen(), LOGIN_MODE_ID, 0x0301, msg_sn);

	if (0 == ret)		//ret=0 成功 -1失败
	{
		INTERACTIVE_INFO interactive;

		interactive.success = true;
		interactive.sender = true;
		interactive.protocol_num = (LOGIN_MODE_ID << 16) + 0x0301;
		interactive.msg_sn = msg_sn;
		interactive.data_size = writer.getLen();
		interactive.business_count = 1;
		interactive.deal_time = std::chrono::steady_clock::now();
		interactive.pkg_total = 1;
		strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
		interactive.term_id = m_term_id;

		if (NULL != m_addInteractiveInfo)
		{
			m_addInteractiveInfo(interactive);
        }
	}
	else
	{
		dlplog_warn(m_log_handle_login, "[%s] term_index[%d] send user login proto failed[%d]", __FUNCTION__, m_term_index, ret);
	}

	return ret;
}

int CCltBusiness::userLoginRsp0381(const unsigned char* msg_body, /* 消息体数据 */ unsigned int msg_body_len, /* 消息体长度 */ RECV_EXTRA * recv_extra)
{
	uint8_t		proto_type = 0;			// 0二进制结构，1json格式
	uint16_t	proto_ver = 0;			// 协议版本
	uint8_t		status = 0;				// 登录状态
										// 0：登录失败 1：登录成功 2：操作员不存在
										// 3：密码错误 4：禁止空密码登录
										// 5：usbkey验证失败 6、操作员绑定的设备ID匹配失败

	tr::proto::CProtoReader reader(msg_body, msg_body_len);
	reader.readUInt8(&proto_type);
	reader.readUInt16(&proto_ver);
	reader.readUInt8(&status);

	if (1 == status)
	{
		// 登录成功

		m_uer_login_state = enSVR_LOGIN_SUCCESS;
		dlplog_info(m_log_handle_login, "user login success : term_index[%d] term_id[%d] usr_id[%d]",
			m_term_index, m_term_id, m_user_id);
	}
	else
	{
		dlplog_warn(m_log_handle_login, "user login failed : term_index[%d] term_id[%d] usr_id[%d]",
			m_term_index, m_term_id, m_user_id);
	}

	if (NULL != m_statusReport)
	{
		m_statusReport(m_term_index, INTERACTIVE_USER_LOGIN, 1 == status ? true : false);
	}

	INTERACTIVE_INFO interactive;

	interactive.success = true;
	interactive.sender = false;
	interactive.protocol_num = (LOGIN_MODE_ID << 16) + 0x0381;
	interactive.msg_sn = recv_extra->msg_sn;
	interactive.data_size = msg_body_len;
	interactive.business_count = 0;
	interactive.deal_time = std::chrono::steady_clock::now();
	interactive.pkg_total = 1;
	strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
	interactive.term_id = m_term_id;

	if (NULL != m_addInteractiveInfo)
	{
		m_addInteractiveInfo(interactive);
	}
	
	return status;
}

int CCltBusiness::strategyRequest0101(uint16_t req_stg_type)
{
	if (0 == req_stg_type)
	{
		return 0;
	}

	uint8_t proto_type = 0;					// 数据类型 0 二进制， 1 json
	uint16_t proto_ver = 0;

	uint8_t data[2048] = {};
	tr::proto::CProtoWriter writer(data, sizeof(data));
	writer.writeUInt8(0);
	writer.writeUInt16(1);		// 协议版本
	writer.writeUInt8(CTestConfig::instance().term_type);
	writer.writeUInt32(m_term_id);
	writer.writeUInt32(m_user_id);
	writer.writeUInt16(req_stg_type);		// 请求所有策略类型:0x01基础策略 0x02终端策略 0x04操作员策略
	writer.writeUInt16(0);

	dlplog_info(m_log_handle_stg, "[%s] term_id[%d]", __FUNCTION__, m_term_id);


	// 登录协议发送时，设备类型或设备id应填0
	unsigned int msg_sn = 0;
	int ret = m_local_clt->sendDataToServerByType(miDataServer, data, writer.getLen(), STG_MODE_ID, 0x0101, msg_sn);

	if (0 == ret)		//ret=0 成功 -1失败
	{
		INTERACTIVE_INFO interactive;

		interactive.success = true;
		interactive.sender = true;
		interactive.protocol_num = (STG_MODE_ID << 16) + 0x0101;
		interactive.msg_sn = msg_sn;
		interactive.data_size = writer.getLen();
		interactive.business_count = 0;
		interactive.deal_time = std::chrono::steady_clock::now();
		interactive.pkg_total = 1;
		strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
		interactive.term_id = m_term_id;

		if (NULL != m_addInteractiveInfo)
		{
			m_addInteractiveInfo(interactive);
		}
	}

	return ret;
}

int CCltBusiness::strategyRespond0181(const unsigned char* msg_body, /* 消息体数据 */ unsigned int msg_body_len, /* 消息体长度 */ RECV_EXTRA * recv_extra)
{
	uint8_t		proto_type = 0;			// 0二进制结构，1json格式
	uint16_t	proto_ver = 0;			// 协议版本
	uint16_t	status = 0;				// 0：失败 1：成功 
	uint16_t	pkg_total = 0;			// 总帧数
	uint16_t	pkg_index = 0;			// 当前帧，从1开始
	uint32_t	stg_len = 0;			// 策略信息长度,每一帧的策略信息长度不会超过1Mb

	tr::proto::CProtoReader reader(msg_body, msg_body_len);
	reader.readUInt8(&proto_type);
	reader.readUInt16(&proto_ver);
	reader.readUInt16(&status);

	if (1 == status)
	{
		reader.readUInt16(&pkg_total);
		reader.readUInt16(&pkg_index);
		reader.readUInt32(&stg_len);

		dlplog_info(m_log_handle_stg, "[%s] term_id[%d] pkg_total[%d] pkg_index[%d] stg_size[%d] msg_sn[%d]\n",
			__FUNCTION__, m_term_id, pkg_total, pkg_index, stg_len, recv_extra->msg_sn);
	}
	else
	{
		dlplog_warn(m_log_handle_stg, "[%s] term_id[%d] strategy download failed",
			__FUNCTION__, m_term_id);
		return status;
	}

	INTERACTIVE_INFO interactive;

	interactive.success = true;
	interactive.sender = false;
	interactive.protocol_num = (STG_MODE_ID << 16) + 0x0181;
	interactive.msg_sn = recv_extra->msg_sn;
	interactive.data_size = msg_body_len;
	interactive.business_count = 1;
	interactive.deal_time = std::chrono::steady_clock::now();
	interactive.pkg_total = pkg_total;
	strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
	interactive.term_id = m_term_id;

	if (NULL != m_addInteractiveInfo)
	{
		m_addInteractiveInfo(interactive);
	}
	else
    {
        dlplog_warn(m_log_handle_stg, "[%s] InteractiveInfo loss", __FUNCTION__);
	}

	if (pkg_total == pkg_index)
    {
		// 收取到最后一帧的时候才发送成功通知
        strategySuccessNotice0102(recv_extra->msg_sn);
	}

	return status;
}

int CCltBusiness::strategySuccessNotice0102(uint32_t req_msg_sn)
{
	uint8_t proto_type = 0;				// 数据类型 0 二进制， 1 json
	uint16_t proto_ver = 0;				// 协议版本

	uint8_t data[2048] = {};
	tr::proto::CProtoWriter writer(data, sizeof(data));
	writer.writeUInt8(0);
	writer.writeUInt16(0);
	writer.writeUInt32(req_msg_sn);

	// 登录协议发送时，设备类型或设备id应填0
	unsigned int msg_sn = 0;
	int ret = m_local_clt->sendDataToServerByType(miDataServer, data, writer.getLen(), STG_MODE_ID, 0x0102, msg_sn);


	if (0 == ret)		//ret=0 成功 -1失败
	{
		INTERACTIVE_INFO interactive;

		interactive.success = true;
		interactive.sender = true;
		interactive.protocol_num = (STG_MODE_ID << 16) + 0x0102;
		interactive.msg_sn = msg_sn;
		interactive.data_size = writer.getLen();
		interactive.business_count = 1;
		interactive.deal_time = std::chrono::steady_clock::now();
		interactive.pkg_total = 1;
		strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
		interactive.term_id = m_term_id;

		if (NULL != m_addInteractiveInfo)
		{
			m_addInteractiveInfo(interactive);
		}
	}

	return 0;
}

int CCltBusiness::stgUpdateNotice0203(const unsigned char* msg_body, /* 消息体数据 */ unsigned int msg_body_len, /*消息体长度 */ RECV_EXTRA * recv_extra)
{
	if (CTestConfig::instance().respond_stg_notice == false)		//禁止策略响应，直接返回
	{
		return -1;
	}
	uint8_t		proto_type = 0;			// 0二进制结构，1json格式
	uint16_t	proto_ver = 0;			// 协议版本
	uint32_t	term_id = 0;
	uint32_t	user_id = 0;
	uint16_t	range = 0;

	tr::proto::CProtoReader reader(msg_body, msg_body_len);

	reader.readUInt8(&proto_type);		// 数据类型：0二进制，1JSON
	reader.readUInt16(&proto_ver);		// 版本
	reader.readUInt32(&term_id);		// 终端id
	reader.readUInt32(&user_id);		// 用户id	
	reader.readUInt16(&range);			// 变更范围

	return strategyRequest0101(range);
}

int CCltBusiness::LogUploadNotice0101(const unsigned char* msg_body, /* 消息体数据 */ unsigned int msg_body_len, /*消息体长度 */ RECV_EXTRA * recv_extra)
{
	uint8_t		proto_type = 0;			// 0二进制结构，1json格式
	uint16_t	proto_ver = 0;			// 协议版本
	uint32_t	json_len = 0;			// json字符串长度
	std::string	json;

	if (CTestConfig::instance().enable_log_upload == false)			//禁止日志响应，直接返回
	{
		return  -1;
	}

	tr::proto::CProtoReader reader(msg_body, msg_body_len);
	reader.readUInt8(&proto_type);
	reader.readUInt16(&proto_ver);
	reader.readUInt32(&json_len);

	reader.readVarStringObj(json, json_len);

	//json模拟数据如下：
	//{
	//	"data":[{"bussType":0,"num" : 500},{ "bussType":1,"num" : 200 },{ "bussType":2,"num" : 500 }]
	//}

	std::map<uint8_t, uint16_t> json_data;						//用来保存业务信息 <业务类型, 收集条数>	这个信息之后需要统计。

	//解析json数据内容，并保存在json_data中
	rapidjson::Document dom;
	if (!dom.Parse(json.c_str()).HasParseError())
	{
		if (dom.HasMember("data") && dom["data"].IsArray()) 
		{
			const rapidjson::Value& arr = dom["data"];
			for (int i = 0; i < arr.Size(); ++i)
			{
				const rapidjson::Value& object = arr[i];
				if (object.IsObject())
				{
					uint8_t bussType = 0;
					uint16_t num = 0;
					if (object.HasMember("bussType") && object["bussType"].IsInt())
					{
						bussType = object["bussType"].GetInt();
					}
					if (object.HasMember("num") && object["num"].IsInt())
					{
						num = object["num"].GetInt();
					}
					json_data.insert(std::pair<uint8_t, uint16_t>(bussType, num));
				}
			}
		}
	}

	dlplog_info(m_log_handle, "[%s] term_id[%d]", __FUNCTION__, m_term_id);

	INTERACTIVE_INFO interactive;

	interactive.success = true;
	interactive.sender = false;
	interactive.protocol_num = (LOG_MODE_ID << 16) + 0x0101;
	interactive.msg_sn = recv_extra->msg_sn;
	interactive.data_size = msg_body_len;
	interactive.business_count = json_data.size();
	interactive.deal_time = std::chrono::steady_clock::now();
	interactive.pkg_total = 1;
	strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
	interactive.term_id = m_term_id;

	if (NULL != m_addInteractiveInfo)
	{
		m_addInteractiveInfo(interactive);
	}

	LogUpload0201(json_data);

	return 1;
}

int CCltBusiness::LogUpload0201(std::map<uint8_t, uint16_t> &json_data)
{
	//目前只处理业务类型为1：收集日志的业务
	char * json = NULL;
	uint32_t json_len = 0;

	//界面传入非0的指定个数则按照界面指定个数获取日志条数。为0则取json中要求的条数。
	uint16_t log_num = CTestConfig::instance().log_content_num;

	if (0 == log_num)
	{
		for (std::map<uint8_t, uint16_t>::iterator it = json_data.begin(); it != json_data.end(); it++)
		{
			if (1 == it->first)
			{
				log_num = it->second;
				break;
			}
		}
	}

	json = GenerateLogJson(log_num);
	json_len = strlen(json);

	uint8_t * data = new uint8_t[1024 * 1024];
	memset(data, 0x00, 1024 * 1024);

	tr::proto::CProtoWriter writer(data, sizeof(data));
	writer.writeUInt8(1);
	writer.writeUInt16(1);
	writer.writeUInt32(json_len);
	writer.writeString(json, json_len, json_len);

	// 登录协议发送时，设备类型或设备id应填0
	unsigned int msg_sn = 0;
	int ret = m_local_clt->sendDataToServerByType(miDataServer, data, writer.getLen(), LOG_MODE_ID, 0x0201, msg_sn);
	dlplog_info(m_log_handle, "[%s] term_id[%d], msg[%d]", __FUNCTION__, m_term_id, msg_sn);

	delete[] data;
	data = nullptr;

	delete[] json;
	json = nullptr;

	if (0 == ret)		//ret=0 成功 -1失败
	{
		INTERACTIVE_INFO interactive;

		interactive.success = true;
		interactive.sender = true;
		interactive.protocol_num = (LOG_MODE_ID << 16) + 0x0201;
		interactive.msg_sn = msg_sn;
		interactive.data_size = writer.getLen();
		interactive.business_count = 0;
		interactive.deal_time = std::chrono::steady_clock::now();
		interactive.pkg_total = 1;
		strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
		interactive.term_id = m_term_id;

		if (NULL != m_addInteractiveInfo)
		{
			m_addInteractiveInfo(interactive);
		}
	}

	return ret;
}

int CCltBusiness::LogUpload0281(const unsigned char* msg_body, /* 消息体数据 */ unsigned int msg_body_len, /*消息体长度 */ RECV_EXTRA * recv_extra)
{
	uint8_t		proto_type = 0;			// 0二进制结构，1json格式
	uint16_t	proto_ver = 0;			// 协议版本
	uint32_t	json_len = 0;			// json字符串长度
	char *		json = nullptr;

	tr::proto::CProtoReader reader(msg_body, msg_body_len);
	reader.readUInt8(&proto_type);
	reader.readUInt16(&proto_ver);
	reader.readUInt32(&json_len);

	json = new char[json_len + 1];
	memset(json, 0x00, json_len + 1);
	reader.readString(json, json_len, json_len);


	//json模拟数据如下：
	//{
	//	"data":[{"bussType":0,"num" : 500},{ "bussType":1,"num" : 200 },{ "bussType":2,"num" : 500 }]
	//}
	int json_data_num = 0;						//目前只解析json含有几个响应信息体
													
	rapidjson::Document dom;
	if (!dom.Parse(json).HasParseError())
	{
		if (dom.HasMember("data") && dom["data"].IsArray())
		{
			const rapidjson::Value& arr = dom["data"];
			for (int i = 0; i < arr.Size(); ++i)
			{
				const rapidjson::Value& object = arr[i];
				if (object.IsObject())
				{
					json_data_num++;
				}
			}
		}
	}

	dlplog_info(m_log_handle, "[%s] term_id[%d], msg[%d]", __FUNCTION__, m_term_id, recv_extra->msg_sn);
	//dlplog_info(m_log_handle, "[%s] term_id[%d]", __FUNCTION__, m_term_id);
	delete[] json;

	INTERACTIVE_INFO interactive;

	interactive.success = true;
	interactive.sender = false;
	interactive.protocol_num = (LOG_MODE_ID << 16) + 0x0281;
	interactive.msg_sn = recv_extra->msg_sn;
	interactive.data_size = msg_body_len;
	interactive.business_count = json_data_num;
	interactive.deal_time = std::chrono::steady_clock::now();
	interactive.pkg_total = 1;
	strncpy(interactive.term_guid, m_term_guid, sizeof(interactive.term_guid));
	interactive.term_id = m_term_id;

	if (NULL != m_addInteractiveInfo)
	{
		m_addInteractiveInfo(interactive);
	}

	return 1;
}


int CCltBusiness::beginsend2detector(std::string v_sendMsg)
{
	SVR_CONN_INFO svr_conn_info;
	m_local_clt->queryConnInfoByType(miAdvAlgoContentAwareServer, svr_conn_info);
	//dlplog_info(m_log_handle, "[%s] buss_conn_state:%d", __FUNCTION__, svr_conn_info.buss_conn_state);
	if (svr_conn_info.buss_conn_state == enBCS_Connected)
	{
		for (int i = 0; i < 50; i++)
		{
			threadPool_->enqueue([this,v_sendMsg]() {send2detector(v_sendMsg); });
		}
	}
	else
	{
		dlplog_error(m_log_handle, "[%s] buss_conn_state:%d", __FUNCTION__, svr_conn_info.buss_conn_state);
	}
	return 0;
}

int CCltBusiness::send2detector(std::string v_sendMsg)
{
	uint8_t proto_type = 0;				// 数据类型 0 二进制， 1 json
	uint16_t proto_ver = 0;				// 协议版本

	uint8_t data[2048] = {};
	tr::proto::CProtoWriter writer(data, sizeof(data));
	writer.writeUInt8(0);
	writer.writeUInt16(0);

	writer.writeUInt8(CTestConfig::instance().term_type);
	writer.writeUInt32(m_term_id);
	//!< 解析终端操作上下文信息
	char detectionInf_str_p[4096];
	SYSTEMTIME st;
	GetLocalTime(&st);
	char task_time[256];
	snprintf(task_time, 256, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear,st.wMonth,st.wDay,st.wHour, st.wMinute, st.wSecond);

	snprintf(detectionInf_str_p, 4096, "{\"strategyIds\":[{\"sid\":%d}],\"properties\":{\"guid\":\"25aad5bcad854796bf977e9d7fe49a0e\",\"devId\":\"%d\",\"lossType\":7,\
	\"contentType\":1,\"fileName\":\"test\",\"filePath\":\"C:\\\\Users\\\\test\\\\test.txt\",\"devType\":1,\"occurTime\":\"%s\",\
	\"sourceName\":\"DESKTOP-BTUKFTA\",\"sourceIP\":\"192.168.163.133\",\"mac\":\"000c292c2ea4\",\"destName\":\"\",\"destIP\":\"\",\
	\"matchPosType\":0,\"userLoginName\":\"User%d\",\"userLoginNickName\":\"Name_%d\",\"userId\":%u}}",
		CTestConfig::instance().send_info.stg_id, m_term_id, task_time,
		m_term_id, m_term_id, m_user_id
	);

	//snprintf(detectionInf_str_p, 4096,
	//	"{\"strategyIds\":[{\"sid\":2}],\"properties\":{\"guid\":\
	//		\"34b623868484473a9184856d838438b5\",\"devId\":\"%d\",\"lossType\":7,\
	//		\"contentType\":1,\"fileName\":\"文件指纹-栀子花.txt\",\"filePath\":\
	//		\"C:\\\\Users\\\\TTT\\\\Desktop\\\\高级检测测试文件\\\\高级检测测试文件\\\\文件指纹-栀子花.txt\",\
	//		\"devType\":1,\"occurTime\":\"2023-08-14 16:41:47\",\"sourceName\":\"中文测试设备名\",\"sourceIP\":\"192.168.163.133\",\
	//		\"mac\":\"000c292c2ea4\",\"destName\":\"\",\"destIP\":\"\",\"matchPosType\":0,\"userLoginName\":\
	//		\"User%d\",\"userLoginNickName\":\"TTT\",\"userId\":%u}}",
	//	m_term_id, m_term_id, m_user_id
	//);
	std::string detectionInf_str = detectionInf_str_p;

	writer.writeUInt32(detectionInf_str.length());
	writer.writeBlock((uint8_t *)detectionInf_str.c_str(), detectionInf_str.length());
	//!< 待检测数据
	writer.writeUInt8(2);

	//!< 总帧数
	writer.writeUInt32(1);
	//!< 当前帧
	writer.writeUInt32(1);
	
	//1023*1024 最长
	/*std::string content_str_p = "栀子花（学名：Gardenia jasminoides），又名栀子、黄栀子。属双子叶植物纲、茜草科、栀子属常绿灌木，枝叶繁茂，叶色四季常绿，花芳香，是重要的庭院观赏植物。单叶对生或三叶轮生，叶片倒卵形，革质，翠绿有光泽。浆果卵形，黄色或橙色。除观赏外，其花、果实、叶和根可入药，\
		有泻火除烦，清热利尿，凉血解毒之功效。花可做茶之香料，果实可消炎祛热。是优良的芳香花卉。栀子花喜光照充足且通风良好的环境，但忌强光曝晒。宜用疏松肥沃、排水良好的酸性土壤种植。可用扦插、压条、分株或播种繁殖，喜温湿，向阳，较耐寒，耐半阴，怕积水，要求疏松、肥沃和酸性的沙壤土，易发生叶子发黄黄化病。\
		主要分布在贵州、四川、江苏、浙江、安徽、江西、广东、广西、云南、福建、台湾、湖南、湖北等地，是岳阳市的市花。\
		通常说的栀子花指观赏用重瓣的变种大花栀子。革质呈长椭圆形，有光泽。花腋生，有短梗，肉质。果实卵状至长椭圆状，有5 - 9条翅状直棱，1室，，嵌生于肉质胎座。5 - 7月开花，花、叶、果皆美，花芳香四溢。它的果实可以用来作画画涂料。";*/

	//!< 当前帧内容长度
	writer.writeUInt32(v_sendMsg.length());
	//!< 当前帧内容
	writer.writeBlock((uint8_t *)v_sendMsg.c_str(), v_sendMsg.length());

	unsigned int msg_sn = 0;
	int ret = m_local_clt->sendDataToServerByType(miAdvAlgoContentAwareServer, data, writer.getLen(), 0x16, 0x0301, msg_sn);
	//dlplog_info(m_log_handle, "[%s] term_id[%d], ret[%d]", __FUNCTION__, m_term_id, ret);
	return 1;
}

void CCltBusiness::handleProtocol(
	unsigned char mod_id,
	unsigned short msg_id,
	const unsigned char * msg_body,
	unsigned int msg_body_len,
	RECV_EXTRA * recv_extra
)
{
	uint32_t ret = 0;
	// 接收数据后的回调
	if (mod_id == LOGIN_MODE_ID)
	{
		switch (msg_id)
		{
		case 0x0181:
			ret = loginRespond0181(msg_body, msg_body_len, recv_extra);
			if (0x01 == ret)
			{
				// 终端登录成功
				CLoginInfo::instance().term_login_count++;

				if (1 == CTestConfig::instance().stg_req_condition)
				{
					// 请求基础策略和终端策略
					strategyRequest0101(CTestConfig::instance().stg_req_range & 0x03);
				}
				//if (3 == CTestConfig::instance().stg_req_condition)
				//{

				//}
				if (0x03 == CTestConfig::instance().term_oper_login)
				{
					// 发送用户登录协议
					userLoginReq0301();
				}
			}
			else if (0x02 == ret)
			{
				// 需要切换采集登录
			}
			break;
		case 0x0381:
			if (1 == userLoginRsp0381(msg_body, msg_body_len, recv_extra))
            {
				// 用户登录成功
				CLoginInfo::instance().oper_login_count++;

				if (1 == CTestConfig::instance().stg_req_condition)
				{
					// 请求操作员策略
					strategyRequest0101(CTestConfig::instance().stg_req_range & 0x04);
				}
			}
			break;
		default:
			break;
		}
	}
	else if (mod_id == STG_MODE_ID)
	{
		switch (msg_id)
		{
		case 0x0181:
			strategyRespond0181(msg_body, msg_body_len, recv_extra);
			break;
		case 0x0203:
			stgUpdateNotice0203(msg_body, msg_body_len, recv_extra);
			break;
		default:
			break;
		}
	}
	else if (mod_id == LOG_MODE_ID)
	{
		switch (msg_id)
		{
		case 0x0101:
			LogUploadNotice0101(msg_body, msg_body_len, recv_extra);
			//LogUpload0201();
			break;
		case 0x0281:
			LogUpload0281(msg_body, msg_body_len, recv_extra);
			break;
		default:
			break;
		}
	}
}

char * CCltBusiness::GenerateLogJson(int log_num)
{
	int json_def_len = 1023 * 1024;
	char * json = new char[json_def_len];
	memset(json, 0x00, json_def_len);

	int json_total_len = 
		snprintf(json, json_def_len, "{\"devId\":%u,\"completed\":1,\"table\":[", m_term_id);

	// 每种日志类型需要上传的条数，不需要很精确
	int average_count = 0;
	if (0 == (log_num % LOGTABLE_COUNT))
	{
		average_count = log_num / LOGTABLE_COUNT;
	}
	else
	{
		average_count = (log_num / LOGTABLE_COUNT) +1;
	}

	for (int table_type = 0; table_type < LOGTABLE_COUNT; table_type++)
	{
		json_total_len += GenerateRecordsByType(json + json_total_len, json_def_len - json_total_len, table_type, average_count);
		if (table_type < LOGTABLE_COUNT - 1)
		{
			strncat(json, ",", json_def_len - 1);
			json_total_len++;
		}
	}

	strncat(json, "]}", json_def_len - 1);
	json_total_len += 2;

	dlplog_info(m_log_handle, "[%s] log upload json:\n%s", __FUNCTION__, json);

	return json;
}

/*
	返回值：写入json的长度
*/
int CCltBusiness::GenerateRecordsByType(char * json, int len, int table_type, int log_num)
{
	int json_total_len = 0;

	if (len < 200 )
	{
		return json_total_len;
	}

	time_t time_stamp;
	time(&time_stamp);

	char cur_data[64];
	strftime(cur_data, sizeof(cur_data), "%F", localtime(&time_stamp));

	json_total_len = snprintf(json, len, 
		"{\"tableName\":\"%s\",\"date\":\"%s\",\"data\":[{\"recordDate\":\"%s\",\"records\":[",
		m_log_table_name[table_type].c_str(), cur_data, cur_data);

	char cur_time[64];
	strftime(cur_time, sizeof(cur_time), "%F %T", localtime(&time_stamp));

	char szJsonTemp[2048] = {};
	int json_temp_len = 0;					
	for (int index = 1; index <= log_num; index++)
	{
		json_temp_len = m_generate_record_func[table_type](
			szJsonTemp, 2048, m_map_log_type_to_max_id[table_type]+index, cur_time, time_stamp);
		if (json_temp_len >= 2048)
		{
			// 有截断，不处理
			continue;
		}

		if (json_total_len + json_temp_len + 50 > len)
		{
			break;
		}

		strncat(json, szJsonTemp, len - 1);
		json_total_len += json_temp_len;

		if (index < log_num)
		{
			strncat(json, ",", len - 1);
			json_total_len++;
		}
	}

	m_map_log_type_to_max_id[table_type] += log_num;

	strncat(json, "]}]}", len - 1);
	json_total_len += 4;

	return json_total_len;
}

int CCltBusiness::GeneratePrintMonitorRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"OpTimeStr\":\"%s\",\"file_size\":10254,\
			\"file_name\":\"hello.txt\",\"printed_pages\":3,\"printed_copies\":3,\
			\"printer_name\":\"hello.txt\",\"local_file_path\":\"C:/a/b\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateChatTextLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"MsgTime\":\"%s\",\"ChatSessionInfo\":\"㵘燚张三李四\",\
			\"MsgText\":\"~!@#$%^&*()_+`-=[]\{}|;':"",./\\\\<>? %s\",\"ChatType\":\"3\",\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, m_term_nickname.c_str(), time_stamp);
}

int CCltBusiness::GenerateCdBurnLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"TermID\":%u,\"OperatorId\":%u,\
			\"BurnTime\":\"%s\",\"BurnID\":\"654321\",\
			\"DeviceInfo\":\"DeviceInfo654321\",\"BurnCDInfo\":\"BurnCDInfo654321\",\
			\"BurnSize\":1234,\"IsSensitive\":0,\
			\"LocalIP\":\"127.0.0.1\",\"HostName\":\"壭壱売壳壴壵壶壷壸壶壻壸壾壿夀夁\",\
			\"ftp_id\":1,\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateBurnFileLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"BurnID\":\"654321\",\"BurnResult\":0,\
			\"FilePath\":\"C:/寚寜寝寠寡寣寥寪寭寮寯寰寱寲寳寴寷\",\"FileMD5\":\"123456789\",\
			\"FileSize\":1234,\"LocalFilePath\":\"C:/a\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, time_stamp);
}

int CCltBusiness::GenerateFileOpsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"OpTimeStr\":\"%s\",\"ProcessName\":\"a.exe\",\
			\"FileOpType\":1,\"FileName\":\"abc.txt\",\
			\"DiskType1\":1,\"FileOpPath1\":\"C:/abc\",\
			\"FileMd5\":\"123456789\",\"DiskName2\":\"彁彂彃彄彅彇彉彋弥彍彏\",\
			\"LocalFilePath\":\"C:/12345\",\"FtpId\":1,\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateOpAlarmMsgRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"AlarmType\":1,\"AlarmTime\":\"%s\",\
			\"AlarmMsg\":\"the alarm message\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateUrlsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"TimeStr\":\"%s\",\"URL\":\"www.helloworld.com\",\
			\"URLEx1\":\"www.爢爣爤爥爦爧爨爩.com\",\"Host\":\"127.0.0.1\",\
			\"WebTitle\":\"hello world\",\"UrlGUID\":\"123456789\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateLogsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"LogTime\":\"%s\",\"LogType\":1,\
			\"LogDesc\":\"hello log\",\"ObjName\":\"the obj name\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateSystemLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"SysLogDate\":\"%s\",\"sysLogSource\":\"C:/\",\
			\"EventID\":1,\"RecordID\":1,\
			\"logType\":1,\"LogLevel\":1,\"LogDescription\":\"log description\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateLoginLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"SysLogDate\":\"%s\",\"sysLogSource\":\"C:/\",\
			\"EventID\":1,\"RecordID\":1,\"LogDescription\":\"log description\",\
			\"LoginUser\":\"login user\",\"LoginDomain\":\"domain\",\"LoginType\":1,\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateDisclosureLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"OccurDate\":\"%s\",\"LossType\":1,\
			\"Severity\":1,\"Strategy\":1,\"Content\":\"hello world\",\
			\"Rules\":1,\"Responds\":1,\"FileName\":\"a.txt\",\
			\"Sender\":\"sender name\",\"Receiver\":\"receiver name\",\
			\"SenderIp\":\"192.168.10.10\",\"ReceiverIp\":\"192.168.10.20\",\
			\"HandlerName\":\"handler name\",\"Remark\":\"remake\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateCurTaskInfoRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"StatusChangeTime\":\"%s\",\"GUID\":\"123456789\",\
			\"TaskStatus\":1,\"FileName\":\"a.txt\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateforumdataRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"TimeStr\":\"%s\",\"Author\":\"the author\",\
			\"Title\":\"hello world\",\"Url\":123,\
			\"BackupFileName\":\"a.txt\",\"ServerFileName\":\"a.txt\",\
			\"LocalFilePath\":\"c:/\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateMailsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"TimeStr\":\"%s\",\"CmdType\":1,\
			\"MailTitle\":\"hello world\",\
			\"Sender\":\"sender name\",\"Recver\":\"receiver name\",\
			\"AttachCount\":3,\"FileSize\":123,\
			\"FileName\":\"a.txt\",\"ReadFlag\":1,\"LocalFilePath\":\"C:/\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateScreenShotLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"BeginTime\":\"%s\",\"GUID\":\"123456789\",\
			\"TriggerType\":1,\"AuditType\":1,\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateChatImagetLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"MsgTime\":\"%s\",\"LocalAccountInfo\":\"local account info\",\
			\"ChatSessionInfo\":\"chat session info\",\"SenderInfo\":\"sender info\",\
			\"ChatType\":1,\"LocalFilePath\":\"c:/\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateChatFiletLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerId\":%u,\"OperatorId\":%u,\
			\"MsgTime\":\"%s\",\"ChatSessionInfo\":\"chat session info\",\
			\"ChatType\":1,\"NeedBackup\":0,\
			\"FileSize\":123,\"FileName\":\"a.txt\",\"LocalFilePath\":\"c:/\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateUserInfoChangeRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"ChangeDate\":\"%s\",\"ChangeType\":1,\
			\"UserName\":\"the user name\",\"FullName\":\"full name\",\
			\"OldUserName\":\"old user name\",\"OldFullName\":\"old full name\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateGroupInfoChangeRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"ChangeDate\":\"%s\",\"ChangeType\":1,\
			\"GroupName\":\"group name\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateUsbEventLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"EventTime\":\"%s\",\"EventType\":1,\
			\"DeviceDesc\":\"device desc\",\"DeviceType\":1,\
			\"DevTypeID\":1,\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateBluetoothConnectChangeRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"ChangeDate\":\"%s\",\"Address\":\"the address\",\
			\"DevName\":\"device name\",\"DevType\":1,\
			\"ChangeType\":1,\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}

int CCltBusiness::GenerateBlueToothFileLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp)
{
	return snprintf(record, record_size,
		"{\"ID\":%d,\"ComputerID\":%u,\"OperatorId\":%u,\
			\"OpTime\":\"%s\",\"OpType\":1,\
			\"FilePath\":\"c:/\",\"FileName\":\"a.txt\",\
			\"fileSize\":123,\"localFilePath\":\"c:/\",\
			\"FirstSendTime\":%lld}",
		record_index, m_term_id, m_user_id, cur_time, time_stamp);
}


CLocalCltCallback::CLocalCltCallback()
{
	m_clt_bussiness = nullptr;
	m_handle_protocol = nullptr;
}
CLocalCltCallback::~CLocalCltCallback()
{
	m_clt_bussiness = nullptr;
	m_handle_protocol = nullptr;
}

void CLocalCltCallback::handleProtocol(
	unsigned char mod_id, /* 模块id */ 
	unsigned short msg_id, /* 消息id */ 
	const unsigned char* msg_body, /* 消息体数据 */ 
	unsigned int msg_body_len, /* 消息体长度 */ 
	RECV_EXTRA * recv_extra)
{
	if (nullptr == m_clt_bussiness || 
		nullptr == m_handle_protocol)
	{
		return;
	}
	(m_clt_bussiness->*m_handle_protocol)(mod_id, msg_id, msg_body, msg_body_len, recv_extra);
}

void CLocalCltCallback::setLocalCltInstance(CCltBusiness * instance)
{
	m_clt_bussiness = instance;
}

void CLocalCltCallback::setHandleProtocolFunc(FN_handleProtocol_C func)
{
	m_handle_protocol = func;
}
