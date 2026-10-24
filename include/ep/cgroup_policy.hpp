#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ep/util.hpp"

namespace ep {

// One cgroup v2 knob a CgroupPolicy wants to move. cpu.weight only, for now: it is the
// same knob the A2 baseline arm is measured with, so an effect can be attributed to a
// control whose cost we already know how to compare against.
struct CgroupEffect {
  std::string path;        // normalized /sys/fs/cgroup/... directory
  long long cpu_weight = 0;  // 1..10000
};

struct CgroupPolicySummary {
  std::string name;
  std::string source;
  std::vector<CgroupEffect> effects;
  Json safety = Json::object();
};

// Validates the v1alpha1 CgroupPolicy contract plus the constraints JSON Schema cannot
// express. Unknown fields are rejected; a policy that declares nothing to do is rejected.
bool validate_cgroup_policy(const Json& policy, CgroupPolicySummary* summary,
                            std::string* error);

}  // namespace ep
