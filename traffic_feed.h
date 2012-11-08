//
//

#ifndef _TRAFFIC_FEED_H_
#define _TRAFFIC_FEED_H_

#include <unistd.h>
#include <time.h>
#include <set>
#include "zhelpers.hpp"
#include "../jsoncpp-src-0.5.0/include/json/json.h"
#include "tss_log.h"
#include "tss.pb.h"
#include "tss_helper.h"

#include <string>
#include <list>
#include "boost/date_time/posix_time/posix_time.hpp"
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/utility.hpp>

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

const string dbns("roadclouding_production.devices");

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
protected:
    time_t last_update;
    string address;
//    LYRoute route;
    LYTrafficSub traffic_sub;
    LYMsgOnAir snd_msg;
    LYCityTraffic* relevant_traffic;

  public:
    virtual void Update (RoadTrafficSubject *sub, bool should_pub);
    virtual int ReplyToClient ();
    void AttachToTraffic(const string& adr, LYTrafficSub& ts);
    virtual void Register (const string& adr, LYTrafficSub& ts);
    virtual void Unregister ();

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

    virtual ~TrafficObserver(){};
};

class ClientObservers
{
private:
//    string address;
	map<int, TrafficObserver> map_route_relevant_traffic;
	bool has_sub_hot_traffic;

public:
//	void SubHotTraffic (LYMsgOnAir& pkg);
	virtual void CreateSubscription (const string& adr, LYMsgOnAir& pkg);
//    void UpdateSubscription (const string& adr, LYMsgOnAir& pkg);
	virtual void DeleteSubscription (const string& adr, LYMsgOnAir& pkg);

    ClientObservers ()
    {
    	has_sub_hot_traffic = false;
    }

    virtual ~ClientObservers(){}
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

    void ProcTrafficReport(const string& adr, LYTrafficReport& report);
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

using namespace boost::posix_time;
using namespace boost::gregorian;

class CronTrafficObserver: public TrafficObserver
{
private:
    enum OS_VER{
        IOS = 0,
        ANDROID = 1
    };
    string s_hex_token;
    OS_VER os_ver;

  public:
    virtual void Update (RoadTrafficSubject *sub, bool should_pub);
    virtual int ReplyToClient ();
    virtual void Register (const string& adr, LYTrafficSub& ts);
    virtual void Unregister ();

    LYTrafficSub & getTrafficSub(){
        return this->traffic_sub;
    }
    CronTrafficObserver ():os_ver(ANDROID){}
};

class CronClientObservers: public ClientObservers
{
//    string address;
    map<int, CronTrafficObserver> cron_route_relevant_traffic;

public:
    virtual void CreateSubscription (const string& adr, LYMsgOnAir& pkg);
    virtual void DeleteSubscription (const string& adr, LYMsgOnAir& pkg);

    CronClientObservers (){}

    void ProcCronSub(const string& dev_tk, int route_id);

    CronTrafficObserver* getObs(int id){
        map<int, CronTrafficObserver>::iterator itr = cron_route_relevant_traffic.find(id);
        if(itr != cron_route_relevant_traffic.end())
        {
            return &cron_route_relevant_traffic[id];
        }
        return 0;
    }
};

class CronClientPanorama
{
    std::map<string, CronClientObservers> cron_client_relevant_traffic;
    bool inited;

  public:
    int SubTraffic (string& adr, LYMsgOnAir& pkg);

    CronClientPanorama ():inited(false){}

    void CreateSubscription (const string& adr, LYMsgOnAir& pkg);
    void DeleteSubscription (const string& adr, LYMsgOnAir& pkg);
    void ProcSchedInfo(string& dev_tk, string& route_id);
    void Init ();
};
class CronJob
{
private:
	int wait_time_;       //unit: minute
	int repeate_time_;    //default 3 ; period 5 minute
	string dev_tk_;
	int route_id_;
	void Renew();
	void Exec();
	CronTrafficObserver* p_observer;
	const LYCrontab& tab;

public:
    static int CalcWaitTime(const LYCrontab& tab);
    static int GetDaysInterval(date& today, int dow);
    static bool IsInDow(int day_of_week, int dow);

	inline int GetWaitTime(){return wait_time_;}

	CronJob(const string& dev_tk ,int tm, CronTrafficObserver* pobs):dev_tk_(dev_tk),
			wait_time_(tm),
			repeate_time_(2),
			route_id_(-1),
			tab(pobs->getTrafficSub().cron_tab()){
	    p_observer = pobs;
		if(pobs->getTrafficSub().has_route())
		{
			route_id_ = pobs->getTrafficSub().route().identity();
		}
	}
	void Do();
	void ModifyTime(int tm);
	bool operator==(const CronJob& other)const;

	~CronJob(){};
};

class JobQueue: boost::noncopyable
{
private:
	typedef std::list< boost::shared_ptr<CronJob> > Queue;
	Queue queue_;
	int tm_elapse_; // minutes elapse in a day,  max(24*60 = 1440)

	boost::mutex mutex_;

public:
	JobQueue():tm_elapse_(0){}
	void Submit(boost::shared_ptr<CronJob> & job);
	void Remove(const string& dev_token, LYTrafficSub& ts, CronTrafficObserver* pobs);
    void DoJob();
};

class CronSchelder : boost::noncopyable
{
private:
	JobQueue jobqueue_;
	void InitQueue();

public:
	CronSchelder(zmq::context_t& ctxt):skt_(ctxt, ZMQ_PAIR){}

	void Init();
	void ProcCronSub(const string& dev_token, LYTrafficSub& ts);
	void OnTimer();

	void AddJob(const string& dev_tk, LYTrafficSub& ts, CronTrafficObserver* p_observer);
    void DelJob(const string& dev_token, LYTrafficSub& ts, CronTrafficObserver* p_observer);

	zmq::socket_t skt_;
};

class VersionManager
{
	vector<LYCheckin> vec_latest_version;

public:
	void Init ();
	bool GetLatestVersion (LYCheckin& checkin);
};

#endif
