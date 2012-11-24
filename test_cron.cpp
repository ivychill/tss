#include "../gtest-1.6.0/include/gtest.h"
#include "traffic_feed.h"

using namespace std;
using namespace tss;
class CronTest: public ::testing::Test
{
protected:
    tss::LYCrontab tab;
    date today;
    tm now;
    int dow;
    int offset;

    void SetUp(){
        today = day_clock::local_day();
        offset = 2;
        now = to_tm(second_clock::local_time());
        dow = 0xff;
    }

    void TearDown(){
        tab.Clear();
    }

};

TEST_F(CronTest, CalcWaitTime)
{
    tab.set_minute(0xffffffffffffffffL);
    tab.set_dow(127);
    tab.set_hour(0xffffffff);
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 0);

    tab.clear_minute();
    long mask = (0x1L << now.tm_min);
    tab.set_minute(mask);
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 0);

    tab.clear_minute();
    mask = (0x1L << (now.tm_min + offset));
    tab.set_minute(mask);
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == offset);

    tab.Clear();
    dow = 0x1 << today.day_of_week();
    tab.set_minute(0x1L << now.tm_min);
    tab.set_hour(0x1 << now.tm_hour);
    tab.set_dow(dow);
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 24 * 0);

    tab.Clear();
    dow = 0x1 << ((today.day_of_week() + offset) % 7);
    tab.set_minute(0x1L << now.tm_min);
    tab.set_hour(0x1 << now.tm_hour);
    tab.set_dow(dow);

//    std::cout<<"CronJob::CalcWaitTime(tab): "<<CronJob::CalcWaitTime(tab)<<endl;
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 24 * 60 * offset);
}

TEST_F(CronTest, CalcWaitTime_dow)
{
    int day_offset = 1;
    dow = 0x1 << ((today.day_of_week() + day_offset) % 7);
    tab.set_minute(0x1L << now.tm_min);

    offset = -1;
    tab.set_hour(0x1 << (now.tm_hour + offset));
    tab.set_dow(dow);

    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 1440 * day_offset + offset*60);
}

TEST_F(CronTest, CalcWaitTime_dom)
{
    int day_offset = 2;
    int today_no = today.day().as_number() - 1;
    int dom = 0x1 << ((today_no + day_offset) % today.end_of_month().day().as_number());
    tab.set_minute(0x1L << now.tm_min);

    offset = 0;
    tab.set_hour(0x1 << (now.tm_hour + offset));
    tab.set_dom(dom);

    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 1440 * day_offset + offset*60);
}

TEST_F(CronTest, CalcWaitTime_dom_next_mon)
{
    int up = today.end_of_month().day().as_number();
    int day_offset = ( up - today.day().as_number() + 15) % up;

    int today_no = today.day().as_number() - 1;

    int dom = 0x1 << ((today_no + day_offset) % today.end_of_month().day().as_number());
    tab.set_minute(0x1L << now.tm_min);

    offset = 0;
    tab.set_hour(0x1 << (now.tm_hour + offset));
    tab.set_dom(dom);

    //cout<<"result :"<< CronJob::CalcWaitTime(tab)<< " expect: " << (1440 * day_offset + offset*60)<<endl;

    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == (1440 * day_offset + offset*60));
}
