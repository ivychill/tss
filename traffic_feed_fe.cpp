//
//

#include "traffic_feed.h"

extern Logger logger;
extern CityTrafficPanorama citytrafficpanorama;
extern OnRouteClientPanorama onrouteclientpanorama;
extern CronClientPanorama cronclientpanorama;
//extern ClientMsgProcessor client_msg_processor;
extern DBClientConnection db_client;
extern VersionManager version_manager;
extern zmq::socket_t* p_skt_client;
extern CronTrafficObserver *p_hot_traffic_observer;
//extern zmq::socket_t* p_skt_apns_client;

//回复成功失败的信息
int ClientMsgProcessor::ReturnToClient (LYRetCode ret_code)
{
    snd_msg.set_timestamp (time (NULL));
    snd_msg.set_msg_type (LY_RET_CODE);
    snd_msg.set_ret_code (ret_code);
    LOG4CPLUS_DEBUG (logger, "return to client, address: " << address << ", package:\n" << snd_msg.DebugString ());

    string str_msg;
    if (!snd_msg.SerializeToString (&str_msg))
    {
        LOG4CPLUS_ERROR (logger, "Failed to write relevant city traffic.");
        return -1;
    }

    s_sendmore (*p_skt_client, address);
    s_send     (*p_skt_client, str_msg);
    return 0;
}

int ClientMsgProcessor::ReturnToClient (LYCheckin checkin)
{
    snd_msg.set_timestamp (time (NULL));
    snd_msg.set_msg_type (LY_CHECKIN);
    *snd_msg.mutable_checkin() = checkin;
    LOG4CPLUS_DEBUG (logger, "return to client, address: " << address << ", package:\n" << snd_msg.DebugString ());
    string str_msg;
    if (!snd_msg.SerializeToString (&str_msg))
    {
        LOG4CPLUS_ERROR (logger, "Failed to write relevant city traffic.");
        return -1;
    }

    s_sendmore (*p_skt_client, address);
    s_send     (*p_skt_client, str_msg);
    if (snd_msg.has_checkin ())
    {
    	snd_msg.clear_checkin();
    }
    return 0;
}

int ClientMsgProcessor::PreprocessRcvMsg (string& adr, LYMsgOnAir& msg)
{
    //LOG4CPLUS_DEBUG (logger, "preprocess package: \n" <<  msg.DebugString ());
    address = adr;
    rcv_msg = msg;
    InvertMsg (rcv_msg, snd_msg);

    int version;
    int ret_code = 0;
    time_t now = time (NULL);
    time_t ts = msg.timestamp ();
    LYParty from_party = msg.from_party ();
    LYParty to_party = msg.to_party ();

    if (now - ts > CLIENT_REQUEST_TIMEOUT * 60)
    {
        LOG4CPLUS_WARN (logger, "package timeout, timestamp: " << ::ctime(&ts));
        ReturnToClient (LY_TIMEOUT);
        ret_code = -1;
    }

    else if ((version = msg.version ()) != 1)
    {
        LOG4CPLUS_ERROR (logger, "invalid version: " << version);
        ReturnToClient (LY_VERSION_IMCOMPATIBLE);
        ret_code = -1;
    }

    else if (from_party != LY_CLIENT || to_party != LY_TSS)
    {
        LOG4CPLUS_ERROR (logger, "invalid message party, from:: " << from_party << ", to: " << to_party);
        ReturnToClient (LY_PARTY_ERROR);
        ret_code = -1;
    }

    return ret_code;
}

int ClientMsgProcessor::ProcessRcvMsg (string& adr, LYMsgOnAir& msg)
{
    if (PreprocessRcvMsg (adr, msg) != 0) 
    {
        LOG4CPLUS_ERROR (logger, "fail to preprocess package");
        return -1;
    }

    LOG4CPLUS_DEBUG (logger, "process package: \n" <<  rcv_msg.DebugString ());
    LYMsgType msg_type = rcv_msg.msg_type ();
    switch (msg_type)
    {
        case LY_TRAFFIC_SUB:
        {
            ReturnToClient (LY_SUCCESS);

            if(rcv_msg.traffic_sub().pub_type() == LY_PUB_CRON)
            {
                LOG4CPLUS_DEBUG (logger, "LY_PUB_CRON operation type: ");
                cronclientpanorama.SubTraffic(adr, rcv_msg);
            }
            else
            {
                onrouteclientpanorama.SubTraffic (adr, rcv_msg);
            }

            return 0;
        }

        case LY_DEVICE_REPORT:
        {
            ReturnToClient (LY_SUCCESS);
            LYDeviceReport device_report = rcv_msg.device_report ();
            RegisterDevice (db_client, device_report);
            return 0;
        }

        case LY_TRAFFIC_REPORT:
        {
            ReturnToClient (LY_SUCCESS);
            LOG4CPLUS_ERROR (logger, "LY_TRAFFIC_REPORT ");
            break;
        }

        case LY_CHECKIN:
        {
            LYCheckin checkin = rcv_msg.checkin();
            if (version_manager.GetLatestVersion (checkin))
            {
                ReturnToClient (checkin);
            }
            else
            {
                LOG4CPLUS_ERROR (logger, "missing checkin: " << checkin.DebugString());
                ReturnToClient (LY_OTHER_ERROR);
            }

            return 0;
        }

        default:
        {
            ReturnToClient (LY_MSG_TYPE_ERROR);
            LOG4CPLUS_ERROR (logger, "invalid message type: " << msg_type);
            return -1;
        }
    }

    LOG4CPLUS_ERROR (logger, "should not pass here");
    return 0;
}

int TrafficObserver::ReplyToClient ()
{
    if (relevant_traffic->road_traffics_size () == 0 )
    {
        LOG4CPLUS_DEBUG (logger, "no traffic, don't reply to client: " << address);
        return 0;
    }

    snd_msg.set_timestamp (time (NULL));
    LOG4CPLUS_DEBUG (logger, "reply to client, address: " << address << ", package:\n" << snd_msg.DebugString ());
    string str_msg;
    if (!snd_msg.SerializeToString (&str_msg))
    {
        LOG4CPLUS_ERROR (logger, "Failed to write relevant city traffic.");
        return -1;
    }

    s_sendmore (*p_skt_client, address);
    s_send     (*p_skt_client, str_msg);
    return 0;
}

void TrafficObserver::Update (RoadTrafficSubject *sub, bool should_pub)
{
    time_t now = time (NULL);
    time_t ts = sub->GetRoadTraffic().timestamp();
    if (now - ts > ROAD_TRAFFIC_TIMEOUT * 60)
    {
        LOG4CPLUS_INFO (logger, "expired road traffic, road: " << sub->GetRoadTraffic().road() << ", timestamp: " << ::ctime(&ts));
    }
    else
    {
    	LYRoadTraffic road_traffic = sub->GetRoadTraffic();
    	if (road_traffic.segment_traffics_size () != 0)
    	{
            LYRoadTraffic *rdtf = relevant_traffic->add_road_traffics();
            *rdtf = road_traffic;
            LOG4CPLUS_DEBUG (logger, "add road traffic:\n" << road_traffic.DebugString() << " to observer: " << address);
    	}
    }

    if (should_pub)
    {
        ReplyToClient ();
        last_update = now;
        relevant_traffic->clear_road_traffics();
    }

    /* core dump induced, because the following clear will clear *sub
    LOG4CPLUS_DEBUG (logger, "add road traffic: " << sub->GetRoadTraffic().DebugString() << " to observer: " << address);
    RepeatedPtrField<LYRoadTraffic>* rdtf = relevant_traffic->mutable_road_traffics();
    rdtf->AddAllocated (sub->GetRoadTraffic());
    */
}

void TrafficObserver::AttachToTraffic(const string& adr, LYTrafficSub& ts)
{
    address = adr;
    traffic_sub = ts;
    LYRoute route = traffic_sub.route();
    LYTrafficPub* traffic_pub = snd_msg.mutable_traffic_pub();
    traffic_pub->set_route_id (route.identity());
    relevant_traffic = traffic_pub->mutable_city_traffic();
    if (relevant_traffic->road_traffics_size () != 0)
    {
        LOG4CPLUS_WARN (logger, "there exists relevant traffic before register: " << relevant_traffic->DebugString());
        relevant_traffic->clear_road_traffics();
    }

    relevant_traffic->set_city (citytrafficpanorama.GetCityTraffic().city());
    //relevant_traffic->set_timestamp (citytrafficpanorama.GetCityTraffic().timestamp());
    for (int indexk = 0; indexk < route.segments_size (); indexk++)
    {
        const LYSegment& segment = route.segments(indexk);
        string roadname = segment.road();
        LOG4CPLUS_DEBUG (logger, "register road: " << roadname);
        citytrafficpanorama.Attach (this, roadname);
    }
}

void TrafficObserver::Register (const string& adr, LYTrafficSub& ts)
{
    this->AttachToTraffic(adr, ts);

    time_t now = time (NULL);
    LOG4CPLUS_DEBUG (logger, "now: " << ::ctime(&now) << ", last update: " << ::ctime(&last_update));

    ReplyToClient ();
    last_update = now;
    relevant_traffic->clear_road_traffics();

    LYPubType pub_type = traffic_sub.pub_type();
    //LOG4CPLUS_DEBUG (logger, "register pub type: " << pub_type);

    if (pub_type == LY_PUB_ADHOC)
    {
    	Unregister ();
    }
    else if (pub_type == LY_PUB_CRON)
    {
      	//submit the cron request to cron_worker
    	LOG4CPLUS_DEBUG (logger, "can't reach here ");
    }
}

void TrafficObserver::Unregister ()
{
    if (relevant_traffic != NULL)
    {
        LOG4CPLUS_DEBUG (logger, "clear relevant traffic:\n" << relevant_traffic->DebugString());
        relevant_traffic->clear_road_traffics();
    }
    else
    {
        LOG4CPLUS_INFO (logger, "no relevant traffic before ungister");
    }

    LYRoute route = traffic_sub.route();
    for (int indexk = 0; indexk < route.segments_size(); indexk++)
    {
        const LYSegment& segment = route.segments(indexk);
        string roadname = segment.road();
        LOG4CPLUS_DEBUG (logger, "unregister road: " << roadname);
        citytrafficpanorama.Detach (this, roadname);
    }
}

int OnRouteClientPanorama::SubTraffic (string& adr, LYMsgOnAir& pkg)
{
//    CreateSubscription (adr, hot_traffic_sub);
    if (p_hot_traffic_observer)
    {
        LOG4CPLUS_DEBUG (logger, "reply hot traffic");
        CronTrafficObserver hot_traffic_observer(*p_hot_traffic_observer);
        hot_traffic_observer.SetAddress(adr);
        hot_traffic_observer.ReplyToClient();
    }
    else
    {
        LOG4CPLUS_ERROR (logger, "no hot traffic observer");
    }
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
}

void ClientObservers::CreateSubscription (const string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();
//    address = adr;
    LOG4CPLUS_DEBUG (logger, "insert/update subscription, address: " << adr << " identity: " << identity);
    map_route_relevant_traffic [identity].Unregister ();
    map_route_relevant_traffic [identity].Register (adr, traffic_sub);
}

void ClientObservers::DeleteSubscription (const string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();

    map_route_relevant_traffic [identity].Unregister ();
    map_route_relevant_traffic.erase(identity);
    LOG4CPLUS_DEBUG (logger, "delete subscription, address: " << adr << " identity: " << identity);
}

//订阅热点路况，被移往CronOnRouteClientPanorama
void OnRouteClientPanorama::Init ()
{
    hot_traffic_sub.set_version (1);
    hot_traffic_sub.set_from_party (LY_CLIENT);
    hot_traffic_sub.set_to_party (LY_TSS);
    hot_traffic_sub.set_msg_type (LY_TRAFFIC_SUB);
    hot_traffic_sub.set_msg_id (TRAFFIC_PUB_MSG_ID);
    hot_traffic_sub.set_timestamp (time (NULL));
    LYTrafficSub *traffic_sub = hot_traffic_sub.mutable_traffic_sub ();
    traffic_sub->set_city ("深圳");
    traffic_sub->set_opr_type (LYTrafficSub::LY_SUB_CREATE);
    traffic_sub->set_pub_type (LY_PUB_EVENT);
    LYRoute *route = traffic_sub->mutable_route ();
    route->set_identity (EVENT_HOT_TRAFFIC_ROUTE_ID);

    vector<string> vec_hot_road = citytrafficpanorama.GetHotRoad ();
    for (int index = 0; index < vec_hot_road.size(); index++)
    {
    	LYSegment *segment = route->add_segments();
    	segment->set_road(vec_hot_road[index]);
    	segment->mutable_start()->set_lng(0);
    	segment->mutable_start()->set_lat(0);
    	segment->mutable_end()->set_lng(0);
    	segment->mutable_end()->set_lat(0);
    }

    CreateSubscription ("*", hot_traffic_sub); //*表示所有的客户端都订阅
}

// Add if there does not exist, update if there exists.
void OnRouteClientPanorama::CreateSubscription (const string& adr, LYMsgOnAir& pkg)
{
    LOG4CPLUS_DEBUG (logger, "insert/update subscription, address: " );//<< adr);
    map_client_relevant_traffic[adr].CreateSubscription(adr, pkg);
}

void OnRouteClientPanorama::DeleteSubscription (const string& adr, LYMsgOnAir& pkg)
{
    //LOG4CPLUS_DEBUG (logger, "delete subscription, address: " << adr);
    map_client_relevant_traffic[adr].DeleteSubscription(adr,pkg);
}

void VersionManager::Init()
{
    std::ifstream ifs;
    ifs.open (GetCfgFile ("version.cfg"));
    std::stringstream ostr;
    ostr << ifs.rdbuf();
    Json::Reader reader;
    Json::Value jv_version_set;

    bool parsingSuccessful = reader.parse ( ostr.str(), jv_version_set );
    if ( !parsingSuccessful )
    {
        // report to the user the failure and their locations in the document.
        LOG4CPLUS_ERROR (logger, "Failed to parse version configuration\n" \
               << reader.getFormatedErrorMessages());
    }
    else
    {
        LOG4CPLUS_DEBUG (logger, "version:\n" << jv_version_set.toStyledString());
        int version_nbr = jv_version_set.size();

        for ( int indexi = 0; indexi < version_nbr; indexi++ )
        {
            LYCheckin checkin;
            checkin.set_os_type (OsStrToInt(jv_version_set[indexi]["os_type"].asString()));
            checkin.set_os_version(jv_version_set[indexi]["os_version"].asString());
            checkin.set_ly_major_release(jv_version_set[indexi]["version"].asInt());
            checkin.set_ly_minor_release(jv_version_set[indexi]["release"].asInt());
            checkin.set_download_url(jv_version_set[indexi]["download_url"].asString());
            checkin.set_desc(jv_version_set[indexi]["desc"].asString());
            vec_latest_version.push_back(checkin);
        }
    }
}

//return: true: 找到; false: 未找到;
bool VersionManager::GetLatestVersion (LYCheckin& checkin)
{
	vector<LYCheckin>::iterator it;
    for ( it = vec_latest_version.begin(); it != vec_latest_version.end(); it++ )
    {
    	if (checkin.os_type() == it->os_type())
    	{
            checkin.set_ly_major_release(it->ly_major_release());
            checkin.set_ly_minor_release(it->ly_minor_release());
            checkin.set_download_url(it->download_url());
            checkin.set_desc(it->desc());
            return true;
    	}
    }
    return false;
}
