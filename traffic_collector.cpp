#include "zhelpers.hpp"
#include "tss_log.h"
#include "tss_helper.h"
#include "tss.pb.h"


static const std::string collector("traffic_collector");
static const string db_traffic_rpt("roadclouding_production.traffic_rpt");

Logger logger;
using namespace std;

class TrafficReportCollector{
private:
    mongo::DBClientConnection db;
    zmq::socket_t skt_feed;

    void ProcTrafficReport(const string& adr, const LYTrafficReport& report);

public:
    TrafficReportCollector(zmq::context_t& ctxt):skt_feed(ctxt, ZMQ_DEALER){};
    void Init();
    void Run();

//    ~TrafficReportCollector(){
//    }

};

void TrafficReportCollector::Init()
{
    skt_feed.setsockopt (ZMQ_IDENTITY, collector.c_str(), collector.size());
    skt_feed.connect("tcp://localhost:6002");

    try
    {
      db.connect("localhost");
      db.ensureIndex(db_traffic_rpt, BSON ("dev_token" << 1), false); // not unique
      //LOG4CPLUS_DEBUG (logger, "succeed to connect to mongod");
    }
    catch( DBException &e )
    {
      LOG4CPLUS_ERROR (logger, "fail to connect to mongod, caught " << e.what());
    }
}

void TrafficReportCollector::ProcTrafficReport(const string& adr, const LYTrafficReport& report)
{
    char hex_token [DEVICE_TOKEN_SIZE * 2];
    HexDump (hex_token, adr.c_str(), DEVICE_TOKEN_SIZE);
    std::string s_hex_token (hex_token, DEVICE_TOKEN_SIZE * 2);

//    LOG4CPLUS_INFO(logger, "recv a report");

   for(int i = 0; i < report.points_size(); i++)
   {
       mongo::BSONObjBuilder query;
       query << mongo::GENOID;
       query << "dev_token" << s_hex_token;

       query.append("timestamp", boost::lexical_cast<string>(report.points(i).timestamp()));

//       query.appendNumber("timestamp", (long long)report.points(i).timestamp());
       query.append("lng", report.points(i).sp_coordinate().lng());
       query.append("lat", report.points(i).sp_coordinate().lat());

       if(report.points(i).has_altitude())
       {
           query.append("altitude", report.points(i).altitude());
       }

       if(report.points(i).has_course())
       {
           query.append("course", report.points(i).course());
       }

       db.insert(db_traffic_rpt, query.obj());
   }
}

void TrafficReportCollector::Run()
{
    zmq::pollitem_t items[] = {
            {skt_feed,  0, ZMQ_POLLIN, 0 },
    };

    while (true)
    {
        zmq::poll (&items [0], 1, -1);

        if (items [0].revents & ZMQ_POLLIN)
        {
            const std::string address = s_recv (skt_feed);
            std::string request = s_recv (skt_feed);

//            LOG4CPLUS_ERROR (logger, "receive address: " << address);
//            LOG4CPLUS_ERROR (logger, "receive request: " << request);

            LYMsgOnAir rcv_msg;

            if (!rcv_msg.ParseFromString (request))
            {
                LOG4CPLUS_ERROR (logger, "parse fail: " << request.length());
            }
            else
            {
                if(rcv_msg.msg_type() != LY_TRAFFIC_REPORT)
                {
                    LOG4CPLUS_ERROR (logger, "wrong msg type: " << rcv_msg.msg_type());
                }

                ProcTrafficReport(address, rcv_msg.traffic_report());
            }
        }
    }
}

int main (int argc, char *argv[])
{
    InitLog (argv[0], logger);

    LOG4CPLUS_INFO (logger, "collector init...");

    zmq::context_t context(1);
    TrafficReportCollector collectors(context);
    collectors.Init();
    collectors.Run();

    return 0;
}
