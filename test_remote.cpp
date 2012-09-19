//  Basic request-reply client using REQ socket
//

#include <fstream>
//#include <pthread.h>
#include "zhelpers.hpp"
#include "tss.pb.h"
#include "tss_log.h"

#define CLT_NBR 320
Logger logger;

void TestRequestRoad (zmq::socket_t& skt, std::string* roadarray)
{
    tss::DrivingRoute drivingroute;
    std::string sdrivingroute;
        
    drivingroute.mutable_start()->set_lon(120.558957);
    drivingroute.mutable_start()->set_lat(31.325152);
    
    for (int index = 0; index < 2; index++)
    {
        tss::DrivingRoute::RoadAndPoint *segment = drivingroute.add_segment();
        segment->set_road(roadarray[index]);
        segment->mutable_end()->set_lon(120.559000);
        segment->mutable_end()->set_lat(31.325000);
    }

    LOG4CPLUS_DEBUG (logger, "driving route: " << drivingroute.DebugString());
    if (!drivingroute.SerializeToString (&sdrivingroute))
    {
        LOG4CPLUS_ERROR (logger, "fail to serialize to string");
    }
    
    s_send (skt, sdrivingroute);
    std::string reply = s_recv (skt);

    tss::CityTraffic citytraffic;
    if (!citytraffic.ParseFromString (reply))
    {
        LOG4CPLUS_ERROR (logger, "fail to parse from string");
    }

    LOG4CPLUS_DEBUG (logger, "reply: " << citytraffic.DebugString());
}

struct ContextAndArg
{
    zmq::context_t *m_context;
    int m_argc;
    char *m_argv[2];
};

void *worker_routine (void *arg)
{
    ContextAndArg *context_and_arg = (ContextAndArg *) arg;
    zmq::context_t *context = context_and_arg->m_context;;
    int argc = context_and_arg->m_argc;
    char *argv[2];
    argv[0] = context_and_arg->m_argv[0];
    argv[1] = context_and_arg->m_argv[1];

    char client_identity [64];
    sprintf (client_identity, "%s_%u", argv[1], pthread_self());
    LOG4CPLUS_DEBUG (logger, "client_identity: " << client_identity);

    zmq::socket_t client (*context, ZMQ_DEALER);
    client.setsockopt (ZMQ_IDENTITY, client_identity, strlen (client_identity));
    client.connect ("tcp://42.121.18.140:7001");

    while (1)
    {
        std::string vt_road_hit_all[] = {"shennan", "nanhai"};
        TestRequestRoad (client, vt_road_hit_all);

        std::string vt_road_hit_part[] = {"shennan", "nonexist"};
        TestRequestRoad (client, vt_road_hit_part);

        std::string vt_road_hit_none[] = {"fake", "mock"};
        TestRequestRoad (client, vt_road_hit_none);

        sleep (60);
    }

    return (NULL);
}

int main (int argc, char *argv[])
{
    InitLog (argv[0], logger);
    zmq::context_t context(1);
    ContextAndArg context_and_arg;

    if (argc < 2)
    {
        std::cout << "usage: test_client identity" << std::endl;
        return -1;
    }

    context_and_arg.m_context = &context;
    context_and_arg.m_argc = argc;
    context_and_arg.m_argv[0] = argv[0];
    context_and_arg.m_argv[1] = argv[1];

    //  prepare protocol buffer
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    pthread_t clt[CLT_NBR];

    //  Launch pool of worker threads
    for (int thread_nbr = 0; thread_nbr != CLT_NBR; thread_nbr++)
    {
        pthread_create (&clt[thread_nbr], NULL, worker_routine, (void *) &context_and_arg);
        LOG4CPLUS_DEBUG (logger, "precess " << argv[1] << " create thread NO. " << thread_nbr << " ID: " << clt[thread_nbr]);
    }

    for (int thread_nbr = 0; thread_nbr != CLT_NBR; thread_nbr++)
    {
        pthread_join (clt[thread_nbr], NULL);
        LOG4CPLUS_DEBUG (logger, "precess " << argv[1] << " return from thread NO. " << thread_nbr << " ID: " << clt[thread_nbr]);
    }

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
