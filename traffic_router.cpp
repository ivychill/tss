//  Least-recently used (LRU) queue device
//  Clients and workers are shown here in-process
//
// Chen Feng < ivychill@163.com>

#include <map>
#include "zhelpers.hpp"
#include "tss_log.h"
#include "tss.pb.h"
#include <set>
using namespace std;
using namespace tss;
#define CHECK_ACTIVE_INTERVAL 3600*24	//每天检查一次
#define ACTIVE_TIMEOUT 3600*24*7		//一周上过线即为活跃用户
#define SHARED_SECRET "LYUN"

//#define MAX_MSG_NUM 16

struct RouteSession
{
	string address;		//zmq_id，是由zmq随机分配的，同一个客户端重新连接后会发生变化
	int    last_update;
};

Logger logger;
static const std::string collector("traffic_collector");
set<string> set_address; //V1客户端，参数为zmq_id
map<string, RouteSession> route_adapter;	//V2客户端。第一个参数：snd_id，即device_id

/*
int genCheckSum(string payload)
{
	if (payload == NULL || payload.length == 0) {
		return 0;
	}
	string checkSum = SHARED_SECRET;
	int howManyWords = payload.length/4;
	int remainder = payload.length % 4;

	for (int i=0; i<howManyWords; i++) {
		for (int j=0; j<4; j++) {
			checkSum[j] = checkSum[j] ^ payload[i*4+j];
		}
	}
	if (remainder != 0) {
		int j;
		for (j=0; j<remainder; j++) {
			checkSum[j] = checkSum[j] ^ payload[howManyWords*4+j];
		}
		for (; j<4; j++) {
			checkSum[j] = (checkSum[j]) ^ (char)0;
		}
	}

	int value = 0;
	int mask = 0xff;
	for (int j=0; j<4; j++) {
		value <<= 8;
		int tmpValue = checkSum[j] & mask;
		value = value | tmpValue;
	}
	return value;
}
*/

int main (int argc, char *argv[])
{
    InitLog (argv[0], logger);
    time_t last_check = time (NULL);

    // Prepare our context and sockets
    zmq::context_t context(1);
    
    zmq::socket_t skt_client (context, ZMQ_ROUTER);
    zmq::socket_t skt_feed (context, ZMQ_ROUTER);
    //int64_t msg_num = MAX_MSG_NUM;
    //skt_client.setsockopt (ZMQ_IDENTITY, &msg_num, sizeof msg_num);
    //skt_feed.setsockopt (ZMQ_IDENTITY, &msg_num, sizeof msg_num);
    
    skt_client.bind("tcp://*:6001");
    skt_feed.bind("tcp://127.0.0.1:6002");

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
            	if (client_addr.compare("*") == 0)	//广播，用于热点路况
            	{
            		LOG4CPLUS_INFO (logger, "hotroad broadcast");
            		std::string reply = s_recv (skt_feed);

            		map<string, RouteSession>::iterator itv2;
            	    for ( itv2 = route_adapter.begin(); itv2 != route_adapter.end(); itv2++ )
            	    {
                		s_sendmore (skt_client, itv2->second.address);
            			LOG4CPLUS_INFO (logger, "broadcast for V2 client, rcv_id: " << itv2->first << ", zmq_id: " << itv2->second.address);
                		s_send     (skt_client, reply);
            	    }

            	    set<string>::iterator itv1;
            	    for ( itv1 = set_address.begin(); itv1 != set_address.end(); itv1++ )
            	    {
            	    	s_sendmore (skt_client, *itv1);
            			LOG4CPLUS_INFO (logger, "broadcast for V1 client, address: " << *itv1);
            	    	s_send     (skt_client, reply);
            	    }
            	}

            	else
            	{
            		std::string reply = s_recv (skt_feed);
            		std::string to_address = client_addr;

            		//V2:如果能在route_adapter中找到，说明是新版本的客户端，则把地址从snd_id转为zmq_id。否则是旧版本客户端，不作转换
            		map<string, RouteSession>::iterator it;
            		it = route_adapter.find (client_addr);
            		if (it != route_adapter.end())
            		{
            			to_address = it -> second.address;
            			LOG4CPLUS_INFO (logger, "reply for V2 client, rcv_id: " << client_addr << ", zmq_id: " << to_address);
            		}
            		else
            		{
            			LOG4CPLUS_INFO (logger, "reply for V1 client, address: " << client_addr);
            		}

            		//LOG4CPLUS_DEBUG (logger, "reply from worker: " << reply);
            		s_sendmore (skt_client, to_address);
            		s_send     (skt_client, reply);
            	}
            }
        }
        
        //  Handle client activity on skt_client
        if (items [1].revents & ZMQ_POLLIN)
        {
            //  Now get next client request, route to LRU worker
            //  Client request is [address] [request]
            std::string client_addr = s_recv (skt_client);
            LOG4CPLUS_INFO (logger, "address from client: " << client_addr);
            std::string from_addr = client_addr;
 
            std::string request = s_recv (skt_client);
//            LOG4CPLUS_DEBUG (logger, "request from client: " << request);
//            LOG4CPLUS_INFO (logger, "schedule to worker: " << worker_addr);
            
            LYMsgOnAir rcv_msg;

            if (!rcv_msg.ParseFromString (request))
            {
                LOG4CPLUS_ERROR (logger, "fail to parse from string");
            }

            else
            {
            	//由于客户端设置zmq_id有问题，因此客户端不设固定的zmq_id。新的寻址方案以snd_id，即device_id为关键字。因此要把zmq_id转为snd_id发给feed。
            	if (rcv_msg.has_snd_id ())
            	{
            		string snd_id = rcv_msg.snd_id ();
            		route_adapter[snd_id].address = client_addr;
            		from_addr = snd_id;
            		time_t now = time (NULL);
            		route_adapter[snd_id].last_update = now;
            		LOG4CPLUS_INFO (logger, "request from V2 client, snd_id: " << snd_id << ", address: " << client_addr << ", timestamp: " << ::ctime(&now));

            		if (now >= last_check + CHECK_ACTIVE_INTERVAL)
            		{
            			last_check = now;
            			std::map<string, RouteSession>::iterator it;
            			for ( it = route_adapter.begin(); it != route_adapter.end(); it++ )
            			{
            				if ( now >= it->second.last_update + ACTIVE_TIMEOUT)
            				{
//            					route_adapter.erase (it);	//非活跃用户剔除。如果没有用户上报路况，不一定合适。
            					LOG4CPLUS_INFO (logger, "inactive client: " << it->first);
            				}
            			}
            		}
            		LOG4CPLUS_INFO (logger, "active V2 client number: " << route_adapter.size ());
            	}

            	else
            	{
            		LOG4CPLUS_INFO (logger, "request from  V1 client, address: " << client_addr);
            		set_address.insert(client_addr);
            		LOG4CPLUS_INFO (logger, "active V1 client number: " << set_address.size ());
            	}

            	if (::tss::LY_TC == rcv_msg.to_party ())
                {
                    LOG4CPLUS_INFO (logger, "send to collector");
                    s_sendmore (skt_feed, collector);
                    s_sendmore (skt_feed, from_addr);
                    s_send     (skt_feed, request);
                }
                else if (::tss::LY_TSS == rcv_msg.to_party ())
                {
                    s_sendmore (skt_feed, worker_addr);
                    s_sendmore (skt_feed, from_addr);
                    s_send     (skt_feed, request);
                }
                else
                {
                	LOG4CPLUS_INFO (logger, "unknown to_party");
                }
            }
        }
    }
    return 0;
}
