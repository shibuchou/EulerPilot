#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "ep/util.hpp"

namespace ep {

// One measurement window of the served workload, as reported by a probe. It is
// advisory telemetry only: the executor never writes the scheduler because of
// it, and a bad or missing observation can only ever *refuse* a change.
struct ServiceObservation {
  std::string workload;
  std::int64_t observed_at_ms = 0;
  std::int64_t window_ms = 0;
  std::string expert;              // expert that was in place while measuring
  bool has_foreground_p99 = false;
  double foreground_p99_ms = 0.0;
  bool has_bg_work_units = false;
  double bg_work_units_per_sec = 0.0;
  bool has_bg_max_wait = false;
  double bg_max_wait_ms = 0.0;
};

// Validates the service observation contract. Rejects unknown fields, non-finite
// numbers and windows outside 100ms..60s so a probe bug cannot silence the gate.
bool parse_service_observation(const Json& value, ServiceObservation* out, std::string* error);

struct SloInput {
  Json features = Json::object();      // FeatureWindow snapshot
  std::string expert;                  // expert being asked about
  std::int64_t now_ms = 0;
  std::int64_t max_observation_age_ms = 1000;
  Json safety = Json::object();        // policy-declared limits; empty = not declared
  bool has_observation = false;
  ServiceObservation observation;      // latest measurement, under whatever is in place now
  // Experts a measurement already found to violate the declared limits while they
  // were in place. This is the feedback half: a switch that made the service worse
  // must not be proposed again on the same evidence.
  std::map<std::string, std::string> adverse;
};

// Read-only SLO gate. It answers "may this expert be applied now?" and never
// writes the scheduler. Insufficient/stale features or missing required signals
// deny instead of guessing, and measured service evidence can only ever deny.
class SloGate {
 public:
  Json evaluate(const SloInput& in) const;
};

// Measured window-to-window variation of a signal, in percent, taken from the
// same expert's own consecutive windows. The SP3 characterisation shows p99
// varies 3.8-20% CV between windows of the *same* state (and longer windows are
// not quieter: they sample a different world), while background progress varies
// under 1%. Comparative verdicts therefore have a resolution floor.
struct ServiceNoise {
  double p99_pct = 0.0;
  double bg_progress_pct = 0.0;
};

struct OutcomeVerdict {
  std::string code;  // empty when the declared limits were not breached
  std::string detail;
  // A check whose declared sensitivity is finer than the measured noise is not
  // run at all: passing it silently would claim precision the measurement does
  // not have, and failing it would condemn experts on jitter.
  bool skipped_foreground = false;
  bool skipped_ratio = false;
};

// Compare a measurement taken after a policy change with the one taken before it,
// using only the limits the policy declares. This is the feedback half of service
// SLO: the executor records the result against the expert that produced it.
OutcomeVerdict evaluate_switch_outcome(const Json& safety, const ServiceObservation& before,
                                       const ServiceObservation& after, const ServiceNoise& noise);

}  // namespace ep
