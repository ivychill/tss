#include "../gtest-1.6.0/include/gtest.h"
#include "traffic_feed.h"

using namespace std;
using namespace tss;
 class CronTest : public ::testing::Test {
 	protected:
     CronJob job();
 };

TEST_F(CronTest, CalcWaitTime) {
    tss::LYCrontab tab;
    date today(day_clock::local_day());

    tm now = to_tm(second_clock::local_time());
    int dow;
    int offset = 2;

    tab.set_minute(0xffffffffffffffffL);
    tab.set_dow(127);
    tab.set_hour(0xffffffff);
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 0);

    tab.clear_minute();
    long mask = (0x1L << now.tm_min );
    tab.set_minute( mask );
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 0);

    tab.clear_minute();
    mask = (0x1L << (now.tm_min + offset));
    tab.set_minute( mask );
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == offset);

    tab.Clear();
    dow = 0x1 << today.day_of_week();
    tab.set_minute(0x1L << now.tm_min);
    tab.set_hour(0x1 << now.tm_hour);
    tab.set_dow(dow);
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 24 * 0);

    tab.Clear();
    dow = 0x1 << ((today.day_of_week()+offset) % 7);
    tab.set_minute(0x1L << now.tm_min);
    tab.set_hour(0x1 << now.tm_hour);
    tab.set_dow(dow);

//    std::cout<<"CronJob::CalcWaitTime(tab): "<<CronJob::CalcWaitTime(tab)<<endl;
    ASSERT_TRUE(CronJob::CalcWaitTime(tab) == 24 * 60 * offset);

}

