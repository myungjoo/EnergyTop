#include <gtest/gtest.h>

#include "stats/power_stats.h"

namespace energytop {

TEST(PowerStatsTest, ComputesAggregates) {
  PowerStats stats;

  stats.add_sample({1, 1000000, 4000000});   // 4,000,000 uW
  stats.add_sample({2, -500000, 4000000});   // -2,000,000 uW
  stats.add_sample({3, 250000, 4000000});    // 1,000,000 uW

  const auto snap = stats.snapshot();
  EXPECT_EQ(snap.sample_count, 3u);
  EXPECT_EQ(snap.total_power_uw, 3000000);
  EXPECT_EQ(snap.avg_power_uw, 1000000);
  EXPECT_EQ(snap.min_power_uw, -2000000);
  EXPECT_EQ(snap.max_power_uw, 4000000);
  EXPECT_EQ(snap.total_energy_uj, 500);
}

TEST(PowerStatsTest, ResetClearsState) {
  PowerStats stats;
  stats.add_sample({1, 1000000, 4000000});
  stats.reset();
  const auto snap = stats.snapshot();
  EXPECT_EQ(snap.sample_count, 0u);
  EXPECT_EQ(snap.total_power_uw, 0);
  EXPECT_EQ(snap.avg_power_uw, 0);
  EXPECT_EQ(snap.min_power_uw, 0);
  EXPECT_EQ(snap.max_power_uw, 0);
  EXPECT_EQ(snap.total_energy_uj, 0);
}

TEST(PowerStatsTest, ComputePowerUtility) {
  EXPECT_EQ(compute_power_uw(2000000, 3500000), 7000000);
}

}  // namespace energytop
