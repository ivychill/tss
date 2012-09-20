//
//

#include "traffic_feed.h"

extern Logger logger;
extern CityTrafficPanorama citytrafficpanorama;
extern OnRouteClientPanorama onrouteclientpanorama;
extern ClientMsgProcessor client_msg_processor;
extern DBClientConnection db_client;
extern zmq::socket_t* p_skt_client;

int ClientMsgProcessor::ReturnToClient ()
{
    snd_msg.set_timestamp (time (NULL));
    LOG4CPLUS_DEBUG (logger, "return to client, address: " << address << ", package:\n" << snd_msg.DebugString ());
    std::string str_msg;
    if (!snd_msg.SerializeToString (&str_msg))
    {
        LOG4CPLUS_ERROR (logger, "Failed to write relevant city traffic.");
        return -1;
    }

    s_sendmore (*p_skt_client, address);
    s_send     (*p_skt_client, str_msg);
    return 0;
}

int ClientMsgProcessor::PreprocessRcvMsg (std::string& adr, LYMsgOnAir& msg)
{
    //LOG4CPLUS_DEBUG (logger, "preprocess package: \n" <<  msg.DebugString ());
    address = adr;
    rcv_msg = msg;
    snd_msg.set_msg_id (msg.msg_id());
    int version;
    int ret_code = 0;
    time_t now = time (NULL);
    time_t ts = msg.timestamp ();
    LYParty from_party = msg.from_party ();
    LYParty to_party = msg.to_party ();

    if (now - ts > CLIENT_REQUEST_TIMEOUT * 60)
    {
        snd_msg.set_ret_code (LY_TIMEOUT);
        LOG4CPLUS_WARN (logger, "package timeout, timestamp: " << ::ctime(&ts));
        ret_code = -1;
    }

    else if ((version = msg.version ()) != 1)
    {
        snd_msg.set_ret_code (LY_VERSION_IMCOMPATIBLE);
        LOG4CPLUS_ERROR (logger, "invalid version: " << version);
        ret_code = -1;
    }

    else if (from_party != LY_CLIENT || to_party != LY_TSS)
    {
        snd_msg.set_ret_code (LY_PARTY_ERROR);
        LOG4CPLUS_ERROR (logger, "invalid message party, from:: " << from_party << ", to: " << to_party);
        ret_code = -1;
    }

    if (ret_code == -1)
    {
        ReturnToClient ();
    }
    return ret_code;
}

int ClientMsgProcessor::ProcessRcvMsg (std::string& adr, LYMsgOnAir& msg)
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
            snd_msg.set_ret_code (LY_SUCCESS);
            ReturnToClient ();
            onrouteclientpanorama.SubTraffic (adr, rcv_msg);
            return 0;
        }

        case LY_DEVICE_REPORT:
        {
            snd_msg.set_ret_code (LY_SUCCESS);
            ReturnToClient ();
            LYDeviceReport device_report = rcv_msg.device_report ();
            RegisterDevice (db_client, device_report);
            return 0;
        }

        case LY_TRAFFIC_REPORT:
            snd_msg.set_ret_code (LY_SUCCESS);
            ReturnToClient ();
            LOG4CPLUS_ERROR (logger, "to be supported message type: " << msg_type);
            return 0;

        default:
            snd_msg.set_ret_code (LY_MSG_TYPE_ERROR);
            ReturnToClient ();
            LOG4CPLUS_ERROR (logger, "invalid message type: " << msg_type);
            return -1;
    }

    LOG4CPLUS_ERROR (logger, "should not pass here");
    return 0;
}

int TrafficObserver::ReplyToClient ()
{
    snd_msg.set_timestamp (time (NULL));
    LOG4CPLUS_DEBUG (logger, "reply to client, address: " << address << ", package:\n" << snd_msg.DebugString ());
    std::string str_msg;
    if (!snd_msg.SerializeToString (&str_msg))
    {
        LOG4CPLUS_ERROR (logger, "Failed to write relevant city traffic.");
        return -1;
    }

    s_sendmore (*p_skt_client, address);
    s_send     (*p_skt_client, str_msg);
    return 0;
}

void TrafficObserver::Update (RoadTrafficSubject *sub)
{
    time_t now = time (NULL);
    time_t ts = sub->GetRoadTraffic().timestamp();
    if (now - ts > ROAD_TRAFFIC_TIMEOUT * 60)
    {
        LOG4CPLUS_INFO (logger, "expired road traffic, road: " << sub->GetRoadTraffic().road() << ", timestamp: " << ::ctime(&ts));
    }
    else
    {
        LYRoadTraffic *rdtf = relevant_traffic->add_road_traffics();
        *rdtf = sub->GetRoadTraffic();
        LOG4CPLUS_DEBUG (logger, "add road traffic:\n" << sub->GetRoadTraffic().DebugString() << " to observer: " << address);
    }

    //LOG4CPLUS_DEBUG (logger, "now: " << now << ", last update: " << last_update);
    //if (now - last_update > UPDATE_INTERVAL) //temmporary
    if (relevant_traffic->road_traffics_size () != 0)
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

void TrafficObserver::Register (LYRoute& drrt)
{
    route = drrt;
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
        const std::string roadname = segment.road();
        LOG4CPLUS_DEBUG (logger, "register road: " << roadname);
        citytrafficpanorama.Attach (this, roadname);
    }
}

void TrafficObserver::Unregister ()
{
    if (relevant_traffic->road_traffics_size () != 0)
    {
        LOG4CPLUS_DEBUG (logger, "clear relevant traffic:\n" << relevant_traffic->DebugString());
        relevant_traffic->clear_road_traffics();
    }
    else
    {
        LOG4CPLUS_INFO (logger, "no relevant traffic before ungister");
    }

    for (int indexk = 0; indexk < route.segments_size(); indexk++)
    {
        const LYSegment& segment = route.segments(indexk);
        const std::string roadname = segment.road();
        LOG4CPLUS_DEBUG (logger, "unregister road: " << roadname);
        citytrafficpanorama.Detach (this, roadname);
    }
}

int OnRouteClientPanorama::SubTraffic (std::string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYTrafficSub::LYOprType opr_type = traffic_sub.opr_type ();
    switch (opr_type)
    {
        case LYTrafficSub::LY_SUB_CREATE:
            CreateSubscription (adr, pkg);
            return 0;

        case LYTrafficSub::LY_SUB_UPDATE:
            UpdateSubscription (adr, pkg);
            return 0;

        case LYTrafficSub::LY_SUB_DELETE:
            DeleteSubscription (adr, pkg);
            return 0;

        default:
            LOG4CPLUS_ERROR (logger, "invalid operation type: " << opr_type);
            return -1; //failure
    }
}

// Add if there does not exist, update if there exists.
void OnRouteClientPanorama::CreateSubscription (const std::string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();
    std::map<std::string, std::map<int, TrafficObserver *> >::iterator it_client;
    it_client = map_client_relevant_traffic.find (adr);
    
    if (it_client == map_client_relevant_traffic.end()) // insert
    {
        TrafficObserver *traffic_observer = new TrafficObserver (adr);
        std::map<int, TrafficObserver *> map_route_relevant_traffic;
        map_route_relevant_traffic [identity] = traffic_observer;
        map_client_relevant_traffic [adr] = map_route_relevant_traffic;
        LOG4CPLUS_DEBUG (logger, "insert subscription of nonexistent client, address: " << adr << " identity: " << identity);
        traffic_observer->Register (route);
    }
    else // update
    {
        std::map<int, TrafficObserver *>::iterator it_route;
        it_route = it_client->second.find (identity);
    
        if (it_route == it_client->second.end()) // insert
        {
            TrafficObserver *traffic_observer = new TrafficObserver (adr);
            it_client->second [identity] = traffic_observer;
            LOG4CPLUS_DEBUG (logger, "insert subscription of existent client, address: " << adr << " identity: " << identity);
            traffic_observer->Register (route);
        }
        else
        {
            LOG4CPLUS_WARN (logger, "create existent subscription of existent client, address: " << adr << " identity: " << identity);
            it_route->second->Unregister ();
            it_route->second->Register (route);
        }
    }
}

// Add if there does not exist, update if there exists.
void OnRouteClientPanorama::UpdateSubscription (const std::string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();
    std::map<std::string, std::map<int, TrafficObserver *> >::iterator it_client;
    it_client = map_client_relevant_traffic.find (adr);
    
    if (it_client == map_client_relevant_traffic.end()) // insert
    {
        TrafficObserver *traffic_observer = new TrafficObserver (adr);
        std::map<int, TrafficObserver *> map_route_relevant_traffic;
        map_route_relevant_traffic [identity] = traffic_observer;
        map_client_relevant_traffic [adr] = map_route_relevant_traffic;
        LOG4CPLUS_WARN (logger, "update subscription of inexistent client, address: " << adr << " identity: " << identity);
        traffic_observer->Register (route);
    }
    else // update
    {
        std::map<int, TrafficObserver *>::iterator it_route;
        it_route = it_client->second.find (identity);
    
        if (it_route == it_client->second.end()) // insert
        {
            TrafficObserver *traffic_observer = new TrafficObserver (adr);
            it_client->second [identity] = traffic_observer;
            LOG4CPLUS_WARN (logger, "update inexistent subscription of existent client, address: " << adr << " identity: " << identity);
            traffic_observer->Register (route);
        }
        else
        {
            LOG4CPLUS_DEBUG (logger, "update existent subscription of existent client, address: " << adr << " identity: " << identity);
            it_route->second->Unregister ();
            it_route->second->Register (route);
        }
    }
}

void OnRouteClientPanorama::DeleteSubscription (const std::string& adr, LYMsgOnAir& pkg)
{
    LYTrafficSub traffic_sub = pkg.traffic_sub ();
    LYRoute route = traffic_sub.route ();
    int identity = route.identity ();
    std::map<std::string, std::map<int, TrafficObserver *> >::iterator it_client;
    it_client = map_client_relevant_traffic.find (adr);
    
    if (it_client == map_client_relevant_traffic.end())
    {
        LOG4CPLUS_WARN (logger, "delete subscription of inexistent client, address: " << adr << " identity: " << identity);
    }
    else // update
    {
        std::map<int, TrafficObserver *>::iterator it_route;
        it_route = it_client->second.find (identity);
    
        if (it_route == it_client->second.end()) // insert
        {
            LOG4CPLUS_WARN (logger, "delete inexistent subscription of existent client, address: " << adr << " identity: " << identity);
        }
        else
        {
            it_route->second->Unregister ();
            delete it_route->second;
            it_client->second.erase (it_route);
            LOG4CPLUS_DEBUG (logger, "delete existent subscription of existent client, address: " << adr << " identity: " << identity);
        }
    }
}
