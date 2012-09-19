//
//

#include "traffic_feed.h"

Logger logger;
CityTrafficPanorama citytrafficpanorama;
OnRouteClientPanorama onrouteclientpanorama;
ClientMsgProcessor client_msg_processor;
DBClientConnection db_client;
zmq::socket_t* p_skt_client;

int main (int argc, char *argv[])
{
    InitLog (argv[0], logger);
    InitDB (db_client);

    // setup zeromq
    zmq::context_t context(1);
    zmq::socket_t skt_client (context, ZMQ_DEALER);
    skt_client.setsockopt (ZMQ_IDENTITY, "WORKER", 6);
    skt_client.connect ("tcp://localhost:7002");
    p_skt_client = &skt_client;

    zmq::socket_t skt_probe (context, ZMQ_SUB);
    skt_probe.setsockopt (ZMQ_SUBSCRIBE, "", 0);		//Subscribe on everything
    skt_probe.setsockopt (ZMQ_IDENTITY, "SINK", 5);
    skt_probe.connect ("tcp://localhost:7004");
    
    // Tell traffic_router we're ready for work
    s_send (skt_client, "READY");

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    while (1)
    {

        //  Initialize poll set
        zmq::pollitem_t items [] = {
            { skt_probe, 0, ZMQ_POLLIN, 0 },
            // to be improved, Poll filter only if we have available sink
            { skt_client,  0, ZMQ_POLLIN, 0 }
        };
        
        // to be improved, poll skt_client only in presence of skt_probe;
        zmq::poll (&items [0], 2, -1);
                
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
    }
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
