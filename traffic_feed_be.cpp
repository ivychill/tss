//
//

#include "traffic_feed.h"

extern Logger logger;
//extern CityTrafficPanorama citytrafficpanorama;
//extern OnRouteClientPanorama onrouteclientpanorama;
extern CronClientPanorama cronclientpanorama;

void RoadTrafficSubject::Attach(TrafficObserver *obs)
{
    set_observers.insert(obs);
    LOG4CPLUS_DEBUG (logger, "set_observers size: " << set_observers.size());
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

void RoadTrafficSubject::SetRoad (string& road)
{
    road_traffic.Clear();
    road_traffic.set_road(road);
}

void RoadTrafficSubject::SetState (const Json::Value& jv_road)
{
    road_traffic.Clear();
    road_traffic.set_road(jv_road ["rn"].asString());

    LOG4CPLUS_DEBUG (logger, "before set_alias");
    road_traffic.set_alias(jv_road ["alias"].asString());
    LOG4CPLUS_DEBUG (logger, "after set_alias");
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
}

void CityTrafficPanorama::Init ()
{
    city_traffic.set_city (CITY_NAME);

    std::ifstream ifs;
    ifs.open (GetCfgFile ("hot_road.cfg"));
    std::stringstream ostr;
    ostr << ifs.rdbuf();
    Json::Reader reader;
    Json::Value jv_hot_roads;

    bool parsingSuccessful = reader.parse ( ostr.str(), jv_hot_roads );
    if ( parsingSuccessful )
    {
        LOG4CPLUS_DEBUG (logger, "hot road:\n" << jv_hot_roads.toStyledString());
        int hot_road_nbr = jv_hot_roads.size();
        LOG4CPLUS_DEBUG (logger, "hot_road_nbr: " << hot_road_nbr);

        for ( int index = 0; index < hot_road_nbr; index++ )
        {
        	vec_hot_road.push_back(jv_hot_roads[index]["name"].asString());
        }
    }
    else
    {
        // report to the user the failure and their locations in the document.
        LOG4CPLUS_ERROR (logger, "Failed to parse hot_road configuration\n" \
               << reader.getFormatedErrorMessages());
        string hot_road[] = {"南坪快速", "北环大道", "红荔路", "深南东路", "深南中路", "深南大道", "宝安大道", "滨河大道", "滨海大道", \
        		"梅观高速", "皇岗路", "彩田路", "金田路", "新洲路", "香蜜湖路", "福龙路", \
        		"沙河西路", "后海大道", "南海大道", "南山大道", "月亮湾大道", \
        		"南山创业路", "宝安创业路", "留仙大道", "广深公路", "扳雪岗大道", "布龙公路"};

        for (int index = 0; index < sizeof(hot_road)/sizeof(string); index++)
        {
        	vec_hot_road.push_back(hot_road[index]);
        }
    }
}

LYCityTraffic& CityTrafficPanorama::GetCityTraffic ()
{
    return city_traffic;
}


vector<string>& CityTrafficPanorama::GetHotRoad ()
{
	return vec_hot_road;
}

void CityTrafficPanorama::Attach (TrafficObserver *obs, string& road)
{
	map_roadtraffic[road].Attach(obs);
	map_roadtraffic[road].SetRoad(road);
}

void CityTrafficPanorama::Detach (TrafficObserver *obs, string& road)
{
	map_roadtraffic[road].Detach(obs);
}

void CityTrafficPanorama::SetState (const Json::Value& jv_roadset)
{
    //city_traffic.set_city (CITY_NAME);
    int road_traffic_nbr = jv_roadset.size();
    LOG4CPLUS_DEBUG (logger, "road_traffic_nbr: " << road_traffic_nbr);

    for ( int indexi = 0; indexi < road_traffic_nbr; ++indexi )
    {
        Json::Value jv_road = jv_roadset [indexi];
        std::string roadname = jv_road ["rn"].asString();
        map_roadtraffic[roadname].SetState(jv_road);
    }

    //tommy TODO
//    cronclientpanorama.Init();	//由于Attach已经为从未收到路况的路作订阅，重复订阅已不再需要。
}
