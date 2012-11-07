

#include <fstream>
#include "zhelpers.hpp"

int main (int argc, char *argv[])
{
    zmq::context_t context(1);
    zmq::socket_t trafficfilter (context, ZMQ_PUB);
    trafficfilter.setsockopt( ZMQ_IDENTITY, "PROBE", 5);
    trafficfilter.connect("tcp://localhost:6003");

    std::ifstream ifs;
    std::stringstream ostr;
    ifs.open("tss.json");
    ostr << ifs.rdbuf();
    s_send (trafficfilter, ostr.str());
    
    return 0;
}
