#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ep/target.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace ep {
namespace {

std::string read_trimmed(const std::string& path) {
  std::ifstream input(path);
  if (!input) return "";
  std::stringstream buffer;
  buffer << input.rdbuf();
  std::string text = buffer.str();
  while (!text.empty() &&
         (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.pop_back();
  }
  return text;
}

std::string boot_id() { return read_trimmed("/proc/sys/kernel/random/boot_id"); }

bool stat_cgroup(const std::string& path, std::uint64_t* inode, std::uint64_t* device,
                 std::string* error) {
  const int fd = ::open(path.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    if (error != nullptr) {
      *error = "cannot open target '" + path + "': " + std::strerror(errno);
    }
    return false;
  }
  const int controllers = ::openat(fd, "cgroup.controllers", O_RDONLY | O_CLOEXEC);
  if (controllers < 0) {
    ::close(fd);
    if (error != nullptr) {
      *error = "target '" + path + "' is not a cgroup v2 directory (no cgroup.controllers)";
    }
    return false;
  }
  ::close(controllers);
  struct stat info {};
  if (::fstat(fd, &info) != 0) {
    ::close(fd);
    if (error != nullptr) {
      *error = "fstat target '" + path + "' failed: " + std::strerror(errno);
    }
    return false;
  }
  ::close(fd);
  if (!S_ISDIR(info.st_mode)) {
    if (error != nullptr) *error = "target '" + path + "' is not a directory";
    return false;
  }
  *inode = static_cast<std::uint64_t>(info.st_ino);
  *device = static_cast<std::uint64_t>(info.st_dev);
  return true;
}

bool read_cgroup_id(const std::string& path, std::uint64_t* cgroup_id) {
  // cgroup2 is kernfs; its file handle is the kernel cgroup id (8 bytes).
  std::vector<unsigned char> storage(sizeof(struct file_handle) + 8, 0);
  auto* handle = reinterpret_cast<struct file_handle*>(storage.data());
  handle->handle_bytes = 8;
  int mount_id = 0;
  if (::name_to_handle_at(AT_FDCWD, path.c_str(), handle, &mount_id, 0) != 0) {
    return false;
  }
  if (handle->handle_bytes != 8) return false;
  std::uint64_t value = 0;
  std::memcpy(&value, handle->f_handle, 8);
  if (value == 0) return false;
  *cgroup_id = value;
  return true;
}

std::uint64_t process_starttime(std::int64_t pid) {
  std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
  if (!input) return 0;
  std::string stat;
  std::getline(input, stat);
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

bool open_pidfd(std::int64_t pid, int* fd, std::string* error) {
#ifdef SYS_pidfd_open
  const int result = static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0));
  if (result < 0) {
    if (error != nullptr) {
      *error = "cannot open pidfd for " + std::to_string(pid) + ": " + std::strerror(errno);
    }
    return false;
  }
  *fd = result;
  return true;
#else
  (void)pid;
  (void)fd;
  if (error != nullptr) *error = "pidfd_open is not available on this build";
  return false;
#endif
}

std::shared_ptr<int> hold_pidfd(int fd) {
  return std::shared_ptr<int>(new int(fd), [](int* value) {
    if (value != nullptr && *value >= 0) ::close(*value);
    delete value;
  });
}

bool pidfd_alive(int fd) {
#ifdef SYS_pidfd_send_signal
  const int result = static_cast<int>(::syscall(SYS_pidfd_send_signal, fd, 0, nullptr, 0));
  if (result == 0) return true;
  // EPERM/EINVAL do not prove death; only ESRCH does.
  return errno != ESRCH;
#else
  (void)fd;
  return true;
#endif
}

}  // namespace

Json TargetBinding::to_json() const {
  return Json{{"kind", kind},
              {"path", path},
              {"role", role},
              {"role_source", role_source},
              {"identity", kind == "pid" ? "pidfd+starttime" : "cgroup2-kernfs"},
              {"inode", inode},
              {"device", device},
              {"cgroup_id", cgroup_id},
              {"pid", pid},
              {"starttime", starttime},
              {"boot_id", boot_id}};
}

bool resolve_targets(const std::vector<PolicyTarget>& targets,
                     std::vector<TargetBinding>* bindings, std::string* error) {
  const std::string current_boot = boot_id();
  if (current_boot.empty()) {
    if (error != nullptr) *error = "cannot read /proc/sys/kernel/random/boot_id";
    return false;
  }
  std::vector<TargetBinding> resolved;
  resolved.reserve(targets.size());
  for (const auto& target : targets) {
    TargetBinding binding;
    binding.kind = target.kind;
    binding.path = target.path;
    binding.role = target.role;
    binding.role_source = target.role_source;
    binding.boot_id = current_boot;
    if (target.kind == "pid") {
      int pidfd = -1;
      if (!open_pidfd(target.pid, &pidfd, error)) return false;
      binding.pid = target.pid;
      binding.starttime = process_starttime(target.pid);
      if (binding.starttime == 0) {
        ::close(pidfd);
        if (error != nullptr) {
          *error = "cannot read starttime for pid " + std::to_string(target.pid);
        }
        return false;
      }
      binding.pidfd = hold_pidfd(pidfd);
    } else if (target.kind == "cgroup") {
      if (!stat_cgroup(target.path, &binding.inode, &binding.device, error)) return false;
      read_cgroup_id(target.path, &binding.cgroup_id);
    } else {
      if (error != nullptr) *error = "unsupported target kind: " + target.kind;
      return false;
    }
    resolved.push_back(std::move(binding));
  }
  if (bindings != nullptr) *bindings = std::move(resolved);
  return true;
}

bool revalidate_targets(const std::vector<TargetBinding>& bindings, std::string* error) {
  const std::string current_boot = boot_id();
  for (const auto& binding : bindings) {
    if (binding.boot_id != current_boot) {
      if (error != nullptr) *error = "boot_id changed since the target was resolved";
      return false;
    }
    if (binding.kind == "pid") {
      if (binding.pidfd != nullptr && *binding.pidfd >= 0) {
        if (!pidfd_alive(*binding.pidfd)) {
          if (error != nullptr) {
            *error = "pid " + std::to_string(binding.pid) + " is gone (held pidfd)";
          }
          return false;
        }
      } else {
        int pidfd = -1;
        if (!open_pidfd(binding.pid, &pidfd, error)) return false;
        ::close(pidfd);
      }
      if (process_starttime(binding.pid) != binding.starttime) {
        if (error != nullptr) {
          *error = "pid " + std::to_string(binding.pid) +
                   " was replaced (starttime changed)";
        }
        return false;
      }
      continue;
    }
    std::uint64_t inode = 0;
    std::uint64_t device = 0;
    if (!stat_cgroup(binding.path, &inode, &device, error)) return false;
    if (inode != binding.inode || device != binding.device) {
      if (error != nullptr) {
        *error = "target '" + binding.path + "' was replaced (inode/device changed)";
      }
      return false;
    }
    if (binding.cgroup_id != 0) {
      std::uint64_t current_id = 0;
      if (!read_cgroup_id(binding.path, &current_id) || current_id != binding.cgroup_id) {
        if (error != nullptr) {
          *error = "target '" + binding.path + "' cgroup_id changed or disappeared";
        }
        return false;
      }
    }
  }
  return true;
}

}  // namespace ep
