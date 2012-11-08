#include "traffic_feed.h"
#include <iterator>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <set>
#include <time.h>

#define TSS_TST 0
#ifdef TSS_TST
#if TSS_TST
#define TSS_CRON_SLEEP (50)  // 0.1 second
#define TSS_DOW  62
#else
#define TSS_CRON_SLEEP (60*1000)  // 60 second


#endif
#endif

#define DAYS_WEEK 7
#define MINUTES_DAY 1440

extern Logger logger;
extern DBClientConnection db_client;
extern zmq::socket_t* p_skt_apns_client;
zmq::socket_t* p_cron_server;
extern CronSchelder* p_cron_sched;

const static int k_repeat_period = 5;  // 5 minitue
const static int k_repeat_time = 2;
//
//

bool CronJob::operator==(const CronJob& other) const
{
	return this->dev_tk_ == other.dev_tk_
			&& this->route_id_ == other.route_id_;
}

void CronJob::Exec()
{
	s_sendmore(*p_cron_server, this->dev_tk_);
	s_send(*p_cron_server, string(boost::lexical_cast<std::string>( this->route_id_)));
}

void CronJob::Do()
{
//#if TSS_TST
	if(wait_time_ % 10 == 0 || wait_time_ < 10)
		LOG4CPLUS_DEBUG (logger, "CronJob::Do left_time: "<< this->wait_time_);
//#endif

	if( wait_time_-- <= 0)
	{
		this->Exec();
		this->Renew();
	}
}

void CronJob::Renew()
{
	if(this->repeate_time_--)
	{
		this->wait_time_ += k_repeat_period;
	}
	else
	{
		this->wait_time_ = CalcWaitTime(this->tab);
		this->repeate_time_ = k_repeat_time;
	}
}

void CronJob::ModifyTime(int tm)
{
	this->wait_time_ = tm;
}

bool CronJob::IsInDow(int day_of_week, int dow)
{
	int weekmask = dow & 0x7f; //week mask
//	LOG4CPLUS_DEBUG (logger, "IsInDow dow: "<< weekmask);

#if TSS_TST
    weekmask = TSS_DOW;
#endif

	return (weekmask >> day_of_week) & 0x1;
}

int CronJob::GetDaysInterval(date& today, int dow)
{
	int days = 0;
	int weekmask = dow & 0x7f; //week mask

#if TSS_TST
	weekmask = TSS_DOW;
#endif

//	LOG4CPLUS_DEBUG (logger, "GetDaysByDow: "<< weekmask);
	if(0 == weekmask)
	{
		return -1;
	}

	int nextdayinweek = (today.day_of_week() + 1) % DAYS_WEEK; // [0 ~ 6]

	do
	{
		days++;
		if(nextdayinweek == DAYS_WEEK)
		{
			nextdayinweek = 0;
		}
	}while( !( (weekmask >> nextdayinweek++ ) & 0x1));

//	LOG4CPLUS_DEBUG (logger, "GetDaysInterval: "<< days);

	return days;
}

int CronJob::CalcWaitTime(const LYCrontab& tab)
{
    date today(day_clock::local_day());
    tm n = to_tm(second_clock::local_time());
    ptime now(today, hours(n.tm_hour)+minutes(n.tm_min)+seconds(n.tm_sec));

    int work_hour = 8;
    int home_hour = 18;
    //default time
    if(tab.has_hour())
    {
        work_hour = tab.hour() >> 32;
        home_hour = tab.hour() & 0xff;

        LOG4CPLUS_DEBUG (logger, "gowork : " <<work_hour<< " gohome :"<< home_hour);
        if(home_hour < work_hour)
        {
            LOG4CPLUS_DEBUG (logger, "wrong home hour "<< home_hour);
            return -1;
        }
    }

	switch(tab.cron_type())
	{

	case LYCrontab_LYCronType_LY_REP_DOW:
        {
            int days = GetDaysInterval(today, tab.dow());
            // not today job
            if(! IsInDow(today.day_of_week() ,tab.dow()))
            {
                work_hour += days*24;
                home_hour += days*24;
            }

            ptime gowork(today, hours(work_hour)+minutes(0));
            ptime gohome(today, hours(home_hour)+minutes(0));

            int time_len;  //unit: second
            time_duration timetowork = gowork - now;
            time_duration timetohome = gohome - now;

            if(! timetowork.is_negative())
            {
                //book the gowork time , negative
                time_len = timetowork.total_seconds();
                LOG4CPLUS_INFO (logger, "time to work: "<< time_len/60 <<" min");
            }
            else if(! timetohome.is_negative())
            {
                time_len = timetohome.total_seconds();
                LOG4CPLUS_INFO (logger, "time go home: "<< time_len/60 <<" min");
            }
            else  //current time is later than today go home, need next work time
            {
                //next valid day gowork time
                time_len = timetowork.total_seconds() + days* 1440 * 60;
                LOG4CPLUS_INFO (logger, "time go nextday work: "<< time_len/60 <<" min");
            }

            return time_len/60;
        }
        break;
	case LYCrontab_LYCronType_LY_REP_MONTH:
		break;


	case LYCrontab_LYCronType_LY_REP_DOM:
		break;

	case LYCrontab_LYCronType_LY_REP_HOUR:
		break;

	case LYCrontab_LYCronType_LY_REP_MINUTE:
		break;

	default:
		LOG4CPLUS_DEBUG (logger, "cron type error: "<< tab.cron_type());

		break;
	}

	return -1;
}

template< typename T >
struct Finder{
	Finder(T &p) : p_(p) { }
	bool operator()(const T & other) {
		return  *p_.get() == *other.get();
	}
	T & p_;
};

void JobQueue::Submit(shared_ptr<CronJob>& job)
{
	boost::lock_guard<boost::mutex> lk(mutex_);

	Queue::iterator itr = std::find_if(queue_.begin(), queue_.end(), Finder< shared_ptr<CronJob> >(job));
	if(itr != queue_.end())
	{
		LOG4CPLUS_DEBUG (logger, "find duplicate job. the queue size:" << queue_.size());

		//update to the new job timer
		(*itr)->ModifyTime(job->GetWaitTime());
		return;
	}

	//LOG4CPLUS_DEBUG (logger, "insert job queue : " << queue_.size());

	queue_.push_back(job);
	LOG4CPLUS_DEBUG (logger, "after push_back JobQueue size : " << queue_.size());
}

void JobQueue::Remove(const string& dev_token, LYTrafficSub& ts, CronTrafficObserver* pobs)
{
	shared_ptr<CronJob> job(new CronJob(dev_token, 0, pobs));

	boost::lock_guard<boost::mutex> lk(mutex_);
	Queue::iterator itr = std::find_if(queue_.begin(), queue_.end(), Finder< shared_ptr<CronJob> >(job));
	if(itr != queue_.end())
	{
		queue_.erase(itr);
	}

	LOG4CPLUS_DEBUG (logger, "after Remove size : " << queue_.size());
}

void JobQueue::DoJob()
{
	//LOG4CPLUS_DEBUG (logger, "JobQueue.DoJob entry:JobQueue size : " << queue_.size());
	boost::lock_guard<boost::mutex> lk(mutex_);
	for(Queue::iterator itr = queue_.begin(); itr != queue_.end();)
	{
		(*itr)->Do();
		if((*itr)->GetWaitTime() < 0)
		{
			itr = queue_.erase(itr);
		}
		else
		{
			++itr;
		}
	}

	//LOG4CPLUS_DEBUG (logger, "JobQueue.DoJob done:JobQueue size :" << queue_.size());
}

void TimerEntry(CronSchelder* pcron)
{
	while(true)
	{
		pcron->OnTimer();
		s_sleep(TSS_CRON_SLEEP);
	}
}

void CronSchelder::OnTimer()
{
	//LOG4CPLUS_INFO (logger, "cron ontimer");
	this->jobqueue_.DoJob();
}

void CronSchelder::Init()
{
    skt_.bind("ipc://cron_scheler.ipc");
    p_cron_server = &skt_;
    boost::thread timer(boost::bind(TimerEntry, this));
}

void CronSchelder::AddJob(const string& adr, LYTrafficSub& ts, CronTrafficObserver* pobs)
{
	int tm = CronJob::CalcWaitTime(ts.cron_tab());

	LOG4CPLUS_DEBUG (logger, "Cron::GenJob->cron wait : " << tm <<" minutes");
//	LOG4CPLUS_DEBUG (logger, "Cron::GenJob->cron dow: " << ts.cron_tab().dow());
//	LOG4CPLUS_DEBUG (logger, "Cron::GenJob->cron dow: " << (ts.cron_tab().dow() & 0x7f));
	if(tm >= 0)
	{
		shared_ptr<CronJob> pJob(new CronJob(adr, tm, pobs));
		this->jobqueue_.Submit(pJob);
	}
	else
	{
		LOG4CPLUS_DEBUG (logger, "invalid wait time: " << tm);
	}
}

void CronSchelder::DelJob(const string& adr, LYTrafficSub& ts, CronTrafficObserver* p_obs)
{
	this->jobqueue_.Remove(adr, ts, p_obs);
}

void CronTrafficObserver::Update (RoadTrafficSubject *sub, bool should_pub)
{
    LOG4CPLUS_INFO (logger, "CronTrafficObserver::Update: ");

    time_t now = time (NULL);
    time_t ts = sub->GetRoadTraffic().timestamp();
    if (now - ts > ROAD_TRAFFIC_TIMEOUT * 60)
    {
        LOG4CPLUS_INFO (logger, "expired road traffic, road: " << sub->GetRoadTraffic().road() << ", timestamp: " << ::ctime(&ts));
    }
    else
    {
        // only 1 road
        relevant_traffic->clear_road_traffics();

        LYRoadTraffic *rdtf = relevant_traffic->add_road_traffics();
        *rdtf = sub->GetRoadTraffic();
        LOG4CPLUS_DEBUG (logger, "add road traffic:\n" << sub->GetRoadTraffic().DebugString() << " to observer: " << address);
    }

    last_update = now;
}

int CronTrafficObserver::ReplyToClient ()
{
    string reply;

    if(this->os_ver == IOS)
    {
        if(LY_TRAFFIC_PUB == snd_msg.msg_type())
        {
            const tss::LYTrafficPub& pub = snd_msg.traffic_pub();
            LOG4CPLUS_DEBUG (logger, "LY_TRAFFIC_PUB: " << pub.city_traffic().road_traffics_size());

            if(pub.has_city_traffic())
            {
                LOG4CPLUS_DEBUG (logger, "has_city_traffic: ");

//                if(pub.city_traffic().road_traffics_size() > 0)
                for(int rd = 0; rd< pub.city_traffic().road_traffics_size(); rd++)
                {
//                    string rn = pub.city_traffic().road_traffics(0).road();
                    const LYRoadTraffic&  road = pub.city_traffic().road_traffics(rd);
                    reply += road.road();

//                    LOG4CPLUS_DEBUG (logger, "send to client msg:" << pub.city_traffic().road_traffics().size());
                    for(int segment = 0; segment < road.segment_traffics_size(); segment++)
                    {
                        const LYSegmentTraffic& sgmt = road.segment_traffics(segment);
                        reply += sgmt.details();
//                        reply += "方向";
                        reply += sgmt.direction();
//                        reply += "速度";
                        if(reply.size() < 140)
                            reply += sgmt.speed();
                    }
                }
            }
        }

//        LOG4CPLUS_DEBUG (logger, "send to apns token: " << dev_token.size());
        LOG4CPLUS_DEBUG (logger, "IOS send to apns msg len: "<<reply.size());

        s_sendmore(*p_skt_apns_client, s_hex_token);
        s_send (*p_skt_apns_client, reply);
    }
    else
    {
        LOG4CPLUS_INFO (logger, "android ReplyToClient: " );
        TrafficObserver::ReplyToClient();
    }

    return 0;
}

void CronTrafficObserver::Register (const string& adr, LYTrafficSub& ts)
{
    AttachToTraffic(adr, ts);

    //record the ts info to db
    char hex_token [DEVICE_TOKEN_SIZE * 2];
    HexDump (hex_token, adr.c_str(), DEVICE_TOKEN_SIZE);
    std::string s_hex_token (hex_token, DEVICE_TOKEN_SIZE * 2);

    this->s_hex_token = s_hex_token;

    mongo::Query condition = QUERY("dev_token"<<s_hex_token);
    LOG4CPLUS_INFO (logger, "db device count: " << db_client.count("roadclouding_production.devices"));

    auto_ptr<DBClientCursor> cursor = db_client.query(dbns, condition);
    if (cursor->more())
    {
        mongo::BSONObj dev = cursor->next();

        mongo::BSONObjBuilder query;
        string ts_str = ts.SerializeAsString();

        query.appendBinData("trafficsub", ts_str.length(), BinDataGeneral, ts_str.c_str());
        BSONObj obj = BSON( "$set" << query.obj());

        db_client.update(dbns, condition, obj, false, true);

        string os_ver = dev["dev_os_ver"].String();
        boost::to_lower(os_ver);
        if(boost::find_first(os_ver, "ios"))
        {
            this->os_ver = IOS;
        }
        else
        {
            this->os_ver = ANDROID;
        }

        p_cron_sched->AddJob(adr, ts, this);
    }
    else
    {
        LOG4CPLUS_INFO (logger, "device not register: " << s_hex_token);
    }
}

void CronTrafficObserver::Unregister ()
{
    TrafficObserver::Unregister();

    mongo::Query condition = QUERY("dev_token"<<s_hex_token);
    LOG4CPLUS_INFO (logger, "db device count: " << db_client.count("roadclouding_production.devices"));

    auto_ptr<DBClientCursor> cursor = db_client.query(dbns, condition);
    if (cursor->more())
    {
        LOG4CPLUS_INFO (logger, "delete cron info: " << this->address);
        db_client.update(dbns, condition, BSON( "$unset"<< BSON("trafficsub"<<1)), true);

        //db_client.remove(dbns, condition, true);
    }

    p_cron_sched->DelJob(this->address, this->traffic_sub, this);
}

void CronClientObservers::CreateSubscription (const string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();

//    LOG4CPLUS_DEBUG (logger, "run to CreateSubscription: ");
    //LOG4CPLUS_ERROR (logger, "ts pub type "<< traffic_sub.pub_type());

    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();

    LOG4CPLUS_DEBUG (logger, "insert/update subscription, address: " << adr << " identity: " << identity);
    cron_route_relevant_traffic [identity].Unregister ();
    cron_route_relevant_traffic [identity].Register (adr, traffic_sub);
}

void CronClientObservers::DeleteSubscription (const string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();

    cron_route_relevant_traffic[identity].Unregister ();
    cron_route_relevant_traffic.erase(identity);
    LOG4CPLUS_DEBUG (logger, "delete subscription, address: " << adr << " identity: " << identity);
}

void CronClientPanorama::ProcSchedInfo(string& dev_tk, string& route)
{
    LOG4CPLUS_DEBUG (logger, "ProcSchedInfo, route_id : " << route);
    int route_id = boost::lexical_cast<int>(route);

    std::map<string, CronClientObservers> ::iterator itr = cron_client_relevant_traffic.find(dev_tk);
    if (itr != cron_client_relevant_traffic.end())
    {
        CronTrafficObserver * pobj = (*itr).second.getObs(route_id);
        if(pobj)
        {
            pobj->ReplyToClient();
        }
        else
        {
            LOG4CPLUS_ERROR (logger, "ProcSchedInfo, no find route : " << route_id);
        }
    }
    else
    {
        LOG4CPLUS_ERROR (logger, "ProcSchedInfo, no find dev : " << dev_tk);
    }
}

// Add if there does not exist, update if there exists.
void CronClientPanorama::CreateSubscription (const string& adr, LYMsgOnAir& pkg)
{
    LOG4CPLUS_DEBUG (logger, "insert/update subscription, address: " );//<< adr);
    cron_client_relevant_traffic [adr].CreateSubscription(adr, pkg);
}

void CronClientPanorama::DeleteSubscription (const string& adr, LYMsgOnAir& pkg)
{
    //LOG4CPLUS_DEBUG (logger, "delete subscription, address: " << adr);
    cron_client_relevant_traffic [adr].DeleteSubscription(adr, pkg);
}

int CronClientPanorama::SubTraffic (string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYTrafficSub::LYOprType opr_type = traffic_sub.opr_type ();
    switch (opr_type)
    {
        case LYTrafficSub::LY_SUB_CREATE:
        case LYTrafficSub::LY_SUB_UPDATE:
            CreateSubscription (adr, pkg);
            return 0;

        case LYTrafficSub::LY_SUB_DELETE:
            DeleteSubscription (adr, pkg);
            return 0;

        default:
            LOG4CPLUS_ERROR (logger, "invalid operation type: " << opr_type);
            return -1; //failure
    }

    return 0;
}

void CronClientPanorama::Init ()
{
    if(inited)
    {
        inited = true;
        return;
    }

    char byte_token[DEVICE_TOKEN_SIZE];

    auto_ptr<DBClientCursor> cursor = db_client.query(dbns, BSONObj());
    while (cursor->more())
    {
        LOG4CPLUS_INFO (logger, "Init queue");

        mongo::BSONObj obj = cursor->next();
        //std::string dev_token =  obj.getStringField("dev_token");

        if(obj.hasField("trafficsub"))
        {
            ByteDump (byte_token, obj["dev_token"].String().c_str(), DEVICE_TOKEN_SIZE);
            std::string str_byte_token (byte_token, DEVICE_TOKEN_SIZE);
            //LOG4CPLUS_DEBUG (logger, "dev_token:" << dev_token);

            int binlen;
            string LYTrafficSubStr(obj.getField("trafficsub").binDataClean(binlen));
//            LOG4CPLUS_DEBUG (logger, "get binlen: " << binlen);

            LYMsgOnAir msg;
            if(msg.mutable_traffic_sub()->ParseFromString(LYTrafficSubStr))
            {
                LOG4CPLUS_INFO (logger, "recover 1 traffic sub");
                this->CreateSubscription(str_byte_token, msg);
            }
            else
            {
                LOG4CPLUS_ERROR (logger, "ParseFromString fail");
            }
        }
        else
        {
            LOG4CPLUS_DEBUG (logger, "no trafficsub");
        }
    }
}
