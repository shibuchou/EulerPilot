#include "eulerpilot.hpp"
#include "executors.hpp"

#include <cassert>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

eulerpilot::WorkloadSample sample(std::string comm,
                                  std::uint64_t runtime_ns = 1000000,
                                  std::uint64_t wait_ns = 0,
                                  std::uint64_t wakeups = 0,
                                  std::uint64_t migrations = 0) {
    eulerpilot::WorkloadSample s;
    s.pid = 100;
    s.tgid = 100;
    s.comm = std::move(comm);
    s.runtime_ns = runtime_ns;
    s.total_wait_ns = wait_ns;
    s.wakeup_count = wakeups;
    s.migrate_count = migrations;
    return s;
}

eulerpilot::TriggerContext context(bool latency_exists,
                                   bool background_exists,
                                   bool latency_wait_high,
                                   bool cpu_psi_high,
                                   bool background_runtime_high) {
    eulerpilot::TriggerContext ctx;
    ctx.latency_exists = latency_exists;
    ctx.background_exists = background_exists;
    ctx.latency_wait_high = latency_wait_high;
    ctx.cpu_psi_high = cpu_psi_high;
    ctx.background_runtime_high = background_runtime_high;
    return ctx;
}

} // namespace

int main() {
    using eulerpilot::ControlMode;
    using eulerpilot::RuntimeThresholds;
    using eulerpilot::WorkloadClass;

    unsetenv("EULERPILOT_FEATURE_CLASSIFY_UNKNOWN");

    const auto redis = eulerpilot::classify_sample(sample("redis-server", 1000000, 200000, 8));
    assert(redis.klass == WorkloadClass::LatencySensitive);
    assert(redis.managed_target);
    assert(redis.gate_relevant);
    assert(redis.target_profile == "normal_profile");

    const auto stress = eulerpilot::classify_sample(sample("stress-ng-cpu", 8000000, 1000000, 1, 3));
    assert(stress.klass == WorkloadClass::BackgroundNoisy);
    assert(stress.managed_target);
    assert(stress.gate_relevant);

    const auto helper = eulerpilot::classify_sample(sample("yes", 8000000, 1000000, 1, 3));
    assert(helper.klass == WorkloadClass::BackgroundNoisy);
    assert(!helper.managed_target);
    assert(!helper.gate_relevant);

    const auto batch = eulerpilot::classify_sample(sample("sysbench", 9000000, 100000, 1));
    assert(batch.klass == WorkloadClass::ThroughputBatch);
    assert(batch.managed_target);
    assert(!batch.gate_relevant);

    const auto unknown = eulerpilot::classify_sample(sample("python-worker", 100000, 0, 0));
    assert(unknown.klass == WorkloadClass::Unknown);
    assert(!unknown.managed_target);
    assert(!unknown.gate_relevant);

    const auto feature_off = eulerpilot::classify_sample(sample("opaque-worker", 1000000, 900000, 10));
    assert(feature_off.klass == WorkloadClass::Unknown);

    setenv("EULERPILOT_FEATURE_CLASSIFY_UNKNOWN", "1", 1);
    const auto latency_like = eulerpilot::classify_sample(sample("opaque-lat", 1000000, 900000, 10));
    assert(latency_like.klass == WorkloadClass::LatencySensitive);
    assert(!latency_like.managed_target);
    assert(!latency_like.gate_relevant);
    assert(latency_like.gate_reason == "feature-vector-diagnostic-only");

    const auto batch_like = eulerpilot::classify_sample(sample("opaque-batch", 6000000, 100000, 1));
    assert(batch_like.klass == WorkloadClass::ThroughputBatch);
    assert(!batch_like.managed_target);
    assert(!batch_like.gate_relevant);

    const auto noisy_like = eulerpilot::classify_sample(sample("opaque-noisy", 1000000, 100000, 1, 3));
    assert(noisy_like.klass == WorkloadClass::BackgroundNoisy);
    assert(!noisy_like.managed_target);
    assert(!noisy_like.gate_relevant);
    unsetenv("EULERPILOT_FEATURE_CLASSIFY_UNKNOWN");

    RuntimeThresholds base;
    base.adaptive_enabled = true;
    const auto calibrated = eulerpilot::calibrate_runtime_thresholds(
        base,
        {10.0, 20.0, 30.0, 40.0, 50.0},
        {100.0, 200.0, 300.0, 400.0, 500.0},
        {0.001, 0.01, 0.02, 0.03, 0.04});
    assert(calibrated.calibrated);
    assert(calibrated.wait_threshold_ns == 50.0);
    assert(calibrated.background_runtime_threshold_ns == 500.0);
    assert(calibrated.cpu_psi_threshold == 0.04);

    const auto fallback = eulerpilot::calibrate_runtime_thresholds(base, {}, {}, {});
    assert(fallback.wait_threshold_ns == base.wait_threshold_ns);
    assert(fallback.background_runtime_threshold_ns == base.background_runtime_threshold_ns);
    assert(fallback.cpu_psi_threshold == base.cpu_psi_threshold);

    RuntimeThresholds explicit_base = base;
    explicit_base.wait_threshold_ns = 1234.0;
    explicit_base.wait_explicit = true;
    explicit_base.cpu_psi_threshold = 0.25;
    explicit_base.cpu_psi_explicit = true;
    const auto explicit_kept = eulerpilot::calibrate_runtime_thresholds(
        explicit_base,
        {50.0, 60.0},
        {100.0, 200.0},
        {0.01, 0.02});
    assert(explicit_kept.wait_threshold_ns == 1234.0);
    assert(explicit_kept.cpu_psi_threshold == 0.25);

    assert(eulerpilot::derive_desired_mode(context(false, true, true, true, true)) ==
           ControlMode::Normal);
    assert(eulerpilot::derive_desired_mode(context(true, false, true, true, true)) ==
           ControlMode::Normal);
    assert(eulerpilot::derive_desired_mode(context(true, true, false, true, false)) ==
           ControlMode::Latency);
    assert(eulerpilot::derive_desired_mode(context(true, true, true, false, false)) ==
           ControlMode::Latency);
    assert(eulerpilot::derive_desired_mode(context(true, true, true, true, false)) ==
           ControlMode::Mixed);
    assert(eulerpilot::derive_desired_mode(context(true, true, true, false, true)) ==
           ControlMode::Mixed);

    setenv("EULERPILOT_SCX_BINARY", "/bin/false", 1);
    eulerpilot::RuntimeConfig scx_cfg;
    scx_cfg.scheduler_binary_path = "/bin/true";
    scx_cfg.scheduler_binary_source = "yaml:scheduler.binary";
    auto yaml_binary = eulerpilot::resolve_scx_binary(scx_cfg);
    assert(yaml_binary.path == "/bin/true");
    assert(yaml_binary.source == "yaml:scheduler.binary");
    assert(yaml_binary.executable);

    scx_cfg.scheduler_binary_path.clear();
    auto env_binary = eulerpilot::resolve_scx_binary(scx_cfg);
    assert(env_binary.path == "/bin/false");
    assert(env_binary.source == "env:EULERPILOT_SCX_BINARY");
    assert(env_binary.executable);

    scx_cfg.scheduler_binary_path = "/no/such/scx_eulerpilot";
    scx_cfg.scheduler_binary_source = "yaml:scheduler.binary";
    auto missing_binary = eulerpilot::resolve_scx_binary(scx_cfg);
    assert(missing_binary.path == "/no/such/scx_eulerpilot");
    assert(missing_binary.source == "yaml:scheduler.binary");
    assert(!missing_binary.executable);
    unsetenv("EULERPILOT_SCX_BINARY");

    return 0;
}
