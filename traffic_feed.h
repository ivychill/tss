//
//

#include <unistd.h>
#include <time.h>
#include <set>
#include "zhelpers.hpp"
#include "../jsoncpp-src-0.5.0/include/json/json.h"
#include "tss_log.h"
#include "tss.pb.h"
#include "tss_helper.h"
#define CITY_NAME "深圳"
#define ROAD_TRAFFIC_TIMEOUT 15 //minute
#define CLIENT_REQUEST_TIMEOUT 30 //minute
#define UPDATE_INTERVAL 120 //second
#define TRAFFIC_PUB_MSG_ID 255
#define HOT_TRAFFIC_ROUTE_ID 255
using namespace std;
using namespace tss;
//#include <google/protobuf/repeated_field.h>
//using namespace google::protobuf;


class TrafficObserver;

class RoadTrafficSubject
{
    LYRoadTraffic road_traffic;
    set <TrafficObserver *> set_observers;

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
    map<string, RoadTrafficSubject *> map_roadtraffic;

  public:
    CityTrafficPanorama ();
    LYCityTraffic& GetCityTraffic ();
    void Attach (TrafficObserver *obs, const string& road);
    void Detach (TrafficObserver *obs, const string& road);
    int SetState (const Json::Value& jv_city);
};

class TrafficObserver
{
    time_t last_update;
    string address;
//    LYRoute route;
    LYTrafficSub traffic_sub;
    LYMsgOnAir snd_msg;
    LYCityTraffic* relevant_traffic;

  public:
    void Update (RoadTrafficSubject *sub, bool should_pub);
    int ReplyToClient ();
    void Register (const string& adr, LYTrafficSub& ts);
    void Unregister ();

    TrafficObserver ()
    {
        last_update = 0;
        relevant_traffic = NULL;
        snd_msg.set_version (1);
        snd_msg.set_msg_id (TRAFFIC_PUB_MSG_ID);
        snd_msg.set_from_party (LY_TSS);
        snd_msg.set_to_party (LY_CLIENT);
        snd_msg.set_msg_type (LY_TRAFFIC_PUB);
    }
};

class ClientObservers
{
//    string address;
	map<int, TrafficObserver> map_route_relevant_traffic;
	bool has_sub_hot_traffic;

public:
//	void SubHotTraffic (LYMsgOnAir& pkg);
    void CreateSubscription (const string& adr, LYMsgOnAir& pkg);
//    void UpdateSubscription (const string& adr, LYMsgOnAir& pkg);
    void DeleteSubscription (const string& adr, LYMsgOnAir& pkg);

    ClientObservers ()
    {
    	has_sub_hot_traffic = false;
    }
};

class OnRouteClientPanorama
{
    map<string, ClientObservers> map_client_relevant_traffic;
    LYMsgOnAir hot_traffic_sub;

  public:
    int SubTraffic (string& adr, LYMsgOnAir& pkg);
    /*
    int SubEventTraffic (string& adr, LYMsgOnAir& pkg);
    int SubAdhocTraffic (string& adr, LYMsgOnAir& pkg);
    int SubCronTraffic (string& adr, LYMsgOnAir& pkg);
    */
    void Init ();
//    void SubHotTraffic (const string& adr, LYMsgOnAir& pkg);
    void CreateSubscription (const string& adr, LYMsgOnAir& pkg);
//    void UpdateSubscription (const string& adr, LYMsgOnAir& pkg);
    void DeleteSubscription (const string& adr, LYMsgOnAir& pkg);
    int Publicate (zmq::socket_t& skt);
};

class ClientMsgProcessor
{
    string address;
    LYMsgOnAir rcv_msg;
    LYMsgOnAir snd_msg;
    int ReturnToClient (LYRetCode ret_code);
    int ReturnToClient (LYCheckin checkin);
    int PreprocessRcvMsg (string& adr, LYMsgOnAir& msg);
    
  public:
    ClientMsgProcessor ()
    {
        snd_msg.set_version (1);
        snd_msg.set_from_party (LY_TSS);
        snd_msg.set_to_party (LY_CLIENT);
        snd_msg.set_msg_type (LY_RET_CODE);
    }

    int ProcessRcvMsg (string& adr, LYMsgOnAir& msg);
};

class VersionManager
{
	vector<LYCheckin> vec_latest_version;

public:
	void Init ();
	bool GetLatestVersion (LYCheckin& checkin);
};
