#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace energytop::test {

inline std::filesystem::path make_temp_dir(const std::string& prefix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir =
      std::filesystem::temp_directory_path() / (prefix + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

inline void write_text(const std::filesystem::path& path, const std::string& v) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << v;
  out.flush();
}

}  // namespace energytop::test
