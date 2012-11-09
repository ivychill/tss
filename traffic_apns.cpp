using namespace std;

#include "traffic_apns.h"
#include <boost/bind.hpp>
#include <boost/thread.hpp>

Logger logger;
DBClientConnection db_client;
uint32_t whicheverOrderIWantToGetBackInAErrorResponse_ID = 0;

class FeedbackChecker{
private:
    bool is_running;
    Apns& apns;
    boost::thread holder;
    void monitor(){
        boost::thread feedback(boost::bind(&Apns::checkFeedback, &apns));
        feedback.join();
        is_running = false;
    }

public:
    FeedbackChecker(Apns& apns):is_running(false),apns(apns){}

    void start(){
        if(is_running)
        {
            return;
        }
        else
        {
            is_running = true;
            holder = boost::thread(boost::bind(&FeedbackChecker::monitor, this));
        }
    }
};

int main (int argc, char *argv[])
{
    signal(SIGTTOU,SIG_IGN);  
    signal(SIGTTIN,SIG_IGN);  
    signal(SIGTSTP,SIG_IGN);  
    signal(SIGHUP ,SIG_IGN); 
    signal(SIGPIPE,SIG_IGN);
    signal(SIGCHLD,SIG_IGN);

    InitLog (argv[0], logger);
    InitDB (db_client);
    Apns apns;

    zmq::context_t ctxt(1);
	zmq::socket_t  apns_skt(ctxt, ZMQ_PAIR);
	apns_skt.bind("ipc://apns.ipc");


    //  Initialize poll set
    zmq::pollitem_t items [] = {
        //  Always poll for worker activity on skt_feed
        { apns_skt,  0, ZMQ_POLLIN, 0 }
    };

    apns.InitPush ();
    apns.InitFeedback();
    FeedbackChecker checker(apns);

	while (true)
	{
        time_t now = time (NULL);
        struct tm *timeinfo;
        timeinfo = ::localtime (&now);

		zmq::poll (&items [0], 1, -1);
//		int poll_in = items [0].revents & ZMQ_POLLIN;

		if (items [0].revents & ZMQ_POLLIN)
		{
			LOG4CPLUS_DEBUG (logger, "begin to push message, " << timeinfo->tm_year + 1900 << "-" << timeinfo->tm_mon + 1 << "-" << timeinfo->tm_mday << " " << timeinfo->tm_hour << ":" << timeinfo->tm_min << " week day " << timeinfo->tm_wday);
			//char pld[] = "{\"aps\":{\"alert\":\"提醒您关注上下班拥堵路段\"}}";

			std::string dev_token = s_recv(apns_skt);
			LOG4CPLUS_DEBUG (logger, "dev_tk size : " << dev_token.size());

			std::string info = s_recv(apns_skt);
			LOG4CPLUS_DEBUG (logger, "s_recv info : " << info.size());

			Json::Value jv_payload;
			//Json::Value jv_alert, jv_badge, jv_sound;
			jv_payload["aps"]["alert"] = info;
			jv_payload["aps"]["badge"] = 1;
			jv_payload["aps"]["sound"] = "chime";
			Json::FastWriter writer;
			std::string str_payload = writer.write(jv_payload);
			LOG4CPLUS_DEBUG (logger, "payload: \n" << str_payload);
			LOG4CPLUS_DEBUG (logger, "payload: \n" << jv_payload.toStyledString());
		    LOG4CPLUS_DEBUG (logger, "payload len: " << str_payload.length ());

			char byte_token [DEVICE_TOKEN_SIZE];
			ByteDump (byte_token, dev_token.data(), DEVICE_TOKEN_SIZE);

			apns.sendPayload (byte_token, str_payload.c_str (), str_payload.length ());
			checker.start();
		}
	}

	apns.ReleasePush();
	apns.ReleaseFeedback();

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

//    BIO_set_nbio(bio, 1); // 1 non block

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

//      LOG4CPLUS_DEBUG (logger, "before SSL_write tm: ");
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
          else
          {
              LOG4CPLUS_DEBUG (logger, "read null: " << SSL_get_error(push.ssl, rbytes));
          }
      }
      else
      {
          LOG4CPLUS_DEBUG (logger, "fail to write push: " <<  SSL_get_error(push.ssl, wbytes));
          rtn = false;
      }
//      LOG4CPLUS_DEBUG (logger, "after SSL_write");
  }

  return rtn;
}

void Apns::checkFeedback ()
{
    char byte_buff [FEEDBACK_SIZE];
    char hex_buff [FEEDBACK_SIZE * 2];
    int rbytes;

    while ((rbytes = SSL_read (feedback.ssl, byte_buff, FEEDBACK_SIZE)) > 0) //to be improved
    {
        LOG4CPLUS_DEBUG (logger, "read feedback bytes: " << rbytes);
        HexDump (hex_buff, byte_buff, FEEDBACK_SIZE);
        std::string str_feedback (hex_buff, FEEDBACK_SIZE * 2);
        LOG4CPLUS_DEBUG (logger, "dump feedback: " << hex_buff);
        std::string device_token ( hex_buff + FEEDBACK_HEAD_SIZE * 2, (FEEDBACK_SIZE - FEEDBACK_HEAD_SIZE ) * 2);
        UnregisterDevice (db_client, device_token);
    }

//    LOG4CPLUS_DEBUG (logger, "checkFeedBack out: ");
}
