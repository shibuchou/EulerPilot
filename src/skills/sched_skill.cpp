#include "ep/sched_skill.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

namespace ep {
namespace {

constexpr int kStartupTimeoutMs = 8000;
constexpr int kStopTimeoutMs = 5000;
constexpr int kPollMs = 50;

bool read_file(const std::string& path, std::string* out) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  std::stringstream buffer;
  buffer << input.rdbuf();
  *out = buffer.str();
  return true;
}

std::string trim(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
                           text.back() == '\t')) {
    text.pop_back();
  }
  std::size_t start = 0;
  while (start < text.size() &&
         (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r')) {
    ++start;
  }
  return text.substr(start);
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

std::string log_tail(const std::filesystem::path& path, std::size_t limit) {
  std::string text;
  if (!read_file(path.string(), &text)) return "";
  if (text.size() <= limit) return trim(text);
  return trim(text.substr(text.size() - limit));
}

}  // namespace

Json SchedSkill::PinEntry::to_json() const {
  return Json{{"name", name}, {"inode", inode}, {"device", device}};
}

Json SchedSkill::Runtime::to_json() const {
  Json array = Json::array();
  for (const auto& pin : pins) array.push_back(pin.to_json());
  Json value{{"pid", pid},
             {"starttime", starttime},
             {"mode", mode},
             {"enable_seq", enable_seq},
             {"pins", array}};
  if (!cfg_before.is_null()) value["cfg_before"] = cfg_before;
  if (!cfg_after.is_null()) value["cfg_after"] = cfg_after;
  if (!bg_pids.empty()) {
    Json pids = Json::array();
    for (const auto pid : bg_pids) pids.push_back(pid);
    value["bg_pids"] = pids;
  }
  if (!bg_cgroups.empty()) value["bg_cgroups"] = bg_cgroups;
  return value;
}

bool SchedSkill::Runtime::parse(const Json& value, Runtime* out) {
  if (!value.is_object()) return false;
  Runtime runtime;
  try {
    runtime.pid = value.at("pid").get<std::int64_t>();
    runtime.starttime = value.value("starttime", 0ULL);
    runtime.mode = value.value("mode", "");
    runtime.enable_seq = value.value("enable_seq", 0ULL);
    for (const auto& pin : value.value("pins", Json::array())) {
      PinEntry entry;
      entry.name = pin.at("name").get<std::string>();
      entry.inode = pin.value("inode", 0ULL);
      entry.device = pin.value("device", 0ULL);
      runtime.pins.push_back(entry);
    }
    runtime.cfg_before = value.value("cfg_before", Json());
    runtime.cfg_after = value.value("cfg_after", Json());
    for (const auto& item : value.value("bg_pids", Json::array())) {
      runtime.bg_pids.push_back(item.get<std::int64_t>());
    }
    for (const auto& item : value.value("bg_cgroups", Json::array())) {
      runtime.bg_cgroups.push_back(item.get<std::string>());
    }
  } catch (const std::exception&) {
    return false;
  }
  *out = std::move(runtime);
  return true;
}

SchedSkill::SchedSkill(SchedPaths paths) : paths_(std::move(paths)) {}

SchedSkill::~SchedSkill() {
  if (link_fd_ >= 0) {
    ::close(link_fd_);
    link_fd_ = -1;
  }
}

void SchedSkill::configure(const SkillContext& context) {
  if (!paths_.pin_dir.empty()) {
    resolved_pin_dir_ = paths_.pin_dir;
  } else {
    resolved_pin_dir_ = paths_.pin_root + "/" + context.instance_id;
  }
}

std::string SchedSkill::pin_dir() const {
  if (!resolved_pin_dir_.empty()) return resolved_pin_dir_;
  if (!paths_.pin_dir.empty()) return paths_.pin_dir;
  return paths_.pin_root + "/unconfigured";
}

std::string SchedSkill::read_text(const std::string& path) const {
  std::string text;
  if (!read_file(path, &text)) return "";
  return trim(text);
}

std::string SchedSkill::read_state() const {
  return read_text(paths_.sched_ext_dir + "/state");
}

std::uint64_t SchedSkill::read_enable_seq() const {
  const std::string text = read_text(paths_.sched_ext_dir + "/enable_seq");
  if (text.empty()) return 0;
  try {
    return std::stoull(text);
  } catch (const std::exception&) {
    return 0;
  }
}

std::string SchedSkill::read_ops() const {
  std::string combined;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(paths_.sched_ext_dir, ec)) {
    if (ec) break;
    if (!entry.is_directory()) continue;
    const std::string ops = read_text(entry.path().string() + "/ops");
    if (!ops.empty()) {
      if (!combined.empty()) combined += ",";
      combined += ops;
    }
  }
  return combined;
}

std::vector<SchedSkill::PinEntry> SchedSkill::list_pins() const {
  std::vector<PinEntry> pins;
  std::error_code ec;
  const std::filesystem::path dir(pin_dir());
  if (!std::filesystem::exists(dir, ec)) return pins;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) break;
    struct stat info {};
    if (::lstat(entry.path().c_str(), &info) != 0) continue;
    if (!S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)) continue;
    PinEntry pin;
    pin.name = entry.path().filename().string();
    pin.inode = static_cast<std::uint64_t>(info.st_ino);
    pin.device = static_cast<std::uint64_t>(info.st_dev);
    pins.push_back(std::move(pin));
  }
  return pins;
}

bool SchedSkill::preflight(std::string* error) const {
  if (foreign_daemon_running()) {
    if (error != nullptr) {
      *error = "schedpilotd is running; the sched adapter requires exclusive ownership";
    }
    return false;
  }
  if (::access(paths_.loader.c_str(), X_OK) != 0) {
    if (error != nullptr) *error = "loader is not executable: " + paths_.loader;
    return false;
  }
  if (!std::filesystem::exists(paths_.sched_ext_dir)) {
    if (error != nullptr) *error = "sched_ext is not available at " + paths_.sched_ext_dir;
    return false;
  }
  const std::string state = read_state();
  if (state != "disabled") {
    if (error != nullptr) *error = "sched_ext state is '" + state + "', expected disabled";
    return false;
  }
  const std::string ops = read_ops();
  if (ops.find("eulerpilot") != std::string::npos) {
    if (error != nullptr) *error = "eulerpilot is still registered (ops=" + ops + ")";
    return false;
  }
  const auto pins = list_pins();
  if (!pins.empty()) {
    std::string names;
    for (const auto& pin : pins) names += pin.name + " ";
    if (error != nullptr) *error = "pin dir is not empty: " + names;
    return false;
  }
  if (std::filesystem::exists(std::filesystem::path(paths_.state_dir) / "sched-runtime.json")) {
    if (error != nullptr) *error = "sched-runtime.json exists; reconcile before applying";
    return false;
  }
  return true;
}

bool SchedSkill::write_runtime(const Runtime& runtime, std::string* error) const {
  std::error_code ec;
  std::filesystem::create_directories(paths_.state_dir, ec);
  const std::filesystem::path path =
      std::filesystem::path(paths_.state_dir) / "sched-runtime.json";
  const std::filesystem::path tmp =
      path.string() + ".tmp." + std::to_string(static_cast<long>(::getpid()));
  const std::string text = runtime.to_json().dump(2) + "\n";
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (error != nullptr) *error = std::string("cannot write runtime: ") + std::strerror(errno);
    return false;
  }
  bool ok = write_all(fd, text);
  if (ok && ::fsync(fd) != 0) {
    ok = false;
    if (error != nullptr) *error = std::string("fsync runtime failed: ") + std::strerror(errno);
  }
  ::close(fd);
  if (!ok) return false;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    if (error != nullptr) *error = "rename runtime failed: " + ec.message();
    return false;
  }
  return true;
}

bool SchedSkill::read_runtime(Runtime* runtime) const {
  const std::filesystem::path path =
      std::filesystem::path(paths_.state_dir) / "sched-runtime.json";
  std::string text;
  if (!read_file(path.string(), &text)) return false;
  try {
    return Runtime::parse(Json::parse(text), runtime);
  } catch (const std::exception&) {
    return false;
  }
}

void SchedSkill::remove_runtime() const {
  std::error_code ec;
  std::filesystem::remove(std::filesystem::path(paths_.state_dir) / "sched-runtime.json", ec);
}

SkillProbe SchedSkill::probe() {
  std::string error;
  if (!preflight(&error)) return SkillProbe{false, error};
  uint64_t seq = read_enable_seq();
  return SkillProbe{true, "sched_ext disabled, pins empty, enable_seq=" +
                              std::to_string(seq)};
}

bool SchedSkill::plan(const Json& intent, Plan* out, std::string* error) {
  std::string mode = "class";
  Json parameters = Json::object();
  Json targets = Json::array();
  if (intent.is_object() && intent.contains("spec") && intent["spec"].is_object()) {
    mode = intent["spec"].value("mode", "class");
    if (intent["spec"].contains("parameters")) parameters = intent["spec"]["parameters"];
    if (intent["spec"].contains("targets")) targets = intent["spec"]["targets"];
  } else if (intent.is_object() && intent.contains("mode")) {
    mode = intent.value("mode", "class");
    if (intent.contains("parameters")) parameters = intent["parameters"];
    if (intent.contains("targets")) targets = intent["targets"];
  }
  if (mode != "basic" && mode != "class") {
    if (error != nullptr) {
      *error = "mode '" + mode + "' is not supported by the P1 sched skill "
               "(adaptive needs schedpilotd and is deferred)";
    }
    return false;
  }
  Plan plan;
  plan.intent = intent;
  plan.params = Json{{"mode", mode}};
  if (parameters.is_object() && !parameters.empty()) plan.params["parameters"] = parameters;
  if (targets.is_array() && !targets.empty()) plan.params["targets"] = targets;
  if (out != nullptr) *out = std::move(plan);
  return true;
}

bool SchedSkill::snapshot(const Plan&, Snapshot* out, std::string* error) {
  (void)error;
  Snapshot snapshot;
  Json pins = Json::array();
  for (const auto& pin : list_pins()) pins.push_back(pin.to_json());
  Runtime runtime;
  const bool has_runtime = read_runtime(&runtime);
  snapshot.data = Json{{"state", read_state()},
                       {"ops", read_ops()},
                       {"enable_seq", read_enable_seq()},
                       {"pins", pins},
                       {"runtime_present", has_runtime}};
  snapshot.id = "snap-" + sha256_hex(canonical_json(snapshot.data) + random_hex(8)).substr(0, 32);
  if (out != nullptr) *out = std::move(snapshot);
  return true;
}

bool SchedSkill::create_listener(const std::string& path, int* listen_fd,
                                 std::string* error) const {
  if (path.size() + 1 > sizeof(sockaddr_un::sun_path)) {
    if (error != nullptr) *error = "link socket path is too long: " + path;
    return false;
  }
  ::unlink(path.c_str());
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    if (error != nullptr) *error = std::string("socket failed: ") + std::strerror(errno);
    return false;
  }
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    if (error != nullptr) *error = std::string("bind failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  if (::listen(fd, 1) != 0) {
    if (error != nullptr) *error = std::string("listen failed: ") + std::strerror(errno);
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  *listen_fd = fd;
  return true;
}

bool SchedSkill::receive_link_fd(int listen_fd, int* link_fd, std::string* error) const {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartupTimeoutMs);
  auto remaining_ms = [&deadline]() -> int {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - std::chrono::steady_clock::now())
                          .count();
    return left <= 0 ? 0 : static_cast<int>(left);
  };
  pollfd descriptor {listen_fd, POLLIN, 0};
  const int ready = ::poll(&descriptor, 1, remaining_ms());
  if (ready <= 0) {
    if (error != nullptr) *error = "timed out waiting for the link fd";
    return false;
  }
  const int connection = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
  if (connection < 0) {
    if (error != nullptr) *error = std::string("accept failed: ") + std::strerror(errno);
    return false;
  }
  pollfd payload {connection, POLLIN, 0};
  const int payload_ready = ::poll(&payload, 1, remaining_ms());
  if (payload_ready <= 0) {
    if (error != nullptr) *error = "connection accepted but no link fd arrived";
    ::close(connection);
    return false;
  }
  char data[1] = {0};
  char control[CMSG_SPACE(sizeof(int))] = {};
  struct iovec iov = {data, 1};
  struct msghdr message = {};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = control;
  message.msg_controllen = sizeof(control);
  const ssize_t length = ::recvmsg(connection, &message, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);
  int fd = -1;
  if (length > 0) {
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&message); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&message, cmsg)) {
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
          cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
        int received = -1;
        std::memcpy(&received, CMSG_DATA(cmsg), sizeof(int));
        if (received >= 0) {
          if (fd < 0) {
            fd = received;
          } else {
            ::close(received);  // unexpected extra fd
          }
        }
      }
    }
  }
  if (fd < 0) {
    if (error != nullptr) *error = "loader did not send a link fd";
    ::close(connection);
    return false;
  }
  // MSG_CMSG_CLOEXEC should already set this; make it explicit and fail closed
  // if the kernel/filesystem did not honour it.
  if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    if (error != nullptr) {
      *error = std::string("cannot set CLOEXEC on link fd: ") + std::strerror(errno);
    }
    ::close(fd);
    ::close(connection);
    return false;
  }
  const char ack = 'A';
  ::send(connection, &ack, 1, 0);
  ::close(connection);
  *link_fd = fd;
  return true;
}

bool SchedSkill::wait_disabled(int timeout_ms) const {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (read_state() == "disabled") return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  return read_state() == "disabled";
}

std::int64_t SchedSkill::spawn_loader(const std::string& mode,
                                      const std::string& fd_socket,
                                      std::string* error) const {
  std::error_code ec;
  std::filesystem::create_directories(paths_.state_dir, ec);
  const std::string log = (std::filesystem::path(paths_.state_dir) / "sched-loader.log").string();
  const int log_fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (log_fd < 0) {
    if (error != nullptr) *error = std::string("cannot open loader log: ") + std::strerror(errno);
    return -1;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(log_fd);
    if (error != nullptr) *error = std::string("fork failed: ") + std::strerror(errno);
    return -1;
  }
  if (pid == 0) {
    ::setsid();
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (::getppid() == 1) _exit(1);
    ::dup2(log_fd, STDOUT_FILENO);
    ::dup2(log_fd, STDERR_FILENO);
    ::close(log_fd);
    const std::string pin = pin_dir();
    if (!fd_socket.empty()) {
      ::execl(paths_.loader.c_str(), paths_.loader.c_str(), "--mode", mode.c_str(),
              "--pin-dir", pin.c_str(), "--fd-socket", fd_socket.c_str(),
              static_cast<char*>(nullptr));
    } else {
      ::execl(paths_.loader.c_str(), paths_.loader.c_str(), "--mode", mode.c_str(),
              "--pin-dir", pin.c_str(), static_cast<char*>(nullptr));
    }
    _exit(127);
  }
  ::close(log_fd);
  return static_cast<std::int64_t>(pid);
}

bool SchedSkill::wait_enabled(std::int64_t pid, std::string* error) const {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartupTimeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    const std::string state = read_state();
    const std::string ops = read_ops();
    if (state == "enabled" && ops.find("eulerpilot") != std::string::npos) {
      return true;
    }
    if (pid > 0) {
      int status = 0;
      const pid_t result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
      if (result == static_cast<pid_t>(pid)) {
        if (error != nullptr) {
          *error = "loader exited during startup; log tail: " +
                   log_tail(std::filesystem::path(paths_.state_dir) / "sched-loader.log", 2048);
        }
        return false;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  if (error != nullptr) {
    *error = "scheduler did not become enabled within " +
             std::to_string(kStartupTimeoutMs) + "ms; log tail: " +
             log_tail(std::filesystem::path(paths_.state_dir) / "sched-loader.log", 2048);
  }
  return false;
}

std::vector<SchedSkill::PinEntry> SchedSkill::wait_pins_settled(int timeout_ms) const {
  auto last = list_pins();
  int stable = 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    auto current = list_pins();
    bool same = current.size() == last.size();
    if (same) {
      for (std::size_t i = 0; i < current.size(); ++i) {
        if (current[i].name != last[i].name || current[i].inode != last[i].inode) {
          same = false;
          break;
        }
      }
    }
    if (same && !current.empty()) {
      ++stable;
      if (stable >= 4) return current;
    } else {
      stable = 0;
    }
    last = std::move(current);
  }
  // Return whatever is there; a partial/empty set will make rollback fail closed
  // with an ownership conflict instead of deleting something we do not own.
  return last;
}

bool SchedSkill::run_ctrl(const std::vector<std::string>& args, Json* output,
                          std::string* error, std::uint64_t timeout_ms) const {
  if (paths_.ctrl.empty() || ::access(paths_.ctrl.c_str(), X_OK) != 0) {
    if (error != nullptr) *error = "ep-schedctl is not executable: " + paths_.ctrl;
    return false;
  }
  int fds[2];
  if (::pipe(fds) != 0) {
    if (error != nullptr) *error = std::string("pipe failed: ") + std::strerror(errno);
    return false;
  }
  ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(fds[0]);
    ::close(fds[1]);
    if (error != nullptr) *error = std::string("fork failed: ") + std::strerror(errno);
    return false;
  }
  if (pid == 0) {
    // Own process group so a timeout can also stop helper descendants.
    if (::setpgid(0, 0) != 0) _exit(126);
    // The helper must never inherit the struct_ops link fd: closing it here is
    // a second line of defence after FD_CLOEXEC.
    if (link_fd_ >= 0) ::close(link_fd_);
    ::dup2(fds[1], STDOUT_FILENO);
    ::dup2(fds[1], STDERR_FILENO);
    ::close(fds[0]);
    ::close(fds[1]);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(paths_.ctrl.c_str()));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    ::execv(paths_.ctrl.c_str(), argv.data());
    _exit(127);
  }
  ::close(fds[1]);

  const std::uint64_t budget =
      timeout_ms == 0 ? (paths_.ctrl_timeout_ms == 0 ? 1 : paths_.ctrl_timeout_ms)
                      : timeout_ms;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
  std::string data;
  char buffer[4096];
  bool timed_out = false;
  while (true) {
    if (std::chrono::steady_clock::now() >= deadline) {
      timed_out = true;
      break;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - std::chrono::steady_clock::now())
                               .count();
    pollfd descriptor {fds[0], POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, static_cast<int>(remaining < 1 ? 1 : remaining));
    if (ready == 0) {
      timed_out = true;
      break;
    }
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    const ssize_t n = ::read(fds[0], buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    data.append(buffer, static_cast<std::size_t>(n));
    if (data.size() >= (1u << 20)) break;
  }
  ::close(fds[0]);

  auto kill_and_reap = [&]() {
    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
    const auto grace =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    int status = 0;
    while (std::chrono::steady_clock::now() < grace) {
      const pid_t result = ::waitpid(pid, &status, WNOHANG);
      if (result == pid || (result < 0 && errno == ECHILD)) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };

  if (timed_out) {
    kill_and_reap();
    if (error != nullptr) {
      *error = "ep-schedctl timed out after " + std::to_string(budget) + "ms";
    }
    return false;
  }

  // EOF does not mean the helper exited. Reap within the same deadline; do not
  // add a second grace window on top of it.
  int status = 0;
  while (true) {
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) break;
    if (result < 0 && errno == ECHILD) {
      status = 0;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      kill_and_reap();
      if (error != nullptr) {
        *error = "ep-schedctl did not exit within " + std::to_string(budget) + "ms";
      }
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (error != nullptr) {
      *error = "ep-schedctl failed (exit " + std::to_string(WIFEXITED(status)
                                                              ? WEXITSTATUS(status)
                                                              : -1) +
               "): " + trim(data);
    }
    return false;
  }
  try {
    const Json parsed = Json::parse(data);
    if (!parsed.value("ok", false)) {
      if (error != nullptr) *error = "ep-schedctl: " + parsed.value("error", trim(data));
      return false;
    }
    if (output != nullptr) *output = parsed;
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = std::string("cannot parse ep-schedctl output: ") + exception.what() + ": " +
               trim(data);
    }
    return false;
  }
  return true;
}

bool SchedSkill::ctrl_get(Json* cfg, std::string* error) const {
  Json output;
  if (!run_ctrl({"get", "--pin-dir", pin_dir()}, &output, error)) return false;
  if (!output.contains("cfg") || !output.at("cfg").is_object()) {
    if (error != nullptr) *error = "ep-schedctl get returned no cfg object";
    return false;
  }
  *cfg = output.at("cfg");
  return true;
}

bool SchedSkill::ctrl_set(const Json& parameters, Json* cfg, std::string* error) const {
  Json output;
  if (!run_ctrl({"set", "--pin-dir", pin_dir(), "--json", parameters.dump()}, &output,
                error)) {
    return false;
  }
  if (!output.contains("cfg") || !output.at("cfg").is_object()) {
    if (error != nullptr) *error = "ep-schedctl set returned no cfg object";
    return false;
  }
  *cfg = output.at("cfg");
  return true;
}

bool SchedSkill::ctrl_restore(const Json& snapshot, Json* cfg, std::string* error) const {
  Json output;
  const Json payload{{"cfg", snapshot}};
  if (!run_ctrl({"restore", "--pin-dir", pin_dir(), "--json", payload.dump()}, &output,
                error)) {
    return false;
  }
  if (!output.contains("cfg") || !output.at("cfg").is_object()) {
    if (error != nullptr) *error = "ep-schedctl restore returned no cfg object";
    return false;
  }
  *cfg = output.at("cfg");
  return true;
}

bool SchedSkill::ctrl_class_set(std::int64_t pid, std::string* error,
                                std::uint64_t timeout_ms) const {
  Json output;
  if (!run_ctrl({"class-set", "--pin-dir", pin_dir(), "--pid", std::to_string(pid),
                 "--class", "BG", "--confidence", "1000"},
                &output, error, timeout_ms)) {
    return false;
  }
  if (!output.value("ok", false)) {
    if (error != nullptr) *error = "class-set returned no ok";
    return false;
  }
  return true;
}

bool SchedSkill::ctrl_verify(const Json& expected, std::string* error) const {  Json current;
  if (!ctrl_get(&current, error)) return false;
  static const char* keys[] = {"lat_slice_ns",      "comp_slice_ns", "cache_slice_ns",
                               "bg_slice_ns",       "preempt_threshold_ns",
                               "bg_vtime_pct",      "llc_affinity"};
  for (const char* key : keys) {
    if (current.value(key, Json()) != expected.value(key, Json())) {
      if (error != nullptr) {
        *error = std::string("cfg field '") + key + "' does not match the requested value";
      }
      return false;
    }
  }
  return true;
}

bool SchedSkill::foreign_daemon_running() {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
    if (ec) break;
    const std::string name = entry.path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
    std::string comm;
    if (!read_file(entry.path().string() + "/comm", &comm)) continue;
    if (trim(comm) == "schedpilotd") return true;
  }
  return false;
}

std::uint64_t SchedSkill::process_starttime(std::int64_t pid) {
  std::string stat;
  if (!read_file("/proc/" + std::to_string(pid) + "/stat", &stat)) return 0;
  const std::size_t close = stat.rfind(')');
  if (close == std::string::npos) return 0;
  std::istringstream stream(stat.substr(close + 1));
  std::string token;
  for (int index = 0; index <= 19; ++index) {
    if (!(stream >> token)) return 0;
  }
  try {
    return std::stoull(token);
  } catch (const std::exception&) {
    return 0;
  }
}

std::string SchedSkill::process_cmdline(std::int64_t pid) {
  std::string raw;
  if (!read_file("/proc/" + std::to_string(pid) + "/cmdline", &raw)) return "";
  for (char& c : raw) {
    if (c == '\0') c = ' ';
  }
  return raw;
}

bool SchedSkill::pid_matches(std::int64_t pid, std::uint64_t starttime) const {
  if (pid <= 0) return false;
  if (::kill(static_cast<pid_t>(pid), 0) != 0) return false;
  if (process_starttime(pid) != starttime) return false;
  const std::string base = std::filesystem::path(paths_.loader).filename().string();
  return process_cmdline(pid).find(base) != std::string::npos;
}

bool SchedSkill::stop_pid(std::int64_t pid, std::uint64_t starttime, int timeout_ms) const {
  if (!pid_matches(pid, starttime)) return true;
  ::kill(static_cast<pid_t>(pid), SIGTERM);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    if (result == static_cast<pid_t>(pid)) return true;
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  if (pid_matches(pid, starttime)) {
    ::kill(static_cast<pid_t>(pid), SIGKILL);
    for (int i = 0; i < 20; ++i) {
      int status = 0;
      const pid_t result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
      if (result == static_cast<pid_t>(pid)) return true;
      if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
  }
  return !pid_matches(pid, starttime);
}

bool SchedSkill::apply(const Plan& plan, const Snapshot&, const LeaseState&, Applied* out,
                       std::string* error) {
  if (!preflight(error)) return false;
  const std::string mode = plan.params.value("mode", "class");
  std::string fd_socket;
  int listen_fd = -1;
  if (paths_.fd_passing) {
    fd_socket = (std::filesystem::path(paths_.state_dir) / "sched-link.sock").string();
    if (!create_listener(fd_socket, &listen_fd, error)) return false;
  }
  const std::int64_t pid = spawn_loader(mode, fd_socket, error);
  if (pid < 0) {
    if (listen_fd >= 0) {
      ::close(listen_fd);
      ::unlink(fd_socket.c_str());
    }
    return false;
  }
  if (paths_.fd_passing) {
    int received = -1;
    if (!receive_link_fd(listen_fd, &received, error)) {
      ::close(listen_fd);
      ::unlink(fd_socket.c_str());
      stop_pid(pid, process_starttime(pid), kStopTimeoutMs);
      return false;
    }
    ::close(listen_fd);
    ::unlink(fd_socket.c_str());
    link_fd_ = received;
    int status = 0;
    for (int i = 0; i < 60; ++i) {
      if (::waitpid(static_cast<pid_t>(pid), &status, WNOHANG) ==
          static_cast<pid_t>(pid)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    if (!wait_enabled(0, error)) {
      ::close(link_fd_);
      link_fd_ = -1;
      return false;
    }
  } else if (!wait_enabled(pid, error)) {
    stop_pid(pid, process_starttime(pid), kStopTimeoutMs);
    return false;
  }
  const std::uint64_t starttime = paths_.fd_passing ? 0 : process_starttime(pid);
  if (!paths_.fd_passing && starttime == 0) {
    stop_pid(pid, 0, kStopTimeoutMs);
    if (error != nullptr) *error = "cannot read loader starttime";
    return false;
  }
  std::vector<PinEntry> owned_pins;
  auto abort_attach = [&]() -> bool {
    if (paths_.fd_passing) {
      if (link_fd_ >= 0) {
        ::close(link_fd_);
        link_fd_ = -1;
      }
      if (!wait_disabled(kStopTimeoutMs)) {
        if (error != nullptr) *error = "link fd closed but sched_ext is still enabled";
        return false;
      }
    } else {
      if (!stop_pid(pid, starttime, kStopTimeoutMs)) {
        if (error != nullptr) *error = "loader did not stop during apply abort";
        return false;
      }
    }
    bool conflict = false;
    std::string cleanup_error;
    if (!cleanup_pins(owned_pins, &conflict, &cleanup_error)) {
      if (error != nullptr) *error = cleanup_error;
      return false;
    }
    remove_runtime();
    return true;
  };
  Runtime runtime;
  runtime.pid = pid;
  runtime.starttime = starttime;
  runtime.mode = mode;
  runtime.enable_seq = read_enable_seq();
  runtime.pins = wait_pins_settled(3000);
  owned_pins = runtime.pins;
  if (runtime.pins.empty()) {
    (void)abort_attach();
    if (error != nullptr) {
      *error = "loader enabled sched_ext but no pins appeared within 3s";
    }
    return false;
  }
  const Json parameters = plan.params.value("parameters", Json::object());
  if (!parameters.empty()) {
    if (!ctrl_get(&runtime.cfg_before, error)) {
      (void)abort_attach();
      return false;
    }
    if (!ctrl_set(parameters, &runtime.cfg_after, error)) {
      Json ignored;
      std::string restore_error;
      if (!ctrl_restore(runtime.cfg_before, &ignored, &restore_error) && error != nullptr) {
        *error += "; cfg restore failed: " + restore_error;
      }
      (void)abort_attach();
      return false;
    }
  }
  const Json targets = plan.params.value("targets", Json::array());
  for (const auto& target : targets) {
    if (!target.is_object()) continue;
    const std::string kind = target.value("kind", "");
    if (target.value("role", "") != "BACKGROUND") continue;
    if (kind == "pid") {
      const std::int64_t pid = target.value("pid", 0LL);
      if (pid <= 0) continue;
      if (!ctrl_class_set(pid, error)) {
        (void)abort_attach();
        return false;
      }
      runtime.bg_pids.push_back(pid);
    } else if (kind == "cgroup") {
      const std::string path = target.value("path", "");
      if (path.empty()) continue;
      runtime.bg_cgroups.push_back(path);
      for (const auto pid : cgroup_pids(path)) {
        if (!ctrl_class_set(pid, error)) {
          (void)abort_attach();
          return false;
        }
      }
    }
  }
  if (!write_runtime(runtime, error)) {
    (void)abort_attach();
    return false;
  }
  if (out != nullptr) out->data = runtime.to_json();
  return true;
}

std::vector<std::int64_t> SchedSkill::cgroup_pids(const std::string& path) const {
  std::vector<std::int64_t> pids;
  std::ifstream input(path + "/cgroup.procs");
  if (!input) return pids;
  std::string line;
  while (std::getline(input, line) && pids.size() < 256) {
    try {
      const std::int64_t pid = std::stoll(line);
      if (pid > 0) pids.push_back(pid);
    } catch (const std::exception&) {
      // ignore malformed lines
    }
  }
  return pids;
}

bool SchedSkill::refresh(const Applied& applied, const RefreshContext& context,
                         std::string* error) {
  if (!applied.data.is_object()) return true;
  if (context.deadline_ms <= 0) return true;
  auto remaining = [&context]() { return context.deadline_ms - now_epoch_ms(); };
  auto per_call = [&]() -> std::uint64_t {
    const std::int64_t left = remaining();
    if (left <= 0) return 0;
    const std::uint64_t limit =
        paths_.ctrl_timeout_ms == 0 ? 1 : paths_.ctrl_timeout_ms;
    return static_cast<std::uint64_t>(left) < limit
               ? static_cast<std::uint64_t>(left)
               : limit;
  };
  auto stop = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };

  for (const auto& item : applied.data.value("bg_pids", Json::array())) {
    if (!item.is_number_integer() && !item.is_number_unsigned()) continue;
    const std::uint64_t budget = per_call();
    if (budget == 0) return stop("refresh budget exhausted before bg pid refresh");
    if (!ctrl_class_set(item.get<std::int64_t>(), error, budget)) return false;
  }
  for (const auto& item : applied.data.value("bg_cgroups", Json::array())) {
    if (!item.is_string()) continue;
    const std::string path = item.get<std::string>();
    for (const auto pid : cgroup_pids(path)) {
      const std::uint64_t budget = per_call();
      if (budget == 0) return stop("refresh budget exhausted during cgroup refresh");
      if (!ctrl_class_set(pid, error, budget)) return false;
    }
  }
  return true;
}

VerifyResult SchedSkill::verify(const Applied& applied, std::int64_t) {
  Runtime runtime;
  if (!Runtime::parse(applied.data, &runtime)) {
    return VerifyResult{false, "applied data cannot be parsed as sched runtime"};
  }
  if (paths_.fd_passing) {
    if (link_fd_ < 0) {
      return VerifyResult{false, "executor does not hold the struct_ops link fd"};
    }
  } else if (!pid_matches(runtime.pid, runtime.starttime)) {
    return VerifyResult{false, "loader process is gone or was replaced"};
  }
  const std::string state = read_state();
  if (state != "enabled") {
    return VerifyResult{false, "sched_ext state is '" + state + "', expected enabled"};
  }
  const std::string ops = read_ops();
  if (ops.find("eulerpilot") == std::string::npos) {
    return VerifyResult{false, "ops does not contain eulerpilot: " + ops};
  }
  if (read_enable_seq() != runtime.enable_seq) {
    return VerifyResult{false, "enable_seq changed since attach"};
  }
  if (!runtime.cfg_after.is_null()) {
    std::string cfg_error;
    if (!ctrl_verify(runtime.cfg_after, &cfg_error)) {
      return VerifyResult{false, "cfg verify failed: " + cfg_error};
    }
  }
  return VerifyResult{true, "loader pid=" + std::to_string(runtime.pid) + " mode=" +
                                runtime.mode + " enable_seq=" +
                                std::to_string(runtime.enable_seq)};
}

bool SchedSkill::cleanup_pins(const std::vector<PinEntry>& owned, bool* conflict,
                              std::string* error) const {
  if (conflict != nullptr) *conflict = false;
  const auto current = list_pins();
  // Preflight every pin before deleting any: a foreign/replaced pin must not
  // leave this instance with a half-cleaned set.
  for (const auto& pin : current) {
    bool match = false;
    for (const auto& candidate : owned) {
      if (candidate.name == pin.name && candidate.inode == pin.inode &&
          candidate.device == pin.device) {
        match = true;
        break;
      }
    }
    if (!match) {
      if (conflict != nullptr) *conflict = true;
      if (error != nullptr) {
        *error = "pin '" + pin.name + "' was not created by this instance; refusing to remove";
      }
      return false;
    }
  }
  for (const auto& pin : current) {
    const std::filesystem::path path = std::filesystem::path(pin_dir()) / pin.name;
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
      if (error != nullptr) *error = "cannot unlink " + path.string() + ": " + std::strerror(errno);
      return false;
    }
  }
  std::error_code ec;
  const std::filesystem::path dir(pin_dir());
  if (std::filesystem::exists(dir, ec) && std::filesystem::is_empty(dir, ec)) {
    std::filesystem::remove(dir, ec);
  }
  return true;
}

RecoveryResult SchedSkill::rollback(const Applied& applied, const Snapshot&, std::string* error) {
  Runtime runtime;
  const bool has_runtime = Runtime::parse(applied.data, &runtime);
  bool conflict = false;
  std::string cfg_warning;
  const bool link_held = paths_.fd_passing && link_fd_ >= 0;
  if (has_runtime && !runtime.cfg_before.is_null() &&
      (link_held || (runtime.pid > 0 && pid_matches(runtime.pid, runtime.starttime)))) {
    Json ignored;
    std::string restore_error;
    if (!ctrl_restore(runtime.cfg_before, &ignored, &restore_error)) {
      cfg_warning = "; cfg restore failed: " + restore_error;
    }
  }
  if (paths_.fd_passing) {
    if (link_fd_ >= 0) {
      ::close(link_fd_);
      link_fd_ = -1;
    }
    if (!wait_disabled(kStopTimeoutMs)) {
      return RecoveryResult{false, true,
                            "link fd closed but sched_ext is still enabled" + cfg_warning};
    }
  } else if (has_runtime && runtime.pid > 0) {
    if (!stop_pid(runtime.pid, runtime.starttime, kStopTimeoutMs)) {
      return RecoveryResult{false, true,
                            "loader did not exit after SIGTERM/SIGKILL" + cfg_warning};
    }
  }
  const std::string state = read_state();
  const std::string ops = read_ops();
  if (state == "enabled" && ops.find("eulerpilot") != std::string::npos) {
    return RecoveryResult{false, true,
                          "an eulerpilot instance is still attached but is not owned by this "
                          "action" + cfg_warning};
  }
  if (!has_runtime) {
    const auto pins = list_pins();
    if (!pins.empty()) {
      return RecoveryResult{false, true, "pins exist without an owned runtime marker"};
    }
    return RecoveryResult{true, false, "no scheduler was attached" + cfg_warning};
  }
  if (!cleanup_pins(runtime.pins, &conflict, error)) {
    return RecoveryResult{false, conflict, *error};
  }
  remove_runtime();
  return RecoveryResult{true, false, "detached and removed " + std::to_string(runtime.pins.size()) +
                                         " owned pin(s)" + cfg_warning};
}

VerifyResult SchedSkill::verify_rolled_back(const Snapshot&, std::int64_t) {
  if (paths_.fd_passing && link_fd_ >= 0) {
    return VerifyResult{false, "executor still holds the struct_ops link fd"};
  }
  const std::string state = read_state();
  if (state != "disabled") {
    return VerifyResult{false, "sched_ext state is '" + state + "' after rollback"};
  }
  const std::string ops = read_ops();
  if (ops.find("eulerpilot") != std::string::npos) {
    return VerifyResult{false, "eulerpilot is still registered after rollback: " + ops};
  }
  const auto pins = list_pins();
  if (!pins.empty()) {
    std::string names;
    for (const auto& pin : pins) names += pin.name + " ";
    return VerifyResult{false, "pins remain after rollback: " + names};
  }
  if (std::filesystem::exists(std::filesystem::path(paths_.state_dir) / "sched-runtime.json")) {
    return VerifyResult{false, "runtime marker remains after rollback"};
  }
  return VerifyResult{true, "detached, pins cleaned, runtime marker removed"};
}

RecoveryResult SchedSkill::reconcile(const ReconcileContext&, std::string* error) {
  return recover_orphan(error);
}

RecoveryResult SchedSkill::recover_orphan(std::string* error) {
  Runtime runtime;
  const bool has_runtime = read_runtime(&runtime);
  bool conflict = false;
  std::string cfg_warning;
  if (has_runtime && !runtime.cfg_before.is_null() && runtime.pid > 0 &&
      pid_matches(runtime.pid, runtime.starttime)) {
    Json ignored;
    std::string restore_error;
    if (!ctrl_restore(runtime.cfg_before, &ignored, &restore_error)) {
      cfg_warning = "; cfg restore failed: " + restore_error;
    }
  }
  if (has_runtime) {
    if (runtime.pid > 0 && pid_matches(runtime.pid, runtime.starttime)) {
      if (!stop_pid(runtime.pid, runtime.starttime, kStopTimeoutMs)) {
        return RecoveryResult{false, true, "orphan loader did not exit" + cfg_warning};
      }
    }
    const std::string state = read_state();
    const std::string ops = read_ops();
    if (state == "enabled" && ops.find("eulerpilot") != std::string::npos) {
      return RecoveryResult{false, true,
                            "eulerpilot still attached after stopping the owned loader" +
                                cfg_warning};
    }
    if (!cleanup_pins(runtime.pins, &conflict, error)) {
      return RecoveryResult{false, conflict, *error};
    }
    remove_runtime();
    return RecoveryResult{true, false,
                          "stopped orphan loader and removed owned pins" + cfg_warning};
  }
  const std::string state = read_state();
  const std::string ops = read_ops();
  if (state == "enabled" && ops.find("eulerpilot") != std::string::npos) {
    return RecoveryResult{false, true, "eulerpilot is attached but no runtime marker exists"};
  }
  if (!list_pins().empty()) {
    return RecoveryResult{false, true, "pins exist but no runtime marker exists"};
  }
  return RecoveryResult{true, false, "nothing to reconcile"};
}

}  // namespace ep
