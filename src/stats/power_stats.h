#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace energytop {

struct Sample {
  std::uint64_t timestamp_real_ms = 0;
  std::int32_t current_ua = 0;
  std::int32_t voltage_uv = 0;
};

struct StatsSnapshot {
  std::size_t sample_count = 0;
  std::int64_t avg_power_uw = 0;
  std::int64_t min_power_uw = 0;
  std::int64_t max_power_uw = 0;
  std::int64_t total_power_uw = 0;
};

std::int64_t compute_power_uw(std::int32_t current_ua, std::int32_t voltage_uv);

class PowerStats {
 public:
  void add_sample(const Sample& s);
  StatsSnapshot snapshot() const;
  void reset();

 private:
  std::size_t sample_count_ = 0;
  std::int64_t sum_power_uw_ = 0;
  std::int64_t min_power_uw_ = std::numeric_limits<std::int64_t>::max();
  std::int64_t max_power_uw_ = std::numeric_limits<std::int64_t>::min();
};

}  // namespace energytop
