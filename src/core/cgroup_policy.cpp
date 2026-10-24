#include "ep/cgroup_policy.hpp"

#include <regex>
#include <set>
#include <string>

#include "ep/domain.hpp"
#include "policy_json.hpp"

namespace ep {

bool validate_cgroup_policy(const Json& policy, CgroupPolicySummary* summary,
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
  if (kind != "CgroupPolicy") {
    if (error != nullptr) *error = "kind must be CgroupPolicy";
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
  if (!check_keys(spec, {"scope", "source", "effects", "safety"}, "spec", error)) return false;
  std::string scope;
  if (!get_string(spec, "scope", "spec", 1, 64, &scope, error)) return false;
  if (scope != "cgroup-effects") {
    if (error != nullptr) *error = "spec.scope must be cgroup-effects";
    return false;
  }
  std::string source;
  // rule-template etc. stay out until the event-driven path can hand the lease to a
  // named owner: an autonomous writer needs the same authority check the scheduling
  // domain requires, and inventing a weaker one for cgroups would be a hole.
  if (!get_enum(spec, "source", "spec", {"human"}, &source, error)) {
    if (error != nullptr) *error = "spec.source: " + *error + " (CgroupPolicy is human-set)";
    return false;
  }

  if (!spec.contains("effects") || !spec.at("effects").is_array() ||
      spec.at("effects").empty() || spec.at("effects").size() > 8) {
    if (error != nullptr) *error = "spec.effects must be an array of 1..8 entries";
    return false;
  }
  CgroupPolicySummary parsed;
  parsed.name = name;
  parsed.source = source;
  std::set<std::string> seen_paths;
  std::size_t index = 0;
  const std::string path = "spec.effects";
  for (const Json& effect : spec.at("effects")) {
    const std::string where = path + "[" + std::to_string(index++) + "]";
    if (!effect.is_object()) {
      if (error != nullptr) *error = where + " must be an object";
      return false;
    }
    if (!check_keys(effect, {"path", "cpu_weight"}, where, error)) return false;
    CgroupEffect parsed_effect;
    if (!get_string(effect, "path", where, 10, 1024, &parsed_effect.path, error)) return false;
    if (!valid_cgroup_path(parsed_effect.path)) {
      if (error != nullptr) *error = where + ".path must be a normalized /sys/fs/cgroup/... path";
      return false;
    }
    if (!seen_paths.insert(parsed_effect.path).second) {
      if (error != nullptr) *error = "duplicate effect path: " + parsed_effect.path;
      return false;
    }
    long long weight = 0;
    if (!get_int(effect, "cpu_weight", where, 1, 10000, &weight, error)) return false;
    parsed_effect.cpu_weight = weight;
    parsed.effects.push_back(parsed_effect);
  }

  if (!spec.contains("safety") || !spec.at("safety").is_object()) {
    if (error != nullptr) *error = "spec.safety must be an object";
    return false;
  }
  const Json& safety = spec.at("safety");
  if (!check_keys(safety, {"policy_ttl_ms"}, "spec.safety", error)) return false;
  long long ttl = 0;
  if (!get_int(safety, "policy_ttl_ms", "spec.safety", 10000, 3600000, &ttl, error)) {
    return false;
  }
  parsed.safety = safety;

  if (summary != nullptr) *summary = std::move(parsed);
  return true;
}

}  // namespace ep
