#include "ep/slo.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace ep {
namespace {

bool known_expert(const std::string& expert) {
  return expert == "safe-basic" || expert == "latency-balanced" ||
         expert == "throughput-balanced" || expert == "cache-local";
}

// Declared limits are optional; a missing or zero field means "not declared",
// so the gate cannot invent a threshold the policy never stated.
double declared(const Json& safety, const char* key) {
  if (!safety.is_object() || !safety.contains(key)) return 0.0;
  if (!safety.at(key).is_number()) return 0.0;
  const double value = safety.at(key).get<double>();
  return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

bool number(const Json& value, const char* key, double* out) {
  if (!value.is_object() || !value.contains(key) || !value.at(key).is_number()) return false;
  const double candidate = value.at(key).get<double>();
  if (!std::isfinite(candidate)) return false;
  *out = candidate;
  return true;
}

std::string percent(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f%%", value);
  return buffer;
}

}  // namespace

bool parse_service_observation(const Json& value, ServiceObservation* out, std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  auto string_field = [&value, &fail](const char* key, std::size_t min_len, std::size_t max_len,
                                      std::string* dest) {
    if (!value.contains(key) || !value.at(key).is_string()) {
      return fail(std::string("observation.") + key + " must be a string");
    }
    const std::string text = value.at(key).get<std::string>();
    if (text.size() < min_len || text.size() > max_len) {
      return fail(std::string("observation.") + key + " length must be " +
                  std::to_string(min_len) + ".." + std::to_string(max_len));
    }
    *dest = text;
    return true;
  };
  auto int_field = [&value, &fail](const char* key, long long minimum, long long maximum,
                                   std::int64_t* dest) {
    if (!value.contains(key) ||
        (!value.at(key).is_number_integer() && !value.at(key).is_number_unsigned())) {
      return fail(std::string("observation.") + key + " must be an integer");
    }
    const long long candidate = value.at(key).get<long long>();
    if (candidate < minimum || candidate > maximum) {
      return fail(std::string("observation.") + key + " must be within " +
                  std::to_string(minimum) + ".." + std::to_string(maximum));
    }
    *dest = candidate;
    return true;
  };

  if (!value.is_object()) return fail("observation must be an object");
  static const std::set<std::string> allowed{"workload", "observed_at_ms", "window_ms",
                                             "expert", "foreground", "background"};
  for (const auto& entry : value.items()) {
    if (!allowed.count(entry.key())) return fail("unknown observation field: " + entry.key());
  }
  ServiceObservation parsed;
  if (!string_field("workload", 1, 64, &parsed.workload)) return false;
  if (!int_field("observed_at_ms", 1, std::numeric_limits<long long>::max(),
                 &parsed.observed_at_ms)) {
    return false;
  }
  if (!int_field("window_ms", 100, 60000, &parsed.window_ms)) return false;
  std::string expert;
  if (!string_field("expert", 3, 32, &expert)) return false;
  if (!known_expert(expert)) return fail("observation.expert is not a known expert: " + expert);
  parsed.expert = expert;

  const Json foreground = value.contains("foreground") ? value.at("foreground") : Json::object();
  if (!foreground.is_object()) return fail("observation.foreground must be an object");
  for (const auto& entry : foreground.items()) {
    if (entry.key() != "p99_latency_ms") return fail("unknown foreground field: " + entry.key());
  }
  if (number(foreground, "p99_latency_ms", &parsed.foreground_p99_ms)) {
    if (parsed.foreground_p99_ms < 0.0) return fail("foreground.p99_latency_ms must be >= 0");
    parsed.has_foreground_p99 = true;
  }

  const Json background = value.contains("background") ? value.at("background") : Json::object();
  if (!background.is_object()) return fail("observation.background must be an object");
  for (const auto& entry : background.items()) {
    if (entry.key() != "work_units_per_sec" && entry.key() != "max_wait_ms") {
      return fail("unknown background field: " + entry.key());
    }
  }
  if (number(background, "work_units_per_sec", &parsed.bg_work_units_per_sec)) {
    if (parsed.bg_work_units_per_sec < 0.0) return fail("background.work_units_per_sec must be >= 0");
    parsed.has_bg_work_units = true;
  }
  if (number(background, "max_wait_ms", &parsed.bg_max_wait_ms)) {
    if (parsed.bg_max_wait_ms < 0.0) return fail("background.max_wait_ms must be >= 0");
    parsed.has_bg_max_wait = true;
  }
  if (!parsed.has_foreground_p99 && !parsed.has_bg_work_units && !parsed.has_bg_max_wait) {
    return fail("observation needs at least one measured signal in foreground/background");
  }
  if (out != nullptr) *out = parsed;
  return true;
}

Json SloGate::evaluate(const SloInput& in) const {
  Json checks = Json::array();
  bool allowed = true;
  std::string code;
  std::string reason;

  auto add = [&checks](const std::string& name, bool ok, const std::string& detail) {
    checks.push_back(Json{{"name", name}, {"ok", ok}, {"detail", detail}});
  };
  auto deny = [&allowed, &code, &reason](const std::string& new_code,
                                         const std::string& new_reason) {
    if (allowed) {
      allowed = false;
      code = new_code;
      reason = new_reason;
    }
  };

  if (!known_expert(in.expert)) {
    add("expert", false, "unknown expert '" + in.expert + "'");
    deny("EXPERT_UNKNOWN", "unknown expert");
  } else {
    add("expert", true, in.expert);
  }

  // Service evidence is judged before the host features: a measurement that
  // already shows the served workload hurting is more specific than a stale
  // CPU sample, and the operator needs to see it first. Absence is reported as
  // "not evaluated" and never as a pass; presence can only ever deny, because
  // telemetry is not allowed to open a gate the host features already closed.
  const auto adverse = in.adverse.find(in.expert);
  if (adverse != in.adverse.end()) {
    add("service_evidence", false, "measured while in place: " + adverse->second);
    deny("SERVICE_EVIDENCE_DENIED",
         "a service measurement already found " + in.expert + " to violate the declared limits");
  } else if (!in.has_observation) {
    add("service_evidence", true, "not evaluated: no service observation ingested");
  } else {
    const ServiceObservation& obs = in.observation;
    const std::int64_t obs_age_ms = in.now_ms - obs.observed_at_ms;
    // One window of drift is tolerated: a probe that stops reporting must not
    // keep approving changes with a measurement of a workload that has moved on.
    if (obs_age_ms > 2 * obs.window_ms) {
      add("service_fresh", false,
          "observation age " + std::to_string(obs_age_ms) + "ms exceeds 2x window " +
              std::to_string(obs.window_ms) + "ms");
      deny("SERVICE_OBSERVATION_STALE", "service observation is older than two of its windows");
    } else {
      add("service_fresh", true, obs.workload + " age_ms=" + std::to_string(obs_age_ms));
    }

    const double floor_units = declared(in.safety, "min_bg_work_units_per_sec");
    if (floor_units > 0.0 && obs.has_bg_work_units) {
      if (obs.bg_work_units_per_sec < floor_units) {
        add("bg_progress_floor", false,
            "bg " + std::to_string(obs.bg_work_units_per_sec) + " < declared " +
                std::to_string(floor_units));
        deny("BG_PROGRESS_FLOOR", "measured background progress is below the declared floor");
      } else {
        add("bg_progress_floor", true, "within floor");
      }
    } else {
      add("bg_progress_floor", true, "not evaluated: no floor or no bg measurement");
    }

    const double wait_ceiling = declared(in.safety, "max_bg_wait_ms");
    if (wait_ceiling > 0.0 && obs.has_bg_max_wait) {
      if (obs.bg_max_wait_ms > wait_ceiling) {
        add("bg_wait_ceiling", false,
            "bg wait " + std::to_string(obs.bg_max_wait_ms) + "ms > declared " +
                std::to_string(wait_ceiling) + "ms");
        deny("BG_WAIT_EXCEEDED", "measured background wait is above the declared ceiling");
      } else {
        add("bg_wait_ceiling", true, "within ceiling");
      }
    } else {
      add("bg_wait_ceiling", true, "not evaluated: no ceiling or no bg measurement");
    }

    const double fg_ceiling = declared(in.safety, "max_foreground_p99_ms");
    if (fg_ceiling > 0.0 && obs.has_foreground_p99) {
      if (obs.foreground_p99_ms > fg_ceiling) {
        add("foreground_ceiling", false,
            "foreground p99 " + std::to_string(obs.foreground_p99_ms) + "ms > declared " +
                std::to_string(fg_ceiling) + "ms");
        deny("FOREGROUND_P99_EXCEEDED",
             "measured foreground p99 is above the declared absolute ceiling");
      } else {
        add("foreground_ceiling", true, "within ceiling");
      }
    } else {
      add("foreground_ceiling", true, "not evaluated: no ceiling or no foreground measurement");
    }
  }

  const Json metrics = in.features.is_object()
                           ? in.features.value("metrics", Json::object())
                           : Json::object();
  auto metric = [&metrics](const char* name) {
    Json value{{"valid", false}, {"avg", 0.0}};
    if (metrics.is_object() && metrics.contains(name) && metrics.at(name).is_object()) {
      const Json& source = metrics.at(name);
      value["valid"] = source.value("valid", false);
      value["avg"] = source.value("avg", 0.0);
    }
    return value;
  };
  const Json cpu = metric("cpu_busy_pct");
  const Json psi = metric("psi_cpu_some_avg10");

  const bool sufficient = in.features.is_object() && in.features.value("sufficient", false);
  const std::int64_t age_ms =
      in.features.is_object()
          ? in.features.value("age_ms", std::numeric_limits<std::int64_t>::max())
          : std::numeric_limits<std::int64_t>::max();
  if (!sufficient) {
    add("features_fresh", false, "insufficient samples in the window");
    deny("FEATURES_INSUFFICIENT", "FeatureWindow has too few samples");
  } else if (age_ms > in.max_observation_age_ms) {
    add("features_fresh", false,
        "feature age " + std::to_string(age_ms) + "ms exceeds " +
            std::to_string(in.max_observation_age_ms) + "ms");
    deny("FEATURES_STALE", "FeatureWindow is older than max_observation_age_ms");
  } else {
    add("features_fresh", true, "age_ms=" + std::to_string(age_ms));
  }

  if (!cpu.value("valid", false)) {
    add("cpu", false, "cpu_busy_pct invalid");
    deny("CPU_FEATURE_MISSING", "cpu_busy_pct is not valid");
  } else {
    add("cpu", true, "avg=" + std::to_string(cpu.value("avg", 0.0)));
  }

  const bool needs_psi = in.expert == "latency-balanced" || in.expert == "throughput-balanced";
  if (!psi.value("valid", false)) {
    if (needs_psi) {
      add("psi", false, "PSI required for " + in.expert + " but unavailable");
      deny("PSI_FEATURE_MISSING", "PSI CPU pressure is required for this expert");
    } else {
      add("psi", true, "optional for " + in.expert);
    }
  } else {
    add("psi", true, "avg10=" + std::to_string(psi.value("avg", 0.0)));
    if (in.expert == "throughput-balanced" && psi.value("avg", 0.0) >= 20.0) {
      add("pressure_budget", false, "PSI avg10 >= 20 with throughput-balanced");
      deny("PSI_PRESSURE_TOO_HIGH", "throughput-balanced is denied under high CPU pressure");
    } else {
      add("pressure_budget", true, "within budget");
    }
  }

  if (in.expert == "cache-local") {
    add("llc", false, "LLC/topology features are not sampled by the host window");
    deny("LLC_FEATURE_MISSING", "cache-local needs LLC/topology features");
  }

  add("quiet_regression", true, "not evaluated: no quiet baseline in this window");

  return Json{{"status", "OK"},
              {"allowed", allowed},
              {"expert", in.expert},
              {"code", code},
              {"reason", reason},
              {"max_observation_age_ms", in.max_observation_age_ms},
              {"feature_age_ms", age_ms},
              {"service_evidence", in.has_observation ? "present" : "absent"},
              {"checks", checks}};
}

OutcomeVerdict evaluate_switch_outcome(const Json& safety, const ServiceObservation& before,
                                       const ServiceObservation& after, const ServiceNoise& noise) {
  OutcomeVerdict verdict;

  const double floor_units = declared(safety, "min_bg_work_units_per_sec");
  if (floor_units > 0.0 && after.has_bg_work_units &&
      after.bg_work_units_per_sec < floor_units) {
    verdict.code = "BG_PROGRESS_FLOOR";
    verdict.detail = "background progress " + std::to_string(after.bg_work_units_per_sec) +
                     " below declared floor " + std::to_string(floor_units);
    return verdict;
  }

  const double wait_ceiling = declared(safety, "max_bg_wait_ms");
  if (wait_ceiling > 0.0 && after.has_bg_max_wait && after.bg_max_wait_ms > wait_ceiling) {
    verdict.code = "BG_WAIT_EXCEEDED";
    verdict.detail = "background wait " + std::to_string(after.bg_max_wait_ms) +
                     "ms above declared " + std::to_string(wait_ceiling) + "ms";
    return verdict;
  }

  // Comparative progress check: only as sharp as the measurement can be.
  const double ratio = declared(safety, "evaluation_min_bg_progress_ratio");
  if (ratio > 0.0 && before.has_bg_work_units && after.has_bg_work_units &&
      before.bg_work_units_per_sec > 0.0) {
    // Sensitivity the contract asks for, as a percentage drop.
    const double sensitivity = (1.0 - ratio) * 100.0;
    const double progress = after.bg_work_units_per_sec / before.bg_work_units_per_sec;
    if (noise.bg_progress_pct > 0.0 && sensitivity <= noise.bg_progress_pct) {
      verdict.skipped_ratio = true;
    } else if (progress < ratio) {
      verdict.code = "BG_PROGRESS_REGRESSED";
      verdict.detail = "background progress ratio " + std::to_string(progress) +
                       " below declared " + std::to_string(ratio) +
                       (noise.bg_progress_pct > 0.0
                            ? " (measured noise " + percent(noise.bg_progress_pct) + ")"
                            : "");
      return verdict;
    }
  }

  // quiet_max_regression_pct is the only forward-regression limit the policy
  // contract declares, so a switch is judged against it whether or not the box
  // was quiet. It is skipped when that budget is finer than the measurement can
  // resolve, because p99 moves by more than the budget between two windows of
  // the same state (SP3: 3.8-20% CV; see evidence/p2/slo-noise-floor-20261022/).
  const double regression_pct = declared(safety, "quiet_max_regression_pct");
  if (regression_pct > 0.0 && before.has_foreground_p99 && after.has_foreground_p99 &&
      before.foreground_p99_ms > 0.0) {
    if (noise.p99_pct > 0.0 && regression_pct <= noise.p99_pct) {
      verdict.skipped_foreground = true;
    } else {
      const double limit = before.foreground_p99_ms * (1.0 + regression_pct / 100.0);
      if (after.foreground_p99_ms > limit) {
        verdict.code = "FOREGROUND_REGRESSED";
        verdict.detail =
            "foreground p99 " + std::to_string(after.foreground_p99_ms) + "ms exceeds " +
            std::to_string(regression_pct) + "% over the pre-switch " +
            std::to_string(before.foreground_p99_ms) + "ms" +
            (noise.p99_pct > 0.0 ? " (measured noise " + percent(noise.p99_pct) + ")" : "");
        return verdict;
      }
    }
  }

  if (verdict.skipped_ratio || verdict.skipped_foreground) {
    verdict.detail = "comparative check not run: declared sensitivity is finer than " +
                     std::string(verdict.skipped_foreground ? "p99 noise " +
                                                                 percent(noise.p99_pct)
                                                             : "progress noise " +
                                                                 percent(noise.bg_progress_pct));
  }
  return verdict;
}

}  // namespace ep
