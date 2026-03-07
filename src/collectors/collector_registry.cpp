#include "collectors/collector_registry.h"

#include "collectors/battery_collector.h"

namespace energytop {

void CollectorRegistry::add(std::unique_ptr<ICollector> collector) {
  collectors_.push_back(std::move(collector));
}

std::vector<std::optional<BatterySample>> CollectorRegistry::collect_all() {
  std::vector<std::optional<BatterySample>> out;
  out.reserve(collectors_.size());
  for (const auto& collector : collectors_) {
    out.push_back(collector->collect());
  }
  return out;
}

CollectorRegistry create_default_registry(const DaemonConfig& config) {
  CollectorRegistry registry;
  const std::string sysfs_base =
      config.sysfs_path_override.empty() ? "/sys/class/power_supply/battery"
                                         : config.sysfs_path_override;
  registry.add(
      std::make_unique<BatteryCollector>(sysfs_base, config.invert_current_sign));
  return registry;
}

}  // namespace energytop
