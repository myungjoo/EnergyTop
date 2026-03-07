#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <zmq.hpp>

#include "config/config.h"
#include "daemon/energy_daemon.h"
#include "power_record.h"
#include "test_helpers.h"

namespace energytop {

TEST(DaemonSystemTest, PublishesBatchAndWritesCsvUsingMockSysfs) {
  const auto root = test::make_temp_dir("energytop_daemon_e2e_");
  const auto sysfs = root / "sysfs";
  std::filesystem::create_directories(sysfs);
  test::write_text(sysfs / "current_now", "1000000\n");
  test::write_text(sysfs / "voltage_now", "4000000\n");

  const auto socket_path = root / "energytop.ipc";
  const auto csv_path = root / "energytop_log.csv";

  DaemonConfig config;
  config.daemon_polling_interval_ms = 20;
  config.zmq_publish_interval_sec = 1;
  config.zmq_endpoint = "ipc://" + socket_path.string();
  config.sysfs_path_override = sysfs.string();
  config.invert_current_sign = false;
  config.csv_output_path = csv_path.string();
  config.csv_max_size_mb = 10;

  EnergyDaemon daemon(config);
  daemon.start();

  zmq::context_t ctx(1);
  zmq::socket_t sub(ctx, zmq::socket_type::sub);
  sub.set(zmq::sockopt::subscribe, "");
  sub.set(zmq::sockopt::rcvtimeo, 1500);
  sub.connect(config.zmq_endpoint);

  bool received = false;
  zmq::message_t payload;
  for (int i = 0; i < 5; ++i) {
    if (sub.recv(payload, zmq::recv_flags::none)) {
      received = true;
      break;
    }
  }

  ASSERT_TRUE(received);
  ASSERT_GT(payload.size(), 0u);
  ASSERT_EQ(payload.size() % sizeof(PowerRecord), 0u);

  const std::size_t n = payload.size() / sizeof(PowerRecord);
  std::vector<PowerRecord> records(n);
  std::memcpy(records.data(), payload.data(), payload.size());
  EXPECT_GT(records.size(), 0u);
  EXPECT_EQ(records.back().voltage_uv, 4000000);

  daemon.stop();

  ASSERT_TRUE(std::filesystem::exists(csv_path));
  std::ifstream in(csv_path);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines.front(), "timestamp_boot_ns,timestamp_real_ms,current_ua,voltage_uv");
}

}  // namespace energytop
