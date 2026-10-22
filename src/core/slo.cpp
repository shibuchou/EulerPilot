#include "ep/slo.hpp"

#include <limits>

namespace ep {
namespace {

bool known_expert(const std::string& expert) {
  return expert == "safe-basic" || expert == "latency-balanced" ||
         expert == "throughput-balanced" || expert == "cache-local";
}

}  // namespace

Json SloGate::evaluate(const Json& feature_snapshot, const std::string& expert,
                       std::int64_t max_observation_age_ms) const {
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

  if (!known_expert(expert)) {
    add("expert", false, "unknown expert '" + expert + "'");
    deny("EXPERT_UNKNOWN", "unknown expert");
  } else {
    add("expert", true, expert);
  }

  const Json metrics = feature_snapshot.is_object()
                           ? feature_snapshot.value("metrics", Json::object())
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

  const bool sufficient = feature_snapshot.is_object() &&
                          feature_snapshot.value("sufficient", false);
  const std::int64_t age_ms =
      feature_snapshot.is_object()
          ? feature_snapshot.value("age_ms", std::numeric_limits<std::int64_t>::max())
          : std::numeric_limits<std::int64_t>::max();
  if (!sufficient) {
    add("features_fresh", false, "insufficient samples in the window");
    deny("FEATURES_INSUFFICIENT", "FeatureWindow has too few samples");
  } else if (age_ms > max_observation_age_ms) {
    add("features_fresh", false, "feature age " + std::to_string(age_ms) +
                                     "ms exceeds " +
                                     std::to_string(max_observation_age_ms) + "ms");
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

  const bool needs_psi = expert == "latency-balanced" || expert == "throughput-balanced";
  if (!psi.value("valid", false)) {
    if (needs_psi) {
      add("psi", false, "PSI required for " + expert + " but unavailable");
      deny("PSI_FEATURE_MISSING", "PSI CPU pressure is required for this expert");
    } else {
      add("psi", true, "optional for " + expert);
    }
  } else {
    add("psi", true, "avg10=" + std::to_string(psi.value("avg", 0.0)));
    if (expert == "throughput-balanced" && psi.value("avg", 0.0) >= 20.0) {
      add("pressure_budget", false, "PSI avg10 >= 20 with throughput-balanced");
      deny("PSI_PRESSURE_TOO_HIGH", "throughput-balanced is denied under high CPU pressure");
    } else {
      add("pressure_budget", true, "within budget");
    }
  }

  if (expert == "cache-local") {
    add("llc", false, "LLC/topology features are not sampled by the host window");
    deny("LLC_FEATURE_MISSING", "cache-local needs LLC/topology features");
  }

  add("quiet_regression", true, "not evaluated: no quiet baseline in this window");

  return Json{{"status", "OK"},
              {"allowed", allowed},
              {"expert", expert},
              {"code", code},
              {"reason", reason},
              {"max_observation_age_ms", max_observation_age_ms},
              {"feature_age_ms", age_ms},
              {"checks", checks}};
}

}  // namespace ep
