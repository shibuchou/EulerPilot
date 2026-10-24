// ep-evtmon: the read-only eBPF event pump of the R5 link.
//
// It loads the events_trace tracing object (sched_wakeup/sched_switch ->
// per-cgroup runnable wait), drains the aggregate map once per window and
// reports it as one JSON line -- to stdout and, when --socket is given, to the
// executor's event.observe RPC. It never writes scheduler state, control maps
// or cgroup files: the executor acts, this process only measures. Killing it
// stops the observations, which is exactly what freezes the executor's event
// gate.
//
// SPDX-License-Identifier: GPL-2.0 (links libbpf, LGPL-2.1; see NOTICE)

#include <bpf/libbpf.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

extern "C" {
#include "events_trace.bpf.skel.h"
}

namespace {

using Json = nlohmann::json;

std::atomic<int> g_stop{0};

void on_signal(int) { g_stop = 1; }

std::int64_t mono_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::int64_t epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

struct Agg {
  std::uint64_t events = 0;
  std::uint64_t wait_total_ns = 0;
  std::uint64_t wait_max_ns = 0;
};

// The BPF aggregate is keyed by the kernfs id; user space only has the inode
// number of each cgroup directory, which is the low half of that id. Colliding
// inodes inside one window would mislabel a path, not invent a measurement.
std::map<std::uint32_t, std::string> walk_cgroup_tree(const std::string& root) {
  std::map<std::uint32_t, std::string> by_ino;
  std::vector<std::filesystem::path> pending{std::filesystem::path(root)};
  while (!pending.empty() && by_ino.size() < 8192) {
    const std::filesystem::path dir = pending.back();
    pending.pop_back();
    struct stat st {};
    if (::stat(dir.c_str(), &st) != 0) continue;
    std::error_code ec;
    by_ino.emplace(static_cast<std::uint32_t>(st.st_ino), dir.string());
    for (const auto& entry : std::filesystem::directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      std::error_code dec;
      if (entry.is_directory(dec) && !dec) pending.push_back(entry.path());
    }
    if (ec) continue;
  }
  return by_ino;
}

bool send_observation(const std::string& socket_path, const Json& request, Json* response,
                      std::string* error) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    *error = std::string("socket: ") + std::strerror(errno);
    return false;
  }
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    *error = std::string("connect ") + socket_path + ": " + std::strerror(errno);
    ::close(fd);
    return false;
  }
  const std::string line = request.dump() + "\n";
  std::size_t written = 0;
  while (written < line.size()) {
    const ssize_t n = ::write(fd, line.data() + written, line.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      *error = std::string("write: ") + std::strerror(errno);
      ::close(fd);
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  std::string data;
  char buffer[4096];
  while (data.size() < (1u << 20)) {
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    data.append(buffer, static_cast<std::size_t>(n));
    if (data.find('\n') != std::string::npos) break;
  }
  ::close(fd);
  const std::size_t newline = data.find('\n');
  if (newline != std::string::npos) data.resize(newline);
  if (data.empty()) {
    *error = "executor closed the connection without a response";
    return false;
  }
  try {
    *response = Json::parse(data);
  } catch (const std::exception& e) {
    *error = std::string("response is not JSON: ") + e.what();
    return false;
  }
  return true;
}

void usage() {
  std::cerr << "usage: ep-evtmon [--socket PATH] [--window-ms N] [--interval-ms N]\n"
               "                 [--cgroup-root PATH] [--duration-ms N] [--print-only]\n"
               "  drains the events_trace aggregates once per window and reports them\n"
               "  as event.observe lines. The producer period is window+interval and\n"
               "  must stay under 2x window or the consumer's freshness rule trips.\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string socket_path;
  std::string cgroup_root = "/sys/fs/cgroup";
  std::int64_t window_ms = 5000;
  std::int64_t interval_ms = 1000;
  std::int64_t duration_ms = 0;
  bool print_only = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](std::int64_t* out) -> bool {
      if (i + 1 >= argc) return false;
      char* end = nullptr;
      const long long parsed = std::strtoll(argv[++i], &end, 10);
      if (end == argv[i] || *end != '\0') return false;
      *out = parsed;
      return true;
    };
    if (arg == "--socket" && i + 1 < argc) {
      socket_path = argv[++i];
    } else if (arg == "--cgroup-root" && i + 1 < argc) {
      cgroup_root = argv[++i];
    } else if (arg == "--window-ms") {
      if (!next(&window_ms) || window_ms < 100 || window_ms > 60000) {
        std::cerr << "ep-evtmon: --window-ms needs 100..60000\n";
        return 2;
      }
    } else if (arg == "--interval-ms") {
      if (!next(&interval_ms) || interval_ms < 100 || interval_ms >= window_ms) {
        std::cerr << "ep-evtmon: --interval-ms needs 100..window-ms (producer period must stay\n"
                     "under the 2x window freshness rule)\n";
        return 2;
      }
    } else if (arg == "--duration-ms") {
      if (!next(&duration_ms) || duration_ms < 0) {
        std::cerr << "ep-evtmon: --duration-ms needs a non-negative integer\n";
        return 2;
      }
    } else if (arg == "--print-only") {
      print_only = true;
    } else {
      usage();
      return 2;
    }
  }
  if (print_only || socket_path.empty()) print_only = true;

  libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
  events_trace* skel = events_trace__open();
  if (skel == nullptr) {
    std::cerr << "ep-evtmon: failed to open events_trace skeleton\n";
    return 1;
  }
  if (events_trace__load(skel) != 0) {
    std::cerr << "ep-evtmon: failed to load events_trace (see libbpf errors above)\n";
    events_trace__destroy(skel);
    return 1;
  }
  if (events_trace__attach(skel) != 0) {
    std::cerr << "ep-evtmon: failed to attach tracepoints\n";
    events_trace__destroy(skel);
    return 1;
  }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::cerr << "[evtmon] attached window_ms=" << window_ms << " interval_ms=" << interval_ms
            << " cgroup_root=" << cgroup_root << (print_only ? " print-only" : "")
            << std::endl;

  struct bpf_map* agg_map = skel->maps.wait_agg;
  struct bpf_map* stats_map = skel->maps.evt_stats;
  const std::int64_t started_ms = mono_ms();
  std::uint64_t reports = 0;
  while (g_stop == 0) {
    const std::int64_t next_boundary =
        started_ms + (reports + 1) * window_ms - interval_ms;
    while (g_stop == 0 && mono_ms() < next_boundary) {
      std::this_thread::sleep_for(std::chrono::milliseconds(
          std::min<std::int64_t>(200, next_boundary - mono_ms())));
    }
    if (g_stop != 0) break;
    // Sleep out the reporting interval so observed_at_ms names the window that
    // just closed, not the moment we happened to read the map.
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    if (g_stop != 0) break;

    std::map<std::uint64_t, Agg> drained;
    std::uint64_t key = 0, next_key = 0;
    while (bpf_map__get_next_key(agg_map, &key, &next_key, sizeof(next_key)) == 0) {
      Agg value {};
      if (bpf_map__lookup_elem(agg_map, &next_key, sizeof(next_key), &value, sizeof(value), 0) == 0) {
        drained[next_key] = value;
        bpf_map__delete_elem(agg_map, &next_key, sizeof(next_key), 0);
      }
      key = next_key;
    }
    std::uint64_t stats[4] = {};
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
      bpf_map__lookup_elem(stats_map, &slot, sizeof(slot), &stats[slot], sizeof(stats[slot]), 0);
    }
    const auto by_ino = walk_cgroup_tree(cgroup_root);
    Json cgroups = Json::array();
    for (const auto& [id, agg] : drained) {
      const auto found = by_ino.find(static_cast<std::uint32_t>(id & 0xffffffffu));
      if (found == by_ino.end()) continue;  // cgroup vanished mid-window; drop, do not guess
      Json entry = Json{{"path", found->second},
                        {"cgroup_id", id},
                        {"events", agg.events}};
      if (agg.events > 0) {
        entry["wait_max_ms"] = std::round(agg.wait_max_ns / 1e6 * 1000.0) / 1000.0;
        entry["wait_avg_ms"] =
            std::round(agg.wait_total_ns / static_cast<double>(agg.events) / 1e6 * 1000.0) /
            1000.0;
      }
      cgroups.push_back(std::move(entry));
    }
    reports++;
    Json observation = Json{{"source", "evtmon"},
                            {"observed_at_ms", epoch_ms() - interval_ms},
                            {"window_ms", window_ms},
                            {"cgroups", std::move(cgroups)},
                            {"pump", Json{{"wakeups", stats[0]},
                                          {"waits_counted", stats[1]},
                                          {"pid_reuse_discards", stats[2]},
                                          {"agg_updates_lost", stats[3]},
                                          {"reports", reports}}}};
    std::cout << observation.dump() << std::endl;
    if (!print_only) {
      Json response;
      std::string error;
      Json request = Json{{"operation", "event.observe"},
                          {"request_id", "req-evtmon-" + std::to_string(reports)},
                          {"observation", observation}};
      if (send_observation(socket_path, request, &response, &error)) {
        std::cout << response.dump() << std::endl;
      } else {
        std::cerr << "[evtmon] report " << reports << " not delivered: " << error << std::endl;
      }
    }
    if (duration_ms > 0 && mono_ms() - started_ms >= duration_ms) break;
  }

  events_trace__destroy(skel);
  std::cerr << "[evtmon] detached after " << reports << " report(s)" << std::endl;
  return 0;
}
