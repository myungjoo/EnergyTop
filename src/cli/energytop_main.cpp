#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

#include "config/config.h"
#include "power_record.h"
#include "stats/power_stats.h"

namespace {
std::atomic<bool> g_keep_running{true};

void handle_signal(int) { g_keep_running.store(false); }
}  // namespace

int main(int argc, char** argv) {
  std::optional<std::string> config_path;
  bool once = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--once") {
      once = true;
    } else {
      std::cerr << "Usage: energytop [--config PATH] [--once]\n";
      return 2;
    }
  }

  try {
    const auto config = energytop::load_daemon_config(config_path);
    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.set(zmq::sockopt::subscribe, "");
    sub.set(zmq::sockopt::rcvtimeo, 200);
    sub.connect(config.zmq_endpoint);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    energytop::PowerStats stats;
    energytop::PowerRecord latest {};

    while (g_keep_running.load()) {
      zmq::message_t msg;
      if (!sub.recv(msg, zmq::recv_flags::none)) {
        continue;
      }

      if (msg.size() % sizeof(energytop::PowerRecord) != 0) {
        std::cerr << "[energytop] received malformed message\n";
        continue;
      }

      const std::size_t count = msg.size() / sizeof(energytop::PowerRecord);
      std::vector<energytop::PowerRecord> records(count);
      std::memcpy(records.data(), msg.data(), msg.size());

      for (const auto& r : records) {
        latest = r;
        stats.add_sample({r.timestamp_real_ms, r.current_ua, r.voltage_uv});
      }

      const auto snap = stats.snapshot();
      std::cout << "\033[2J\033[H";
      std::cout << "EnergyTop Monitor\n";
      std::cout << "samples      : " << snap.sample_count << "\n";
      std::cout << "latest current(uA): " << latest.current_ua << "\n";
      std::cout << "latest voltage(uV): " << latest.voltage_uv << "\n";
      std::cout << "avg power(uW): " << snap.avg_power_uw << "\n";
      std::cout << "min/max(uW)  : " << snap.min_power_uw << " / "
                << snap.max_power_uw << "\n";
      std::cout.flush();

      if (once) {
        break;
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[energytop] fatal: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
