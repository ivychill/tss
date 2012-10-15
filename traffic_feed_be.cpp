//
//

#include "traffic_feed.h"

extern Logger logger;
extern CityTrafficPanorama citytrafficpanorama;
extern OnRouteClientPanorama onrouteclientpanorama;

int JsonStringToJsonValue (const std::string& str_input, Json::Value& jv_output)
{  
    //LOG4CPLUS_DEBUG (logger, "string input: " << str_input);
    Json::Reader reader;
    bool parsingSuccessful = reader.parse( str_input, jv_output );
    LOG4CPLUS_DEBUG (logger, "json output:\n" << jv_output.toStyledString());
    if ( !parsingSuccessful )
    {
        // report to the user the failure and their locations in the document.
        LOG4CPLUS_ERROR (logger, "Failed to parse configuration\n" \
               << reader.getFormatedErrorMessages());
        return -1;
    }

    return 0;  
}

// Input: time string expressed in local time. The time format from probe is like: 2012-06-14T09:39:49+08:00.
// Output/return: time_t baed on GMT, i.e. UTC.
// Notice: Since mktime interprets the contents of the tm structure pointed by timeptr as a calendar time expressed in local time, timezone information is ignored
int TimeStrToInt(const std::string& str_time)
{
    int nYear, nMonth, nDay, nHour, nMinute, nSecond, nDifHour, nDifMinute;
    time_t rawtime;
    struct tm *timeinfo;

    sscanf(str_time.c_str(), "%d-%d-%dT%d:%d:%d%d:%d", &nYear, &nMonth, &nDay, &nHour, &nMinute, &nSecond, &nDifHour, &nDifMinute);
    time ( &rawtime );
    timeinfo = ::localtime ( &rawtime );
    timeinfo->tm_year = nYear - 1900;
    timeinfo->tm_mon = nMonth - 1;
    timeinfo->tm_mday = nDay;
    timeinfo->tm_hour = nHour;
    timeinfo->tm_min = nMinute;
    timeinfo->tm_sec = nSecond;

    //LOG4CPLUS_DEBUG (logger, "time string: " << str_time);
    //LOG4CPLUS_DEBUG (logger, "hour: " << timeinfo->tm_hour << ", minute: " << timeinfo->tm_min);

    return mktime (timeinfo);
}

LYDirection DirectionStrToInt(const std::string& str_direction)
{
    size_t ifound;
    LYDirection idirection = LY_UNKNOWN; 
    if ((ifound = str_direction.find ("东")) != std::string::npos)
    {
        idirection = LY_EAST;
    }
    else if ((ifound = str_direction.find ("北")) != std::string::npos)
    {
        idirection = LY_NORTH;
    }
    else if ((ifound = str_direction.find ("西")) != std::string::npos)
    {
        idirection = LY_WEST;
    }
    else if ((ifound = str_direction.find ("南")) != std::string::npos)
    {
        idirection = LY_SOUTH;
    }
    else
    {
        idirection = LY_UNKNOWN;
    }

    LOG4CPLUS_DEBUG (logger, "direction string: " << str_direction << ", direction: " << idirection);
    return idirection;
}

void RoadTrafficSubject::Attach(TrafficObserver *obs)
{
    set_observers.insert(obs);
    Notify (obs);
}

void RoadTrafficSubject::Detach(TrafficObserver *obs)
{
    set_observers.erase(obs);
}

void RoadTrafficSubject::Notify ()
{
    std::set<TrafficObserver *>::iterator it;
    for ( it = set_observers.begin(); it != set_observers.end(); it++ )
    {
        (*it)->Update(this, true);
    }
}

void RoadTrafficSubject::Notify (TrafficObserver *obs)
{
    obs->Update(this, false);
}

int RoadTrafficSubject::SetState(const Json::Value& jv_road)
{
    road_traffic.Clear();
    road_traffic.set_road(jv_road ["rn"].asString());
    //int timestamp = TimeStrToInt(jv_road["ts"].asString());
    int timestamp = atoi (jv_road["ts_in_sec"].asString().c_str());
    road_traffic.set_timestamp (timestamp);
    road_traffic.set_href(jv_road["rid"].asString());
    Json::Value jv_segmentset = jv_road["segments"]; 

    int segment_nbr = jv_segmentset.size();
    LOG4CPLUS_DEBUG (logger, "road segment number: " << segment_nbr);
    for ( int indexj = 0; indexj < segment_nbr; ++indexj ) 
    {
        LYSegmentTraffic *segment_traffic = road_traffic.add_segment_traffics();
        segment_traffic->mutable_segment()->mutable_start()->set_lng(atof(jv_segmentset[indexj]["s_lng"].asString().c_str()));                     
        segment_traffic->mutable_segment()->mutable_start()->set_lat(atof(jv_segmentset[indexj]["s_lat"].asString().c_str()));
        segment_traffic->mutable_segment()->mutable_end()->set_lng(atof(jv_segmentset[indexj]["e_lng"].asString().c_str()));
        segment_traffic->mutable_segment()->mutable_end()->set_lat(atof(jv_segmentset[indexj]["e_lat"].asString().c_str()));

        segment_traffic->set_timestamp(timestamp);
        LYDirection direction = DirectionStrToInt(jv_segmentset[indexj]["dir"].asString());
        segment_traffic->set_direction(direction);

        segment_traffic->set_speed(atoi(jv_segmentset[indexj]["spd"].asString().c_str()));
        segment_traffic->set_details(jv_segmentset[indexj]["desc"].asString());
    }

    LOG4CPLUS_DEBUG (logger, "set road traffic:\n " << road_traffic.DebugString());

    Notify ();
    return 0;
}

CityTrafficPanorama::CityTrafficPanorama ()
{
    city_traffic.set_city (CITY_NAME);
}

LYCityTraffic& CityTrafficPanorama::GetCityTraffic ()
{
    return city_traffic;
}

void CityTrafficPanorama::Attach (TrafficObserver *obs, const std::string& road)
{
    std::map<std::string, RoadTrafficSubject *>::iterator it;
    it = map_roadtraffic.find (road);
    if (it != map_roadtraffic.end())
    {
        LOG4CPLUS_DEBUG (logger, "Attach road: " << it->first);
        it->second->Attach(obs);
    }
    else
    {
        LOG4CPLUS_INFO (logger, "No such road: " << road << ", unnecessary to attach");
    }
}

void CityTrafficPanorama::Detach (TrafficObserver *obs, const std::string& road)
{
    std::map<std::string, RoadTrafficSubject *>::iterator it;
    it = map_roadtraffic.find (road);
    if (it != map_roadtraffic.end())
    {
        it->second->Detach(obs);
    }
    else
    {
        LOG4CPLUS_INFO (logger, "No such road: " << road << ", unnecessary to detach");
    }
}

int CityTrafficPanorama::SetState (const Json::Value& jv_roadset)
{
    //city_traffic.set_city (CITY_NAME);
    int road_traffic_nbr = jv_roadset.size();
    LOG4CPLUS_DEBUG (logger, "road_traffic_nbr: " << road_traffic_nbr);

    for ( int indexi = 0; indexi < road_traffic_nbr; ++indexi )
    {
        Json::Value jv_road = jv_roadset [indexi];
        std::string roadname = jv_road ["rn"].asString();

        std::map<std::string, RoadTrafficSubject *>::iterator it;
        it = map_roadtraffic.find (roadname);
        if (it == map_roadtraffic.end()) // insert
        {
            RoadTrafficSubject* rdtfsub = new RoadTrafficSubject;
            map_roadtraffic.insert (std::pair <std::string, RoadTrafficSubject *>(roadname, rdtfsub));
            rdtfsub->SetState(jv_road);
        }
        else // update
        {
            LOG4CPLUS_DEBUG (logger, "there exists traffic subject for road: " << roadname);
            it->second->SetState(jv_road);
        }
    }
    return 0;
}
