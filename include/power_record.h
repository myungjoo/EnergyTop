#pragma once

#include <cstdint>

namespace energytop {

#pragma pack(push, 1)
struct PowerRecord {
  std::uint64_t timestamp_boot_ns;
  std::uint64_t timestamp_real_ms;
  std::int32_t current_ua;
  std::int32_t voltage_uv;
};
#pragma pack(pop)

static_assert(sizeof(PowerRecord) == 24, "PowerRecord must be 24 bytes.");

}  // namespace energytop
