#pragma once

#include <memory>
#include <vector>

#include "collectors/collector.h"
#include "config/config.h"

namespace energytop {

class CollectorRegistry {
 public:
  void add(std::unique_ptr<ICollector> collector);
  std::vector<std::optional<BatterySample>> collect_all();

 private:
  std::vector<std::unique_ptr<ICollector>> collectors_;
};

CollectorRegistry create_default_registry(const DaemonConfig& config);

}  // namespace energytop
