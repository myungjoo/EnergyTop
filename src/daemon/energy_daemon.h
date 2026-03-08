#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

#include "collectors/collector_registry.h"
#include "config/config.h"
#include "power_record.h"

namespace energytop {

class EnergyDaemon {
 public:
  explicit EnergyDaemon(DaemonConfig config);
  ~EnergyDaemon();

  void start();
  void stop();

 private:
  void poller_loop();
  void publisher_logger_loop();
  void compressor_loop();

  void publish_records(const std::vector<PowerRecord>& records);
  void append_csv(const std::vector<PowerRecord>& records);
  void maybe_rotate_csv();
  void open_csv_for_append();
  void enqueue_compression(const std::string& rotated_path);

  PowerRecord build_record(const BatterySample& sample) const;

  DaemonConfig config_;
  CollectorRegistry registry_;
  std::atomic<bool> running_{false};

  std::thread poller_thread_;
  std::thread publisher_thread_;
  std::thread compressor_thread_;

  std::mutex buffer_mutex_;
  std::vector<PowerRecord> active_buffer_;
  std::vector<PowerRecord> publish_buffer_;

  std::mutex compress_mutex_;
  std::condition_variable compress_cv_;
  std::queue<std::string> compress_queue_;

  zmq::context_t zmq_ctx_{1};
  zmq::socket_t pub_socket_{zmq_ctx_, zmq::socket_type::pub};

  std::ofstream csv_stream_;
};

}  // namespace energytop
