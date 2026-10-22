#include "ep/ownership.hpp"

#include "ep/domain.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ep {
namespace {

bool write_all(int fd, const std::string& data, std::string* error) {
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (error != nullptr) *error = std::string("write failed: ") + std::strerror(errno);
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  return true;
}

bool fsync_dir(const std::filesystem::path& dir, std::string* error) {
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    if (error != nullptr) *error = std::string("open dir failed: ") + std::strerror(errno);
    return false;
  }
  const bool ok = ::fsync(fd) == 0;
  if (!ok && error != nullptr) *error = std::string("fsync dir failed: ") + std::strerror(errno);
  ::close(fd);
  return ok;
}

}  // namespace

Json LeaseState::to_json() const {
  Json value{{"owner", owner}, {"fence_epoch", fence}, {"expires_ms", expires_ms}};
  if (expires_ms > 0) value["expires_at"] = iso8601_from_ms(expires_ms);
  return value;
}

Ownership::Ownership(std::filesystem::path dir) : dir_(std::move(dir)) {
  lock_path_ = dir_ / "executor.lock";
  state_path_ = dir_ / "state.json";
}

Ownership::~Ownership() {
  if (lock_fd_ >= 0) {
    ::flock(lock_fd_, LOCK_UN);
    ::close(lock_fd_);
  }
  lock_fd_ = -1;
}

bool Ownership::open(std::string* error) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    if (error != nullptr) *error = "cannot create state dir: " + ec.message();
    return false;
  }

  lock_fd_ = ::open(lock_path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_fd_ < 0) {
    if (error != nullptr) *error = std::string("cannot open lock: ") + std::strerror(errno);
    return false;
  }
  if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
    if (error != nullptr) {
      *error = errno == EWOULDBLOCK
                   ? "another executor instance holds " + lock_path_.string()
                   : std::string("flock failed: ") + std::strerror(errno);
    }
    return false;
  }

  if (std::filesystem::exists(state_path_)) {
    std::ifstream input(state_path_);
    std::stringstream buffer;
    buffer << input.rdbuf();
    Json state;
    try {
      state = Json::parse(buffer.str());
    } catch (const std::exception&) {
      if (error != nullptr) *error = "cannot parse " + state_path_.string();
      return false;
    }
    try {
      instance_id_ = state.at("instance_id").get<std::string>();
      generation_ = state.value("generation", 0ULL);
      const Json lease = state.value("lease", Json::object());
      lease_.owner = lease.value("owner", "");
      lease_.fence = lease.value("fence_epoch", 0ULL);
      lease_.expires_ms = lease.value("expires_ms", 0LL);
    } catch (const std::exception&) {
      if (error != nullptr) *error = "state.json missing required fields";
      return false;
    }
    if (instance_id_.empty()) {
      if (error != nullptr) *error = "state.json has an empty instance_id";
      return false;
    }
    return true;
  }

  instance_id_ = "inst-" + random_hex(8);
  generation_ = 0;
  lease_ = LeaseState{};
  return persist(error);
}

bool Ownership::persist(std::string* error) {
  Json state{{"apiVersion", kApiVersion},
             {"instance_id", instance_id_},
             {"generation", generation_},
             {"lease", lease_.to_json()},
             {"updated_at", now_iso8601()}};
  const std::string text = state.dump(2) + "\n";
  const std::filesystem::path tmp =
      dir_ / ("state.json.tmp." + std::to_string(static_cast<long>(::getpid())));
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (error != nullptr) *error = std::string("cannot write state: ") + std::strerror(errno);
    return false;
  }
  bool ok = write_all(fd, text, error);
  if (ok && ::fsync(fd) != 0) {
    ok = false;
    if (error != nullptr) *error = std::string("fsync state failed: ") + std::strerror(errno);
  }
  ::close(fd);
  if (!ok) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, state_path_, ec);
  if (ec) {
    if (error != nullptr) *error = "rename state failed: " + ec.message();
    return false;
  }
  return fsync_dir(dir_, error);
}

bool Ownership::commit_generation(std::uint64_t generation, std::string* error) {
  generation_ = generation;
  return persist(error);
}

bool Ownership::acquire_lease(const std::string& owner, std::uint64_t ttl_ms,
                              LeaseState* out, std::string* error) {
  if (owner.size() < 8 || owner.size() > 128) {
    if (error != nullptr) *error = "lease_owner must be 8..128 characters";
    return false;
  }
  std::uint64_t ttl = ttl_ms;
  if (ttl < 1000) ttl = 1000;
  if (ttl > 3600000) ttl = 3600000;
  lease_.owner = owner;
  lease_.fence += 1;
  lease_.expires_ms = now_epoch_ms() + static_cast<std::int64_t>(ttl);
  if (!persist(error)) return false;
  if (out != nullptr) *out = lease_;
  return true;
}

bool Ownership::validate_lease(const std::string& owner, std::uint64_t fence,
                               std::string* error) const {
  if (lease_.owner.empty()) {
    if (error != nullptr) *error = "no active lease; acquire one first";
    return false;
  }
  if (owner != lease_.owner) {
    if (error != nullptr) *error = "lease owner mismatch";
    return false;
  }
  if (fence != lease_.fence) {
    if (error != nullptr) {
      *error = "stale fence_epoch " + std::to_string(fence) + ", current " +
               std::to_string(lease_.fence);
    }
    return false;
  }
  if (now_epoch_ms() >= lease_.expires_ms) {
    if (error != nullptr) *error = "lease expired";
    return false;
  }
  return true;
}

}  // namespace ep
