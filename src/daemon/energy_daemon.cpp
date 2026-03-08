#include "daemon/energy_daemon.h"

#include <time.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace energytop {

namespace {

std::uint64_t now_realtime_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t now_boot_ns() {
  struct timespec ts {};
  if (::clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

std::string make_gzip_command(const std::string& path) {
  return "gzip -f \"" + path + "\"";
}

}  // namespace

EnergyDaemon::EnergyDaemon(DaemonConfig config)
    : config_(std::move(config)), registry_(create_default_registry(config_)) {}

EnergyDaemon::~EnergyDaemon() { stop(); }

void EnergyDaemon::start() {
  if (running_.exchange(true)) {
    return;
  }

  const fs::path csv_path(config_.csv_output_path);
  if (!csv_path.parent_path().empty()) {
    fs::create_directories(csv_path.parent_path());
  }
  open_csv_for_append();

  pub_socket_.set(zmq::sockopt::sndhwm, 64);
  pub_socket_.bind(config_.zmq_endpoint);

  poller_thread_ = std::thread(&EnergyDaemon::poller_loop, this);
  publisher_thread_ = std::thread(&EnergyDaemon::publisher_logger_loop, this);
  compressor_thread_ = std::thread(&EnergyDaemon::compressor_loop, this);
}

void EnergyDaemon::stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (poller_thread_.joinable()) {
    poller_thread_.join();
  }
  if (publisher_thread_.joinable()) {
    publisher_thread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(compress_mutex_);
  }
  compress_cv_.notify_all();
  if (compressor_thread_.joinable()) {
    compressor_thread_.join();
  }

  if (csv_stream_.is_open()) {
    csv_stream_.flush();
    csv_stream_.close();
  }
  pub_socket_.close();
}

void EnergyDaemon::poller_loop() {
  const auto sleep_duration =
      std::chrono::milliseconds(config_.daemon_polling_interval_ms);
  while (running_.load()) {
    try {
      const auto samples = registry_.collect_all();
      for (const auto& sample : samples) {
        if (!sample.has_value()) {
          continue;
        }
        const auto record = build_record(*sample);
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        active_buffer_.push_back(record);
      }
    } catch (const std::exception& e) {
      std::cerr << "[energytopd] poller error: " << e.what() << "\n";
      running_.store(false);
      break;
    }
    std::this_thread::sleep_for(sleep_duration);
  }
}

void EnergyDaemon::publisher_logger_loop() {
  const auto publish_period =
      std::chrono::seconds(std::max(1, config_.zmq_publish_interval_sec));

  while (running_.load()) {
    std::this_thread::sleep_for(publish_period);

    {
      std::lock_guard<std::mutex> lock(buffer_mutex_);
      active_buffer_.swap(publish_buffer_);
    }

    if (publish_buffer_.empty()) {
      continue;
    }

    publish_records(publish_buffer_);
    append_csv(publish_buffer_);
    maybe_rotate_csv();
    publish_buffer_.clear();
  }

  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    active_buffer_.swap(publish_buffer_);
  }
  if (!publish_buffer_.empty()) {
    publish_records(publish_buffer_);
    append_csv(publish_buffer_);
    publish_buffer_.clear();
  }
}

void EnergyDaemon::compressor_loop() {
  while (true) {
    std::string rotate_target;
    {
      std::unique_lock<std::mutex> lock(compress_mutex_);
      compress_cv_.wait(lock, [&]() {
        return !running_.load() || !compress_queue_.empty();
      });
      if (compress_queue_.empty()) {
        if (!running_.load()) {
          break;
        }
        continue;
      }
      rotate_target = compress_queue_.front();
      compress_queue_.pop();
    }

    const auto command = make_gzip_command(rotate_target);
    const int rc = std::system(command.c_str());
    if (rc != 0) {
      std::cerr << "[energytopd] gzip failed for " << rotate_target << "\n";
    }
  }
}

void EnergyDaemon::publish_records(const std::vector<PowerRecord>& records) {
  if (records.empty()) {
    return;
  }
  zmq::message_t payload(records.size() * sizeof(PowerRecord));
  std::memcpy(payload.data(), records.data(), payload.size());
  pub_socket_.send(payload, zmq::send_flags::none);
}

void EnergyDaemon::append_csv(const std::vector<PowerRecord>& records) {
  for (const auto& r : records) {
    csv_stream_ << r.timestamp_boot_ns << "," << r.timestamp_real_ms << ","
                << r.current_ua << "," << r.voltage_uv << "\n";
  }
  csv_stream_.flush();
}

void EnergyDaemon::maybe_rotate_csv() {
  const auto max_bytes =
      static_cast<std::uintmax_t>(std::max(1, config_.csv_max_size_mb)) *
      1024ULL * 1024ULL;
  const fs::path current(config_.csv_output_path);

  if (!fs::exists(current)) {
    return;
  }
  if (fs::file_size(current) <= max_bytes) {
    return;
  }

  csv_stream_.flush();
  csv_stream_.close();

  const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  const fs::path rotated = current.parent_path() /
                           (current.stem().string() + "." + std::to_string(ts) +
                            current.extension().string());
  fs::rename(current, rotated);
  enqueue_compression(rotated.string());
  open_csv_for_append();
}

void EnergyDaemon::open_csv_for_append() {
  const fs::path csv_path(config_.csv_output_path);
  const bool needs_header = !fs::exists(csv_path) || fs::file_size(csv_path) == 0;
  csv_stream_.open(config_.csv_output_path, std::ios::out | std::ios::app);
  if (!csv_stream_.is_open()) {
    throw std::runtime_error("failed to open csv output: " +
                             config_.csv_output_path);
  }
  if (needs_header) {
    csv_stream_ << "timestamp_boot_ns,timestamp_real_ms,current_ua,voltage_uv\n";
    csv_stream_.flush();
  }
}

void EnergyDaemon::enqueue_compression(const std::string& rotated_path) {
  {
    std::lock_guard<std::mutex> lock(compress_mutex_);
    compress_queue_.push(rotated_path);
  }
  compress_cv_.notify_one();
}

PowerRecord EnergyDaemon::build_record(const BatterySample& sample) const {
  PowerRecord record {};
  record.timestamp_boot_ns = now_boot_ns();
  record.timestamp_real_ms = now_realtime_ms();
  record.current_ua = sample.current_ua;
  record.voltage_uv = sample.voltage_uv;
  return record;
}

}  // namespace energytop
