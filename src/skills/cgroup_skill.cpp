#include "ep/cgroup_skill.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ep/cgroup_policy.hpp"
#include "ep/util.hpp"

namespace ep {
namespace {

std::string trim(std::string value) {
  const char* space = " \t\r\n";
  const std::size_t begin = value.find_first_not_of(space);
  if (begin == std::string::npos) return "";
  const std::size_t end = value.find_last_not_of(space);
  return value.substr(begin, end - begin + 1);
}

bool same_value(const std::string& left, const std::string& right) {
  if (left == right) return true;
  try {
    return std::stoll(left) == std::stoll(right);
  } catch (const std::exception&) {
    return false;
  }
}

bool effect_states_from(const Json& value, std::vector<CgroupSkillEffect>* out) {
  if (!value.is_object() || !value.contains("effects") || !value.at("effects").is_array()) {
    return false;
  }
  for (const Json& item : value.at("effects")) {
    if (!item.is_object()) return false;
    CgroupSkillEffect state;
    state.path = item.value("path", "");
    state.file = item.value("file", "");
    state.desired = item.value("desired", 0LL);
    state.previous = item.value("previous", "");
    if (state.path.empty() || state.file.empty()) return false;
    out->push_back(state);
  }
  return !out->empty();
}

}  // namespace

// Declared paths are absolute in the contract; the root is swapped so a test can run the
// same code against a directory of plain files.
std::string cgroup_knob_file(const std::string& cgroup_root, const std::string& declared_prefix,
                             const std::string& declared_path, const std::string& knob) {
  std::string relative = declared_path;
  if (relative.rfind(declared_prefix, 0) == 0) {
    relative = relative.substr(declared_prefix.size());
  }
  while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
  std::filesystem::path file = std::filesystem::path(cgroup_root) / relative;
  file /= knob;
  return file.string();
}

std::vector<CgroupSkillEffect> cgroup_effects(const std::vector<CgroupEffect>& declared,
                                             const std::string& cgroup_root,
                                             const std::string& declared_prefix) {
  std::vector<CgroupSkillEffect> out;
  for (const auto& effect : declared) {
    CgroupSkillEffect state;
    state.path = effect.path;
    state.file = cgroup_knob_file(cgroup_root, declared_prefix, effect.path, "cpu.weight");
    state.desired = effect.cpu_weight;
    out.push_back(state);
  }
  return out;
}

CgroupSkill::CgroupSkill(CgroupPaths paths) : paths_(std::move(paths)) {}

std::filesystem::path CgroupSkill::marker_path() const {
  return paths_.state_dir / "cgroup-effects.json";
}

SkillProbe CgroupSkill::probe() {
  std::error_code ec;
  const std::filesystem::path controllers =
      std::filesystem::path(paths_.cgroup_root) / "cgroup.controllers";
  if (!std::filesystem::exists(controllers, ec)) {
    return SkillProbe{false, "no cgroup v2 hierarchy at " + paths_.cgroup_root};
  }
  std::ifstream input(controllers);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (text.find("cpu") == std::string::npos) {
    return SkillProbe{false, "cpu controller is not available in " + controllers.string()};
  }
  std::filesystem::create_directories(paths_.state_dir, ec);
  if (ec) {
    return SkillProbe{false, "cannot create state dir: " + ec.message()};
  }
  return SkillProbe{true, "cgroup v2 with cpu controller; effects are file writes with read-back"};
}

bool CgroupSkill::plan(const Json& intent, Plan* out, std::string* error) {
  CgroupPolicySummary summary;
  if (!validate_cgroup_policy(intent, &summary, error)) return false;
  std::vector<CgroupSkillEffect> effects =
      cgroup_effects(summary.effects, paths_.cgroup_root, paths_.declared_prefix);
  Json array = Json::array();
  for (const auto& effect : effects) {
    // The contract spells the knob `cpu_weight`; inside the skill every record carries
    // `desired` so a snapshot, an applied effect and a marker cannot drift apart by name.
    // This is the only place the two spellings meet.
    array.push_back(Json{{"path", effect.path},
                         {"file", effect.file},
                         {"desired", effect.desired}});
  }
  Plan plan;
  plan.intent = intent;
  plan.params = Json{{"domain", "cgroup"}, {"effects", array},
                     {"policy_name", summary.name},
                     {"safety", summary.safety}};
  if (out != nullptr) *out = std::move(plan);
  return true;
}

bool CgroupSkill::read_knob(const std::string& file, std::string* value, std::string* error) const {
  std::ifstream input(file);
  if (!input) {
    if (error != nullptr) {
      *error = "cannot read " + file + ": " + std::strerror(errno) +
               " (is the cpu controller enabled for this cgroup?)";
    }
    return false;
  }
  std::string text;
  std::getline(input, text);
  *value = trim(text);
  if (value->empty()) {
    if (error != nullptr) *error = file + " returned an empty value";
    return false;
  }
  return true;
}

bool CgroupSkill::write_knob(const std::string& file, long long value, std::string* error) const {
  // O_TRUNC matters even though the target is "just" a knob: writing a shorter value
  // over a longer one leaves the tail behind (200 -> 50 reads back as 500). kernfs
  // ignores the flag, regular files in tests do not.
  const int fd = ::open(file.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (fd < 0) {
    if (error != nullptr) *error = "cannot open " + file + ": " + std::strerror(errno);
    return false;
  }
  const std::string text = std::to_string(value);
  const ssize_t written = ::write(fd, text.data(), text.size());
  const int saved = errno;
  ::close(fd);
  if (written != static_cast<ssize_t>(text.size())) {
    if (error != nullptr) *error = "cannot write " + file + ": " + std::strerror(saved);
    return false;
  }
  return true;
}

bool CgroupSkill::set_knob(const std::string& file, long long value, std::string* error) const {
  if (!write_knob(file, value, error)) return false;
  std::string read;
  if (!read_knob(file, &read, error)) return false;
  if (!same_value(read, std::to_string(value))) {
    if (error != nullptr) *error = "read-back of " + file + " is '" + read + "', not " +
                                     std::to_string(value);
    return false;
  }
  return true;
}

bool CgroupSkill::write_marker(const std::vector<CgroupSkillEffect>& effects,
                              const std::string& action_id, std::string* error) const {
  Json array = Json::array();
  for (const auto& effect : effects) {
    array.push_back(Json{{"path", effect.path},
                         {"file", effect.file},
                         {"desired", effect.desired},
                         {"previous", effect.previous}});
  }
  const Json marker{{"action_id", action_id}, {"effects", array}};
  std::error_code ec;
  std::filesystem::create_directories(paths_.state_dir, ec);
  const std::filesystem::path tmp = marker_path().string() + ".tmp";
  std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
  if (!output) {
    if (error != nullptr) *error = "cannot write " + tmp.string();
    return false;
  }
  output << marker.dump() << "\n";
  output.flush();
  if (!output) {
    if (error != nullptr) *error = "cannot flush " + tmp.string();
    return false;
  }
  output.close();
  std::filesystem::rename(tmp, marker_path(), ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    if (error != nullptr) *error = "cannot publish " + marker_path().string() + ": " + ec.message();
    return false;
  }
  return true;
}

bool CgroupSkill::read_marker(std::vector<CgroupSkillEffect>* effects, std::string* error) const {
  std::ifstream input(marker_path());
  if (!input) return false;
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  try {
    const Json marker = Json::parse(text);
    if (!effect_states_from(marker, effects)) throw std::runtime_error("malformed marker");
  } catch (const std::exception& exception) {
    if (error != nullptr) *error = "unparsable cgroup marker: " + std::string(exception.what());
    return false;
  }
  return true;
}

void CgroupSkill::remove_marker() const {
  std::error_code ec;
  std::filesystem::remove(marker_path(), ec);
}

bool CgroupSkill::snapshot(const Plan& plan, Snapshot* out, std::string* error) {
  if (!plan.params.contains("effects")) {
    if (error != nullptr) *error = "plan carries no effects";
    return false;
  }
  std::vector<CgroupSkillEffect> effects;
  if (!effect_states_from(Json{{"effects", plan.params.at("effects")}}, &effects)) {
    if (error != nullptr) *error = "plan effects are malformed";
    return false;
  }
  Json array = Json::array();
  for (auto& effect : effects) {
    std::string current;
    // Fail closed before writing anything: without the old value there is nothing to
    // compensate to, so a half-known effect must not be applied.
    if (!read_knob(effect.file, &current, error)) return false;
    effect.previous = current;
    array.push_back(Json{{"path", effect.path},
                         {"file", effect.file},
                         {"desired", effect.desired},
                         {"previous", current}});
  }
  Snapshot snapshot;
  snapshot.data = Json{{"domain", "cgroup"}, {"effects", array}};
  snapshot.id = "snap-" + sha256_hex(canonical_json(snapshot.data)).substr(0, 32);
  if (out != nullptr) *out = std::move(snapshot);
  return true;
}

bool CgroupSkill::apply(const Plan&, const Snapshot& snapshot, const LeaseState&,
                        Applied* out, std::string* error) {
  // The sched domain refuses a second attach at preflight; this is the cgroup
  // domain's equivalent. A live marker means an effect from a previous apply is
  // still in place (this process's, or an unrecovered leftover): writing over it
  // would overwrite the marker's previous values and make the old effect
  // uncompensable. Roll back or reconcile first.
  if (std::filesystem::exists(marker_path())) {
    if (error != nullptr) {
      *error = "a cgroup effect is still in place (" + marker_path().string() +
               "); roll it back or reconcile before applying another";
    }
    return false;
  }
  std::vector<CgroupSkillEffect> effects;
  if (!effect_states_from(snapshot.data, &effects)) {
    if (error != nullptr) *error = "snapshot does not describe cgroup effects";
    return false;
  }
  const std::string action_id = "cg-" + random_hex(12);
  if (!write_marker(effects, action_id, error)) return false;
  std::size_t written = 0;
  for (const auto& effect : effects) {
    std::string write_error;
    if (!set_knob(effect.file, effect.desired, &write_error)) {
      // Restore everything the snapshot knows about, not just what succeeded: the
      // unwritten knobs already hold their previous value, so re-writing them is a
      // no-op while a half-restored list is not.
      Applied partial;
      partial.data = Json{{"effects", snapshot.data.at("effects")}};
      std::string restore_error;
      const RecoveryResult compensation = rollback(partial, snapshot, &restore_error);
      if (!compensation.ok) {
        if (error != nullptr) {
          *error = write_error + "; compensation failed: " + compensation.detail;
        }
        return false;
      }
      if (error != nullptr) *error = write_error;
      return false;
    }
    ++written;
  }
  Applied applied;
  applied.data = Json{{"domain", "cgroup"},
                      {"action_id", action_id},
                      {"effects", snapshot.data.at("effects")},
                      {"written", static_cast<std::int64_t>(written)}};
  if (out != nullptr) *out = std::move(applied);
  return true;
}

std::string CgroupSkill::drift_report(const std::vector<CgroupSkillEffect>& effects) const {
  std::string report;
  for (const auto& effect : effects) {
    std::string current;
    std::string error;
    if (!read_knob(effect.file, &current, &error)) {
      report += (report.empty() ? "" : "; ") + effect.path + ": " + error;
      continue;
    }
    if (!same_value(current, std::to_string(effect.desired))) {
      report += (report.empty() ? "" : "; ") + effect.path + ": is " + current + ", want " +
                std::to_string(effect.desired);
    }
  }
  return report;
}

VerifyResult CgroupSkill::verify(const Applied& applied, std::int64_t) {
  std::vector<CgroupSkillEffect> effects;
  if (!effect_states_from(applied.data, &effects)) {
    return VerifyResult{false, "applied data does not describe cgroup effects"};
  }
  const std::string drift = drift_report(effects);
  if (!drift.empty()) return VerifyResult{false, "cpu.weight does not match the policy: " + drift};
  return VerifyResult{true, "all " + std::to_string(effects.size()) +
                              " cgroup effect(s) read back as declared"};
}

bool CgroupSkill::refresh(const Applied& applied, const std::vector<TargetBinding>&,
                          const RefreshContext&, std::string* error) {
  std::vector<CgroupSkillEffect> effects;
  if (!effect_states_from(applied.data, &effects)) {
    if (error != nullptr) *error = "applied data does not describe cgroup effects";
    return false;
  }
  for (const auto& effect : effects) {
    std::string current;
    std::string read_error;
    if (!read_knob(effect.file, &current, &read_error)) {
      if (error != nullptr) *error = read_error;
      return false;
    }
    if (same_value(current, std::to_string(effect.desired))) continue;
    // cgroup files are plain writes, so unlike the class map this can afford to be
    // re-asserted every cycle: an out-of-band change is corrected, not just observed.
    std::cerr << "[cgroup-skill] cpu.weight drift corrected: " << effect.path << " was "
              << current << ", restored to " << effect.desired << "\n";
    std::string write_error;
    if (!set_knob(effect.file, effect.desired, &write_error)) {
      if (error != nullptr) *error = write_error;
      return false;
    }
  }
  return true;
}

RecoveryResult CgroupSkill::rollback(const Applied& applied, const Snapshot& snapshot,
                                     std::string* error) {
  std::vector<CgroupSkillEffect> effects;
  if (!effect_states_from(applied.data, &effects) &&
      !effect_states_from(snapshot.data, &effects)) {
    (void)read_marker(&effects, error);
  }
  if (effects.empty()) {
    remove_marker();
    return RecoveryResult{true, false, "no cgroup effect was in place"};
  }
  if (!restore(effects, error)) {
    return RecoveryResult{false, true, error == nullptr ? "restore failed" : *error};
  }
  remove_marker();
  return RecoveryResult{true, false,
                        "restored " + std::to_string(effects.size()) + " cpu.weight value(s)"};
}

bool CgroupSkill::restore(const std::vector<CgroupSkillEffect>& effects, std::string* error) const {
  std::string failures;
  for (const auto& effect : effects) {
    long long previous = 0;
    try {
      previous = std::stoll(effect.previous);
    } catch (const std::exception&) {
      failures += (failures.empty() ? "" : "; ") + effect.path + ": recorded previous value '" +
                  effect.previous + "' is not a number";
      continue;
    }
    std::string write_error;
    if (!set_knob(effect.file, previous, &write_error)) {
      failures += (failures.empty() ? "" : "; ") + effect.path + ": " + write_error;
    }
  }
  if (!failures.empty()) {
    if (error != nullptr) *error = "could not restore every value: " + failures;
    return false;
  }
  return true;
}

VerifyResult CgroupSkill::verify_rolled_back(const Snapshot& snapshot, std::int64_t) {
  std::vector<CgroupSkillEffect> effects;
  if (!effect_states_from(snapshot.data, &effects)) {
    return VerifyResult{true, "nothing to check"};
  }
  for (const auto& effect : effects) {
    std::string current;
    std::string error;
    if (!read_knob(effect.file, &current, &error)) {
      return VerifyResult{false, "cannot confirm the restore: " + error};
    }
    if (!same_value(current, effect.previous)) {
      return VerifyResult{false, effect.path + " is " + current + ", expected the pre-apply " +
                                     effect.previous};
    }
  }
  if (std::filesystem::exists(marker_path())) {
    return VerifyResult{false, "cgroup marker survives rollback: " + marker_path().string()};
  }
  return VerifyResult{true, "every restored value read back as the pre-apply one"};
}

RecoveryResult CgroupSkill::reconcile(const ReconcileContext&, std::string* error) {
  return recover_orphan(error);
}

RecoveryResult CgroupSkill::recover_orphan(std::string* error) {
  std::vector<CgroupSkillEffect> effects;
  std::string marker_error;
  if (!read_marker(&effects, &marker_error)) {
    if (!marker_error.empty()) {
      // An unreadable marker means we do not know what to restore: keep the scene and
      // say so rather than leaving the operator guessing.
      const std::string detail = "cgroup marker is unreadable: " + marker_error;
      if (error != nullptr) *error = detail;
      return RecoveryResult{false, true, detail};
    }
    return RecoveryResult{true, false, "nothing to reconcile"};
  }
  std::string restore_error;
  if (!restore(effects, &restore_error)) {
    const std::string detail = "orphan cgroup effects left in place: " + restore_error;
    if (error != nullptr) *error = detail;
    return RecoveryResult{false, true, detail};
  }
  remove_marker();
  return RecoveryResult{true, false,
                        "recovered " + std::to_string(effects.size()) +
                            " cgroup effect(s) left by a previous executor"};
}

}  // namespace ep
