#pragma once

#include <map>
#include <list>
#include <vector>
#include <thread>

#include "../public/trclt_comm_defs.h"
#include "../public/TrCltCommLib.h"

#include "DlpULog/DlpULog.h"
#pragma comment(lib, "DlpULog.lib")

#include "rapidjson/document.h"

#include "ServerInfo.h"

#include "TestConfig.h"
#include "ThreadPool.hpp"

// 终端登录时，响应服务器过载之后，接收到的可用采集信息
struct DSvrInfo
{
	uint32_t dsvr_id;			// 采集服务器设备id
	char intranet_ip[256];		// 内网ip
	uint32_t intranet_port;
	char  internet_ip[256];		// 外网ip
	uint32_t internet_port;
	uint32_t priority;			// 优先级 0其它可用采集 1上一次登录采集 2负载均衡策略推荐采集 3管理员指定采集
	bool tried;					// 是否已经尝试连接过
};

enum _tagIngeractiveType
{
	INTERACTIVE_OTHER = 0,
	INTERACTIVE_TERM_LOGIN,
	INTERACTIVE_USER_LOGIN,
	INTERACTIVE_STRATEGY,
	INTERACTIVE_LOG_UPLOAD,
	INTERACTIVE_COUNT
};

enum _tagLogTables
{
	LOGTABLE_PrintMonitor = 0,
	LOGTABLE_ChatTextLog,
	LOGTABLE_CdBurnLog,
	LOGTABLE_BurnFileLog,
	LOGTABLE_FileOps,
	LOGTABLE_OpAlarmMsg,
	LOGTABLE_Urls,
	LOGTABLE_Logs,
	LOGTABLE_SystemLog,
	LOGTABLE_LoginLog,
	LOGTABLE_DisclosureLog,
	LOGTABLE_CurTaskInfo,
	LOGTABLE_forumdata,
	LOGTABLE_Mails,
	LOGTABLE_ScreenShotLog,
	LOGTABLE_ChatImagetLog,
	LOGTABLE_ChatFiletLog,
	LOGTABLE_UserInfoChange,
	LOGTABLE_GroupInfoChange,
	LOGTABLE_UsbEventLog,
	LOGTABLE_BluetoothConnectChange,
	LOGTABLE_BlueToothFileLog,
	LOGTABLE_COUNT
};

// 服务器状态
#ifndef SERVER_STATUS
#define SERVER_STATUS
enum _tagSERVER_STATUS
{
	enSVR_LOGIN_FAILED = 0x00,	// 未登录/登录失败
	enSVR_LOGIN_SUCCESS = 0x01,	// 登录成功
};
#endif

typedef struct _tagNetWorkInfo {
	std::string mac;
	int ip_count;
	std::vector<std::string> vec_ip;
}NETWORKINFO;

// 与服务器的交互信息
typedef struct _tagInteractiveInfo
{
	_tagInteractiveInfo()
	{
		sender = false;
		success = false;
		protocol_num = 0;
		msg_sn = 0;
		data_size = 0;
		business_count = 0;
		pkg_total = 0;
		term_id = 0;
		memset(term_guid, 0, sizeof(term_guid));
	}
	bool sender;													// true终端发送，false终端接收
	bool success;													// 是否成功
	uint32_t protocol_num;											// 协议号
	uint32_t msg_sn;												// 流水号
	uint32_t business_count;										// 业务数据条数
	uint64_t data_size;												// 处理的数据大小
	std::chrono::steady_clock::time_point   deal_time;				// 处理的时间
	uint8_t	 pkg_total;												// 总帧数
	unsigned int term_id;											// 模拟终端id
	char term_guid[50 + 1];											// 终端guid
}INTERACTIVE_INFO;

typedef std::function<void(const INTERACTIVE_INFO & interactive)> CBFN_addInteractiveInfo;
typedef std::function<void(uint32_t term_index, uint8_t buss_type, bool status)> CBFN_statusReport;

typedef std::function<int(
	char * record,
	int record_size,
	int record_index,
	char * cur_time,
	time_t & time_stamp
	)> FN_GenerateRecord;

class CCltBusiness;

typedef void (CCltBusiness:: * FN_handleProtocol_C)(
	unsigned char mod_id,			// 模块id
	unsigned short msg_id,			// 消息id
	const unsigned char* msg_body,	// 消息体数据
	unsigned int msg_body_len,		// 消息体长度
	RECV_EXTRA * recv_extra
	);

class CLocalCltCallback : public ILocalCltCallback
{
public:
	CLocalCltCallback();
	~CLocalCltCallback();
public:
	virtual void handleProtocol(
		unsigned char mod_id,			// 模块id
		unsigned short msg_id,			// 消息id
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		// 消息体长度
		RECV_EXTRA * recv_extra
	)override;
public:
	void setLocalCltInstance(CCltBusiness * instance);
	void setHandleProtocolFunc(FN_handleProtocol_C func);
private:
	FN_handleProtocol_C m_handle_protocol;
	CCltBusiness * m_clt_bussiness;
};

class CCltBusiness
{
public:
	CCltBusiness();
	~CCltBusiness();

public:
	int init(int term_index, CBFN_addInteractiveInfo addInteractiveInfo, CBFN_statusReport statusReport);
	void connectService();
	int beginsend2detector(std::string v_sendMsg);
	void start();
	void release();
	void setDataServerReconnect();
	int getDataServerConnState();
	bool isLoginSuccessed();
	bool checkLoginState();
	bool requestStrategy();
	int getTermId();
	int getDevSvrId();
private:				   
	void initMap();
	void initLog();
	void initTermInfo();
	void HexStrToByte(const char * source, int sourceLen, unsigned char * dest);

	int loginRequest0101();
	bool parseDevSvrList(std::string & str_json);
	int loginRespond0181(
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		// 消息体长度
		RECV_EXTRA * recv_extra
	);

	int userLoginReq0301();
	int userLoginRsp0381(
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		// 消息体长度
		RECV_EXTRA * recv_extra
	);

	int strategyRequest0101(uint16_t req_stg_type);
	int strategyRespond0181(
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		//消息体长度
		RECV_EXTRA * recv_extra
	);
	int strategySuccessNotice0102(uint32_t req_msg_sn);

	int stgUpdateNotice0203(
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		//消息体长度
		RECV_EXTRA * recv_extra
	);

	int LogUploadNotice0101(
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		//消息体长度
		RECV_EXTRA * recv_extra
	);

	int LogUpload0201(std::map<uint8_t, uint16_t> &json_data);
	int LogUpload0281(
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		//消息体长度
		RECV_EXTRA * recv_extra
	);

	int send2detector(std::string v_sendMsg);

	void handleProtocol(
		unsigned char mod_id,			// 模块id
		unsigned short msg_id,			// 消息id
		const unsigned char* msg_body,	// 消息体数据
		unsigned int msg_body_len,		// 消息体长度
		RECV_EXTRA * recv_extra
	);

private:
	char * GenerateLogJson(int log_num = 0);
	int GenerateRecordsByType(char * json, int len, int table_type, int log_num = 0);

	int GeneratePrintMonitorRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateChatTextLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateCdBurnLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateBurnFileLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateFileOpsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateOpAlarmMsgRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateUrlsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateLogsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateSystemLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateLoginLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateDisclosureLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateCurTaskInfoRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateforumdataRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateMailsRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateScreenShotLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateChatImagetLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateChatFiletLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateUserInfoChangeRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateGroupInfoChangeRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateUsbEventLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateBluetoothConnectChangeRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
	int GenerateBlueToothFileLogRecord(char * record, int record_size, int record_index, char * cur_time, const time_t & time_stamp);
private:

	static uint8_t LOGIN_MODE_ID;		// 登录模块
	static uint8_t STG_MODE_ID;			// 策略模块	
	static uint8_t LOG_MODE_ID;			// 日志模块

	CTrCltCommLib m_trcltcomm_lib;

	LOG_HANDLE m_log_handle;
	LOG_HANDLE m_log_handle_login;
	LOG_HANDLE m_log_handle_stg;

	std::map<uint32_t, CServerInfo *> m_server_info_map;		// 服务器类型 连接的服务器信息

	int m_term_index;			// 模拟终端序号
	ILocalClt * m_local_clt;	// 模拟终端通信对象
	unsigned int m_term_id;		// 模拟终端id

	int m_longin_state;			// 终端登录状态
	int m_uer_login_state;		// 用户登录状态
	bool m_had_send_login;		// 是否发送登录协议

	std::string		m_term_nickname;		// 终端昵称，从终端登录响应获取到
	uint8_t		m_login_mode;				// 用户登录模式，从终端登录响应获取到，用于用户登录
	uint32_t	m_user_id;					// 用户Id，从终端登录响应获取到，用于用户登录

	std::string m_log_table_name[LOGTABLE_COUNT];				// 日志表名
	FN_GenerateRecord m_generate_record_func[LOGTABLE_COUNT];	// 生成记录的函数
	std::map<uint32_t, uint32_t> m_map_log_type_to_max_id;		// 日志类型的最大id

	static int CLT_INDEX_LEN;		// 压测客户端编号的长度
	static int TERM_INDEX_LEN;		// 模拟终端序号的长度
	char m_term_guid[50 + 1];		// 终端guid
	char m_term_mcode[50 + 1];		// 机器特征码
	char m_term_dpt_name[100 + 1];	// 部门
	char m_term_user_name[100 + 1];	// 用户名
	char m_term_cpt_name[50 + 1];	// 计算机名
	char m_term_ver[20 + 1];		// 终端版本号
	std::vector<NETWORKINFO *> m_vec_network;	// 模拟终端网络信息

	CBFN_addInteractiveInfo m_addInteractiveInfo;
	CBFN_statusReport m_statusReport;

	CLocalCltCallback m_local_clt_callback;

	std::map<uint32_t, std::shared_ptr<DSvrInfo>> m_map_dsvr_info;

	CThreadPool *threadPool_;
};