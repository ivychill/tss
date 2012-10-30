//  Least-recently used (LRU) queue device
//  Clients and workers are shown here in-process
//
// Chen Feng < ivychill@163.com>

#include <queue>
#include "zhelpers.hpp"
#include "tss_log.h"
#include "tss.pb.h"

//#define MAX_MSG_NUM 16

Logger logger;
static const std::string collector("traffic_collector");

int main (int argc, char *argv[])
{
    InitLog (argv[0], logger);

    // Prepare our context and sockets
    zmq::context_t context(1);
    
    zmq::socket_t skt_client (context, ZMQ_ROUTER);
    zmq::socket_t skt_feed (context, ZMQ_ROUTER);
    //int64_t msg_num = MAX_MSG_NUM;
    //skt_client.setsockopt (ZMQ_IDENTITY, &msg_num, sizeof msg_num);
    //skt_feed.setsockopt (ZMQ_IDENTITY, &msg_num, sizeof msg_num);
    
    skt_client.bind("tcp://*:7001");
    skt_feed.bind("tcp://127.0.0.1:7002");

    //  Logic of LRU loop
    //  - Poll skt_feed always, skt_client only if 1+ worker ready
    //  - If worker replies, queue worker as ready and forward reply
    //    to client if necessary
    //  - If client requests, pop next worker and send request to it
    //
    //  A very simple queue structure with known max size
    //  worker_queue is of no use for present.
    //  std::queue<std::string> worker_queue;
    std::string worker_addr;

    //  Initialize poll set
    zmq::pollitem_t items [] = {
        //  Always poll for worker activity on skt_feed
        { skt_feed,  0, ZMQ_POLLIN, 0 },
        //  Poll client only if we have available workers
        { skt_client, 0, ZMQ_POLLIN, 0 },                     
        /* Poll front-end only if we have available workers
        { trafficprobe, 0, ZMQ_POLLIN, 0 },
        Poll front-end only if we have available workers
        { trafficsink, 0, ZMQ_POLLIN, 0 }
        */
    };

    while (1)
    {
        if (worker_addr.size())
        {
            zmq::poll (&items [0], 2, -1);
        }
        else
        {
            zmq::poll (&items [0], 1, -1);
        }

        //  Handle worker activity on skt_feed
        if (items [0].revents & ZMQ_POLLIN)
        {
            //  Queue first frame, i.e. worker address for LRU routing
            //  worker_queue.push(s_recv (skt_feed));
            worker_addr = s_recv (skt_feed);
            LOG4CPLUS_INFO (logger, "worker address: " << worker_addr);

            //  second frame is a client reply address
            std::string client_addr = s_recv (skt_feed);
            LOG4CPLUS_INFO (logger, "client address from worker: " << client_addr); 

            if (client_addr.compare("READY") != 0)
            {               
                std::string reply = s_recv (skt_feed);
                //LOG4CPLUS_DEBUG (logger, "reply from worker: " << reply);
                s_sendmore (skt_client, client_addr);
                s_send     (skt_client, reply);
            }
        }
        
        //  Handle client activity on skt_client
        if (items [1].revents & ZMQ_POLLIN)
        {
        	
            //  Now get next client request, route to LRU worker
            //  Client request is [address] [request]
            std::string client_addr = s_recv (skt_client); 
            LOG4CPLUS_INFO (logger, "address from client: " << client_addr);
 
            std::string request = s_recv (skt_client);
            LOG4CPLUS_DEBUG (logger, "request from client: " << request);
            
            LOG4CPLUS_INFO (logger, "schedule to worker: " << worker_addr);
            
            ::tss::LYMsgOnAir rcv_msg;

            if (!rcv_msg.ParseFromString (request))
            {
                LOG4CPLUS_ERROR (logger, "fail to parse from string");
            }

            if(::tss::LY_TC == rcv_msg.to_party ())
            { 
                LOG4CPLUS_INFO (logger, "send to collector");
				s_sendmore (skt_feed, collector);
				s_sendmore (skt_feed, client_addr);
				s_send     (skt_feed, rcv_msg.SerializeAsString());
            }
            else
            {
            	s_sendmore (skt_feed, worker_addr);
				s_sendmore (skt_feed, client_addr);
				s_send     (skt_feed, request);
            }
        }
    }

    return 0;
}
