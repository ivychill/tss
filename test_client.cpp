#include <fstream>
#include "zhelpers.hpp"
#include "tss.pb.h"
#include "tss_log.h"
#include "tss_helper.h"
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/date_time.hpp>

#define CLT_NBR 1

Logger logger;
int identity = 1;

void PreparePackage (LYMsgOnAir& pkg, LYMsgType mt)
{
    pkg.set_version (1);
    pkg.set_from_party (LY_CLIENT);
    pkg.set_to_party (LY_TSS);
    pkg.set_msg_type (mt);
    pkg.set_msg_id (identity++);
    pkg.set_timestamp (time (NULL));
    pkg.set_snd_id("356409047732840");
}

void SendPackage (zmq::socket_t& skt, LYMsgOnAir& pkg)
{
    LOG4CPLUS_DEBUG (logger, "request:\n" << pkg.DebugString());
    std::string str_pkg;
    if (!pkg.SerializeToString (&str_pkg))
    {
        LOG4CPLUS_ERROR (logger, "fail to serialize to string");
    }
    
    s_send (skt, str_pkg);
    pkg.Clear();
}

void RecvPackage (zmq::socket_t& skt)
{
    LYMsgOnAir pkg;
    std::string reply = s_recv (skt);

    if (!pkg.ParseFromString (reply))
    {
        LOG4CPLUS_ERROR (logger, "fail to parse from string");
    }

    LOG4CPLUS_DEBUG (logger, "reply:\n" << pkg.DebugString());
    pkg.Clear();
}

void TestCheckin (zmq::socket_t& skt)
{
    LYMsgOnAir pkg;
    PreparePackage (pkg, LY_CHECKIN);

    LYCheckin *checkin = pkg.mutable_checkin ();
    checkin->set_device_model ("HTC desire");
    checkin->set_os_type (LY_ANDROID);
    checkin->set_os_version ("2.2");
    checkin->set_ly_major_release (1);
    checkin->set_ly_minor_release (1);

    SendPackage (skt, pkg);
}

void TestDeviceReport (zmq::socket_t& skt)
{
    LYMsgOnAir pkg;
    PreparePackage (pkg, LY_DEVICE_REPORT);
    char hex_token[] = "0efc4c9f9bf8a4f8957619bd9207d0c9651cfc2aef936409053c9e4ac8befa89";
    char byte_token[DEVICE_TOKEN_SIZE];
    ByteDump (byte_token, hex_token, DEVICE_TOKEN_SIZE);
    std::string str_byte_token (byte_token, DEVICE_TOKEN_SIZE);
    
    LYDeviceReport *device_report = pkg.mutable_device_report ();
    device_report->set_device_id ("test");
    device_report->set_device_token (str_byte_token);
    device_report->set_device_name ("chenfeng's iphone");
    device_report->set_device_model ("iphone 4S");
    device_report->set_device_os_version ("iOS 5");

    SendPackage (skt, pkg);
}

void TestRequestRoad (zmq::socket_t& skt, std::string* roadarray)
{
    LYMsgOnAir pkg;
    PreparePackage (pkg, LY_TRAFFIC_SUB);

    LYTrafficSub *traffic_sub = pkg.mutable_traffic_sub ();
    traffic_sub->set_city ("娣卞�");
    traffic_sub->set_opr_type (LYTrafficSub::LY_SUB_CREATE);
     traffic_sub->set_pub_type (LY_PUB_EVENT);
    LYRoute *route = traffic_sub->mutable_route ();
    route->set_identity (1);

    for (int index = 0; index < 2; index++)
    {
        LYSegment *segment = route->add_segments();
        segment->set_road(roadarray[index]);
        segment->mutable_start()->set_lng(120.558957);
        segment->mutable_start()->set_lat(31.325152);
        segment->mutable_end()->set_lng(120.559000);
        segment->mutable_end()->set_lat(31.325000);
    }

    SendPackage (skt, pkg);
}

struct ContextAndArg
{
    zmq::context_t *m_context;
    int m_argc;
    char *m_argv[2];
};

void DoSendRoutine(zmq::context_t* ctxt, char* client_identity)
{
    zmq::socket_t skt_feed (*ctxt, ZMQ_DEALER);

    skt_feed.setsockopt (ZMQ_IDENTITY, client_identity, strlen (client_identity));
    skt_feed.connect ("tcp://localhost:6001");


    //  Initialize poll set
    zmq::pollitem_t items [] = {
        { skt_feed, 0, ZMQ_POLLIN, 0 },
        };

    while(1)
    {
        TestDeviceReport (skt_feed);
        TestCheckin (skt_feed);

        std::string vt_road_hit_all[] = {"南海大道", "南山大道"};
        TestRequestRoad (skt_feed, vt_road_hit_all);

        std::string vt_road_hit_part[] = {"南海大道", "nonexist"};
        TestRequestRoad (skt_feed, vt_road_hit_part);

        std::string vt_road_hit_none[] = {"fake", "mock"};
        TestRequestRoad (skt_feed, vt_road_hit_none);

        boost::this_thread::sleep(boost::posix_time::seconds(1));
        int poll_result = (items [0].revents & ZMQ_POLLIN);

        while (poll_result)
        {
           RecvPackage (skt_feed);
           poll_result = (items [0].revents & ZMQ_POLLIN);
        }

        boost::this_thread::sleep(boost::posix_time::seconds(10));
    }
}

void *worker_routine_ex (void *arg)
{
    ContextAndArg *context_and_arg = (ContextAndArg *) arg;
    zmq::context_t *context = context_and_arg->m_context;;
    //int argc = context_and_arg->m_argc;
    char *argv[2];
    argv[0] = context_and_arg->m_argv[0];
    argv[1] = context_and_arg->m_argv[1];

    char client_identity [64];
    sprintf (client_identity, "%s_%u", argv[1], pthread_self());
    LOG4CPLUS_DEBUG (logger, "client_identity: " << client_identity);

   boost::thread send_routine(boost::bind(DoSendRoutine, context, client_identity));

    //recv_routine.join();
    send_routine.join();

    return (NULL);
}

void *worker_routine (void *arg)
{
    ContextAndArg *context_and_arg = (ContextAndArg *) arg;
    zmq::context_t *context = context_and_arg->m_context;;
    //int argc = context_and_arg->m_argc;
    char *argv[2];
    argv[0] = context_and_arg->m_argv[0];
    argv[1] = context_and_arg->m_argv[1];

    char client_identity [64];
    sprintf (client_identity, "%s_%u", argv[1], pthread_self());
    LOG4CPLUS_DEBUG (logger, "client_identity: " << client_identity);

    zmq::socket_t skt_feed (*context, ZMQ_DEALER);
    skt_feed.setsockopt (ZMQ_IDENTITY, client_identity, strlen (client_identity));
    skt_feed.connect ("tcp://localhost:6001");

    //  Initialize poll set
    zmq::pollitem_t items [] = {
        { skt_feed, 0, ZMQ_POLLIN, 0 },
    };
 
       
    while (1)
    {
        TestDeviceReport (skt_feed);

        std::string vt_road_hit_all[] = {"���涓�矾", "杩����矾"};
        TestRequestRoad (skt_feed, vt_road_hit_all);

        std::string vt_road_hit_part[] = {"杩����矾", "nonexist"};
        TestRequestRoad (skt_feed, vt_road_hit_part);

        std::string vt_road_hit_none[] = {"fake", "mock"};
        TestRequestRoad (skt_feed, vt_road_hit_none);

        while (1)
        {
            zmq::poll (&items [0], 1, -1);
            if (items [0].revents & ZMQ_POLLIN)
            {
                RecvPackage (skt_feed);
            }
        }

        sleep (10);
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

    for (int thread_nbr = 0; thread_nbr != CLT_NBR; thread_nbr++)
    {
        pthread_create (&clt[thread_nbr], NULL, worker_routine_ex, (void *) &context_and_arg);
        //LOG4CPLUS_DEBUG (logger, "precess " << argv[1] << " create thread NO. " << thread_nbr << " ID: " << clt[thread_nbr]);
    }

    for (int thread_nbr = 0; thread_nbr != CLT_NBR; thread_nbr++)
    {
        pthread_join (clt[thread_nbr], NULL);
        LOG4CPLUS_DEBUG (logger, "precess " << argv[1] << " return from thread NO. " << thread_nbr << " ID: " << clt[thread_nbr]);
    }

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
