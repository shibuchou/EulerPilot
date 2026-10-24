#include "ep/policy.hpp"

#include "ep/domain.hpp"

#include <regex>
#include <set>
#include <string>

namespace ep {
namespace {

bool check_keys(const Json& object, std::initializer_list<const char*> allowed,
                const std::string& path, std::string* error) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    bool known = false;
    for (const char* key : allowed) {
      if (it.key() == key) {
        known = true;
        break;
      }
    }
    if (!known) {
      if (error != nullptr) *error = path + " has unknown field '" + it.key() + "'";
      return false;
    }
  }
  return true;
}

bool get_string(const Json& object, const char* key, const std::string& path,
                std::size_t min_len, std::size_t max_len, std::string* out,
                std::string* error) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    if (error != nullptr) *error = path + "." + key + " must be a string";
    return false;
  }
  *out = object.at(key).get<std::string>();
  if (out->size() < min_len || out->size() > max_len) {
    if (error != nullptr) {
      *error = path + "." + key + " length must be " + std::to_string(min_len) + ".." +
               std::to_string(max_len);
    }
    return false;
  }
  return true;
}

bool get_int(const Json& object, const char* key, const std::string& path,
             long long minimum, long long maximum, long long* out, std::string* error) {
  if (!object.contains(key) ||
      (!object.at(key).is_number_integer() && !object.at(key).is_number_unsigned())) {
    if (error != nullptr) *error = path + "." + key + " must be an integer";
    return false;
  }
  const long long value = object.at(key).get<long long>();
  if (value < minimum || value > maximum) {
    if (error != nullptr) {
      *error = path + "." + key + " out of range [" + std::to_string(minimum) + "," +
               std::to_string(maximum) + "]";
    }
    return false;
  }
  *out = value;
  return true;
}

bool get_number(const Json& object, const char* key, const std::string& path,
                double minimum, double maximum, bool minimum_exclusive, double* out,
                std::string* error) {
  if (!object.contains(key) || !object.at(key).is_number()) {
    if (error != nullptr) *error = path + "." + key + " must be a number";
    return false;
  }
  const double value = object.at(key).get<double>();
  const bool below = minimum_exclusive ? value <= minimum : value < minimum;
  if (below || value > maximum) {
    if (error != nullptr) *error = path + "." + key + " out of range";
    return false;
  }
  *out = value;
  return true;
}

bool get_enum(const Json& object, const char* key, const std::string& path,
              std::initializer_list<const char*> allowed, std::string* out,
              std::string* error) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    if (error != nullptr) *error = path + "." + key + " must be a string";
    return false;
  }
  const std::string value = object.at(key).get<std::string>();
  for (const char* candidate : allowed) {
    if (value == candidate) {
      *out = value;
      return true;
    }
  }
  if (error != nullptr) *error = path + "." + key + " has invalid value '" + value + "'";
  return false;
}

bool valid_cgroup_path(const std::string& path) {
  static const std::regex pattern("^/sys/fs/cgroup/[A-Za-z0-9_./-]+$");
  if (path.size() > 1024 || !std::regex_match(path, pattern)) return false;
  // Reject "." and ".." segments even though the regex admits dots.
  std::string segment;
  for (std::size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (segment == "." || segment == "..") return false;
      segment.clear();
    } else {
      segment.push_back(path[i]);
    }
  }
  return true;
}

}  // namespace

bool validate_scheduling_policy(const Json& policy, PolicySummary* summary,
                                std::string* error) {
  if (!policy.is_object()) {
    if (error != nullptr) *error = "policy must be a JSON object";
    return false;
  }
  if (!check_keys(policy, {"apiVersion", "kind", "metadata", "spec"}, "policy", error)) {
    return false;
  }
  std::string api_version;
  std::string kind;
  if (!get_string(policy, "apiVersion", "policy", 1, 64, &api_version, error)) return false;
  if (!get_string(policy, "kind", "policy", 1, 64, &kind, error)) return false;
  if (api_version != kApiVersion) {
    if (error != nullptr) *error = "unsupported apiVersion: " + api_version;
    return false;
  }
  if (kind != "SchedulingPolicy") {
    if (error != nullptr) *error = "kind must be SchedulingPolicy";
    return false;
  }

  if (!policy.contains("metadata") || !policy.at("metadata").is_object()) {
    if (error != nullptr) *error = "metadata must be an object";
    return false;
  }
  const Json& metadata = policy.at("metadata");
  if (!check_keys(metadata, {"name"}, "metadata", error)) return false;
  std::string name;
  if (!get_string(metadata, "name", "metadata", 1, 63, &name, error)) return false;
  static const std::regex name_pattern("^[a-z][a-z0-9-]{0,62}$");
  if (!std::regex_match(name, name_pattern)) {
    if (error != nullptr) *error = "metadata.name must match ^[a-z][a-z0-9-]{0,62}$";
    return false;
  }

  if (!policy.contains("spec") || !policy.at("spec").is_object()) {
    if (error != nullptr) *error = "spec must be an object";
    return false;
  }
  const Json& spec = policy.at("spec");
  if (!check_keys(spec,
                  {"scope", "mode", "expert", "source", "targets", "parameters", "safety"},
                  "spec", error)) {
    return false;
  }

  PolicySummary result;
  result.name = name;
  std::string scope;
  if (!get_string(spec, "scope", "spec", 1, 64, &scope, error)) return false;
  if (scope != "scheduler-instance") {
    if (error != nullptr) *error = "spec.scope must be scheduler-instance";
    return false;
  }
  if (!get_enum(spec, "mode", "spec", {"basic", "class", "adaptive"}, &result.mode, error)) {
    return false;
  }
  if (!get_enum(spec, "expert", "spec",
                {"safe-basic", "latency-balanced", "throughput-balanced", "cache-local"},
                &result.expert, error)) {
    return false;
  }
  if (!get_enum(spec, "source", "spec",
                {"rule-template", "offline-search", "human", "llm-proposal"}, &result.source,
                error)) {
    return false;
  }

  if (!spec.contains("targets") || !spec.at("targets").is_array()) {
    if (error != nullptr) *error = "spec.targets must be an array";
    return false;
  }
  const Json& targets = spec.at("targets");
  if (targets.size() < 1 || targets.size() > 64) {
    if (error != nullptr) *error = "spec.targets must contain 1..64 entries";
    return false;
  }
  std::set<std::string> seen_paths;
  std::set<std::int64_t> seen_pids;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const Json& target = targets.at(i);
    const std::string path = "spec.targets[" + std::to_string(i) + "]";
    if (!target.is_object()) {
      if (error != nullptr) *error = path + " must be an object";
      return false;
    }
    if (!check_keys(target, {"kind", "path", "pid", "role", "role_source", "class"}, path,
                    error)) {
      return false;
    }
    PolicyTarget parsed;
    if (!get_string(target, "kind", path, 1, 32, &parsed.kind, error)) return false;
    if (parsed.kind == "cgroup") {
      if (target.contains("pid")) {
        if (error != nullptr) *error = path + ".pid is only valid for kind=pid";
        return false;
      }
      if (!get_string(target, "path", path, 10, 1024, &parsed.path, error)) return false;
      if (!valid_cgroup_path(parsed.path)) {
        if (error != nullptr) {
          *error = path + ".path must be a normalized /sys/fs/cgroup/... path";
        }
        return false;
      }
      if (!seen_paths.insert(parsed.path).second) {
        if (error != nullptr) *error = "duplicate target path: " + parsed.path;
        return false;
      }
    } else if (parsed.kind == "pid") {
      if (target.contains("path")) {
        if (error != nullptr) *error = path + ".path is only valid for kind=cgroup";
        return false;
      }
      long long pid = 0;
      if (!get_int(target, "pid", path, 1, 4194304, &pid, error)) return false;
      parsed.pid = static_cast<std::int64_t>(pid);
      if (!seen_pids.insert(parsed.pid).second) {
        if (error != nullptr) *error = "duplicate target pid: " + std::to_string(parsed.pid);
        return false;
      }
    } else {
      if (error != nullptr) *error = path + ".kind must be cgroup or pid";
      return false;
    }
    if (!get_enum(target, "role", path, {"FOREGROUND", "BACKGROUND", "NEUTRAL"},
                  &parsed.role, error)) {
      return false;
    }
    if (!get_string(target, "role_source", path, 1, 32, &parsed.role_source, error)) {
      return false;
    }
    if (parsed.role_source != "explicit") {
      if (error != nullptr) *error = path + ".role_source must be explicit";
      return false;
    }
    // `class` is the runtime-feature tier (which DSQ the task lands in) and is
    // deliberately orthogonal to `role`, which is the business role. It is optional
    // and never inferred: a target that does not declare one is classified by role
    // only (BACKGROUND -> BG, everything else left NORMAL/unclassified).
    if (target.contains("class")) {
      if (!get_enum(target, "class", path, {"NORMAL", "LAT", "COMPUTE", "CACHE", "BG"},
                    &parsed.runtime_class, error)) {
        return false;
      }
      // The one asymmetry worth enforcing: BG is the contained tier (short slice,
      // vtime penalty), so letting any role claim it would be a route to demote a
      // foreground task to background treatment without saying so in `role`.
      if (parsed.runtime_class == "BG" && parsed.role != "BACKGROUND") {
        if (error != nullptr) *error = path + ".class BG requires role BACKGROUND";
        return false;
      }
      if (parsed.role == "BACKGROUND" && !parsed.runtime_class.empty() &&
          parsed.runtime_class != "BG") {
        if (error != nullptr) *error = path + ".role BACKGROUND must use class BG";
        return false;
      }
    }
    result.targets.push_back(std::move(parsed));
  }

  if (!spec.contains("parameters") || !spec.at("parameters").is_object()) {
    if (error != nullptr) *error = "spec.parameters must be an object";
    return false;
  }
  const Json& parameters = spec.at("parameters");
  if (!check_keys(parameters,
                  {"lat_slice_ns", "comp_slice_ns", "cache_slice_ns", "bg_slice_ns",
                   "preempt_threshold_ns", "bg_vtime_pct", "llc_affinity"},
                  "spec.parameters", error)) {
    return false;
  }
  long long value = 0;
  for (const char* key : {"lat_slice_ns", "comp_slice_ns", "cache_slice_ns", "bg_slice_ns"}) {
    if (!get_int(parameters, key, "spec.parameters", 250000, 10000000, &value, error)) {
      return false;
    }
  }
  if (!get_int(parameters, "preempt_threshold_ns", "spec.parameters", 100000, 2000000, &value,
               error)) {
    return false;
  }
  if (!get_int(parameters, "bg_vtime_pct", "spec.parameters", 50, 400, &value, error)) {
    return false;
  }
  if (!parameters.contains("llc_affinity") || !parameters.at("llc_affinity").is_boolean()) {
    if (error != nullptr) *error = "spec.parameters.llc_affinity must be a boolean";
    return false;
  }
  result.parameters = parameters;

  if (!spec.contains("safety") || !spec.at("safety").is_object()) {
    if (error != nullptr) *error = "spec.safety must be an object";
    return false;
  }
  const Json& safety = spec.at("safety");
  if (!check_keys(safety,
                  {"min_dwell_ms", "max_observation_age_ms", "heartbeat_period_ms",
                   "heartbeat_timeout_ms", "policy_ttl_ms", "quiet_max_regression_pct",
                   "evaluation_min_bg_progress_ratio", "bg_work_unit",
                   "bg_progress_window_ms", "min_bg_work_units_per_sec", "max_bg_wait_ms",
                   "max_foreground_p99_ms", "fallback", "allow_affinity_change"},
                  "spec.safety", error)) {
    return false;
  }
  long long min_dwell = 0;
  long long max_age = 0;
  long long heartbeat_period = 0;
  long long heartbeat_timeout = 0;
  long long policy_ttl = 0;
  long long bg_window = 0;
  if (!get_int(safety, "min_dwell_ms", "spec.safety", 5000, 300000, &min_dwell, error)) {
    return false;
  }
  if (!get_int(safety, "max_observation_age_ms", "spec.safety", 100, 10000, &max_age, error)) {
    return false;
  }
  if (!get_int(safety, "heartbeat_period_ms", "spec.safety", 100, 1000, &heartbeat_period,
               error)) {
    return false;
  }
  if (!get_int(safety, "heartbeat_timeout_ms", "spec.safety", 1000, 10000,
               &heartbeat_timeout, error)) {
    return false;
  }
  if (!get_int(safety, "policy_ttl_ms", "spec.safety", 10000, 3600000, &policy_ttl, error)) {
    return false;
  }
  double number = 0;
  if (!get_number(safety, "quiet_max_regression_pct", "spec.safety", 0, 5, false, &number,
                  error)) {
    return false;
  }
  if (!get_number(safety, "evaluation_min_bg_progress_ratio", "spec.safety", 0.1, 1, false,
                  &number, error)) {
    return false;
  }
  std::string work_unit;
  if (!get_string(safety, "bg_work_unit", "spec.safety", 1, 64, &work_unit, error)) {
    return false;
  }
  if (!get_int(safety, "bg_progress_window_ms", "spec.safety", 1000, 60000, &bg_window,
               error)) {
    return false;
  }
  if (!get_number(safety, "min_bg_work_units_per_sec", "spec.safety", 0, 1e12, true, &number,
                  error)) {
    return false;
  }
  if (!get_int(safety, "max_bg_wait_ms", "spec.safety", 50, 10000, &value, error)) {
    return false;
  }
  // Optional: the protocol's online forward-latency limit (§5.1 rule 4) is an
  // absolute ceiling, because p99 cannot resolve a 5% relative change. Absent
  // means not declared, and the gate reports "not evaluated" instead of inventing
  // a number.
  if (safety.contains("max_foreground_p99_ms") &&
      !get_int(safety, "max_foreground_p99_ms", "spec.safety", 1, 60000, &value, error)) {
    return false;
  }
  std::string fallback;
  if (!get_enum(safety, "fallback", "spec.safety",
                {"verified-safe-profile", "detach-to-fair"}, &fallback, error)) {
    return false;
  }
  if (!safety.contains("allow_affinity_change") ||
      !safety.at("allow_affinity_change").is_boolean() ||
      safety.at("allow_affinity_change").get<bool>()) {
    if (error != nullptr) *error = "spec.safety.allow_affinity_change must be false";
    return false;
  }
  result.safety = safety;

  // Semantic constraints that the schema cannot express.
  if (heartbeat_timeout <= heartbeat_period) {
    if (error != nullptr) *error = "heartbeat_timeout_ms must be greater than heartbeat_period_ms";
    return false;
  }
  if (policy_ttl <= min_dwell) {
    if (error != nullptr) *error = "policy_ttl_ms must be greater than min_dwell_ms";
    return false;
  }
  if (max_age > heartbeat_timeout) {
    if (error != nullptr) {
      *error = "max_observation_age_ms must not exceed heartbeat_timeout_ms";
    }
    return false;
  }
  if (bg_window > policy_ttl) {
    if (error != nullptr) *error = "bg_progress_window_ms must not exceed policy_ttl_ms";
    return false;
  }

  if (summary != nullptr) *summary = std::move(result);
  return true;
}

}  // namespace ep
