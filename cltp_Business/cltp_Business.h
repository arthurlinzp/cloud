// 下列 ifdef 块是创建使从 DLL 导出更简单的
// 宏的标准方法。此 DLL 中的所有文件都是用命令行上定义的 CLTP_BUSINESS_EXPORTS
// 符号编译的。在使用此 DLL 的
// 任何其他项目上不应定义此符号。这样，源文件中包含此文件的任何其他项目都会将
// CLTP_BUSINESS_API 函数视为是从 DLL 导入的，而此 DLL 则将用此宏定义的
// 符号视为是被导出的。
#ifdef CLTP_BUSINESS_EXPORTS
#define CLTP_BUSINESS_API __declspec(dllexport)
#else
#define CLTP_BUSINESS_API __declspec(dllimport)
#endif

#include "Cltp_Business_Impl.h"

// 此类是从 cltp_Business.dll 导出的
CLTP_BUSINESS_API int WINAPI cltp_init(const char * ip, int port);
CLTP_BUSINESS_API void WINAPI cltp_release();
CLTP_BUSINESS_API int WINAPI cltp_initMulti(STRESS_TEST_INFO & test_info, CBFN_reflashStatistics cbfn);
CLTP_BUSINESS_API int WINAPI cltp_applyMulti(STRESS_TEST_INFO & test_info);
CLTP_BUSINESS_API int WINAPI cltp_sendMulti(SEND_TEST_INFO &send_info);
