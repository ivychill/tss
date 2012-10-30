#include "traffic_feed.h"
#include <iterator>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#define TSS_TST 0
#ifdef TSS_TST
#if TSS_TST
#define TSS_CRON_SLEEP (1*100)  // 0.1 second
#define TSS_DOW 31
#else
#define TSS_CRON_SLEEP (60*1000)  // 60 second

#endif
#endif

#define DAYS_WEEK 7
#define MINUTES_DAY 1440

extern Logger logger;
extern DBClientConnection db_client;
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
	s_sendmore(pcron_->skt_, this->dev_tk_);
	s_send(pcron_->skt_,string(boost::lexical_cast<std::string>( this->route_id_)));
}

void CronJob::Do()
{
	if(wait_time_ % 10 == 0 || wait_time_ < 10)
		LOG4CPLUS_DEBUG (logger, "CronJob::Do left_time: "<< this->wait_time_);

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
		this->wait_time_ = CalcWaitTime(this->ts_.cron_tab());
		this->repeate_time_ = k_repeat_time;
	}
}

void CronJob::ModifyTime(int tm)
{
	this->wait_time_ = tm;
}

bool CronJob::DayInDow(date& day, int dow)
{
	int weekmask = dow & 0x7f; //week mask

	return (weekmask >> day.day_of_week()) & 0x1;
}

int CronJob::GetDaysInterval(date& today, int dow)
{
	int days = 0;
	int weekmask = dow & 0x7f; //week mask

#if TSS_TST
	weekmask = TSS_DOW;
#endif

	LOG4CPLUS_DEBUG (logger, "GetDaysByDow: "<< weekmask);
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

	//LOG4CPLUS_DEBUG (logger, "GetDays: "<< days);

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

		LOG4CPLUS_DEBUG (logger, "get home hour :"<< home_hour);

		if(home_hour < work_hour)
		{
			LOG4CPLUS_DEBUG (logger, "wrong home hour "<< home_hour);
			return -1;
		}
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
		LOG4CPLUS_DEBUG (logger, "time to work: "<< time_len/60);
	}
	else if(! timetohome.is_negative())
	{
		time_len = timetohome.total_seconds();
		LOG4CPLUS_DEBUG (logger, "time go home: "<< time_len/60 <<" min");
	}
	else  //current time is later than go home, need next work time
	{
		//book the gohome time
		time_len = timetowork.total_seconds();
		LOG4CPLUS_DEBUG (logger, "time go work: "<< time_len/60);
	}

	switch(tab.cron_type())
	{

	case LYCrontab_LYCronType_LY_REP_DOW:

		if(tab.has_dow())
		{
			// next valid day interval
			int days = GetDaysInterval(today, tab.dow());

			// doday is in dow table
			if(DayInDow(today, tab.dow()))
			{
				if(time_len >= 0)
				{
					// today shall do the job
					days = 0;
				}
			}

			int waittime = (time_len/60 + days * MINUTES_DAY);
			LOG4CPLUS_DEBUG (logger, "wait days: "<<days <<" total minutes: " << waittime );

			return waittime;
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
		LOG4CPLUS_DEBUG (logger, "cron type... "<< tab.cron_type());

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
		LOG4CPLUS_DEBUG (logger, "find duplicate job queue size:" << queue_.size());

		//update to the new job timer
		(*itr)->ModifyTime(job->GetWaitTime());
		return;
	}

	//LOG4CPLUS_DEBUG (logger, "insert job queue : " << queue_.size());

	queue_.push_back(job);
	LOG4CPLUS_DEBUG (logger, "after push_back JobQueue size : " << queue_.size());
}

void JobQueue::Remove(const string& dev_token, LYTrafficSub& ts)
{
	shared_ptr<CronJob> job(new CronJob(dev_token, ts, 0, 0));

	boost::lock_guard<boost::mutex> lk(mutex_);
	Queue::iterator itr = std::find_if(queue_.begin(), queue_.end(), Finder< shared_ptr<CronJob> >(job));
	if(itr != queue_.end())
	{
		queue_.erase(itr);
	}
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

void TimerEntry(Cron* pcron)
{
	while(true)
	{
		pcron->OnTimer();
		s_sleep(TSS_CRON_SLEEP);
	}
}

void Cron::OnTimer()
{
	//LOG4CPLUS_INFO (logger, "cron ontimer");
	this->jobqueue_.DoJob();
}

void Cron::Init()
{
	skt_.bind("ipc://cron_worker.ipc");
	InitQueue();

	boost::thread timer(boost::bind(TimerEntry, this));

	/*
	zmq::pollitem_t items [] = {
		//
		{ this->skt_,  0, ZMQ_POLLIN, 0 },
	};
	*/
}

void Cron::InitQueue()
{
	char byte_token[DEVICE_TOKEN_SIZE];

    auto_ptr<DBClientCursor> cursor = db_.query(dbns, BSONObj());
    while (cursor->more())
    {
    	LOG4CPLUS_DEBUG (logger, "Init queue ...");

        mongo::BSONObj obj = cursor->next();
        //std::string dev_token =  obj.getStringField("dev_token");

        if(obj.hasField("trafficsub"))
        {
        	std::string LYTrafficSubStr = obj["trafficsub"].String();

        	ByteDump (byte_token, obj["dev_token"].String().c_str(), DEVICE_TOKEN_SIZE);
        	std::string str_byte_token (byte_token, DEVICE_TOKEN_SIZE);

        	//LOG4CPLUS_DEBUG (logger, "dev_token:" << dev_token);
        	LYTrafficSub ts;
			if(ts.ParseFromString(LYTrafficSubStr))
			{
				LOG4CPLUS_DEBUG (logger, "Init queue GenJob ...");
				this->GenJob(str_byte_token, ts);
			}
			else
			{
				LOG4CPLUS_DEBUG (logger, "ParseFromString fail");
			}
        }
        else
        {
        	LOG4CPLUS_DEBUG (logger, "no trafficsub");
        }
    }
}

void Cron::GenJob(const string& dev_token, LYTrafficSub& ts)
{
	int tm = CronJob::CalcWaitTime(ts.cron_tab());

	LOG4CPLUS_DEBUG (logger, "Cron::GenJob->cron tm: " << tm);
	if(tm >= 0)
	{
		shared_ptr<CronJob> pJob(new CronJob(dev_token, ts, tm, this));
		this->jobqueue_.Submit(pJob);
	}
	else
	{
		LOG4CPLUS_DEBUG (logger, "invalid time: tm: " << tm);
	}
}


void Cron::DelJob(const string& dev_token, LYTrafficSub& ts)
{
	this->jobqueue_.Remove(dev_token, ts);
}

void Cron::ProcCronSub(const string& dev_token, LYTrafficSub& ts)
{
    char hex_token [DEVICE_TOKEN_SIZE * 2];

    HexDump (hex_token, dev_token.c_str(), DEVICE_TOKEN_SIZE);
    std::string s_hex_token (hex_token, DEVICE_TOKEN_SIZE * 2);

	mongo::Query condition = QUERY("dev_token"<<s_hex_token);
	//LOG4CPLUS_INFO (logger, "db device count: " << db_client.count("roadclouding_production.devices"));

    auto_ptr<DBClientCursor> cursor = db_client.query(dbns, condition);
    if (cursor->more())
    {
    	if(LYTrafficSub_LYOprType_LY_SUB_DELETE == ts.opr_type())
    	{
    		LOG4CPLUS_INFO (logger, "delete cron info: " << dev_token);

    		db_client.remove(dbns, condition, true);

    		this->DelJob(dev_token, ts);
    		return;
    	}

    	//mongo::BSONObj dev = cursor->next();
    	BSONObj obj = BSON( "$set"<< BSON("trafficsub"<<ts.SerializeAsString() ) );
    	db_client.update(dbns, condition, obj, false, true);

    	this->GenJob(dev_token, ts);
    }
    else
    {
    	//error
    	LOG4CPLUS_ERROR (logger, "dev_token no register");
    }

    return;
}
