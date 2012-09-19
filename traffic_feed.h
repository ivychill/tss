//
//

#include <unistd.h>
#include <time.h>
#include <set>
#include "zhelpers.hpp"
#include "/home/chenfeng/jsoncpp-src-0.5.0/include/json/json.h"
#include "tss_log.h"
#include "tss.pb.h"
#include "tss_helper.h"
#define CITY_NAME "深圳"
#define ROAD_TRAFFIC_TIMEOUT 30 //minute
#define CLIENT_REQUEST_TIMEOUT 30 //minute
#define UPDATE_INTERVAL 120 //second
using namespace std;
using namespace tss;
//#include <google/protobuf/repeated_field.h>
//using namespace google::protobuf;

int JsonStringToJsonValue (const std::string& str_input, Json::Value& jv_roadset);
int TimeStrToInt(const std::string& str_time);
LYDirection DirectionStrToInt(const std::string& str_direction);
//void PrepareSndMsg (LYMsgOnAir& msg, LYMsgType mt, int msgid);

class TrafficObserver;

class RoadTrafficSubject
{
    LYRoadTraffic road_traffic;
    std::set <TrafficObserver *> set_observers;

  public:
    LYRoadTraffic& GetRoadTraffic ()
    {
        return road_traffic;
    }

    void Attach(TrafficObserver *obs);
    void Detach(TrafficObserver *obs);
    void Notify ();
    void Notify (TrafficObserver *obs);
    int SetState(const Json::Value& jv_road);
};

class CityTrafficPanorama
{
    LYCityTraffic city_traffic;
    std::map<std::string, RoadTrafficSubject *> map_roadtraffic;

  public:
    CityTrafficPanorama ();
    LYCityTraffic& GetCityTraffic ();
    void Attach (TrafficObserver *obs, const std::string& road);
    void Detach (TrafficObserver *obs, const std::string& road);
    int SetState (const Json::Value& jv_city);
};

class TrafficObserver
{
    time_t last_update;
    std::string address;
    LYRoute route;
    LYMsgOnAir snd_msg;
    LYCityTraffic* relevant_traffic;

  public:
    void Update (RoadTrafficSubject *sub);
    void Register (LYRoute& drrt);
    void Unregister ();
    int ReplyToClient ();

    TrafficObserver (std::string adr)
    {
        last_update = 0;
        address = adr;
    }

    std::string& GetAddress ()
    {
        return address;
    }

    /*
    LYMsgOnAir& GetPackage ()
    {
        return package;
    }
    */
};

class OnRouteClientPanorama
{
    std::map<std::string, std::map<int, TrafficObserver *> > map_client_relevant_traffic;
    void NewSubscription (std::map<int, TrafficObserver *>* map_rct, const std::string& adr, LYRoute& drrt);

  public:
    int SubTraffic (std::string& adr, LYMsgOnAir& pkg);
    void CreateSubscription (const std::string& adr, LYMsgOnAir& pkg);
    void UpdateSubscription (const std::string& adr, LYMsgOnAir& pkg);
    void DeleteSubscription (const std::string& adr, LYMsgOnAir& pkg);
    int Publicate (zmq::socket_t& skt);

    std::map<int, TrafficObserver *>& operator [] (const std::string& adr)
    {
        return map_client_relevant_traffic[adr];
    }
};

class ClientMsgProcessor
{
    std::string address;
    LYMsgOnAir rcv_msg;
    LYMsgOnAir snd_msg;
    int ReturnToClient ();
    int PreprocessRcvMsg (std::string& adr, LYMsgOnAir& msg);
    
  public:
    ClientMsgProcessor ()
    {
        snd_msg.set_version (1);
        snd_msg.set_from_party (LY_TSS);
        snd_msg.set_to_party (LY_CLIENT);
        snd_msg.set_msg_type (LY_RET_CODE);
    }

    int ProcessRcvMsg (std::string& adr, LYMsgOnAir& msg);
};
