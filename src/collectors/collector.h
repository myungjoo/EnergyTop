#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace energytop {

struct BatterySample {
  std::int32_t current_ua = 0;
  std::int32_t voltage_uv = 0;
};

class ICollector {
 public:
  virtual ~ICollector() = default;
  virtual std::optional<BatterySample> collect() = 0;
  virtual std::string name() const = 0;
};

}  // namespace energytop
