#include "../gtest-1.6.0/include/gtest.h"
#include "../gtest-1.6.0/include/tap.h"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  testing::TestEventListeners& listeners = testing::UnitTest::GetInstance()->listeners();
  delete listeners.Release(listeners.default_result_printer());
  listeners.Append(new tap::TapListener());

  return RUN_ALL_TESTS();
}
