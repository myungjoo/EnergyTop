#pragma once

#include <optional>
#include <string>

namespace energytop {

struct DaemonConfig {
  int daemon_polling_interval_ms = 100;
  int zmq_publish_interval_sec = 5;
  std::string zmq_endpoint = "ipc:///data/local/tmp/energytop.ipc";
  std::string sysfs_path_override;
  bool invert_current_sign = false;
  std::string csv_output_path = "/data/local/tmp/energytop_log.csv";
  int csv_max_size_mb = 50;
  std::string loaded_from;
};

std::optional<std::string> resolve_config_path(
    const std::optional<std::string>& cli_path);

DaemonConfig load_daemon_config(const std::optional<std::string>& cli_path);

}  // namespace energytop
