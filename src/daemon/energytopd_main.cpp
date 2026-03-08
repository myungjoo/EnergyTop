#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "config/config.h"
#include "daemon/energy_daemon.h"

namespace {
std::atomic<bool> g_keep_running{true};

void handle_signal(int) { g_keep_running.store(false); }
}  // namespace

int main(int argc, char** argv) {
  std::optional<std::string> config_path;
  int duration_sec = -1;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--duration-sec" && i + 1 < argc) {
      duration_sec = std::stoi(argv[++i]);
    } else {
      std::cerr << "Usage: energytopd [--config PATH] [--duration-sec N]\n";
      return 2;
    }
  }

  try {
    auto config = energytop::load_daemon_config(config_path);
    energytop::EnergyDaemon daemon(std::move(config));
    daemon.start();

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (duration_sec > 0) {
      std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
      g_keep_running.store(false);
    }

    while (g_keep_running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    daemon.stop();
  } catch (const std::exception& e) {
    std::cerr << "[energytopd] fatal: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
