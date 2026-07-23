#include "action_journal.hpp"
#include "eulerpilot.hpp"
#include "executors.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    std::vector<eulerpilot::WorkloadDecision> throughput_default{batch};
    eulerpilot::assign_profiles(throughput_default, ControlMode::Normal);
    assert(throughput_default[0].target_profile == "normal_profile");

    setenv("EULERPILOT_THROUGHPUT_FIRST", "1", 1);
    std::vector<eulerpilot::WorkloadDecision> throughput_enabled{batch};
    eulerpilot::assign_profiles(throughput_enabled, ControlMode::Normal);
    assert(throughput_enabled[0].target_profile == "throughput_profile");
    assert(throughput_enabled[0].trigger_reason == "throughput-first-explicitly-enabled");
    unsetenv("EULERPILOT_THROUGHPUT_FIRST");

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

    assert(eulerpilot::safe_counter_delta(30, 10) == 20);
    assert(eulerpilot::safe_counter_delta(5, 10) == 5);

    eulerpilot::reset_sample_delta_history_for_tests();
    auto redis_thread_a = sample("redis-server", 1000, 100, 4);
    redis_thread_a.pid = 201;
    redis_thread_a.tgid = 200;
    redis_thread_a.start_boottime_ns = 10001;
    auto redis_thread_b = sample("redis-server", 2000, 200, 6);
    redis_thread_b.pid = 202;
    redis_thread_b.tgid = 200;
    redis_thread_b.start_boottime_ns = 10002;

    auto aggregated = eulerpilot::compute_sample_deltas_for_test({redis_thread_a, redis_thread_b});
    assert(aggregated.size() == 1);
    assert(aggregated[0].pid == 200);
    assert(aggregated[0].tgid == 200);
    assert(aggregated[0].runtime_ns_delta == 3000);
    assert(aggregated[0].total_wait_ns_delta == 300);
    assert(aggregated[0].wakeup_count_delta == 10);
    assert(aggregated[0].identity_source.find("bpf_start_boottime_ns") != std::string::npos);

    redis_thread_a.runtime_ns = 1300;
    redis_thread_a.total_wait_ns = 150;
    redis_thread_a.wakeup_count = 5;
    redis_thread_b.runtime_ns = 2600;
    redis_thread_b.total_wait_ns = 260;
    redis_thread_b.wakeup_count = 8;
    aggregated = eulerpilot::compute_sample_deltas_for_test({redis_thread_a, redis_thread_b});
    assert(aggregated.size() == 1);
    assert(aggregated[0].runtime_ns_delta == 900);
    assert(aggregated[0].total_wait_ns_delta == 110);
    assert(aggregated[0].wakeup_count_delta == 3);

    redis_thread_a.runtime_ns = 10;
    redis_thread_a.total_wait_ns = 5;
    redis_thread_a.wakeup_count = 1;
    aggregated = eulerpilot::compute_sample_deltas_for_test({redis_thread_a});
    assert(aggregated.size() == 1);
    assert(aggregated[0].runtime_ns_delta == 10);
    assert(aggregated[0].total_wait_ns_delta == 5);
    assert(aggregated[0].wakeup_count_delta == 1);

    eulerpilot::reset_sample_delta_history_for_tests();
    auto reused_tid_old = sample("opaque", 500, 50, 2);
    reused_tid_old.pid = 301;
    reused_tid_old.tgid = 300;
    reused_tid_old.start_boottime_ns = 90001;
    auto reused_first = eulerpilot::compute_sample_deltas_for_test({reused_tid_old});
    assert(reused_first[0].runtime_ns_delta == 500);

    auto reused_tid_new = reused_tid_old;
    reused_tid_new.start_boottime_ns = 90002;
    reused_tid_new.runtime_ns = 7;
    reused_tid_new.total_wait_ns = 3;
    reused_tid_new.wakeup_count = 1;
    auto reused_second = eulerpilot::compute_sample_deltas_for_test({reused_tid_new});
    assert(reused_second[0].runtime_ns_delta == 7);
    assert(reused_second[0].total_wait_ns_delta == 3);

    eulerpilot::reset_sample_delta_history_for_tests();
    auto fallback_identity = sample("fallback", 100, 10, 1);
    fallback_identity.pid = 401;
    fallback_identity.tgid = 400;
    fallback_identity.start_boottime_ns = 0;
    auto fallback_first = eulerpilot::compute_sample_deltas_for_test({fallback_identity});
    assert(fallback_first[0].identity_source.find("user_generation_cookie") != std::string::npos);
    const auto first_cookie = fallback_first[0].generation_cookie;

    fallback_identity.runtime_ns = 130;
    fallback_identity.total_wait_ns = 16;
    fallback_identity.wakeup_count = 2;
    auto fallback_second = eulerpilot::compute_sample_deltas_for_test({fallback_identity});
    assert(fallback_second[0].runtime_ns_delta == 30);
    assert(fallback_second[0].total_wait_ns_delta == 6);
    assert(fallback_second[0].generation_cookie == first_cookie);

    fallback_identity.runtime_ns = 2;
    fallback_identity.total_wait_ns = 1;
    fallback_identity.wakeup_count = 1;
    auto fallback_reset = eulerpilot::compute_sample_deltas_for_test({fallback_identity});
    assert(fallback_reset[0].runtime_ns_delta == 2);
    assert(fallback_reset[0].total_wait_ns_delta == 1);
    assert(fallback_reset[0].generation_cookie != first_cookie);

    const auto journal_path =
        std::filesystem::temp_directory_path() / "eulerpilot-action-journal-test.jsonl";
    std::filesystem::remove(journal_path);
    eulerpilot::JournalAction applied;
    applied.action_id = "unit-applied";
    applied.skill = "unit";
    applied.target = "target";
    applied.operation = "apply";
    std::string journal_error;
    assert(eulerpilot::append_journal_action(journal_path.string(), applied,
                                             &journal_error));
    eulerpilot::JournalAction restored = applied;
    restored.action_id = "unit-restored";
    restored.operation = "rollback";
    restored.restored = true;
    assert(eulerpilot::append_journal_action(journal_path.string(), restored,
                                             &journal_error));
    std::ifstream journal_file(journal_path);
    std::stringstream journal_buffer;
    journal_buffer << journal_file.rdbuf();
    const std::string journal_text = journal_buffer.str();
    assert(journal_text.find("\"state\":\"APPLIED\"") != std::string::npos);
    assert(journal_text.find("\"state\":\"ROLLED_BACK\"") != std::string::npos);
    std::filesystem::remove(journal_path);

    return 0;
}
