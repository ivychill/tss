
#include "traffic_apns.h"


Logger logger;
DBClientConnection db_client;
uint32_t whicheverOrderIWantToGetBackInAErrorResponse_ID = 0;

int main (int argc, char *argv[])
{
    signal(SIGPIPE,SIG_IGN);
    InitLog (argv[0], logger);
    InitDB (db_client);
    Apns apns;

    while (true)
    {
        time_t now = time (NULL);
        struct tm *timeinfo;
        timeinfo = ::localtime (&now);
/*
        if ((timeinfo->tm_wday >= 1 && timeinfo->tm_wday <= 5)
            && (timeinfo->tm_hour == 6 || timeinfo->tm_hour == 17)
            && (timeinfo->tm_min >= 15)) 
*/
        {
            LOG4CPLUS_DEBUG (logger, "begin to push message, " << timeinfo->tm_year + 1900 << "-" << timeinfo->tm_mon + 1 << "-" << timeinfo->tm_mday << " " << timeinfo->tm_hour << ":" << timeinfo->tm_min << " week day " << timeinfo->tm_wday);
            //char pld[] = "{\"aps\":{\"alert\":\"提醒您关注上下班拥堵路段\"}}";

            Json::Value jv_payload;
            //Json::Value jv_alert, jv_badge, jv_sound;
            jv_payload["aps"]["alert"] = "提醒您关注上下班拥堵路段";
            jv_payload["aps"]["badge"] = 1;
            jv_payload["aps"]["sound"] = "chime";
            Json::FastWriter writer;  
            std::string str_payload = writer.write(jv_payload);  
            LOG4CPLUS_DEBUG (logger, "payload: \n" << str_payload);
            LOG4CPLUS_DEBUG (logger, "payload: \n" << jv_payload.toStyledString());  

            apns.InitPush ();
            LOG4CPLUS_DEBUG (logger, "device count: " << db_client.count("roadclouding_production.devices"));
            auto_ptr<DBClientCursor> cursor = db_client.query("roadclouding_production.devices", BSONObj());
            while (cursor->more())
            {
                mongo::BSONObj obj = cursor->next();
                //LOG4CPLUS_DEBUG (logger, obj.toString());
                //LOG4CPLUS_DEBUG (logger, obj.jsonString());
                std::string str_hex_token =  obj.getStringField("dev_token");
                LOG4CPLUS_DEBUG (logger, "str_hex_token: " << str_hex_token);
                char byte_token [DEVICE_TOKEN_SIZE];
                ByteDump (byte_token, str_hex_token.c_str(), DEVICE_TOKEN_SIZE);
                apns.sendPayload (byte_token, str_payload.c_str (), str_payload.length ());
            }
            apns.ReleasePush ();
        }
        sleep (300);
    }

    return 0;
}

Apns::Apns ()
{
#ifdef _SANDBOX
    push.url = DVL_PUSH_URL;
    feedback.url = DVL_FEEDBACK_URL;
    cer_path = DVL_CER_PATH;
    key_path = DVL_KEY_PATH;
    passphrase = DVL_PASSPHRASE;    
    LOG4CPLUS_DEBUG (logger, "development env");
#else
    push.url = PDT_PUSH_URL;
    feedback.url = PDT_FEEDBACK_URL;
    cer_path = PDT_CER_PATH;
    key_path = PDT_KEY_PATH;
    passphrase = PDT_PASSPHRASE;    
    LOG4CPLUS_DEBUG (logger, "production env");
#endif
    Init (feedback);
}

void Apns::Init (UrlAndSSL& uas)
{
    BIO* bio;

    SSL_library_init();
    SSL_load_error_strings();
    //SSL_METHOD* meth = SSLv3_method();
    uas.ctx = SSL_CTX_new(SSLv3_method());
    
    SSL_CTX_use_certificate_file(uas.ctx, cer_path.c_str(), SSL_FILETYPE_PEM);
    SSL_CTX_set_default_passwd_cb_userdata(uas.ctx, (void *)passphrase.c_str());
    SSL_CTX_use_PrivateKey_file(uas.ctx, key_path.c_str(), SSL_FILETYPE_PEM);

    bio = BIO_new_ssl_connect(uas.ctx);
    BIO_get_ssl(bio, &uas.ssl);
    SSL_set_mode(uas.ssl, SSL_MODE_AUTO_RETRY);

    /* Attempt to connect */
    BIO_set_conn_hostname(bio, uas.url.c_str());
    /* Verify the connection opened and perform the handshake */
    if(BIO_do_connect(bio) <= 0)
    {
        LOG4CPLUS_ERROR (logger, "fail to connect, ctx: " << uas.ctx << ", bio: " << bio << ", ssl: " << uas.ssl);
    }

    //X509 * server_cert = SSL_get_peer_certificate (ssl);
}

void Apns::Release (UrlAndSSL& uas)
{
    SSL_free(uas.ssl);
    SSL_CTX_free(uas.ctx);
}

bool Apns::sendPayload (char *deviceTokenBinary, const char *payloadBuff, size_t payloadLength)
{
  bool rtn = false;
  if (push.ssl && deviceTokenBinary && payloadBuff && payloadLength)
  {
      uint8_t command = 1; /* command number */
      char binaryMessageBuff[sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) +
      DEVICE_TOKEN_SIZE + sizeof(uint16_t) + MAXPAYLOAD_SIZE];
      /* message format is, |COMMAND|ID|EXPIRY|TOKENLEN|TOKEN|PAYLOADLEN|PAYLOAD| */
      char *binaryMessagePt = binaryMessageBuff;
      whicheverOrderIWantToGetBackInAErrorResponse_ID++;
      uint32_t networkOrderExpiryEpochUTC = htonl(time(NULL)+86400); // expire message if not delivered in 1 day
      uint16_t networkOrderTokenLength = htons(DEVICE_TOKEN_SIZE);
      uint16_t networkOrderPayloadLength = htons(payloadLength);
 
      /* command */
      *binaryMessagePt++ = command;
 
     /* provider preference ordered ID */
     memcpy(binaryMessagePt, &whicheverOrderIWantToGetBackInAErrorResponse_ID, sizeof(uint32_t));
     binaryMessagePt += sizeof(uint32_t);
 
     /* expiry date network order */
     memcpy(binaryMessagePt, &networkOrderExpiryEpochUTC, sizeof(uint32_t));
     binaryMessagePt += sizeof(uint32_t);
 
     /* token length network order */
      memcpy(binaryMessagePt, &networkOrderTokenLength, sizeof(uint16_t));
      binaryMessagePt += sizeof(uint16_t);
 
      /* device token */
      memcpy(binaryMessagePt, deviceTokenBinary, DEVICE_TOKEN_SIZE);
      binaryMessagePt += DEVICE_TOKEN_SIZE;
 
      /* payload length network order */
      memcpy(binaryMessagePt, &networkOrderPayloadLength, sizeof(uint16_t));
      binaryMessagePt += sizeof(uint16_t);
 
      /* payload */
      memcpy(binaryMessagePt, payloadBuff, payloadLength);
      binaryMessagePt += payloadLength;

      //LOG4CPLUS_DEBUG (logger, "before SSL_write");
      int wbytes;
      if ((wbytes = SSL_write(push.ssl, binaryMessageBuff, (binaryMessagePt - binaryMessageBuff))) > 0)
      {
          LOG4CPLUS_DEBUG (logger, "write push bytes: " << wbytes);
          rtn = true;

          int rbytes;
          char byte_buff [ERROR_RESPONSE_SIZE];
          char hex_buff [ERROR_RESPONSE_SIZE * 2];
          if ((rbytes = SSL_read (push.ssl, byte_buff, ERROR_RESPONSE_SIZE)) > 0)
          {
              LOG4CPLUS_DEBUG (logger, "read error response bytes: " << rbytes);
              HexDump (hex_buff, byte_buff, ERROR_RESPONSE_SIZE);
              std::string str_response (hex_buff, ERROR_RESPONSE_SIZE * 2);
              LOG4CPLUS_DEBUG (logger, "dump error response: " << hex_buff);
              rtn = false;
          }
      }
      else
      {
          LOG4CPLUS_DEBUG (logger, "fail to write push");
          rtn = false;
      }
      //LOG4CPLUS_DEBUG (logger, "after SSL_write");
  }

  checkFeedback ();
  return rtn;
}

bool Apns::checkFeedback ()
{
    bool rtn = false;
    char byte_buff [FEEDBACK_SIZE];
    char hex_buff [FEEDBACK_SIZE * 2];
    int rbytes;

    InitFeedback ();
    //LOG4CPLUS_DEBUG (logger, "before SSL_read");
    while ((rbytes = SSL_read (feedback.ssl, byte_buff, FEEDBACK_SIZE)) > 0) //to be improved
    {
        LOG4CPLUS_DEBUG (logger, "read feedback bytes: " << rbytes);
        HexDump (hex_buff, byte_buff, FEEDBACK_SIZE);
        std::string str_feedback (hex_buff, FEEDBACK_SIZE * 2);
        LOG4CPLUS_DEBUG (logger, "dump feedback: " << hex_buff);
        std::string device_token ( hex_buff + FEEDBACK_HEAD_SIZE * 2, (FEEDBACK_SIZE - FEEDBACK_HEAD_SIZE ) * 2);
        UnregisterDevice (db_client, device_token);
        rtn = true;
    }
    //LOG4CPLUS_DEBUG (logger, "after SSL_read");
    ReleaseFeedback ();
    return rtn;
}
