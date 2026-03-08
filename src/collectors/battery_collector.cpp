#include "collectors/battery_collector.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace energytop {

namespace {

int open_sysfs_fd(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    throw std::runtime_error("failed to open " + path + ": " +
                             std::strerror(errno));
  }
  return fd;
}

}  // namespace

BatteryCollector::BatteryCollector(std::string sysfs_path,
                                   bool invert_current_sign)
    : sysfs_path_(std::move(sysfs_path)),
      invert_current_sign_(invert_current_sign),
      current_fd_(open_sysfs_fd(sysfs_path_ + "/current_now")),
      voltage_fd_(open_sysfs_fd(sysfs_path_ + "/voltage_now")) {}

BatteryCollector::~BatteryCollector() {
  if (current_fd_ >= 0) {
    ::close(current_fd_);
  }
  if (voltage_fd_ >= 0) {
    ::close(voltage_fd_);
  }
}

std::optional<BatterySample> BatteryCollector::collect() {
  return collect_battery_sample();
}

std::optional<BatterySample> BatteryCollector::collect_battery_sample() {
  BatterySample sample;
  sample.current_ua = static_cast<std::int32_t>(read_sysfs_int64(current_fd_));
  sample.voltage_uv = static_cast<std::int32_t>(read_sysfs_int64(voltage_fd_));
  if (invert_current_sign_) {
    sample.current_ua *= -1;
  }
  return sample;
}

std::string BatteryCollector::name() const { return "battery"; }

std::int64_t BatteryCollector::read_sysfs_int64(int fd) const {
  if (::lseek(fd, 0, SEEK_SET) < 0) {
    throw std::runtime_error("lseek failed: " + std::string(std::strerror(errno)));
  }

  char buffer[64] = {};
  const ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
  if (n < 0) {
    throw std::runtime_error("read failed: " + std::string(std::strerror(errno)));
  }
  buffer[n] = '\0';

  std::size_t consumed = 0;
  const auto parsed = std::stoll(buffer, &consumed, 10);
  (void)consumed;
  return parsed;
}

}  // namespace energytop
