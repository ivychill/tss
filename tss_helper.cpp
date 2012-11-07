#include "tss_helper.h"

extern Logger logger;

void HexDump (char* out, const char* in, int size)
{
    for (int index = 0; index < size; index++)
    {
        sprintf (&out[index * 2], "%02x", (unsigned char)in[index]);
    }
}

void ByteDump (char* out, const char* in, int size)
{
    for (int index = 0; index < size; index++)
    {
        const char* cinter = &in[index * 2];
        int iinter;
        sscanf (cinter, "%02x", &iinter);
        out[index] = (char)iinter;
    }
}


char* GetCfgFile (char *file_name)
{
    char* CfgFile = new char[64];
    char *tss_home = getenv("TSS_HOME");
    if (tss_home != NULL)
    {
        sprintf (CfgFile, "%s/etc/%s", tss_home, file_name);
    }
    else
    {
        sprintf (CfgFile, "etc/%s", file_name);
    }

    if (access(CfgFile, R_OK) == 0)
    {
        return CfgFile;
    }
    return NULL;
}

int JsonStringToJsonValue (const std::string& str_input, Json::Value& jv_output)
{
    //LOG4CPLUS_DEBUG (logger, "string input: " << str_input);
    Json::Reader reader;
    bool parsingSuccessful = reader.parse( str_input, jv_output );
    if ( !parsingSuccessful )
    {
        // report to the user the failure and their locations in the document.
        LOG4CPLUS_ERROR (logger, "Failed to parse road traffic input\n" \
               << reader.getFormatedErrorMessages());
        return -1;
    }
    LOG4CPLUS_DEBUG (logger, "road traffic json output:\n" << jv_output.toStyledString());
    return 0;
}

// Input: time string expressed in local time. The time format from probe is like: 2012-06-14T09:39:49+08:00.
// Output/return: time_t baed on GMT, i.e. UTC.
// Notice: Since mktime interprets the contents of the tm structure pointed by timeptr as a calendar time expressed in local time, timezone information is ignored
int TimeStrToInt(const string& str_time)
{
    int nYear, nMonth, nDay, nHour, nMinute, nSecond, nDifHour, nDifMinute;
    time_t rawtime;
    struct tm *timeinfo;

    sscanf(str_time.c_str(), "%d-%d-%dT%d:%d:%d%d:%d", &nYear, &nMonth, &nDay, &nHour, &nMinute, &nSecond, &nDifHour, &nDifMinute);
    time ( &rawtime );
    timeinfo = ::localtime ( &rawtime );
    timeinfo->tm_year = nYear - 1900;
    timeinfo->tm_mon = nMonth - 1;
    timeinfo->tm_mday = nDay;
    timeinfo->tm_hour = nHour;
    timeinfo->tm_min = nMinute;
    timeinfo->tm_sec = nSecond;

    //LOG4CPLUS_DEBUG (logger, "time string: " << str_time);
    //LOG4CPLUS_DEBUG (logger, "hour: " << timeinfo->tm_hour << ", minute: " << timeinfo->tm_min);

    return mktime (timeinfo);
}

LYDirection DirectionStrToInt(const string& str_direction)
{
    size_t ifound;
    LYDirection idirection = LY_UNKNOWN;
    if ((ifound = str_direction.find ("东")) != std::string::npos)
    {
        idirection = LY_EAST;
    }
    else if ((ifound = str_direction.find ("北")) != std::string::npos)
    {
        idirection = LY_NORTH;
    }
    else if ((ifound = str_direction.find ("西")) != std::string::npos)
    {
        idirection = LY_WEST;
    }
    else if ((ifound = str_direction.find ("南")) != std::string::npos)
    {
        idirection = LY_SOUTH;
    }
    else
    {
        idirection = LY_UNKNOWN;
    }

    LOG4CPLUS_DEBUG (logger, "direction string: " << str_direction << ", direction: " << idirection);
    return idirection;
}

LYOsType OsStrToInt(const string& str_os)
{
    if (str_os == "android")
    {
        return LY_ANDROID;
    }
    else if (str_os == "ios")
    {
        return LY_IOS;
    }
    else
    {
    	return LY_WP;
    }
}

void InvertMsg (LYMsgOnAir& input, LYMsgOnAir& output)
{
	output.set_version (input.version());
	output.set_msg_id (input.msg_id());
	output.set_from_party(input.to_party());
	output.set_to_party(input.from_party());
	if (input.has_rcv_id())
	{
		output.set_snd_id(input.rcv_id());
	}
	if (input.has_snd_id())
	{
		output.set_rcv_id(input.snd_id());
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
