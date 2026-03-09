#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "config/config.h"

namespace energytop {
namespace {

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

TEST(ConfigTest, ResolveConfigPathReturnsCliPathWhenItExists) {
  const auto root = make_temp_dir("energytop_config_resolve_");
  const auto config_path = root / "energytop.ini";
  write_text(config_path, "[Daemon]\n");

  const auto resolved = resolve_config_path(config_path.string());
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, config_path.string());
}

TEST(ConfigTest, LoadDaemonConfigParsesAllConfiguredFields) {
  const auto root = make_temp_dir("energytop_config_parse_");
  const auto config_path = root / "energytop.ini";
  write_text(config_path,
             "[Daemon]\n"
             "daemon_polling_interval_ms = 20\n"
             "zmq_publish_interval_sec = 2\n"
             "zmq_endpoint = ipc:///tmp/energytop-test.ipc\n"
             "\n"
             "[Hardware]\n"
             "sysfs_path_override = /tmp/mock_sysfs\n"
             "invert_current_sign = YES\n"
             "\n"
             "[Storage]\n"
             "csv_output_path = /tmp/energytop.csv\n"
             "csv_max_size_mb = 7\n");

  const auto config = load_daemon_config(config_path.string());
  EXPECT_EQ(config.daemon_polling_interval_ms, 20);
  EXPECT_EQ(config.zmq_publish_interval_sec, 2);
  EXPECT_EQ(config.zmq_endpoint, "ipc:///tmp/energytop-test.ipc");
  EXPECT_EQ(config.sysfs_path_override, "/tmp/mock_sysfs");
  EXPECT_TRUE(config.invert_current_sign);
  EXPECT_EQ(config.csv_output_path, "/tmp/energytop.csv");
  EXPECT_EQ(config.csv_max_size_mb, 7);
  EXPECT_EQ(config.loaded_from, config_path.string());
}

TEST(ConfigTest, LoadDaemonConfigTreatsUnknownBoolAsFalse) {
  const auto root = make_temp_dir("energytop_config_bool_");
  const auto config_path = root / "energytop.ini";
  write_text(config_path,
             "[Hardware]\n"
             "invert_current_sign = definitely-not-true\n");

  const auto config = load_daemon_config(config_path.string());
  EXPECT_FALSE(config.invert_current_sign);
}

TEST(ConfigTest, LoadDaemonConfigThrowsOnInvalidSyntax) {
  const auto root = make_temp_dir("energytop_config_invalid_");
  const auto config_path = root / "energytop.ini";
  write_text(config_path,
             "[Daemon]\n"
             "daemon_polling_interval_ms = 10\n"
             "this_line_has_no_equals\n");

  EXPECT_THROW((void)load_daemon_config(config_path.string()), std::runtime_error);
}

}  // namespace
}  // namespace energytop
