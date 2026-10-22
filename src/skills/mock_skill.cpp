#include "ep/mock_skill.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ep {
namespace {

constexpr int kMinValue = 0;
constexpr int kMaxValue = 1000000;

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

}  // namespace

MockSkill::MockSkill(std::filesystem::path state_dir) : dir_(std::move(state_dir)) {
  file_ = dir_ / "value.json";
}

SkillProbe MockSkill::probe() {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) return SkillProbe{false, "cannot create mock state dir: " + ec.message()};
  const std::filesystem::path probe_file = dir_ / ".probe";
  const int fd = ::open(probe_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    return SkillProbe{false, std::string("mock state dir is not writable: ") + std::strerror(errno)};
  }
  ::close(fd);
  std::filesystem::remove(probe_file, ec);
  return SkillProbe{true, "mock state dir writable at " + dir_.string()};
}

bool MockSkill::plan(const Json& intent, Plan* out, std::string* error) {
  if (!intent.is_object() || !intent.contains("value") || !intent["value"].is_number_integer()) {
    if (error != nullptr) *error = "intent must be an object with integer field 'value'";
    return false;
  }
  const int value = intent["value"].get<int>();
  if (value < kMinValue || value > kMaxValue) {
    if (error != nullptr) *error = "value out of range [" + std::to_string(kMinValue) + "," +
                                   std::to_string(kMaxValue) + "]";
    return false;
  }
  Plan plan;
  plan.intent = intent;
  plan.params = Json{{"value", value}};
  if (out != nullptr) *out = std::move(plan);
  return true;
}

bool MockSkill::read_current(int* value, bool* exists, std::string* error) const {
  std::error_code ec;
  *exists = std::filesystem::exists(file_, ec);
  if (!*exists) {
    *value = 0;
    return true;
  }
  std::ifstream input(file_);
  if (!input) {
    if (error != nullptr) *error = "cannot read " + file_.string();
    return false;
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  try {
    const Json state = Json::parse(buffer.str());
    *value = state.at("value").get<int>();
  } catch (const std::exception&) {
    if (error != nullptr) *error = "corrupt mock state in " + file_.string();
    return false;
  }
  return true;
}

bool MockSkill::write_value(int value, std::string* error) const {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    if (error != nullptr) *error = "cannot create mock dir: " + ec.message();
    return false;
  }
  const std::string text = Json{{"value", value}}.dump() + "\n";
  const std::filesystem::path tmp =
      dir_ / ("value.json.tmp." + std::to_string(static_cast<long>(::getpid())));
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (error != nullptr) *error = std::string("cannot write mock state: ") + std::strerror(errno);
    return false;
  }
  bool ok = write_all(fd, text, error);
  if (ok && ::fsync(fd) != 0) {
    ok = false;
    if (error != nullptr) *error = std::string("fsync mock state failed: ") + std::strerror(errno);
  }
  ::close(fd);
  if (!ok) return false;
  std::filesystem::rename(tmp, file_, ec);
  if (ec) {
    if (error != nullptr) *error = "rename mock state failed: " + ec.message();
    return false;
  }
  return true;
}

bool MockSkill::restore(const Json& snapshot, std::string* error) const {
  if (snapshot.value("exists", false)) {
    return write_value(snapshot.at("value").get<int>(), error);
  }
  std::error_code ec;
  std::filesystem::remove(file_, ec);
  if (ec) {
    if (error != nullptr) *error = "cannot remove mock state: " + ec.message();
    return false;
  }
  return true;
}

bool MockSkill::snapshot(const Plan& plan, Snapshot* out, std::string* error) {
  (void)plan;
  int value = 0;
  bool exists = false;
  if (!read_current(&value, &exists, error)) return false;
  Snapshot snapshot;
  snapshot.data = Json{{"exists", exists}};
  if (exists) snapshot.data["value"] = value;
  snapshot.id = "snap-" + sha256_hex(canonical_json(snapshot.data) + random_hex(8)).substr(0, 32);
  if (out != nullptr) *out = std::move(snapshot);
  return true;
}

bool MockSkill::apply(const Plan& plan, const Snapshot& snapshot, const LeaseState&,
                      Applied* out, std::string* error) {
  int current = 0;
  bool exists = false;
  if (!read_current(&current, &exists, error)) return false;
  const bool snapshot_exists = snapshot.data.value("exists", false);
  if (exists != snapshot_exists ||
      (exists && current != snapshot.data.at("value").get<int>())) {
    if (error != nullptr) {
      *error = "state changed since snapshot (expected " +
               (snapshot_exists ? std::to_string(snapshot.data.at("value").get<int>())
                                : std::string("absent")) +
               ", found " + (exists ? std::to_string(current) : std::string("absent")) + ")";
    }
    return false;
  }
  const int value = plan.params.at("value").get<int>();
  if (!write_value(value, error)) return false;
  if (out != nullptr) out->data = Json{{"value", value}};
  return true;
}

VerifyResult MockSkill::verify(const Applied& applied, std::int64_t) {
  if (!applied.data.contains("value")) return VerifyResult{false, "applied data has no value"};
  int current = 0;
  bool exists = false;
  std::string error;
  if (!read_current(&current, &exists, &error)) return VerifyResult{false, error};
  if (!exists) return VerifyResult{false, "state file is missing after apply"};
  const int expected = applied.data.at("value").get<int>();
  if (current != expected) {
    return VerifyResult{false, "expected " + std::to_string(expected) + ", found " +
                                   std::to_string(current)};
  }
  return VerifyResult{true, "state matches applied value " + std::to_string(expected)};
}

RecoveryResult MockSkill::rollback(const Applied& applied, const Snapshot& snapshot,
                                   std::string* error) {
  int current = 0;
  bool exists = false;
  if (!read_current(&current, &exists, error)) return RecoveryResult{false, true, *error};

  // If apply completed, refuse to overwrite a value that was changed by anyone
  // else in the meantime.
  if (applied.data.contains("value")) {
    const int expected = applied.data.at("value").get<int>();
    if (!exists || current != expected) {
      const std::string detail =
          "current state does not match the applied value; external change detected";
      if (error != nullptr) *error = detail;
      return RecoveryResult{false, true, detail};
    }
  }

  if (!restore(snapshot.data, error)) return RecoveryResult{false, false, *error};
  return RecoveryResult{true, false, "restored snapshot " + snapshot.id};
}

VerifyResult MockSkill::verify_rolled_back(const Snapshot& snapshot, std::int64_t) {
  int current = 0;
  bool exists = false;
  std::string error;
  if (!read_current(&current, &exists, &error)) return VerifyResult{false, error};
  const bool snapshot_exists = snapshot.data.value("exists", false);
  if (exists != snapshot_exists) {
    return VerifyResult{false, "state existence differs from snapshot"};
  }
  if (exists) {
    const int expected = snapshot.data.at("value").get<int>();
    if (current != expected) {
      return VerifyResult{false, "expected " + std::to_string(expected) + ", found " +
                                     std::to_string(current)};
    }
  }
  return VerifyResult{true, "state matches snapshot"};
}

RecoveryResult MockSkill::reconcile(const ReconcileContext& context, std::string* error) {
  if (!context.prepared.contains("snapshot") || !context.prepared.contains("intent")) {
    const std::string detail = "journal context lacks snapshot/intent";
    if (error != nullptr) *error = detail;
    return RecoveryResult{false, true, detail};
  }
  const Json snapshot = context.prepared.at("snapshot");
  const Json intent = context.prepared.at("intent");
  if (!intent.contains("value")) {
    const std::string detail = "journal intent lacks value";
    if (error != nullptr) *error = detail;
    return RecoveryResult{false, true, detail};
  }

  int current = 0;
  bool exists = false;
  if (!read_current(&current, &exists, error)) return RecoveryResult{false, true, *error};

  const bool snapshot_exists = snapshot.value("exists", false);
  const bool at_snapshot =
      exists == snapshot_exists &&
      (!exists || current == snapshot.at("value").get<int>());
  const bool at_intent = exists && current == intent.at("value").get<int>();

  if (at_snapshot) {
    return RecoveryResult{true, false, "state already matches the snapshot"};
  }
  if (at_intent) {
    if (!restore(snapshot, error)) return RecoveryResult{false, false, *error};
    return RecoveryResult{true, false, "applied value detected and rolled back to snapshot"};
  }
  const std::string detail = "state matches neither snapshot nor intent; external change or "
                             "unknown partial write";
  if (error != nullptr) *error = detail;
  return RecoveryResult{false, true, detail};
}

}  // namespace ep
