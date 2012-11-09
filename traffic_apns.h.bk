#include "zhelpers.hpp"

#include <stdint.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "/home/chenfeng/jsoncpp-src-0.5.0/include/json/json.h"
#include "tss_log.h"
#include "tss_helper.h"

#define DVL_PUSH_URL "gateway.sandbox.push.apple.com:2195"
#define DVL_FEEDBACK_URL "feedback.sandbox.push.apple.com:2196"
#define DVL_CER_PATH "etc/easyway95_dvl_cer.pem"
#define DVL_KEY_PATH "etc/easyway95_dvl_key.pem"
#define DVL_PASSPHRASE "huawei123"
#define PDT_PUSH_URL "gateway.push.apple.com:2195"
#define PDT_FEEDBACK_URL "feedback.push.apple.com:2196"
#define PDT_CER_PATH "etc/easyway95_pdt_cer.pem"
#define PDT_KEY_PATH "etc/easyway95_pdt_key.pem"
#define PDT_PASSPHRASE "huawei123"
#define MAXPAYLOAD_SIZE 256
#define FEEDBACK_SIZE 38
#define FEEDBACK_HEAD_SIZE 6
#define ERROR_RESPONSE_SIZE 6

void unregisterDevice (const std::string& device_token);

struct UrlAndSSL
{
    std::string url;
    SSL_CTX* ctx;
    SSL *ssl;
};

class Apns
{
    /*
    std::string url_push;
    std::string url_feedback;;
    SSL* ssl_push;
    SSL* ssl_feedback;
    */
    std::string cer_path;
    std::string key_path;
    std::string passphrase;    
    UrlAndSSL push;
    UrlAndSSL feedback;
    void Init (UrlAndSSL& uas);
    void Release (UrlAndSSL& uas);

  public:
    Apns ();
    //~Apns ();

    void InitPush ()
    {
        Init (push);
    }

    void InitFeedback ()
    {
        Init (feedback);
    }

    void ReleasePush ()
    {
        Release (push);
    }

    void ReleaseFeedback ()
    {
        Release (feedback);
    }

    bool sendPayload (char *deviceTokenBinary, const char *payloadBuff, size_t payloadLength);
    bool checkFeedback ();
};
