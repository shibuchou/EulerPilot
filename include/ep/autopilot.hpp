#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ep/util.hpp"

namespace ep {

// AutoPilot turns an advisory routing decision into a policy apply without ever
// becoming a second writer: it runs inside the executor, may only pick an expert
// from a narrow whitelist, and acts only while it holds the lease itself. Targets
// and safety limits always come from the human-supplied baseline policy, so the
// autopilot can change which expert runs but never who is authorised or how far
// it may go.
//
// The suppression knobs exist because of a measurement, not taste: one automatic
// switch costs a 621-665ms window where no v2 policy protects anything
// (evidence/p2/switch-downtime-20261022.txt), and under step load the routed
// target crosses back and forth often enough to matter.
struct AutoInput {
  bool enabled = false;
  std::string auto_owner;

  std::string lease_owner;
  std::int64_t lease_expires_ms = 0;
  bool routing_degraded = false;
  bool slo_allowed = false;
  std::string slo_code;

  // RuleRouter output: expert is the settled (hysteresis-applied) choice.
  std::string routed_expert;

  // Effect currently in place, and how long it has been in place.
  std::string active_expert;
  std::int64_t active_age_ms = 0;

  // How long the routing target has been unchanged. A route that keeps flipping
  // is evidence the workload straddles a threshold, not that the policy should.
  std::int64_t desired_stable_ms = 0;

  // Timestamps of the autopilot's own previous switches.
  std::vector<std::int64_t> switch_times_ms;
  std::int64_t last_action_ms = 0;

  std::int64_t now_ms = 0;
  std::int64_t min_interval_ms = 15000;
  std::int64_t dwell_ms = 60000;
  std::int64_t candidate_hold_ms = 15000;
  std::int64_t budget_window_ms = 600000;
  int budget_max_switches = 2;
  // Measured cost of one switch, used only to report projected exposure.
  std::int64_t measured_downtime_ms = 665;
};

struct AutoDecision {
  bool act = false;
  std::string expert;
  std::string reason;
  // Why it declined, whenever act is false. Reported through status and the
  // journal so a silent autopilot is still explainable.
  std::string hold;
};

// Pure function: no clock, no syscalls, no mutation. Everything observed is input.
AutoDecision auto_pilot_decide(const AutoInput& in);

// cache-local is intentionally absent: never exercised on SP3.
bool auto_pilot_expert_allowed(const std::string& expert);

}  // namespace ep
