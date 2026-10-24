#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ep/util.hpp"

namespace ep {

struct PolicyTarget {
  std::string kind;         // cgroup / pid
  std::string path;         // /sys/fs/cgroup/... (cgroup only)
  std::string role;         // FOREGROUND / BACKGROUND / NEUTRAL
  std::string role_source;  // explicit
  std::string runtime_class{};  // optional: NORMAL / LAT / COMPUTE / CACHE / BG ("" = by role)
  std::int64_t pid = 0;     // pid only
};

struct PolicySummary {
  std::string name;
  std::string mode;
  std::string expert;
  std::string source;
  std::vector<PolicyTarget> targets;
  Json parameters = Json::object();
  Json safety = Json::object();
};

// Validates the v1alpha1 SchedulingPolicy contract plus semantic constraints
// that JSON Schema cannot express. Unknown fields are rejected. On success,
// `summary` contains the parsed fields the executor needs.
bool validate_scheduling_policy(const Json& policy, PolicySummary* summary,
                                std::string* error);

}  // namespace ep
