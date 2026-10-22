#pragma once

#include <cstdint>
#include <string>

#include "ep/util.hpp"

namespace ep {

// Deterministic advisory router over the FeatureWindow. It never writes the
// scheduler; a policy apply remains the only writer. Missing features degrade
// to safe-basic instead of guessing.
class RuleRouter {
 public:
  explicit RuleRouter(std::int64_t min_route_ms = 5000);

  Json decide(const Json& feature_snapshot, std::int64_t now_ms);

 private:
  std::string current_ = "safe-basic";
  std::string candidate_;
  std::int64_t candidate_since_ms_ = 0;
  std::int64_t min_route_ms_;
};

}  // namespace ep
