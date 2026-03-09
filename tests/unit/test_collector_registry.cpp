#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "collectors/collector_registry.h"

namespace energytop {
namespace {

class FakeCollector : public ICollector {
 public:
  explicit FakeCollector(std::optional<BatterySample> sample)
      : sample_(std::move(sample)) {}

  std::optional<BatterySample> collect() override { return sample_; }
  std::string name() const override { return "fake"; }

 private:
  std::optional<BatterySample> sample_;
};

std::filesystem::path make_temp_dir(const std::string& prefix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir =
      std::filesystem::temp_directory_path() / (prefix + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << text;
  out.flush();
}

TEST(CollectorRegistryTest, CollectAllPreservesOrderAndOptionality) {
  CollectorRegistry registry;
  registry.add(std::make_unique<FakeCollector>(BatterySample {123, 456}));
  registry.add(std::make_unique<FakeCollector>(std::nullopt));
  registry.add(std::make_unique<FakeCollector>(BatterySample {-7, 890}));

  const auto samples = registry.collect_all();
  ASSERT_EQ(samples.size(), 3u);
  ASSERT_TRUE(samples[0].has_value());
  EXPECT_EQ(samples[0]->current_ua, 123);
  EXPECT_EQ(samples[0]->voltage_uv, 456);
  EXPECT_FALSE(samples[1].has_value());
  ASSERT_TRUE(samples[2].has_value());
  EXPECT_EQ(samples[2]->current_ua, -7);
  EXPECT_EQ(samples[2]->voltage_uv, 890);
}

TEST(CollectorRegistryTest, DefaultRegistryUsesOverrideAndSignInversion) {
  const auto root = make_temp_dir("energytop_registry_default_");
  write_text(root / "current_now", "321\n");
  write_text(root / "voltage_now", "4100000\n");

  DaemonConfig config;
  config.sysfs_path_override = root.string();
  config.invert_current_sign = true;

  auto registry = create_default_registry(config);
  const auto samples = registry.collect_all();
  ASSERT_EQ(samples.size(), 1u);
  ASSERT_TRUE(samples[0].has_value());
  EXPECT_EQ(samples[0]->current_ua, -321);
  EXPECT_EQ(samples[0]->voltage_uv, 4100000);
}

}  // namespace
}  // namespace energytop
