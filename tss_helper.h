#include <mongo/client/dbclient.h>
#include "tss.pb.h"
#include "tss_log.h"
#include "../jsoncpp-src-0.5.0/include/json/json.h"
#define DEVICE_TOKEN_SIZE 32
#define DB_COLLECTION "roadclouding_production.devices"
using namespace tss;
using namespace mongo;

void HexDump (char* out, const char* in, int size);
void ByteDump (char* out, const char* in, int size);
char* GetCfgFile (char *file_name);
int JsonStringToJsonValue (const string& str_input, Json::Value& jv_roadset);
int TimeStrToInt(const string& str_time);
LYDirection DirectionStrToInt(const string& str_direction);
LYOsType OsStrToInt(const string& str_direction);
void InitDB (DBClientConnection& dbcc);
void RegisterDevice (DBClientConnection& dbcc, LYDeviceReport& devrep);
void UnregisterDevice (DBClientConnection& dbcc, const std::string& device_token);
void InvertMsg (LYMsgOnAir& input, LYMsgOnAir& output);
