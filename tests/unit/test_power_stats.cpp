#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "stats/power_stats.h"

namespace energytop {

namespace {

std::int64_t legacy_compute_power_uw(std::int32_t current_ua, std::int32_t voltage_uv) {
  return (static_cast<std::int64_t>(current_ua) * voltage_uv) / 1000000;
}

std::int64_t legacy_compute_average_uw(const std::vector<Sample>& samples) {
  if (samples.empty()) {
    return 0;
  }
  std::int64_t sum = 0;
  for (const auto& s : samples) {
    sum += legacy_compute_power_uw(s.current_ua, s.voltage_uv);
  }
  return sum / static_cast<std::int64_t>(samples.size());
}

std::int64_t legacy_compute_energy_uj(const std::vector<Sample>& samples) {
  if (samples.size() < 2) {
    return 0;
  }

  std::int64_t total_energy_uj = 0;
  std::int64_t prev_power_uw =
      legacy_compute_power_uw(samples.front().current_ua, samples.front().voltage_uv);
  std::uint64_t prev_real_ms = samples.front().timestamp_real_ms;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const auto& s = samples[i];
    const auto power_uw = legacy_compute_power_uw(s.current_ua, s.voltage_uv);
    if (s.timestamp_real_ms > prev_real_ms) {
      const auto delta_ms =
          static_cast<std::int64_t>(s.timestamp_real_ms - prev_real_ms);
      total_energy_uj += ((prev_power_uw + power_uw) * delta_ms) / 2000;
    }
    prev_real_ms = s.timestamp_real_ms;
    prev_power_uw = power_uw;
  }
  return total_energy_uj;
}

long double exact_average_uw(const std::vector<Sample>& samples) {
  if (samples.empty()) {
    return 0.0L;
  }
  long double sum = 0.0L;
  for (const auto& s : samples) {
    sum += static_cast<long double>(s.current_ua) *
           static_cast<long double>(s.voltage_uv) / 1000000.0L;
  }
  return sum / static_cast<long double>(samples.size());
}

long double exact_energy_uj_from_boot_ns(const std::vector<Sample>& samples) {
  if (samples.size() < 2) {
    return 0.0L;
  }
  long double total = 0.0L;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const auto& prev = samples[i - 1];
    const auto& cur = samples[i];
    if (cur.timestamp_boot_ns <= prev.timestamp_boot_ns) {
      continue;
    }

    const auto prev_power_uw =
        static_cast<long double>(prev.current_ua) *
        static_cast<long double>(prev.voltage_uv) / 1000000.0L;
    const auto cur_power_uw =
        static_cast<long double>(cur.current_ua) *
        static_cast<long double>(cur.voltage_uv) / 1000000.0L;
    const auto delta_ns =
        static_cast<long double>(cur.timestamp_boot_ns - prev.timestamp_boot_ns);
    total += ((prev_power_uw + cur_power_uw) * 0.5L) * (delta_ns / 1000000000.0L);
  }
  return total;
}

std::vector<Sample> make_precision_stress_samples() {
  std::vector<Sample> samples;
  samples.reserve(4001);

  std::uint64_t real_ms = 1000;
  std::uint64_t boot_ns = 1000000000ULL;
  for (std::size_t i = 0; i <= 4000; ++i) {
    const std::int32_t current_ua = (i % 3 == 0) ? 1 : ((i % 3 == 1) ? 2 : -1);
    const std::int32_t voltage_uv = (i % 5 == 0) ? 1500000 : 1499999;
    samples.push_back({real_ms, current_ua, voltage_uv, boot_ns});

    // 0.5ms 간격: legacy(ms) 기준으로는 절반 구간이 사라져 누적 오차가 커짐.
    boot_ns += 500000ULL;
    if (i % 2 == 1) {
      ++real_ms;
    }
  }
  return samples;
}

}  // namespace

TEST(PowerStatsTest, ComputesAggregates) {
  PowerStats stats;

  stats.add_sample({1, 1000000, 4000000});   // 4,000,000 uW
  stats.add_sample({2, -500000, 4000000});   // -2,000,000 uW
  stats.add_sample({3, 250000, 4000000});    // 1,000,000 uW

  const auto snap = stats.snapshot();
  EXPECT_EQ(snap.sample_count, 3u);
  EXPECT_EQ(snap.total_power_uw, 3000000);
  EXPECT_EQ(snap.avg_power_uw, 1000000);
  EXPECT_EQ(snap.min_power_uw, -2000000);
  EXPECT_EQ(snap.max_power_uw, 4000000);
  EXPECT_EQ(snap.total_energy_uj, 500);
}

TEST(PowerStatsTest, ResetClearsState) {
  PowerStats stats;
  stats.add_sample({1, 1000000, 4000000});
  stats.reset();
  const auto snap = stats.snapshot();
  EXPECT_EQ(snap.sample_count, 0u);
  EXPECT_EQ(snap.total_power_uw, 0);
  EXPECT_EQ(snap.avg_power_uw, 0);
  EXPECT_EQ(snap.min_power_uw, 0);
  EXPECT_EQ(snap.max_power_uw, 0);
  EXPECT_EQ(snap.total_energy_uj, 0);
}

TEST(PowerStatsTest, ComputePowerUtility) {
  EXPECT_EQ(compute_power_uw(2000000, 3500000), 7000000);
}

TEST(PowerStatsTest, ComputePowerRoundsToNearest) {
  EXPECT_EQ(compute_power_uw(1, 1500000), 2);
  EXPECT_EQ(compute_power_uw(-1, 1500000), -2);
  EXPECT_EQ(compute_power_uw(1, 1499999), 1);
  EXPECT_EQ(compute_power_uw(-1, 1499999), -1);
}

TEST(PowerStatsTest, AverageRoundsToNearest) {
  PowerStats positive;
  positive.add_sample({1, 1, 1000000});  // 1 uW
  positive.add_sample({2, 2, 1000000});  // 2 uW
  EXPECT_EQ(positive.snapshot().avg_power_uw, 2);

  PowerStats negative;
  negative.add_sample({1, -1, 1000000});  // -1 uW
  negative.add_sample({2, -2, 1000000});  // -2 uW
  EXPECT_EQ(negative.snapshot().avg_power_uw, -2);
}

TEST(PowerStatsTest, EnergyAccumulatesSubUnitIntervalsWithoutDrift) {
  PowerStats stats;
  for (std::uint64_t t = 0; t <= 2000; ++t) {
    stats.add_sample({t, 1, 1000000});  // 1 uW
  }
  EXPECT_EQ(stats.snapshot().total_energy_uj, 2);
}

TEST(PowerStatsTest, UsesBootTimestampForIntegrationWhenRealtimeGoesBackwards) {
  PowerStats stats;
  stats.add_sample({1000, 1000000, 1000000, 1000000000});  // 1 W
  stats.add_sample({900, 1000000, 1000000, 2000000000});   // realtime regresses
  EXPECT_EQ(stats.snapshot().total_energy_uj, 1000000);
}

TEST(PowerStatsTest, ReducesAverageErrorComparedToLegacyTruncation) {
  const auto samples = make_precision_stress_samples();
  const auto exact_avg = exact_average_uw(samples);

  PowerStats stats;
  for (const auto& s : samples) {
    stats.add_sample(s);
  }
  const auto new_avg = stats.snapshot().avg_power_uw;
  const auto legacy_avg = legacy_compute_average_uw(samples);

  const auto new_err = std::fabs(static_cast<long double>(new_avg) - exact_avg);
  const auto legacy_err =
      std::fabs(static_cast<long double>(legacy_avg) - exact_avg);

  EXPECT_LE(new_err, legacy_err);
}

TEST(PowerStatsTest, ReducesEnergyErrorComparedToLegacyTruncation) {
  const auto samples = make_precision_stress_samples();
  const auto exact_energy = exact_energy_uj_from_boot_ns(samples);

  PowerStats stats;
  for (const auto& s : samples) {
    stats.add_sample(s);
  }
  const auto new_energy = stats.snapshot().total_energy_uj;
  const auto legacy_energy = legacy_compute_energy_uj(samples);

  const auto new_err =
      std::fabs(static_cast<long double>(new_energy) - exact_energy);
  const auto legacy_err =
      std::fabs(static_cast<long double>(legacy_energy) - exact_energy);

  EXPECT_LT(new_err, legacy_err);
  EXPECT_GE((legacy_err - new_err), 1.0L);
}

TEST(PowerStatsTest, EliminatesAccumulatedTruncationInLowPowerConstantLoad) {
  std::vector<Sample> samples;
  samples.reserve(2001);
  for (std::uint64_t t = 0; t <= 2000; ++t) {
    samples.push_back({t, 1, 1000000, t * 1000000ULL});  // 1uW, 1ms step
  }

  PowerStats stats;
  for (const auto& s : samples) {
    stats.add_sample(s);
  }

  const auto exact_energy = exact_energy_uj_from_boot_ns(samples);
  const auto new_energy = stats.snapshot().total_energy_uj;
  const auto legacy_energy = legacy_compute_energy_uj(samples);

  const auto new_err =
      std::fabs(static_cast<long double>(new_energy) - exact_energy);
  const auto legacy_err =
      std::fabs(static_cast<long double>(legacy_energy) - exact_energy);

  EXPECT_EQ(new_energy, 2);
  EXPECT_EQ(legacy_energy, 0);
  EXPECT_EQ(new_err, 0.0L);
  EXPECT_EQ(legacy_err, 2.0L);
}

}  // namespace energytop
