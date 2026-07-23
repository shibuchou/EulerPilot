#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eulerpilot {

enum class ExecutorBackend {
    CgroupV2,
    SchedExt,
};

enum class WorkloadClass {
    Unknown,
    LatencySensitive,
    ThroughputBatch,
    BackgroundNoisy,
    MixedService,
};

enum class GateMode {
    AlwaysActive,
    Psi,
    Normal,
};

enum class GateState : std::uint32_t {
    Normal = 0,
    Armed = 1,
    Active = 2,
    Cooldown = 3,
};

struct RuntimeConfig {
    std::string config_path;
    std::uint32_t interval_ms = 1000;
    std::uint32_t duration_s = 1;
    std::uint32_t warmup_cycles = 0;
    bool dry_run = true;
    ExecutorBackend preferred_backend = ExecutorBackend::CgroupV2;
    GateMode gate_mode = GateMode::AlwaysActive;
    bool backend_cli_set = false;
    bool list_skills_only = false;
    bool doctor_skills_only = false;
    bool doctor_safe_only = false;
    bool validate_config_only = false;
    bool status_only = false;
    bool strict = false;
    bool verbose = false;
    bool jsonl = false;
    bool metrics_enabled = false;
    std::string metrics_listen = "127.0.0.1:9108";
    std::string scheduler_binary_path;
    std::string scheduler_binary_source = "auto";
};

struct EnvironmentStatus {
    bool psi_configured = false;
    bool cgroup_v2_mounted = false;
    bool sched_ext_available = false;
};

struct PsiWindowValue {
    double avg10 = 0.0;
    double avg60 = 0.0;
    double avg300 = 0.0;
    std::uint64_t total = 0;
};

struct PsiResourceSnapshot {
    PsiWindowValue some;
    PsiWindowValue full;
};

struct PsiSnapshot {
    PsiResourceSnapshot cpu;
    PsiResourceSnapshot memory;
    PsiResourceSnapshot io;
};

struct ExecutionAction {
    bool applied = false;
    std::string executor = "observe-only";
    std::string target_group = "none";
    std::string target_profile = "normal_profile";
    int cpu_weight = 100;
    std::string cpu_max = "max";
    std::string cpuset_cpus = "";
    std::string memory_high = "max";
    std::string memory_low = "0";
    std::string memory_max = "max";
    std::string memory_reclaim = "";
    std::string io_weight = "";
    std::string io_max = "";
    std::string resource_mode = "normal";
    std::string target_ref = "";
    std::string target_cgroup_path = "";
    std::string reason = "no-action";
};

struct WorkloadSample {
    int pid = 0;
    int tgid = 0;
    std::string comm;
    std::uint64_t cgroup_id = 0;
    std::uint64_t start_boottime_ns = 0;
    std::uint64_t generation_cookie = 0;
    std::string identity_source = "unknown";
    std::uint64_t total_wait_ns = 0;
    std::uint64_t runtime_ns = 0;
    std::uint64_t wakeup_count = 0;
    std::uint64_t ctx_switch_count = 0;
    std::uint64_t migrate_count = 0;
    std::uint64_t total_wait_ns_delta = 0;
    std::uint64_t runtime_ns_delta = 0;
    std::uint64_t wakeup_count_delta = 0;
    std::uint64_t ctx_switch_count_delta = 0;
    std::uint64_t migrate_count_delta = 0;
};

struct WorkloadDecision {
    WorkloadSample sample;
    WorkloadClass klass = WorkloadClass::Unknown;
    double latency_score = 0.0;
    double batch_score = 0.0;
    double interference_score = 0.0;
    bool managed_target = false;
    bool gate_relevant = false;
    std::string gate_reason = "unqualified";
    std::string target_profile = "normal_profile";
    ExecutionAction action;
    bool cpu_psi_high = false;
    bool latency_wait_high = false;
    bool background_runtime_high = false;
    bool latency_exists = false;
    bool background_exists = false;
    std::string trigger_reason = "unclassified";
    bool adaptive_thresholds_enabled = false;
    bool adaptive_thresholds_calibrated = false;
    double calibrated_latency_wait_threshold_ns = 5000000.0;
    double calibrated_background_runtime_threshold_ns = 4000000.0;
    double calibrated_cpu_psi_threshold = 0.10;
};

enum class ControlMode : int {
    Normal,
    Latency,
    Mixed,
};

struct TriggerContext {
    bool cpu_psi_high = false;
    bool cpu_psi_triggered = false;
    bool latency_exists = false;
    bool background_exists = false;
    std::uint32_t gate_relevant_latency_count = 0;
    std::uint32_t gate_relevant_background_count = 0;
    bool latency_wait_high = false;
    bool background_runtime_high = false;
    double wait_threshold_ns = 5000000.0;
    double background_runtime_threshold_ns = 4000000.0;
};

struct RuntimeThresholds {
    double cpu_psi_threshold = 0.10;
    double wait_threshold_ns = 5000000.0;
    double background_runtime_threshold_ns = 4000000.0;
    bool cpu_psi_explicit = false;
    bool wait_explicit = false;
    bool background_explicit = false;
    bool adaptive_enabled = false;
    bool calibrated = false;
};

struct GateDecision {
    GateState previous_state = GateState::Normal;
    GateState next_state = GateState::Normal;
    std::uint32_t generation = 0;
    std::uint64_t timestamp_ns = 0;
    std::uint64_t updated_at_ns = 0;
    std::uint32_t evidence_mask = 0;
    bool cpu_psi_triggered = false;
    bool latency_wait_high = false;
    bool background_runtime_high = false;
    bool latency_workload_present = false;
    bool background_workload_present = false;
    std::string profile = "sched_ext_normal";
};

const char *to_string(ExecutorBackend backend);
const char *to_string(WorkloadClass klass);
const char *to_string(GateMode mode);
const char *to_string(GateState state);
RuntimeConfig parse_args(int argc, char **argv);
EnvironmentStatus detect_environment();
PsiSnapshot read_psi_snapshot();
WorkloadDecision classify_sample(const WorkloadSample &sample);
std::uint64_t safe_counter_delta(std::uint64_t current, std::uint64_t previous);
std::vector<WorkloadSample> compute_sample_deltas_for_test(const std::vector<WorkloadSample> &samples);
void reset_sample_delta_history_for_tests();
RuntimeThresholds calibrate_runtime_thresholds(const RuntimeThresholds &base,
                                                const std::vector<double> &latency_wait_ns,
                                                const std::vector<double> &background_runtime_ns,
                                                const std::vector<double> &cpu_psi_avg10);
ControlMode derive_desired_mode(const TriggerContext &ctx);
void assign_profiles(std::vector<WorkloadDecision> &decisions, ControlMode mode);
ExecutionAction apply_cgroup_assignment(const RuntimeConfig &config, const WorkloadDecision &decision);
ExecutionAction apply_scx_assignment(const RuntimeConfig &config, const WorkloadDecision &decision,
                                     bool scheduler_active, const std::string &scheduler_reason);
TriggerContext build_trigger_context(std::vector<WorkloadDecision> &decisions, bool cpu_psi_high, bool cpu_psi_triggered);
TriggerContext build_trigger_context(std::vector<WorkloadDecision> &decisions, bool cpu_psi_high,
                                     bool cpu_psi_triggered, const RuntimeThresholds &thresholds);
std::vector<WorkloadDecision> run_once(const RuntimeConfig &config);
std::vector<WorkloadDecision> run_cycles(const RuntimeConfig &config);
void request_shutdown();
bool shutdown_requested();

} // namespace eulerpilot
