

#include "zhelpers.hpp"
//#define MAX_MSG_NUM 64

int main (int argc, char *argv[])
{
    zmq::context_t context(1);
    zmq::socket_t skt_probe (context, ZMQ_SUB);
    skt_probe.setsockopt(ZMQ_SUBSCRIBE, "", 0);	//Subscribe on everything
    skt_probe.bind("tcp://*:7003");
    zmq::socket_t skt_feed (context, ZMQ_PUB);
    //int64_t msg_num = MAX_MSG_NUM;
    //skt_feed.setsockopt (ZMQ_IDENTITY, &msg_num, sizeof msg_num);
    skt_feed.bind("tcp://*:7004");   
    
    //  Start built-in device
    zmq::device (ZMQ_FORWARDER, skt_probe, skt_feed);
    
    /*
        if (items [2].revents & ZMQ_POLLIN) {
            while (1)
            {
                zmq::message_t message;
                int64_t more;
                size_t more_size = sizeof (more);

                //  Process all parts of the message
                skt_probe.recv(&message);
                skt_probe.getsockopt( ZMQ_RCVMORE, &more, &more_size);
                skt_feed.send(message, more? ZMQ_SNDMORE: 0);
                if (!more)
                {
                    break;      //  Last message part
                }
            }
        }
    */
    
    return 0; 
}
