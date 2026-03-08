#pragma once

#include <optional>
#include <string>

#include "collectors/collector.h"

namespace energytop {

class BatteryCollector : public ICollector {
 public:
  BatteryCollector(std::string sysfs_path, bool invert_current_sign);
  ~BatteryCollector() override;

  std::optional<BatterySample> collect() override;
  std::optional<BatterySample> collect_battery_sample();
  std::string name() const override;

 private:
  std::int64_t read_sysfs_int64(int fd) const;

  std::string sysfs_path_;
  bool invert_current_sign_;
  int current_fd_ = -1;
  int voltage_fd_ = -1;
};

}  // namespace energytop
