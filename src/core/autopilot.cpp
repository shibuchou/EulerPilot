#include "ep/autopilot.hpp"

#include <algorithm>

namespace ep {
namespace {

constexpr const char* kAutoExperts[] = {"safe-basic", "latency-balanced", "throughput-balanced"};

std::size_t switches_in_window(const AutoInput& in) {
  const std::int64_t cutoff = in.now_ms - in.budget_window_ms;
  std::size_t used = 0;
  for (const std::int64_t at : in.switch_times_ms) {
    if (at > cutoff) ++used;
  }
  return used;
}

}  // namespace

bool auto_pilot_expert_allowed(const std::string& expert) {
  for (const char* name : kAutoExperts) {
    if (expert == name) return true;
  }
  return false;
}

AutoDecision auto_pilot_decide(const AutoInput& in) {
  AutoDecision d;
  // Authority gates first: "I have no right to act" must never be masked by
  // "I chose not to", because operators read these strings to decide who is in
  // control.
  if (!in.enabled) {
    d.hold = "disabled";
    return d;
  }
  if (in.auto_owner.empty()) {
    d.hold = "no auto owner configured";
    return d;
  }
  if (in.active_expert.empty()) {
    // The human applies the baseline first; the autopilot only moves it between
    // whitelisted experts.
    d.hold = "no active policy to switch";
    return d;
  }
  if (in.lease_owner != in.auto_owner) {
    d.hold = in.lease_owner.empty() ? "lease not held by autopilot"
                                    : "lease held by " + in.lease_owner;
    return d;
  }
  if (in.lease_expires_ms <= in.now_ms) {
    d.hold = "lease expired";
    return d;
  }

  // Evidence gates.
  if (in.routing_degraded) {
    d.hold = "routing degraded";
    return d;
  }
  if (!in.slo_allowed) {
    d.hold = in.slo_code.empty() ? "slo denied" : "slo denied: " + in.slo_code;
    return d;
  }
  if (in.routed_expert.empty()) {
    d.hold = "no routing target";
    return d;
  }
  if (!auto_pilot_expert_allowed(in.routed_expert)) {
    d.hold = "expert outside autopilot whitelist: " + in.routed_expert;
    return d;
  }
  if (in.routed_expert == in.active_expert) {
    d.hold = "already in place: " + in.routed_expert;
    return d;
  }

  // Suppression gates, each one paying for the measured ~0.3-0.7s unprotected
  // window a switch leaves behind.
  if (in.active_age_ms < in.dwell_ms) {
    d.hold = "dwell " + std::to_string(in.dwell_ms) + "ms not met (in place " +
             std::to_string(in.active_age_ms) + "ms)";
    return d;
  }
  if (in.desired_stable_ms < in.candidate_hold_ms) {
    d.hold = "routing target stable " + std::to_string(in.desired_stable_ms) +
             "ms of " + std::to_string(in.candidate_hold_ms) + "ms required";
    return d;
  }
  if (in.last_action_ms > 0 && in.now_ms - in.last_action_ms < in.min_interval_ms) {
    d.hold = "cooldown until " + std::to_string(in.last_action_ms + in.min_interval_ms);
    return d;
  }
  const std::size_t used = switches_in_window(in);
  if (in.budget_max_switches > 0 &&
      used >= static_cast<std::size_t>(in.budget_max_switches)) {
    d.hold = "switch budget " + std::to_string(used) + "/" +
             std::to_string(in.budget_max_switches) + " in " +
             std::to_string(in.budget_window_ms) + "ms window";
    return d;
  }

  d.act = true;
  d.expert = in.routed_expert;
  d.reason = in.active_expert + " -> " + in.routed_expert;
  d.hold.clear();
  return d;
}

}  // namespace ep
