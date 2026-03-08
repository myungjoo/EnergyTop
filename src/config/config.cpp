#include "config/config.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cctype>

#include <ini.h>

namespace fs = std::filesystem;

namespace energytop {

namespace {

constexpr const char* kDefaultEtcConfigPath = "/etc/energytop.ini";
constexpr const char* kDefaultTmpConfigPath = "/data/local/tmp/energytop.ini";

bool parse_bool(const std::string& value) {
  const std::string lowered = [&value]() {
    std::string s;
    s.reserve(value.size());
    for (char c : value) {
      s.push_back(static_cast<char>(std::tolower(c)));
    }
    return s;
  }();
  return lowered == "1" || lowered == "true" || lowered == "yes" ||
         lowered == "on";
}

int inih_handler(void* user, const char* section, const char* name,
                 const char* value) {
  auto* config = static_cast<DaemonConfig*>(user);
  const std::string s(section ? section : "");
  const std::string n(name ? name : "");
  const std::string v(value ? value : "");

  if (s == "Daemon" && n == "daemon_polling_interval_ms") {
    config->daemon_polling_interval_ms = std::stoi(v);
    return 1;
  }
  if (s == "Daemon" && n == "zmq_publish_interval_sec") {
    config->zmq_publish_interval_sec = std::stoi(v);
    return 1;
  }
  if (s == "Daemon" && n == "zmq_endpoint") {
    config->zmq_endpoint = v;
    return 1;
  }
  if (s == "Hardware" && n == "sysfs_path_override") {
    config->sysfs_path_override = v;
    return 1;
  }
  if (s == "Hardware" && n == "invert_current_sign") {
    config->invert_current_sign = parse_bool(v);
    return 1;
  }
  if (s == "Storage" && n == "csv_output_path") {
    config->csv_output_path = v;
    return 1;
  }
  if (s == "Storage" && n == "csv_max_size_mb") {
    config->csv_max_size_mb = std::stoi(v);
    return 1;
  }
  return 1;
}

}  // namespace

std::optional<std::string> resolve_config_path(
    const std::optional<std::string>& cli_path) {
  if (cli_path.has_value() && fs::exists(*cli_path)) {
    return cli_path;
  }
  if (fs::exists(kDefaultEtcConfigPath)) {
    return std::string(kDefaultEtcConfigPath);
  }
  if (fs::exists(kDefaultTmpConfigPath)) {
    return std::string(kDefaultTmpConfigPath);
  }
  return std::nullopt;
}

DaemonConfig load_daemon_config(const std::optional<std::string>& cli_path) {
  DaemonConfig config;
  const auto resolved = resolve_config_path(cli_path);
  if (!resolved.has_value()) {
    std::cerr << "[energytopd] config file not found. Using defaults.\n";
    return config;
  }

  const int rc = ini_parse(resolved->c_str(), inih_handler, &config);
  if (rc < 0) {
    throw std::runtime_error("failed to read config file: " + *resolved);
  }
  if (rc > 0) {
    throw std::runtime_error("invalid config syntax at line " +
                             std::to_string(rc) + " in " + *resolved);
  }
  config.loaded_from = *resolved;
  return config;
}

}  // namespace energytop
