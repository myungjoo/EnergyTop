#include <atomic>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <zmq.hpp>

#include "config/config.h"
#include "power_record.h"
#include "stats/power_stats.h"

namespace {
std::atomic<bool> g_keep_running{true};
constexpr std::size_t kIntervalWindowCount = 5;
constexpr int kRenderIntervalMs = 200;

void handle_signal(int) { g_keep_running.store(false); }

std::uint64_t now_real_ms() {
  const auto now = std::chrono::system_clock::now();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
          .count());
}

struct IntervalWindow {
  bool running = false;
  bool has_result = false;
  energytop::PowerStats stats;
  energytop::StatsSnapshot frozen_snapshot;
  std::uint64_t start_real_ms = 0;
  std::uint64_t stop_real_ms = 0;
};

class ScopedTerminalInput {
 public:
  ScopedTerminalInput() {
    if (!::isatty(STDIN_FILENO)) {
      return;
    }
    if (::tcgetattr(STDIN_FILENO, &old_termios_) != 0) {
      return;
    }

    termios raw = old_termios_;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
      return;
    }

    old_flags_ = ::fcntl(STDIN_FILENO, F_GETFL, 0);
    if (old_flags_ != -1) {
      (void)::fcntl(STDIN_FILENO, F_SETFL, old_flags_ | O_NONBLOCK);
    }
    enabled_ = true;
  }

  ~ScopedTerminalInput() {
    if (!enabled_) {
      return;
    }
    (void)::tcsetattr(STDIN_FILENO, TCSANOW, &old_termios_);
    if (old_flags_ != -1) {
      (void)::fcntl(STDIN_FILENO, F_SETFL, old_flags_);
    }
  }

  bool enabled() const { return enabled_; }

 private:
  bool enabled_ = false;
  int old_flags_ = -1;
  termios old_termios_ {};
};

void toggle_window(IntervalWindow& window, std::uint64_t reference_real_ms) {
  if (window.running) {
    window.running = false;
    window.stop_real_ms = reference_real_ms;
    window.frozen_snapshot = window.stats.snapshot();
    window.has_result = true;
    return;
  }

  window.stats.reset();
  window.running = true;
  window.has_result = true;
  window.start_real_ms = reference_real_ms;
  window.stop_real_ms = 0;
  window.frozen_snapshot = {};
}

void handle_keyboard_input(
    std::array<IntervalWindow, kIntervalWindowCount>& windows,
    std::uint64_t reference_real_ms) {
  while (true) {
    char ch = 0;
    const ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n == 1) {
      if (ch >= '1' && ch <= '5') {
        const std::size_t idx = static_cast<std::size_t>(ch - '1');
        toggle_window(windows[idx], reference_real_ms);
      } else if (ch == 'q' || ch == 'Q') {
        g_keep_running.store(false);
      }
      continue;
    }
    if (n == 0) {
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      break;
    }
    break;
  }
}

std::string format_elapsed(std::uint64_t start_ms, std::uint64_t end_ms) {
  if (start_ms == 0) {
    return "-";
  }
  const auto delta_ms = end_ms > start_ms ? (end_ms - start_ms) : 0;
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3)
      << (static_cast<double>(delta_ms) / 1000.0) << "s";
  return oss.str();
}

void render_screen(
    const energytop::StatsSnapshot& global,
    const energytop::PowerRecord& latest,
    const std::array<IntervalWindow, kIntervalWindowCount>& windows,
    std::uint64_t now_ms,
    bool keyboard_enabled) {
  std::cout << "\033[2J\033[H";
  std::cout << "EnergyTop Monitor\n";
  std::cout << "samples      : " << global.sample_count << "\n";
  std::cout << "latest current(uA): " << latest.current_ua << "\n";
  std::cout << "latest voltage(uV): " << latest.voltage_uv << "\n";
  std::cout << "avg power(uW): " << global.avg_power_uw << "\n";
  std::cout << "min/max(uW)  : " << global.min_power_uw << " / "
            << global.max_power_uw << "\n";
  std::cout << "energy(uJ)   : " << global.total_energy_uj << "\n";
  std::cout << "refresh(ms)  : " << kRenderIntervalMs << "\n";
  if (keyboard_enabled) {
    std::cout << "keys         : [1-5] window start/stop(toggle), [q] quit\n";
  } else {
    std::cout << "keys         : stdin is not a TTY (keyboard control disabled)\n";
  }
  std::cout << "\n";

  std::cout << "Window intervals\n";
  std::cout
      << "ID  STATE  samples         avg(uW)          energy(uJ)      elapsed\n";
  for (std::size_t i = 0; i < windows.size(); ++i) {
    const auto& window = windows[i];
    const auto state =
        window.running ? "RUN  " : (window.has_result ? "STOP " : "IDLE ");
    const auto snapshot =
        window.running ? window.stats.snapshot() : window.frozen_snapshot;
    const auto end_ms =
        window.running ? now_ms : (window.stop_real_ms == 0 ? now_ms
                                                            : window.stop_real_ms);

    std::cout << std::setw(2) << (i + 1) << "  " << state << "  "
              << std::setw(8) << snapshot.sample_count << "  "
              << std::setw(14) << snapshot.avg_power_uw << "  "
              << std::setw(16) << snapshot.total_energy_uj << "  "
              << format_elapsed(window.start_real_ms, end_ms) << "\n";
  }
  std::cout.flush();
}
}  // namespace

int main(int argc, char** argv) {
  std::optional<std::string> config_path;
  bool once = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--once") {
      once = true;
    } else {
      std::cerr << "Usage: energytop [--config PATH] [--once]\n";
      return 2;
    }
  }

  try {
    const auto config = energytop::load_daemon_config(config_path);
    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.set(zmq::sockopt::subscribe, "");
    sub.connect(config.zmq_endpoint);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    ScopedTerminalInput terminal_input;
    energytop::PowerStats stats;
    energytop::PowerRecord latest {};
    std::array<IntervalWindow, kIntervalWindowCount> windows;
    auto last_render = std::chrono::steady_clock::now();
    bool once_received = false;

    while (g_keep_running.load()) {
      if (terminal_input.enabled()) {
        handle_keyboard_input(windows, now_real_ms());
      }

      bool received_batch = false;
      while (true) {
        zmq::message_t msg;
        const auto recv_result = sub.recv(msg, zmq::recv_flags::dontwait);
        if (!recv_result) {
          break;
        }
        received_batch = true;

        if (msg.size() % sizeof(energytop::PowerRecord) != 0) {
          std::cerr << "[energytop] received malformed message\n";
          continue;
        }

        const std::size_t count = msg.size() / sizeof(energytop::PowerRecord);
        std::vector<energytop::PowerRecord> records(count);
        std::memcpy(records.data(), msg.data(), msg.size());

        for (const auto& r : records) {
          latest = r;
          const energytop::Sample sample {r.timestamp_real_ms, r.current_ua,
                                          r.voltage_uv};
          stats.add_sample(sample);
          for (auto& window : windows) {
            if (window.running) {
              window.stats.add_sample(sample);
            }
          }
        }
      }

      if (once && received_batch) {
        once_received = true;
      }

      const auto now = std::chrono::steady_clock::now();
      const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now - last_render)
                                  .count();
      if (elapsed_ms >= kRenderIntervalMs || once_received) {
        render_screen(stats.snapshot(), latest, windows, now_real_ms(),
                      terminal_input.enabled());
        last_render = now;
      }

      if (once_received) {
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } catch (const std::exception& e) {
    std::cerr << "[energytop] fatal: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
