#include "stats/power_stats.h"

#include <algorithm>
#include <limits>

namespace energytop {

std::int64_t compute_power_uw(std::int32_t current_ua, std::int32_t voltage_uv) {
  return (static_cast<std::int64_t>(current_ua) * voltage_uv) / 1000000;
}

void PowerStats::add_sample(const Sample& s) {
  const auto power_uw = compute_power_uw(s.current_ua, s.voltage_uv);
  ++sample_count_;
  sum_power_uw_ += power_uw;
  min_power_uw_ = std::min(min_power_uw_, power_uw);
  max_power_uw_ = std::max(max_power_uw_, power_uw);
}

StatsSnapshot PowerStats::snapshot() const {
  StatsSnapshot snap;
  snap.sample_count = sample_count_;
  if (sample_count_ == 0) {
    return snap;
  }
  snap.avg_power_uw = sum_power_uw_ / static_cast<std::int64_t>(sample_count_);
  snap.min_power_uw = min_power_uw_;
  snap.max_power_uw = max_power_uw_;
  snap.total_power_uw = sum_power_uw_;
  return snap;
}

void PowerStats::reset() {
  sample_count_ = 0;
  sum_power_uw_ = 0;
  min_power_uw_ = std::numeric_limits<std::int64_t>::max();
  max_power_uw_ = std::numeric_limits<std::int64_t>::min();
}

}  // namespace energytop
