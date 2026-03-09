#include "stats/power_stats.h"

#include <algorithm>
#include <limits>

namespace energytop {

namespace {

constexpr std::int64_t kPowerScale = 1000000;
constexpr std::int64_t kEnergyTrapezoidDenominator = 2000000000;  // 2 * 1e9
constexpr std::uint64_t kNanosPerMillisecond = 1000000;

std::int64_t round_divide_i64(std::int64_t numer, std::int64_t denom) {
  if (numer >= 0) {
    return (numer + (denom / 2)) / denom;
  }
  return (numer - (denom / 2)) / denom;
}

__int128 round_divide_i128(__int128 numer, std::int64_t denom) {
  const __int128 half = static_cast<__int128>(denom / 2);
  if (numer >= 0) {
    return (numer + half) / denom;
  }
  return (numer - half) / denom;
}

std::int64_t clamp_i128_to_i64(__int128 value) {
  const __int128 max = static_cast<__int128>(std::numeric_limits<std::int64_t>::max());
  const __int128 min = static_cast<__int128>(std::numeric_limits<std::int64_t>::min());
  if (value > max) {
    return std::numeric_limits<std::int64_t>::max();
  }
  if (value < min) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return static_cast<std::int64_t>(value);
}

}  // namespace

std::int64_t compute_power_uw(std::int32_t current_ua, std::int32_t voltage_uv) {
  const auto numer = static_cast<std::int64_t>(current_ua) * voltage_uv;
  return round_divide_i64(numer, kPowerScale);
}

void PowerStats::add_sample(const Sample& s) {
  const auto power_uw = compute_power_uw(s.current_ua, s.voltage_uv);

  if (has_prev_sample_) {
    std::uint64_t delta_ns = 0;
    if (s.timestamp_boot_ns > prev_timestamp_boot_ns_ && prev_timestamp_boot_ns_ > 0) {
      delta_ns = s.timestamp_boot_ns - prev_timestamp_boot_ns_;
    } else if (s.timestamp_real_ms > prev_timestamp_real_ms_) {
      delta_ns = (s.timestamp_real_ms - prev_timestamp_real_ms_) *
                 kNanosPerMillisecond;
    }

    if (delta_ns > 0) {
      const __int128 trapezoid_power_sum =
          static_cast<__int128>(prev_power_uw_) + static_cast<__int128>(power_uw);
      // Trapezoidal integration: ((uW + uW) * ns) / (2 * 1e9) => uJ
      total_energy_numer_uwns_ += trapezoid_power_sum * delta_ns;
    }
  }

  ++sample_count_;
  sum_power_uw_ += power_uw;
  min_power_uw_ = std::min(min_power_uw_, power_uw);
  max_power_uw_ = std::max(max_power_uw_, power_uw);
  has_prev_sample_ = true;
  prev_timestamp_real_ms_ = s.timestamp_real_ms;
  prev_timestamp_boot_ns_ = s.timestamp_boot_ns;
  prev_power_uw_ = power_uw;
}

StatsSnapshot PowerStats::snapshot() const {
  StatsSnapshot snap;
  snap.sample_count = sample_count_;
  if (sample_count_ == 0) {
    return snap;
  }
  snap.avg_power_uw =
      round_divide_i64(sum_power_uw_, static_cast<std::int64_t>(sample_count_));
  snap.min_power_uw = min_power_uw_;
  snap.max_power_uw = max_power_uw_;
  snap.total_power_uw = sum_power_uw_;
  snap.total_energy_uj = clamp_i128_to_i64(
      round_divide_i128(total_energy_numer_uwns_, kEnergyTrapezoidDenominator));
  return snap;
}

void PowerStats::reset() {
  sample_count_ = 0;
  sum_power_uw_ = 0;
  min_power_uw_ = std::numeric_limits<std::int64_t>::max();
  max_power_uw_ = std::numeric_limits<std::int64_t>::min();
  total_energy_numer_uwns_ = 0;
  has_prev_sample_ = false;
  prev_timestamp_real_ms_ = 0;
  prev_timestamp_boot_ns_ = 0;
  prev_power_uw_ = 0;
}

}  // namespace energytop
