#include <filesystem>
#include <stdexcept>

#include <gtest/gtest.h>

#include "collectors/battery_collector.h"
#include "test_helpers.h"

namespace energytop {

TEST(BatteryCollectorSystemTest, ReadsFromMockSysfsNodes) {
  const auto root = test::make_temp_dir("energytop_mock_sysfs_");
  test::write_text(root / "current_now", "12345\n");
  test::write_text(root / "voltage_now", "4000000\n");

  BatteryCollector collector(root.string(), false);
  const auto sample = collector.collect_battery_sample();
  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(sample->current_ua, 12345);
  EXPECT_EQ(sample->voltage_uv, 4000000);
}

TEST(BatteryCollectorSystemTest, AppliesCurrentSignInversion) {
  const auto root = test::make_temp_dir("energytop_mock_sysfs_sign_");
  test::write_text(root / "current_now", "555\n");
  test::write_text(root / "voltage_now", "3700000\n");

  BatteryCollector collector(root.string(), true);
  const auto sample = collector.collect();
  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(sample->current_ua, -555);
  EXPECT_EQ(sample->voltage_uv, 3700000);
}

TEST(BatteryCollectorSystemTest, ThrowsWhenNodeMissing) {
  const auto root = test::make_temp_dir("energytop_mock_sysfs_missing_");
  test::write_text(root / "voltage_now", "3700000\n");
  EXPECT_THROW((void)BatteryCollector(root.string(), false), std::runtime_error);
}

}  // namespace energytop
