#pragma once

#include <cstdint>
#include <string>

#include "ep/util.hpp"

namespace ep {

// Read-only SLO gate. It answers "may this expert be applied now?" and never
// writes the scheduler. Insufficient/stale features or missing required
// signals deny instead of guessing.
class SloGate {
 public:
  Json evaluate(const Json& feature_snapshot, const std::string& expert,
                std::int64_t max_observation_age_ms) const;
};

}  // namespace ep
