#include "ep/router.hpp"

namespace ep {
namespace {

struct Metric {
  bool valid = false;
  double avg = 0.0;
};

Metric metric_of(const Json& metrics, const char* name) {
  Metric metric;
  if (!metrics.is_object() || !metrics.contains(name) || !metrics.at(name).is_object()) {
    return metric;
  }
  const Json& value = metrics.at(name);
  metric.valid = value.value("valid", false);
  metric.avg = value.value("avg", 0.0);
  return metric;
}

}  // namespace

RuleRouter::RuleRouter(std::int64_t min_route_ms)
    : min_route_ms_(min_route_ms < 0 ? 0 : min_route_ms) {}

Json RuleRouter::decide(const Json& feature_snapshot, std::int64_t now_ms) {
  const Json metrics =
      feature_snapshot.is_object() ? feature_snapshot.value("metrics", Json::object())
                                   : Json::object();
  const bool sufficient = feature_snapshot.is_object() &&
                          feature_snapshot.value("sufficient", false);
  const Metric cpu = metric_of(metrics, "cpu_busy_pct");
  const Metric load = metric_of(metrics, "load1");
  const Metric psi = metric_of(metrics, "psi_cpu_some_avg10");

  bool degraded = false;
  std::string desired = "safe-basic";
  std::string reason;
  double confidence = 0.8;
  if (!sufficient || !cpu.valid || !load.valid) {
    degraded = true;
    confidence = 0.3;
    reason = "insufficient host features; degrade to safe-basic";
  } else if (!psi.valid) {
    degraded = true;
    confidence = 0.5;
    reason = "PSI unavailable; route without pressure signal";
    if (cpu.avg >= 85.0) {
      desired = "throughput-balanced";
      reason = "PSI unavailable; high CPU without pressure signal -> throughput-balanced";
    }
  } else if (psi.avg >= 20.0) {
    desired = "latency-balanced";
    reason = "CPU PSI some avg10 >= 20 -> latency-balanced";
  } else if (cpu.avg >= 85.0 && psi.avg < 5.0) {
    desired = "throughput-balanced";
    reason = "high CPU with low PSI -> throughput-balanced";
  } else if (cpu.avg <= 30.0) {
    desired = "safe-basic";
    reason = "CPU has idle headroom; keep safe-basic";
  } else {
    desired = "latency-balanced";
    reason = "moderate CPU pressure; latency-balanced";
  }

  bool switched = false;
  bool pending = false;
  if (desired != current_) {
    if (candidate_ != desired) {
      candidate_ = desired;
      candidate_since_ms_ = now_ms;
    }
    if (now_ms - candidate_since_ms_ >= min_route_ms_) {
      current_ = desired;
      candidate_.clear();
      candidate_since_ms_ = 0;
      switched = true;
    } else {
      pending = true;
      confidence *= 0.75;
    }
  } else {
    candidate_.clear();
    candidate_since_ms_ = 0;
  }

  return Json{{"status", "OK"},
              {"expert", current_},
              {"desired", desired},
              {"switched", switched},
              {"pending", pending},
              {"reason", reason},
              {"confidence", confidence},
              {"degraded", degraded},
              {"evaluated_at_ms", now_ms},
              {"inputs",
               Json{{"sufficient", sufficient},
                    {"cpu_busy_pct", Json{{"valid", cpu.valid}, {"avg", cpu.avg}}},
                    {"load1", Json{{"valid", load.valid}, {"avg", load.avg}}},
                    {"psi_cpu_some_avg10", Json{{"valid", psi.valid}, {"avg", psi.avg}}}}}};
}

}  // namespace ep
