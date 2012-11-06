#include "tss_helper.h"

void HexDump (char* out, const char* in, int size)
{
    for (int index = 0; index < size; index++)
    {
        sprintf (&out[index * 2], "%02x", (unsigned char)in[index]);
    }
}

void ByteDump ( char* out, const char* in, int size)
{
    for (int index = 0; index < size; index++)
    {
        const char* cinter = &in[index * 2];
        int iinter;
        sscanf (cinter, "%02x", &iinter);
        out[index] = (char)iinter;
    }
}

void InitDB (DBClientConnection& dbcc)
{
    try
    {
        dbcc.connect("localhost");
        dbcc.ensureIndex(DB_COLLECTION, BSON ("dev_token" << 1), true);
        //LOG4CPLUS_DEBUG (logger, "succeed to connect to mongod");
    }
    catch( DBException &e )
    {
        //LOG4CPLUS_ERROR (logger, "fail to connect to mongod, caught " << e.what());
    }
}

void RegisterDevice (DBClientConnection& dbcc, LYDeviceReport& devrep)
{
    std::string device_id = devrep.device_id ();
    std::string device_token = devrep.device_token ();
    char hex_token [DEVICE_TOKEN_SIZE * 2];
    HexDump (hex_token, device_token.c_str(), DEVICE_TOKEN_SIZE);
    std::string device_name = devrep.device_name ();
    std::string device_model = devrep.device_model ();
    std::string device_os_version = devrep.device_os_version ();
    std::string str_hex_token (hex_token, DEVICE_TOKEN_SIZE * 2);
    time_t now = time (NULL);

    BSONObj bson_obj = BSON("dev_id" << device_id
        << "dev_token" << str_hex_token
        << "dev_name" << device_name
        << "dev_model" << device_model
        << "dev_os_ver" << device_os_version
        //<< "status" << "active"
        << "created" << ::ctime(&now));

    dbcc.insert(DB_COLLECTION, bson_obj);
}

void UnregisterDevice (DBClientConnection& dbcc, const std::string& device_token)
{
    //LOG4CPLUS_DEBUG (logger, "unregister device: " << device_token);
    dbcc.remove( DB_COLLECTION,
       BSON ("dev_token" << device_token));
    //LOG4CPLUS_DEBUG (logger, "update DB error: " << dbcc.getLastError());
}
