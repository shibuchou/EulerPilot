#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ep/util.hpp"

namespace ep {

// One measurement window of the eBPF event pump (ep-evtmon), as reported over
// the executor socket. Like ServiceObservation it is advisory telemetry: it can
// refuse an action and never cause one on its own -- the arm file is what
// authorizes an action, and only while its named owner holds the lease.
struct EventCgroupSample {
  std::string path;        // normalized /sys/fs/cgroup/... path resolved by the pump
  std::uint64_t cgroup_id = 0;  // kernfs id as seen by BPF, for evidence only
  std::int64_t events = 0;      // wakeup->dispatch pairs observed in this window
  bool has_wait_max = false;
  double wait_max_ms = 0.0;     // longest runnable wait among those events
  bool has_wait_avg = false;
  double wait_avg_ms = 0.0;
};

struct EventObservation {
  std::string source;         // must be "evtmon"
  std::int64_t observed_at_ms = 0;
  std::int64_t window_ms = 0;
  std::vector<EventCgroupSample> cgroups;
};

// Validates the pump observation contract. Rejects unknown fields, non-finite
// numbers and windows outside 100ms..60s so a pump bug cannot silence the gate.
bool parse_event_observation(const Json& value, EventObservation* out, std::string* error);

// The human-authored authority for the autonomous event action. It names the
// watched cgroup, the condition (max runnable wait over a threshold, sustained
// N windows), the bounded action (cpu.weight for at most policy_ttl_ms) and the
// owner identity that must hold the lease before the executor may act.
struct EventArm {
  std::string name;                 // metadata.name
  std::string watch;                // normalized /sys/fs/cgroup/... path
  std::int64_t threshold_wait_ms = 0;
  int sustain_windows = 0;          // consecutive windows over threshold to act
  int release_windows = 0;          // consecutive windows under threshold to undo
  std::int64_t min_interval_ms = 0; // suppression: minimum spacing between actions
  std::int64_t lease_ttl_ms = 0;
  long long cpu_weight = 0;         // the action: cpu.weight while condition holds
  std::int64_t policy_ttl_ms = 0;   // hard cap on the effect, enforced by the TTL path
  std::string owner() const { return "eventguard:" + name; }
};

bool validate_event_arm(const Json& value, EventArm* out, std::string* error);

struct EventGuardInput {
  bool armed = false;
  bool active = false;               // an effect from this guard is in place
  bool has_observation = false;
  bool observation_stale = false;    // older than 2x its own window
  bool watch_present = false;        // the watched cgroup appears in the window
  bool wait_over_threshold = false;
  std::int64_t over_windows = 0;     // consecutive windows over threshold
  std::int64_t under_windows = 0;    // consecutive windows under threshold
  std::int64_t since_action_ms = 0;  // time since the last apply or rollback
  std::int64_t min_interval_ms = 0;
  int sustain_windows = 1;
  int release_windows = 1;
};

struct EventGuardDecision {
  enum class Action { kNone, kApply, kRollback };
  Action action = Action::kNone;
  std::string hold;   // why nothing happened; empty right after an action
  std::string reason; // why the action was taken
};

// Pure decision for the autonomous cgroup action. The order is the contract:
// evidence before suppression -- a stale or missing observation is always a
// hold, never a pass -- and the safe direction (undoing our own effect) needs
// less evidence than the risky one (writing a new one).
EventGuardDecision event_guard_decide(const EventGuardInput& in);

// One consumed window moves exactly one streak: consecutive windows over the
// threshold, or consecutive windows under it. A window where the watch is
// absent carries no evidence in either direction ("condition absent" is not
// "condition cleared"), so both streaks stay.
void advance_event_streaks(std::int64_t* over_windows, std::int64_t* under_windows,
                           bool watch_present, bool over_threshold);

// The policy document the guard applies through the normal transaction path.
// source=event-template only passes the apply gate while the arm's owner holds
// the lease and the event observation is fresh; a hand-crafted policy with the
// same source cannot smuggle itself past those checks.
Json event_action_policy(const EventArm& arm);

}  // namespace ep
