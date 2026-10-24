#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "ep/executor.hpp"
#include "ep/policy.hpp"
#include "ep/cgroup_skill.hpp"
#include "ep/mock_skill.hpp"
#include "ep/sched_skill.hpp"
#include "ep/unsupported_skill.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

std::string default_state_dir() {
  if (::geteuid() == 0) return "/run/eulerpilot-v2/executor";
  if (const char* xdg = ::getenv("XDG_RUNTIME_DIR"); xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/eulerpilot-v2/executor";
  }
  return "/tmp/eulerpilot-v2-" + std::to_string(::geteuid());
}

struct Options {
  std::string state_dir;
  std::string socket_path;
  std::string skill = "sched";
  std::string loader = "/usr/local/bin/ep-sched-loader";
  std::string sched_ctl = "/usr/local/bin/ep-schedctl";
  bool test_mode = false;
  bool allow_any_uid = false;
  bool auto_apply = false;
  std::string auto_policy_path;
  std::string auto_owner = "autopilot-v2";
  long long auto_min_interval_ms = 15000;
  long long auto_lease_ttl_ms = 30000;
  long long auto_dwell_ms = 60000;
  long long auto_candidate_hold_ms = 15000;
  long long auto_budget_window_ms = 600000;
  long long auto_budget_max_switches = 2;
  long long auto_outcome_windows = 2;
  long long auto_measured_downtime_ms = 665;
};

void print_usage() {
  std::cout << "usage: ep-executord [--state-dir DIR] [--socket PATH] "
               "[--skill sched|sched-ref|cgroup|mock] [--loader PATH] [--sched-ctl PATH] "
               "[--test-mode] [--allow-any-uid] "
               "[--auto-apply --auto-policy FILE [--auto-owner ID] "
               "[--auto-min-interval-ms N] [--auto-lease-ttl-ms N]]\n"
               "  --auto-apply moves an in-place policy between whitelisted experts;\n"
               "  it requires --auto-policy, only acts while it holds the lease, and\n"
               "  never changes targets or safety limits.\n"
               "  suppression (one switch costs ~0.3-0.7s of unprotected time):\n"
               "  --auto-dwell-ms N (default 60000) min age before leaving an expert\n"
               "  --auto-candidate-hold-ms N (15000) route must be stable that long\n"
               "  --auto-budget-window-ms N (600000) + --auto-budget-max-switches K (2)\n"
               "  --auto-outcome-windows K (2) violating windows before service evidence\n"
               "  --auto-measured-downtime-ms N (665) reported unprotected time\n";
}

bool parse_args(int argc, char** argv, Options* options, std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](std::string* out) -> bool {
      if (i + 1 >= argc) {
        *error = "missing value for " + arg;
        return false;
      }
      *out = argv[++i];
      return true;
    };
    if (arg == "--state-dir") {
      if (!next(&options->state_dir)) return false;
    } else if (arg == "--socket") {
      if (!next(&options->socket_path)) return false;
    } else if (arg == "--skill") {
      if (!next(&options->skill)) return false;
    } else if (arg == "--loader") {
      if (!next(&options->loader)) return false;
    } else if (arg == "--sched-ctl") {
      if (!next(&options->sched_ctl)) return false;
    } else if (arg == "--test-mode") {
      options->test_mode = true;
    } else if (arg == "--allow-any-uid") {
      options->allow_any_uid = true;
    } else if (arg == "--auto-apply") {
      options->auto_apply = true;
    } else if (arg == "--auto-policy") {
      if (!next(&options->auto_policy_path)) return false;
    } else if (arg == "--auto-owner") {
      if (!next(&options->auto_owner)) return false;
    } else if (arg == "--auto-min-interval-ms" || arg == "--auto-lease-ttl-ms" ||
               arg == "--auto-dwell-ms" || arg == "--auto-candidate-hold-ms" ||
               arg == "--auto-budget-window-ms") {
      std::string value;
      if (!next(&value)) return false;
      char* end = nullptr;
      const long long parsed = std::strtoll(value.c_str(), &end, 10);
      if (end == value.c_str() || *end != '\0' || parsed < 1000 || parsed > 3600000) {
        *error = arg + " needs an integer in 1000..3600000, got: " + value;
        return false;
      }
      if (arg == "--auto-min-interval-ms") options->auto_min_interval_ms = parsed;
      else if (arg == "--auto-lease-ttl-ms") options->auto_lease_ttl_ms = parsed;
      else if (arg == "--auto-dwell-ms") options->auto_dwell_ms = parsed;
      else if (arg == "--auto-candidate-hold-ms") options->auto_candidate_hold_ms = parsed;
      else options->auto_budget_window_ms = parsed;
    } else if (arg == "--auto-budget-max-switches" ||
               arg == "--auto-outcome-windows" ||
               arg == "--auto-measured-downtime-ms") {
      std::string value;
      if (!next(&value)) return false;
      char* end = nullptr;
      const long long parsed = std::strtoll(value.c_str(), &end, 10);
      const long long limit = arg == "--auto-budget-max-switches"
                              ? 100
                              : (arg == "--auto-outcome-windows" ? 10 : 60000);
      const long long lowest = arg == "--auto-outcome-windows" ? 1 : 0;
      if (end == value.c_str() || *end != '\0' || parsed < lowest || parsed > limit) {
        *error = arg + " needs an integer in 0.." + std::to_string(limit) + ", got: " + value;
        return false;
      }
      if (arg == "--auto-budget-max-switches") options->auto_budget_max_switches = parsed;
      else if (arg == "--auto-outcome-windows") options->auto_outcome_windows = parsed;
      else options->auto_measured_downtime_ms = parsed;
    } else if (arg == "-h" || arg == "--help") {
      print_usage();
      std::exit(0);
    } else {
      *error = "unknown argument: " + arg;
      return false;
    }
  }
  if (options->state_dir.empty()) options->state_dir = default_state_dir();
  if (options->socket_path.empty()) options->socket_path = options->state_dir + "/executor.sock";
  if (options->socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
    *error = "socket path is too long: " + options->socket_path;
    return false;
  }
  return true;
}

bool write_all(int fd, const std::string& data) {
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  return true;
}

bool write_text_file(const std::string& path, const std::string& text) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  const bool ok = write_all(fd, text);
  ::close(fd);
  return ok;
}

ep::Json rejection(const std::string& code, const std::string& message,
                   const std::string& request_id = "") {
  return ep::Json{{"request_id", request_id},
                  {"status", "REJECTED"},
                  {"observed_generation", 0},
                  {"policy_sha256", ""},
                  {"target_bindings", ep::Json::array()},
                  {"checks", ep::Json::array()},
                  {"rollback", ep::Json{{"attempted", false}, {"status", "NOT_NEEDED"}}},
                  {"error", ep::Json{{"code", code}, {"message", message}, {"detail", ""}}}};
}

bool read_request(int fd, ep::Json* request, std::string* error) {
  std::string data;
  char buffer[4096];
  while (data.size() < (1u << 20)) {
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) continue;
      *error = std::string("read failed: ") + std::strerror(errno);
      return false;
    }
    if (n == 0) break;
    data.append(buffer, static_cast<std::size_t>(n));
    if (data.find('\n') != std::string::npos) break;
  }
  const std::size_t newline = data.find('\n');
  if (newline != std::string::npos) data.resize(newline);
  if (data.empty()) {
    *error = "empty request";
    return false;
  }
  try {
    *request = ep::Json::parse(data);
  } catch (const std::exception& exception) {
    *error = std::string("invalid JSON: ") + exception.what();
    return false;
  }
  return true;
}

void serve_client(int fd, ep::Executor* executor, bool allow_any_uid) {
  struct timeval timeout {
    5, 0
  };
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  struct ucred credentials {};
  socklen_t length = sizeof(credentials);
  if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
    const ep::Json response = rejection("PERMISSION_DENIED", "cannot read peer credentials");
    write_all(fd, response.dump() + "\n");
    ::close(fd);
    return;
  }
  if (!allow_any_uid && credentials.uid != ::geteuid()) {
    const ep::Json response = rejection(
        "PERMISSION_DENIED",
        "peer uid " + std::to_string(credentials.uid) + " is not allowed");
    write_all(fd, response.dump() + "\n");
    ::close(fd);
    return;
  }

  ep::Json request;
  std::string error;
  if (!read_request(fd, &request, &error)) {
    const ep::Json response = rejection("SCHEMA_INVALID", error);
    write_all(fd, response.dump() + "\n");
    ::close(fd);
    return;
  }
  const ep::Json response = executor->handle(request);
  write_all(fd, response.dump() + "\n");
  ::close(fd);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!parse_args(argc, argv, &options, &error)) {
    std::cerr << "[executord] " << error << "\n";
    return 2;
  }

  std::error_code ec;
  std::filesystem::create_directories(options.state_dir, ec);
  if (ec) {
    std::cerr << "[executord] cannot create " << options.state_dir << ": " << ec.message() << "\n";
    return 1;
  }

  std::unique_ptr<ep::Skill> skill;
  if (options.test_mode || options.skill == "mock") {
    skill = std::make_unique<ep::MockSkill>(std::filesystem::path(options.state_dir) / "mock");
  } else if (options.skill == "cgroup") {
    ep::CgroupPaths paths;
    paths.state_dir = options.state_dir;
    skill = std::make_unique<ep::CgroupSkill>(paths);
  } else if (options.skill == "sched" || options.skill == "sched-ref") {
    ep::SchedPaths paths;
    paths.state_dir = options.state_dir;
    paths.loader = options.loader;
    paths.ctrl = options.sched_ctl;
    paths.fd_passing = (options.skill == "sched");
    skill = std::make_unique<ep::SchedSkill>(paths);
  } else {
    skill = std::make_unique<ep::UnsupportedSkill>(options.skill);
  }

  ep::ExecutorOptions executor_options;
  executor_options.test_mode = options.test_mode;
  if (options.auto_apply) {
    if (options.skill != "sched" && options.skill != "sched-ref") {
      std::cerr << "[executord] --auto-apply needs --skill sched|sched-ref\n";
      return 2;
    }
    if (options.auto_policy_path.empty()) {
      std::cerr << "[executord] --auto-apply requires --auto-policy FILE\n";
      return 2;
    }
    std::ifstream input(options.auto_policy_path);
    if (!input) {
      std::cerr << "[executord] cannot read autopilot baseline: "
                << options.auto_policy_path << '\n';
      return 1;
    }
    ep::Json baseline;
    try {
      baseline = ep::Json::parse(input);
    } catch (const std::exception& e) {
      std::cerr << "[executord] autopilot baseline is not valid JSON: " << e.what() << "\n";
      return 1;
    }
    ep::PolicySummary baseline_summary;
    std::string baseline_error;
    if (!ep::validate_scheduling_policy(baseline, &baseline_summary, &baseline_error)) {
      std::cerr << "[executord] autopilot baseline rejected: " << baseline_error << "\n";
      return 1;
    }
    executor_options.auto_apply = true;
    executor_options.auto_owner = options.auto_owner;
    executor_options.auto_baseline_path = options.auto_policy_path;
    executor_options.auto_min_interval_ms = options.auto_min_interval_ms;
    executor_options.auto_dwell_ms = options.auto_dwell_ms;
    executor_options.auto_candidate_hold_ms = options.auto_candidate_hold_ms;
    executor_options.auto_budget_window_ms = options.auto_budget_window_ms;
    executor_options.auto_budget_max_switches =
        static_cast<int>(options.auto_budget_max_switches);
    executor_options.auto_outcome_windows = static_cast<int>(options.auto_outcome_windows);
    executor_options.auto_measured_downtime_ms = options.auto_measured_downtime_ms;
    executor_options.auto_lease_ttl_ms = options.auto_lease_ttl_ms;
    executor_options.auto_baseline_policy = std::move(baseline);
    std::cout << "[executord] autopilot enabled owner=" << options.auto_owner
              << " baseline=" << baseline_summary.name << " expert=" << baseline_summary.expert
              << " min_interval_ms=" << options.auto_min_interval_ms
              << " dwell_ms=" << options.auto_dwell_ms
              << " candidate_hold_ms=" << options.auto_candidate_hold_ms
              << " budget=" << options.auto_budget_max_switches << "/"
              << options.auto_budget_window_ms << "ms outcome_windows="
              << options.auto_outcome_windows << "\n";
  }

  ep::Executor executor(options.state_dir, std::move(skill), std::move(executor_options));
  if (!executor.init(&error)) {
    std::cerr << "[executord] init failed: " << error << "\n";
    return 1;
  }

  ::unlink(options.socket_path.c_str());
  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listen_fd < 0) {
    std::cerr << "[executord] socket failed: " << std::strerror(errno) << "\n";
    return 1;
  }
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, options.socket_path.c_str(), sizeof(address.sun_path) - 1);
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    std::cerr << "[executord] bind " << options.socket_path << " failed: "
              << std::strerror(errno) << "\n";
    ::close(listen_fd);
    return 1;
  }
  ::chmod(options.socket_path.c_str(), 0600);
  if (::listen(listen_fd, 16) != 0) {
    std::cerr << "[executord] listen failed: " << std::strerror(errno) << "\n";
    ::close(listen_fd);
    ::unlink(options.socket_path.c_str());
    return 1;
  }

  const std::string pid_path = options.state_dir + "/executor.pid";
  write_text_file(pid_path, std::to_string(::getpid()) + "\n");

  struct sigaction action {};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  ::sigaction(SIGTERM, &action, nullptr);
  ::sigaction(SIGINT, &action, nullptr);
  ::signal(SIGPIPE, SIG_IGN);

  std::cout << "[executord] ready state_dir=" << options.state_dir
            << " socket=" << options.socket_path << " skill=" << executor.skill_name()
            << " instance=" << executor.instance_id() << " generation=" << executor.generation()
            << " degraded=" << (executor.degraded() ? "true" : "false") << "\n";
  std::cout.flush();

  while (g_stop == 0) {
    pollfd descriptor {listen_fd, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, 200);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) continue;
    const int client = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
      if (errno == EINTR) continue;
      continue;
    }
    serve_client(client, &executor, options.allow_any_uid);
  }

  ::close(listen_fd);
  ::unlink(options.socket_path.c_str());
  executor.shutdown("executord received a stop signal");
  std::cerr << "[executord] shutting down\n";
  ::unlink(pid_path.c_str());
  return 0;
}
