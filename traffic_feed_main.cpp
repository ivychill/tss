//
//

#include "traffic_feed.h"
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

Logger logger;
CityTrafficPanorama citytrafficpanorama;
OnRouteClientPanorama onrouteclientpanorama;
CronClientPanorama cronclientpanorama;
ClientMsgProcessor client_msg_processor;
DBClientConnection db_client;
VersionManager version_manager;
zmq::socket_t* p_skt_client;

zmq::socket_t* p_skt_cron_client;
zmq::socket_t* p_skt_apns_client;
CronSchelder* p_cron_sched;

int main (int argc, char *argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    //这个要放在以下几个初始化的第一个，因为后面几个都要记日志。
    InitLog (argv[0], logger);
    InitDB (db_client);
    //这两个顺序不能颠倒，因为先有热点路况，后有订阅。
    citytrafficpanorama.Init();
    onrouteclientpanorama.Init();
    version_manager.Init();

    // setup zeromq
    zmq::context_t context(1);
    zmq::socket_t skt_client (context, ZMQ_DEALER);
    skt_client.setsockopt (ZMQ_IDENTITY, "WORKER", 6);
    skt_client.connect ("tcp://localhost:6002");
    p_skt_client = &skt_client;

    zmq::socket_t skt_probe (context, ZMQ_SUB);
    skt_probe.setsockopt (ZMQ_SUBSCRIBE, "", 0);		//Subscribe on everything
    skt_probe.setsockopt (ZMQ_IDENTITY, "SINK", 5);
    skt_probe.connect ("tcp://localhost:6004");
    
    // cron
    CronSchelder cron_sched(context);
    cron_sched.Init();
    p_cron_sched = &cron_sched;

    // TODO
    cronclientpanorama.Init();

	zmq::socket_t cron_client (context, ZMQ_PAIR);
	cron_client.connect("ipc://cron_schelder.ipc");
    p_skt_cron_client = &cron_client;

	zmq::socket_t apns_client (context, ZMQ_PAIR);
	apns_client.connect("ipc://apns.ipc");
    p_skt_apns_client = &apns_client;

    // Tell traffic_router we're ready for work
    s_send (skt_client, "READY");

    // no citytraffic ,  need wait 5 min to do the init
//    cronclientpanorama.Init();

    while (1)
    {

        //  Initialize poll set
        zmq::pollitem_t items [] = {
            { skt_probe, 0, ZMQ_POLLIN, 0 },
            // to be improved, Poll filter only if we have available sink
            { skt_client,  0, ZMQ_POLLIN, 0 },
            { cron_client, 0, ZMQ_POLLIN, 0}
        };
        
        // to be improved, poll skt_client only in presence of skt_probe;
        zmq::poll (&items [0], 3, -1);
                
        //  Handle pub activity on skt_probe
        if (items [0].revents & ZMQ_POLLIN)
        {
            std::string str_citytraffic = s_recv (skt_probe);
            if (!str_citytraffic.empty())
            {
                Json::Value jv_city;
                if (JsonStringToJsonValue (str_citytraffic, jv_city) == 0)
                {
                    citytrafficpanorama.SetState (jv_city);
                    /*
                    time_t now = time (NULL);
                    if (now - last_pub > PUB_INTERVAL)
                    {
                        onrouteclientpanorama.Publicate (skt_client);
                        last_pub = now;
                    }
                    */
                }
                else
                {
                    LOG4CPLUS_ERROR (logger, "fail to parse json");
                }
        
            }
            else
            {
                LOG4CPLUS_WARN (logger, "receive empty city traffic");
            }
        }

        //  Handle activity on client's request for relevant traffic
        if (items [1].revents & ZMQ_POLLIN)
        {
            //  Get Client ID
            std::string address = s_recv (skt_client);
            std::string request = s_recv (skt_client);
            
            //LOG4CPLUS_ERROR (logger, "receive address: " << address);
            //LOG4CPLUS_ERROR (logger, "receive request: " << request);

            LYMsgOnAir rcv_msg;

            if (!rcv_msg.ParseFromString (request))
            {
                LOG4CPLUS_ERROR (logger, "fail to parse from string");
            }
            else if (client_msg_processor.ProcessRcvMsg (address, rcv_msg) != 0)
            {
                LOG4CPLUS_ERROR (logger, "fail to process package");
            }
        }

        //cron sched info
        if (items [2].revents & ZMQ_POLLIN)
        {
        	LOG4CPLUS_INFO (logger, "feed_main receive cron msg: ");
        	std::string dev_token = s_recv(cron_client);
        	std::string routeid = s_recv(cron_client);
        	cronclientpanorama.ProcSchedInfo(dev_token, routeid);
        }
    }
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
