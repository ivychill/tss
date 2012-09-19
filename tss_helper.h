#include <mongo/client/dbclient.h>
#include "tss.pb.h"
#include "tss_log.h"
#define DEVICE_TOKEN_SIZE 32
#define DB_COLLECTION "roadclouding_production.devices"
using namespace tss;
using namespace mongo;

void HexDump (char* out, const char* in, int size);
void ByteDump ( char* out, const char* in, int size);
void InitDB (DBClientConnection& dbcc);
void RegisterDevice (DBClientConnection& dbcc, LYDeviceReport& devrep);
void UnregisterDevice (DBClientConnection& dbcc, const std::string& device_token);
