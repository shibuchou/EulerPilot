#include "ep/event_guard.hpp"

#include <climits>
#include <cmath>
#include <regex>
#include <utility>

#include "ep/domain.hpp"
#include "policy_json.hpp"

namespace ep {
namespace {

// Observations and arms may name the hierarchy root itself ("/sys/fs/cgroup"):
// it is a real cgroup with real runnable wait. The policy validator keeps its
// stricter shape (effects target specific cgroups), so the relaxation lives here.
bool valid_watch_path(const std::string& path) {
  return path == "/sys/fs/cgroup" || valid_cgroup_path(path);
}

}  // namespace

bool parse_event_observation(const Json& value, EventObservation* out, std::string* error) {
  if (!value.is_object()) {
    if (error != nullptr) *error = "event observation must be an object";
    return false;
  }
  if (!check_keys(value, {"source", "observed_at_ms", "window_ms", "cgroups", "pump"},
                  "observation", error)) {
    return false;
  }
  // Optional pump self-report (counters, report id): carried for evidence, not
  // consulted by the gate.
  if (value.contains("pump") && !value.at("pump").is_object()) {
    if (error != nullptr) *error = "observation.pump must be an object";
    return false;
  }
  EventObservation parsed;
  if (!get_string(value, "source", "observation", 1, 32, &parsed.source, error)) return false;
  if (parsed.source != "evtmon") {
    if (error != nullptr) *error = "observation.source must be evtmon";
    return false;
  }
  long long observed_at = 0;
  long long window = 0;
  if (!get_int(value, "observed_at_ms", "observation", 1, LLONG_MAX, &observed_at, error))
    return false;
  if (!get_int(value, "window_ms", "observation", 100, 60000, &window, error)) return false;
  parsed.observed_at_ms = observed_at;
  parsed.window_ms = window;

  if (!value.contains("cgroups") || !value.at("cgroups").is_array() ||
      value.at("cgroups").size() > 512) {
    if (error != nullptr) *error = "observation.cgroups must be an array of at most 512 entries";
    return false;
  }
  std::size_t index = 0;
  for (const Json& item : value.at("cgroups")) {
    const std::string where = "observation.cgroups[" + std::to_string(index++) + "]";
    if (!item.is_object()) {
      if (error != nullptr) *error = where + " must be an object";
      return false;
    }
    if (!check_keys(item, {"path", "cgroup_id", "events", "wait_max_ms", "wait_avg_ms"},
                    where, error)) {
      return false;
    }
    EventCgroupSample sample;
    if (!get_string(item, "path", where, 10, 1024, &sample.path, error)) return false;
    if (!valid_watch_path(sample.path)) {
      if (error != nullptr)
        *error = where + ".path must be a normalized /sys/fs/cgroup path";
      return false;
    }
    if (item.contains("cgroup_id")) {
      long long id = 0;
      if (!get_int(item, "cgroup_id", where, 0, LLONG_MAX, &id, error)) return false;
      sample.cgroup_id = static_cast<std::uint64_t>(id);
    } else {
      if (error != nullptr) *error = where + ".cgroup_id is required";
      return false;
    }
    long long events = 0;
    if (!get_int(item, "events", where, 0, LLONG_MAX, &events, error)) return false;
    sample.events = events;
    // The waits are optional per entry (a cgroup with no wakeup in the window has
    // nothing to report), but when present they must be finite non-negative ms.
    for (const char* key : {"wait_max_ms", "wait_avg_ms"}) {
      if (!item.contains(key)) continue;
      if (!item.at(key).is_number()) {
        if (error != nullptr) *error = std::string(where) + "." + key + " must be a number";
        return false;
      }
      const double number = item.at(key).get<double>();
      if (!std::isfinite(number) || number < 0.0) {
        if (error != nullptr)
          *error = std::string(where) + "." + key + " must be finite and non-negative";
        return false;
      }
      if (key == std::string("wait_max_ms")) {
        sample.has_wait_max = true;
        sample.wait_max_ms = number;
      } else {
        sample.has_wait_avg = true;
        sample.wait_avg_ms = number;
      }
    }
    parsed.cgroups.push_back(std::move(sample));
  }
  if (out != nullptr) *out = std::move(parsed);
  return true;
}

bool validate_event_arm(const Json& value, EventArm* out, std::string* error) {
  if (!value.is_object()) {
    if (error != nullptr) *error = "event arm must be a JSON object";
    return false;
  }
  if (!check_keys(value, {"apiVersion", "kind", "metadata", "spec"}, "arm", error)) return false;
  std::string api_version;
  std::string kind;
  if (!get_string(value, "apiVersion", "arm", 1, 64, &api_version, error)) return false;
  if (!get_string(value, "kind", "arm", 1, 64, &kind, error)) return false;
  if (api_version != kApiVersion) {
    if (error != nullptr) *error = "unsupported apiVersion: " + api_version;
    return false;
  }
  if (kind != "EventArm") {
    if (error != nullptr) *error = "kind must be EventArm";
    return false;
  }
  if (!value.contains("metadata") || !value.at("metadata").is_object()) {
    if (error != nullptr) *error = "metadata must be an object";
    return false;
  }
  const Json& metadata = value.at("metadata");
  if (!check_keys(metadata, {"name"}, "metadata", error)) return false;
  EventArm arm;
  if (!get_string(metadata, "name", "metadata", 1, 63, &arm.name, error)) return false;
  static const std::regex name_pattern("^[a-z][a-z0-9-]{0,62}$");
  if (!std::regex_match(arm.name, name_pattern)) {
    if (error != nullptr) *error = "metadata.name must match ^[a-z][a-z0-9-]{0,62}$";
    return false;
  }
  if (!value.contains("spec") || !value.at("spec").is_object()) {
    if (error != nullptr) *error = "spec must be an object";
    return false;
  }
  const Json& spec = value.at("spec");
  if (!check_keys(spec, {"watch", "threshold_wait_ms", "sustain_windows", "release_windows",
                         "min_interval_ms", "lease_ttl_ms", "action", "policy_ttl_ms"},
                  "spec", error)) {
    return false;
  }
  if (!get_string(spec, "watch", "spec", 10, 1024, &arm.watch, error)) return false;
  if (!valid_watch_path(arm.watch)) {
    if (error != nullptr) *error = "spec.watch must be a normalized /sys/fs/cgroup path";
    return false;
  }
  // The waits the pump reports resolve to well under a millisecond on an idle
  // box and to seconds under real starvation; a runnable-wait threshold outside
  // 1..60000ms describes no machine we have measured.
  long long threshold = 0;
  if (!get_int(spec, "threshold_wait_ms", "spec", 1, 60000, &threshold, error)) return false;
  arm.threshold_wait_ms = threshold;
  long long sustain = 0;
  long long release = 0;
  if (!get_int(spec, "sustain_windows", "spec", 1, 10, &sustain, error)) return false;
  if (!get_int(spec, "release_windows", "spec", 1, 10, &release, error)) return false;
  arm.sustain_windows = static_cast<int>(sustain);
  arm.release_windows = static_cast<int>(release);
  long long min_interval = 0;
  if (!get_int(spec, "min_interval_ms", "spec", 1000, 3600000, &min_interval, error)) {
    return false;
  }
  arm.min_interval_ms = min_interval;
  long long lease_ttl = 0;
  if (!get_int(spec, "lease_ttl_ms", "spec", 1000, 3600000, &lease_ttl, error)) return false;
  arm.lease_ttl_ms = lease_ttl;
  long long policy_ttl = 0;
  if (!get_int(spec, "policy_ttl_ms", "spec", 10000, 3600000, &policy_ttl, error)) return false;
  arm.policy_ttl_ms = policy_ttl;
  if (!spec.contains("action") || !spec.at("action").is_object()) {
    if (error != nullptr) *error = "spec.action must be an object";
    return false;
  }
  const Json& action = spec.at("action");
  if (!check_keys(action, {"cpu_weight"}, "spec.action", error)) return false;
  if (!get_int(action, "cpu_weight", "spec.action", 1, 10000, &arm.cpu_weight, error)) {
    return false;
  }
  if (out != nullptr) *out = std::move(arm);
  return true;
}

EventGuardDecision event_guard_decide(const EventGuardInput& in) {
  EventGuardDecision decision;
  if (!in.armed) {
    decision.hold = "event guard not armed";
    return decision;
  }
  if (in.active) {
    // The undo direction is the safe one, so it needs the least evidence: the
    // watch vanishing is enough (the target may have been destroyed), and so is
    // the condition clearing for release_windows in a row. While the condition
    // holds, the policy TTL bounds the total effect duration from above.
    if (!in.has_observation || !in.watch_present) {
      decision.action = EventGuardDecision::Action::kRollback;
      decision.reason = in.has_observation
                            ? "watched cgroup vanished from the observation"
                            : "no event observation while an effect is active";
      return decision;
    }
    if (in.observation_stale) {
      decision.hold = "EVENT_OBSERVATION_STALE; TTL still bounds the effect";
      return decision;
    }
    if (in.under_windows >= in.release_windows) {
      decision.action = EventGuardDecision::Action::kRollback;
      decision.reason = "condition cleared (" + std::to_string(in.under_windows) +
                        " window(s) below threshold)";
      return decision;
    }
    decision.hold = "condition holds; effect stays (TTL caps it)";
    return decision;
  }
  if (!in.has_observation) {
    decision.hold = "EVENT_OBSERVATION_NONE";
    return decision;
  }
  if (in.observation_stale) {
    decision.hold = "EVENT_OBSERVATION_STALE";
    return decision;
  }
  if (!in.watch_present) {
    // A cgroup with no wakeups in the window simply has no runnable wait: that
    // is "condition absent", not an error, and it must not arm the action.
    decision.hold = "watched cgroup produced no events in this window";
    return decision;
  }
  if (in.over_windows < in.sustain_windows) {
    decision.hold = "below threshold (" + std::to_string(in.over_windows) + "/" +
                    std::to_string(in.sustain_windows) + " window(s))";
    return decision;
  }
  if (in.since_action_ms < in.min_interval_ms) {
    decision.hold = "min interval not elapsed (" + std::to_string(in.since_action_ms) + "/" +
                    std::to_string(in.min_interval_ms) + "ms)";
    return decision;
  }
  decision.action = EventGuardDecision::Action::kApply;
  decision.reason = "runnable wait over threshold for " + std::to_string(in.over_windows) +
                    " window(s)";
  return decision;
}

void advance_event_streaks(std::int64_t* over_windows, std::int64_t* under_windows,
                           bool watch_present, bool over_threshold) {
  if (!watch_present) return;
  if (over_threshold) {
    ++*over_windows;
    *under_windows = 0;
  } else {
    ++*under_windows;
    *over_windows = 0;
  }
}

Json event_action_policy(const EventArm& arm) {
  return Json{{"apiVersion", kApiVersion},
              {"kind", "CgroupPolicy"},
              {"metadata", Json{{"name", "event-" + arm.name}}},
              {"spec", Json{{"scope", "cgroup-effects"},
                            {"source", "event-template"},
                            {"effects", Json::array({Json{{"path", arm.watch},
                                                           {"cpu_weight", arm.cpu_weight}}})},
                            {"safety", Json{{"policy_ttl_ms", arm.policy_ttl_ms}}}}}};
}

}  // namespace ep
