#include "eulerpilot.hpp"
#include "builtin_skills.hpp"
#include "metrics_state.hpp"
#include "skill_manager.hpp"
#include "skill_runtime_context.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <utility>
#include <vector>

#include "workload_observer.h"
#include "workload_observer.skel.h"
#include "psi_reader.h"

namespace eulerpilot {

namespace {

// Signal handlers update only this flag. Runtime cleanup still happens on the
// normal control path, so Skill rollback and metrics shutdown remain ordered.
volatile std::sig_atomic_t g_shutdown_requested = 0;

bool file_exists(const char *path) {
    std::ifstream file(path);
    return file.good();
}

void sleep_interruptible(std::chrono::milliseconds duration) {
    constexpr auto step = std::chrono::milliseconds(50);
    auto remaining = duration;
    while (remaining.count() > 0 && !shutdown_requested()) {
        const auto chunk = remaining < step ? remaining : step;
        std::this_thread::sleep_for(chunk);
        remaining -= chunk;
    }
}

bool looks_latency_service(const WorkloadSample &sample) {
    const char *benchmark_signal = std::getenv("EULERPILOT_MIXED_BENCHMARK_LATENCY_SIGNAL");
    const bool benchmark_signal_enabled = benchmark_signal &&
        std::strcmp(benchmark_signal, "0") != 0 &&
        std::strcmp(benchmark_signal, "false") != 0 &&
        std::strcmp(benchmark_signal, "False") != 0 &&
        std::strcmp(benchmark_signal, "FALSE") != 0;
    const bool benchmark_latency_signal = benchmark_signal_enabled &&
        sample.comm.find("redis-benchmark") != std::string::npos;
    return sample.comm.find("redis-server") != std::string::npos ||
           sample.comm.find("nginx") != std::string::npos ||
           benchmark_latency_signal;
}

bool looks_background_noisy(const WorkloadSample &sample) {
    return sample.comm.find("stress") != std::string::npos ||
           sample.comm == "yes" ||
           sample.comm == "sleep";
}

bool looks_batch(const WorkloadSample &sample) {
    return sample.comm.find("make") != std::string::npos ||
           sample.comm.find("sysbench") != std::string::npos ||
           sample.comm.find("bench") != std::string::npos;
}

bool file_contains(const char *path, const char *needle) {
    std::ifstream file(path);
    if (!file.good()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str().find(needle) != std::string::npos;
}

double get_env_double(const char *name, double fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return std::atof(value);
}

bool env_is_set(const char *name) {
    const char *value = std::getenv(name);
    return value && *value;
}

bool env_flag_enabled(const char *name) {
    const char *value = std::getenv(name);
    if (!value || !*value) {
        return false;
    }
    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "on") == 0;
}

bool throughput_first_enabled() {
    return env_flag_enabled("EULERPILOT_THROUGHPUT_FIRST");
}

double percentile95_or_fallback(std::vector<double> values, double fallback) {
    if (values.empty()) {
        return fallback;
    }
    std::sort(values.begin(), values.end());
    const auto raw_index = std::ceil(static_cast<double>(values.size()) * 0.95) - 1.0;
    const auto bounded = std::min<double>(static_cast<double>(values.size() - 1), std::max<double>(0.0, raw_index));
    return values[static_cast<std::size_t>(bounded)];
}

RuntimeThresholds runtime_thresholds_from_env() {
    RuntimeThresholds thresholds;
    thresholds.cpu_psi_explicit = env_is_set("EULERPILOT_CPU_PSI_THRESHOLD");
    thresholds.wait_explicit = env_is_set("EULERPILOT_LATENCY_WAIT_THRESHOLD_NS");
    thresholds.background_explicit = env_is_set("EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS");
    thresholds.cpu_psi_threshold = get_env_double("EULERPILOT_CPU_PSI_THRESHOLD", thresholds.cpu_psi_threshold);
    thresholds.wait_threshold_ns = get_env_double("EULERPILOT_LATENCY_WAIT_THRESHOLD_NS", thresholds.wait_threshold_ns);
    thresholds.background_runtime_threshold_ns =
        get_env_double("EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS", thresholds.background_runtime_threshold_ns);
    return thresholds;
}

WorkloadSample to_sample(const task_metrics &metrics) {
    WorkloadSample sample;
    sample.pid = static_cast<int>(metrics.pid);
    sample.tgid = static_cast<int>(metrics.tgid);
    sample.comm = metrics.comm;
    sample.cgroup_id = metrics.cgroup_id;
    sample.start_boottime_ns = metrics.start_boottime_ns;
    sample.identity_source = metrics.start_boottime_ns ? "bpf_start_boottime_ns" : "user_generation_cookie";
    sample.total_wait_ns = metrics.total_wait_ns;
    sample.runtime_ns = metrics.runtime_ns;
    sample.wakeup_count = metrics.wakeup_count;
    sample.ctx_switch_count = metrics.ctx_switch_count;
    sample.migrate_count = metrics.migrate_count;
    return sample;
}

std::vector<WorkloadSample> read_samples(struct workload_observer_bpf *skel) {
    std::vector<WorkloadSample> samples;
    __u32 key = 0;
    __u32 next_key = 0;
    struct task_metrics metrics = {};
    int map_fd = bpf_map__fd(skel->maps.task_metrics_map);
    bool first = true;
    std::vector<__u32> keys;

    while (bpf_map_get_next_key(map_fd, first ? nullptr : &key, &next_key) == 0) {
        keys.push_back(next_key);
        key = next_key;
        first = false;
    }

    for (const auto sample_key : keys) {
        if (bpf_map_lookup_elem(map_fd, &sample_key, &metrics) == 0) {
            samples.push_back(to_sample(metrics));
        }
    }

    return samples;
}

bool should_ignore_sample(const WorkloadSample &sample, int self_pid) {
    return sample.pid == 0 || sample.pid == self_pid || sample.comm.rfind("kworker", 0) == 0 ||
           sample.comm.rfind("migration", 0) == 0 || sample.comm.rfind("rcu_", 0) == 0 ||
           sample.comm.rfind("kcompactd", 0) == 0 || sample.comm.rfind("ksoftirqd", 0) == 0;
}

struct TaskIdentityKey {
    int tgid = 0;
    int tid = 0;
    std::uint64_t identity_value = 0;
    bool uses_start_boottime = false;

    bool operator==(const TaskIdentityKey &other) const {
        return tgid == other.tgid &&
               tid == other.tid &&
               identity_value == other.identity_value &&
               uses_start_boottime == other.uses_start_boottime;
    }
};

struct TaskIdentityHash {
    std::size_t operator()(const TaskIdentityKey &key) const {
        std::size_t h = std::hash<int>{}(key.tgid);
        h ^= std::hash<int>{}(key.tid) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(key.identity_value) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(key.uses_start_boottime) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

struct SampleDeltaHistory {
    std::unordered_map<TaskIdentityKey, WorkloadSample, TaskIdentityHash> previous_samples;
    std::unordered_map<std::string, std::uint64_t> fallback_generations;
    std::uint64_t next_generation_cookie = 1;
};

SampleDeltaHistory &sample_delta_history() {
    static SampleDeltaHistory history;
    return history;
}

std::string task_pair_key(const WorkloadSample &sample) {
    return std::to_string(sample.tgid) + ":" + std::to_string(sample.pid);
}

bool sample_counter_reset(const WorkloadSample &current, const WorkloadSample &previous) {
    return current.total_wait_ns < previous.total_wait_ns ||
           current.runtime_ns < previous.runtime_ns ||
           current.wakeup_count < previous.wakeup_count ||
           current.ctx_switch_count < previous.ctx_switch_count ||
           current.migrate_count < previous.migrate_count;
}

TaskIdentityKey resolve_task_identity(WorkloadSample &sample, SampleDeltaHistory &history) {
    if (sample.start_boottime_ns != 0) {
        sample.identity_source = "bpf_start_boottime_ns";
        sample.generation_cookie = 0;
        return TaskIdentityKey{sample.tgid, sample.pid, sample.start_boottime_ns, true};
    }

    const std::string pair_key = task_pair_key(sample);
    auto generation = history.fallback_generations.find(pair_key);
    if (generation == history.fallback_generations.end()) {
        generation = history.fallback_generations.emplace(pair_key, history.next_generation_cookie++).first;
    }

    TaskIdentityKey key{sample.tgid, sample.pid, generation->second, false};
    auto previous = history.previous_samples.find(key);
    if (previous != history.previous_samples.end() && sample_counter_reset(sample, previous->second)) {
        generation->second = history.next_generation_cookie++;
        key.identity_value = generation->second;
    }

    sample.generation_cookie = key.identity_value;
    sample.identity_source = "user_generation_cookie";
    return key;
}

void add_to_tgid_aggregate(std::unordered_map<int, WorkloadSample> &aggregates,
                           std::vector<int> &tgid_order,
                           const WorkloadSample &sample) {
    auto [it, inserted] = aggregates.emplace(sample.tgid, sample);
    if (inserted) {
        tgid_order.push_back(sample.tgid);
        it->second.pid = sample.tgid;
        it->second.identity_source = "tgid_aggregate:" + sample.identity_source;
        return;
    }

    auto &aggregate = it->second;
    aggregate.total_wait_ns += sample.total_wait_ns;
    aggregate.runtime_ns += sample.runtime_ns;
    aggregate.wakeup_count += sample.wakeup_count;
    aggregate.ctx_switch_count += sample.ctx_switch_count;
    aggregate.migrate_count += sample.migrate_count;
    aggregate.total_wait_ns_delta += sample.total_wait_ns_delta;
    aggregate.runtime_ns_delta += sample.runtime_ns_delta;
    aggregate.wakeup_count_delta += sample.wakeup_count_delta;
    aggregate.ctx_switch_count_delta += sample.ctx_switch_count_delta;
    aggregate.migrate_count_delta += sample.migrate_count_delta;
    if (aggregate.cgroup_id == 0 && sample.cgroup_id != 0) {
        aggregate.cgroup_id = sample.cgroup_id;
    }
    if (aggregate.identity_source.find(sample.identity_source) == std::string::npos) {
        aggregate.identity_source += "+" + sample.identity_source;
    }
}

} // namespace

std::uint64_t safe_counter_delta(std::uint64_t current, std::uint64_t previous) {
    return current >= previous ? current - previous : current;
}

std::vector<WorkloadSample> compute_sample_deltas_for_test(const std::vector<WorkloadSample> &samples) {
    auto &history = sample_delta_history();
    std::unordered_map<TaskIdentityKey, WorkloadSample, TaskIdentityHash> next_previous;
    std::unordered_map<int, WorkloadSample> aggregates;
    std::unordered_map<std::string, bool> seen_pairs;
    std::vector<int> tgid_order;

    for (const auto &sample : samples) {
        WorkloadSample current = sample;
        TaskIdentityKey identity = resolve_task_identity(current, history);
        seen_pairs[task_pair_key(current)] = true;

        auto previous = history.previous_samples.find(identity);
        if (previous != history.previous_samples.end()) {
            current.total_wait_ns_delta = safe_counter_delta(current.total_wait_ns, previous->second.total_wait_ns);
            current.runtime_ns_delta = safe_counter_delta(current.runtime_ns, previous->second.runtime_ns);
            current.wakeup_count_delta = safe_counter_delta(current.wakeup_count, previous->second.wakeup_count);
            current.ctx_switch_count_delta = safe_counter_delta(current.ctx_switch_count, previous->second.ctx_switch_count);
            current.migrate_count_delta = safe_counter_delta(current.migrate_count, previous->second.migrate_count);
        } else {
            current.total_wait_ns_delta = current.total_wait_ns;
            current.runtime_ns_delta = current.runtime_ns;
            current.wakeup_count_delta = current.wakeup_count;
            current.ctx_switch_count_delta = current.ctx_switch_count;
            current.migrate_count_delta = current.migrate_count;
        }

        next_previous[identity] = current;
        add_to_tgid_aggregate(aggregates, tgid_order, current);
    }

    for (auto it = history.fallback_generations.begin(); it != history.fallback_generations.end();) {
        if (seen_pairs.find(it->first) == seen_pairs.end()) {
            it = history.fallback_generations.erase(it);
        } else {
            ++it;
        }
    }
    history.previous_samples = std::move(next_previous);

    std::vector<WorkloadSample> out;
    out.reserve(tgid_order.size());
    for (const auto tgid : tgid_order) {
        out.push_back(aggregates[tgid]);
    }
    return out;
}

void reset_sample_delta_history_for_tests() {
    auto &history = sample_delta_history();
    history.previous_samples.clear();
    history.fallback_generations.clear();
    history.next_generation_cookie = 1;
}

const char *to_string(ExecutorBackend backend) {
    switch (backend) {
    case ExecutorBackend::CgroupV2:
        return "cgroup_v2";
    case ExecutorBackend::SchedExt:
        return "sched_ext";
    default:
        return "unknown";
    }
}

const char *to_string(WorkloadClass klass) {
    switch (klass) {
    case WorkloadClass::LatencySensitive:
        return "LATENCY_SENSITIVE";
    case WorkloadClass::ThroughputBatch:
        return "THROUGHPUT_BATCH";
    case WorkloadClass::BackgroundNoisy:
        return "BACKGROUND_NOISY";
    case WorkloadClass::MixedService:
        return "MIXED_SERVICE";
    case WorkloadClass::Unknown:
    default:
        return "UNKNOWN";
    }
}

RuntimeConfig parse_args(int argc, char **argv) {
    RuntimeConfig config;
    config.config_path = "configs/agent.yaml";
    const char *gate_mode_env = std::getenv("EULERPILOT_GATE_MODE");
    if (gate_mode_env) {
        if (std::strcmp(gate_mode_env, "psi") == 0) {
            config.gate_mode = GateMode::Psi;
        } else if (std::strcmp(gate_mode_env, "normal") == 0) {
            config.gate_mode = GateMode::Normal;
        } else if (std::strcmp(gate_mode_env, "always-active") == 0) {
            config.gate_mode = GateMode::AlwaysActive;
        }
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--config requires a path");
            }
            config.config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--active") == 0) {
            config.dry_run = false;
            config.mode_cli_set = true;
        } else if (std::strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires a value");
            }
            const char *backend = argv[++i];
            if (std::strcmp(backend, "cgroup_v2") == 0) {
                config.preferred_backend = ExecutorBackend::CgroupV2;
            } else if (std::strcmp(backend, "sched_ext") == 0) {
                config.preferred_backend = ExecutorBackend::SchedExt;
            } else {
                throw std::runtime_error(std::string("unknown backend: ") + backend);
            }
            config.backend_cli_set = true;
        } else if (std::strcmp(argv[i], "--gate-mode") == 0) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--gate-mode requires a value");
            }
            const char *mode = argv[++i];
            if (std::strcmp(mode, "always-active") == 0) {
                config.gate_mode = GateMode::AlwaysActive;
            } else if (std::strcmp(mode, "psi") == 0) {
                config.gate_mode = GateMode::Psi;
            } else if (std::strcmp(mode, "normal") == 0) {
                config.gate_mode = GateMode::Normal;
            } else {
                throw std::runtime_error(std::string("unknown gate-mode: ") + mode);
            }
        } else if (std::strcmp(argv[i], "--interval-ms") == 0) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--interval-ms requires a value");
            }
            config.interval_ms = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            config.interval_cli_set = true;
        } else if (std::strcmp(argv[i], "--duration-s") == 0) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--duration-s requires a value");
            }
            config.duration_s = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--warmup-cycles") == 0) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--warmup-cycles requires a value");
            }
            config.warmup_cycles = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--help") == 0) {
            throw std::runtime_error("usage: eulerpilot-agent [--config PATH] [--interval-ms N] [--duration-s N] [--warmup-cycles N] [--backend cgroup_v2|sched_ext] [--gate-mode always-active|psi|normal] [--active] [--strict] [--verbose] [--jsonl] [--list-skills] [--doctor-safe] [--doctor-skills] [--validate-config PATH] [--status --json]");
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
        } else if (std::strcmp(argv[i], "--jsonl") == 0) {
            config.jsonl = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            config.jsonl = true;
        } else if (std::strcmp(argv[i], "--strict") == 0) {
            config.strict = true;
        } else if (std::strcmp(argv[i], "--list-skills") == 0) {
            config.list_skills_only = true;
        } else if (std::strcmp(argv[i], "--doctor-safe") == 0) {
            config.doctor_safe_only = true;
        } else if (std::strcmp(argv[i], "--doctor-skills") == 0) {
            config.doctor_skills_only = true;
        } else if (std::strcmp(argv[i], "--validate-config") == 0) {
            config.validate_config_only = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.config_path = argv[++i];
            }
        } else if (std::strcmp(argv[i], "--status") == 0) {
            config.status_only = true;
        } else {
            throw std::runtime_error(std::string("unknown argument: ") + argv[i]);
        }
    }

    return config;
}

EnvironmentStatus detect_environment() {
    EnvironmentStatus status;
    status.psi_configured = file_exists("/proc/pressure/cpu");
    status.cgroup_v2_mounted = file_contains("/proc/self/mountinfo", " - cgroup2 ");
    status.sched_ext_available = file_exists("/sys/kernel/sched_ext");
    return status;
}

RuntimeThresholds calibrate_runtime_thresholds(const RuntimeThresholds &base,
                                                const std::vector<double> &latency_wait_ns,
                                                const std::vector<double> &background_runtime_ns,
                                                const std::vector<double> &cpu_psi_avg10) {
    RuntimeThresholds calibrated = base;
    calibrated.adaptive_enabled = true;
    calibrated.calibrated = true;
    if (!base.wait_explicit) {
        calibrated.wait_threshold_ns = percentile95_or_fallback(latency_wait_ns, base.wait_threshold_ns);
    }
    if (!base.background_explicit) {
        calibrated.background_runtime_threshold_ns =
            percentile95_or_fallback(background_runtime_ns, base.background_runtime_threshold_ns);
    }
    if (!base.cpu_psi_explicit) {
        const double p95 = percentile95_or_fallback(cpu_psi_avg10, base.cpu_psi_threshold);
        calibrated.cpu_psi_threshold = std::min(0.50, std::max(0.001, p95));
    }
    return calibrated;
}

WorkloadDecision classify_sample(const WorkloadSample &sample) {
    WorkloadDecision decision;
    decision.sample = sample;

    const double runtime_weight = sample.runtime_ns > 0 ? static_cast<double>(sample.runtime_ns) : 1.0;
    const double latency_ratio = static_cast<double>(sample.total_wait_ns) / runtime_weight;

    decision.latency_score = sample.wakeup_count > 0 ? std::min(1.0, latency_ratio + 0.2) : 0.0;
    decision.batch_score = sample.runtime_ns > 2000000 ? std::min(1.0, static_cast<double>(sample.runtime_ns) / 10000000.0) : 0.0;
    decision.interference_score = sample.migrate_count > 0
        ? std::min(1.0, static_cast<double>(sample.migrate_count) / 4.0 + latency_ratio * 0.3)
        : latency_ratio * 0.2;

    if (looks_latency_service(sample)) {
        decision.klass = WorkloadClass::LatencySensitive;
        decision.managed_target = true;
        decision.gate_relevant = true;
        decision.gate_reason = "managed-latency-workload";
        decision.target_profile = "normal_profile";
        decision.latency_score = std::max(decision.latency_score, 0.85);
    } else if (looks_background_noisy(sample)) {
        decision.klass = WorkloadClass::BackgroundNoisy;
        decision.managed_target = sample.comm.find("stress-ng") != std::string::npos ||
                                  sample.comm.find("stress-ng-cpu") != std::string::npos;
        decision.gate_relevant = decision.managed_target;
        decision.gate_reason = decision.managed_target ? "managed-background-workload"
                                                       : "helper-process-not-in-managed-workload-set";
        decision.target_profile = "normal_profile";
        decision.interference_score = std::max(decision.interference_score, 0.8);
    } else if (looks_batch(sample) || (sample.runtime_ns > 5000000 && sample.wakeup_count < 4 && should_manage_sample(sample))) {
        decision.klass = WorkloadClass::ThroughputBatch;
        decision.managed_target = sample.comm.find("make") != std::string::npos ||
                                  sample.comm.find("sysbench") != std::string::npos;
        decision.gate_relevant = false;
        decision.gate_reason = decision.managed_target ? "managed-batch-workload-not-gate-relevant"
                                                       : "unmanaged-batch-like-process";
        decision.target_profile = "normal_profile";
        decision.batch_score = std::max(decision.batch_score, 0.8);
    } else if (decision.latency_score > 0.7 && decision.interference_score > 0.4 && should_manage_sample(sample)) {
        decision.klass = WorkloadClass::MixedService;
        decision.managed_target = false;
        decision.gate_relevant = false;
        decision.gate_reason = "mixed-service-not-gate-relevant";
        decision.target_profile = "mixed_profile";
    } else if (env_flag_enabled("EULERPILOT_FEATURE_CLASSIFY_UNKNOWN") &&
               sample.wakeup_count >= 8 && decision.latency_score >= 0.75) {
        decision.klass = WorkloadClass::LatencySensitive;
        decision.managed_target = false;
        decision.gate_relevant = false;
        decision.gate_reason = "feature-vector-diagnostic-only";
        decision.target_profile = "normal_profile";
    } else if (env_flag_enabled("EULERPILOT_FEATURE_CLASSIFY_UNKNOWN") &&
               sample.runtime_ns >= 5000000 && sample.wakeup_count <= 3) {
        decision.klass = WorkloadClass::ThroughputBatch;
        decision.managed_target = false;
        decision.gate_relevant = false;
        decision.gate_reason = "feature-vector-diagnostic-only";
        decision.target_profile = "normal_profile";
    } else if (env_flag_enabled("EULERPILOT_FEATURE_CLASSIFY_UNKNOWN") &&
               (decision.interference_score >= 0.70 || sample.migrate_count >= 3)) {
        decision.klass = WorkloadClass::BackgroundNoisy;
        decision.managed_target = false;
        decision.gate_relevant = false;
        decision.gate_reason = "feature-vector-diagnostic-only";
        decision.target_profile = "normal_profile";
    } else {
        decision.klass = WorkloadClass::Unknown;
        decision.managed_target = false;
        decision.gate_relevant = false;
        decision.gate_reason = "helper-process-not-in-managed-workload-set";
        decision.target_profile = "normal_profile";
    }

    return decision;
}

TriggerContext build_trigger_context(std::vector<WorkloadDecision> &decisions, bool cpu_psi_high,
                                     bool cpu_psi_triggered, const RuntimeThresholds &thresholds) {
    TriggerContext ctx;
    ctx.cpu_psi_high = cpu_psi_high;
    ctx.cpu_psi_triggered = cpu_psi_triggered;
    ctx.wait_threshold_ns = thresholds.wait_threshold_ns;
    ctx.background_runtime_threshold_ns = thresholds.background_runtime_threshold_ns;

    for (auto &decision : decisions) {
        if (decision.klass == WorkloadClass::LatencySensitive) {
            ctx.latency_exists = true;
            if (decision.gate_relevant) {
                ctx.gate_relevant_latency_count++;
            }
            decision.latency_wait_high = decision.sample.total_wait_ns_delta >= ctx.wait_threshold_ns;
            if (decision.latency_wait_high) {
                ctx.latency_wait_high = true;
            }
        }

        if ((decision.klass == WorkloadClass::BackgroundNoisy || decision.klass == WorkloadClass::ThroughputBatch) &&
            decision.gate_relevant) {
            decision.background_runtime_high = decision.sample.runtime_ns_delta >= ctx.background_runtime_threshold_ns;
            if (decision.sample.runtime_ns_delta > 0 || decision.sample.wakeup_count_delta > 0 || decision.sample.ctx_switch_count_delta > 0) {
                ctx.background_exists = true;
                ctx.gate_relevant_background_count++;
            }
            if (decision.background_runtime_high) {
                ctx.background_runtime_high = true;
            }
        }

        decision.cpu_psi_high = cpu_psi_high;
        decision.latency_exists = ctx.latency_exists;
        decision.background_exists = ctx.background_exists;
    }

    for (auto &decision : decisions) {
        decision.latency_exists = ctx.latency_exists;
        decision.background_exists = ctx.background_exists;
    }

    return ctx;
}

TriggerContext build_trigger_context(std::vector<WorkloadDecision> &decisions, bool cpu_psi_high, bool cpu_psi_triggered) {
    return build_trigger_context(decisions, cpu_psi_high, cpu_psi_triggered, runtime_thresholds_from_env());
}

ControlMode derive_desired_mode(const TriggerContext &ctx) {
    if (!ctx.latency_exists || !ctx.background_exists) {
        return ControlMode::Normal;
    }

    if (ctx.latency_wait_high && (ctx.cpu_psi_high || ctx.background_runtime_high)) {
        return ControlMode::Mixed;
    }

    if (ctx.cpu_psi_high || ctx.latency_wait_high || ctx.background_runtime_high) {
        return ControlMode::Latency;
    }

    return ControlMode::Normal;
}

ControlMode update_mode_with_hysteresis(ControlMode current, ControlMode desired,
                                        std::uint32_t &latency_streak,
                                        std::uint32_t &mixed_streak,
                                        std::uint32_t &normal_streak) {
    const std::uint32_t enter_latency_requires = 2;
    const std::uint32_t enter_mixed_requires = 2;
    const std::uint32_t exit_to_normal_requires = 5;

    latency_streak = (desired == ControlMode::Latency) ? latency_streak + 1 : 0;
    mixed_streak = (desired == ControlMode::Mixed) ? mixed_streak + 1 : 0;
    normal_streak = (desired == ControlMode::Normal) ? normal_streak + 1 : 0;

    switch (current) {
    case ControlMode::Normal:
        if (mixed_streak >= enter_mixed_requires) {
            return ControlMode::Mixed;
        }
        if (latency_streak >= enter_latency_requires) {
            return ControlMode::Latency;
        }
        return ControlMode::Normal;
    case ControlMode::Latency:
        if (mixed_streak >= enter_mixed_requires) {
            return ControlMode::Mixed;
        }
        if (normal_streak >= exit_to_normal_requires) {
            return ControlMode::Normal;
        }
        return ControlMode::Latency;
    case ControlMode::Mixed:
        if (normal_streak >= exit_to_normal_requires) {
            return ControlMode::Normal;
        }
        return ControlMode::Mixed;
    default:
        return current;
    }
}

void assign_profiles(std::vector<WorkloadDecision> &decisions, ControlMode mode) {
    for (auto &decision : decisions) {
        decision.target_profile = "normal_profile";
        decision.trigger_reason = "no-pressure-evidence";

        switch (mode) {
        case ControlMode::Normal:
            if (!decision.latency_exists || !decision.background_exists) {
                decision.trigger_reason = "missing-latency-or-background-workload";
            }
            if (throughput_first_enabled() &&
                decision.klass == WorkloadClass::ThroughputBatch &&
                decision.managed_target) {
                decision.target_profile = "throughput_profile";
                decision.trigger_reason = "throughput-first-explicitly-enabled";
            }
            break;
        case ControlMode::Latency:
            decision.trigger_reason = "partial-pressure-evidence";
            if (decision.klass == WorkloadClass::LatencySensitive) {
                decision.target_profile = "latency_profile";
            } else if (decision.klass == WorkloadClass::BackgroundNoisy ||
                       decision.klass == WorkloadClass::ThroughputBatch) {
                decision.target_profile = "latency_profile";
            }
            break;
        case ControlMode::Mixed:
            decision.trigger_reason = "cpu-psi-and-latency-wait-high";
            if (decision.klass == WorkloadClass::LatencySensitive) {
                decision.target_profile = "latency_profile";
            } else if (decision.klass == WorkloadClass::BackgroundNoisy ||
                       decision.klass == WorkloadClass::ThroughputBatch) {
                decision.target_profile = "mixed_profile";
            }
            break;
        }
    }
}

std::vector<WorkloadDecision> collect_cycle_decisions(struct workload_observer_bpf *skel, int self_pid) {
    std::vector<WorkloadDecision> decisions;
    auto samples = read_samples(skel);
    std::vector<WorkloadSample> observed;
    for (const auto &sample : samples) {
        if (should_ignore_sample(sample, self_pid)) {
            continue;
        }
        observed.push_back(sample);
    }

    auto aggregated_samples = compute_sample_deltas_for_test(observed);
    for (const auto &sample : aggregated_samples) {
        decisions.push_back(classify_sample(sample));
    }
    return decisions;
}

std::vector<WorkloadDecision> run_once(const RuntimeConfig &config) {
    RuntimeConfig one_cycle = config;
    one_cycle.duration_s = 1;
    one_cycle.warmup_cycles = 0;
    return run_cycles(one_cycle);
}

std::vector<WorkloadDecision> run_cycles(const RuntimeConfig &config) {
    if (config.list_skills_only || config.doctor_skills_only ||
        config.doctor_safe_only || config.validate_config_only ||
        config.status_only) {
        return {};
    }

    std::vector<WorkloadDecision> merged;
    const std::uint32_t cycle_count = std::max<std::uint32_t>(1, (config.duration_s * 1000) / std::max<std::uint32_t>(1, config.interval_ms));
    const int self_pid = static_cast<int>(getpid());
    std::uint32_t latency_streak = 0;
    std::uint32_t mixed_streak = 0;
    std::uint32_t normal_streak = 0;
    ControlMode current_mode = ControlMode::Normal;
    auto &skill_context = global_skill_runtime_context();
    RuntimeThresholds thresholds = runtime_thresholds_from_env();
    thresholds.adaptive_enabled = env_flag_enabled("EULERPILOT_ADAPTIVE_THRESHOLDS") && config.warmup_cycles > 0;
    std::vector<double> warmup_latency_wait_ns;
    std::vector<double> warmup_background_runtime_ns;
    std::vector<double> warmup_cpu_psi_avg10;

    if (!skill_context.resource_ops) {
        throw std::runtime_error("resource_control skill not running; cannot apply decisions");
    }

    struct workload_observer_bpf *skel = workload_observer_bpf__open();
    if (!skel) {
        throw std::runtime_error("failed to open workload observer skeleton");
    }
    if (workload_observer_bpf__load(skel) != 0) {
        workload_observer_bpf__destroy(skel);
        throw std::runtime_error("failed to load workload observer skeleton");
    }
    if (workload_observer_bpf__attach(skel) != 0) {
        workload_observer_bpf__destroy(skel);
        throw std::runtime_error("failed to attach workload observer skeleton");
    }

    for (std::uint32_t cycle = 0; cycle < cycle_count && !shutdown_requested(); ++cycle) {
        RuntimeConfig cycle_config = config;
        if (cycle < config.warmup_cycles) {
            cycle_config.dry_run = true;
        }

        sleep_interruptible(std::chrono::milliseconds(config.interval_ms));
        if (shutdown_requested()) {
            break;
        }
        auto cycle_decisions = collect_cycle_decisions(skel, self_pid);
        const PsiSnapshot psi = read_psi_snapshot();
        if (thresholds.adaptive_enabled && cycle < config.warmup_cycles) {
            warmup_cpu_psi_avg10.push_back(psi.cpu.some.avg10);
            for (const auto &decision : cycle_decisions) {
                if (decision.klass == WorkloadClass::LatencySensitive && decision.sample.total_wait_ns_delta > 0) {
                    warmup_latency_wait_ns.push_back(static_cast<double>(decision.sample.total_wait_ns_delta));
                }
                if ((decision.klass == WorkloadClass::BackgroundNoisy ||
                     decision.klass == WorkloadClass::ThroughputBatch) &&
                    decision.sample.runtime_ns_delta > 0) {
                    warmup_background_runtime_ns.push_back(static_cast<double>(decision.sample.runtime_ns_delta));
                }
            }
            if (cycle + 1 >= config.warmup_cycles) {
                thresholds = calibrate_runtime_thresholds(thresholds, warmup_latency_wait_ns,
                                                          warmup_background_runtime_ns, warmup_cpu_psi_avg10);
            }
        }

        const bool cpu_psi_high = psi.cpu.some.avg10 >= thresholds.cpu_psi_threshold;

        auto trigger_ctx = build_trigger_context(cycle_decisions, cpu_psi_high, false, thresholds);
        GateDecision gate_decision;
        if (skill_context.psi_gate_ops) {
            gate_decision = skill_context.psi_gate_ops->tick_gate(trigger_ctx);
        } else {
            gate_decision.previous_state = GateState::Normal;
            gate_decision.next_state = GateState::Normal;
            gate_decision.profile = "sched_ext_normal";
        }

        ControlMode desired_mode = ControlMode::Normal;
        switch (gate_decision.next_state) {
        case GateState::Active:
        case GateState::Cooldown:
            desired_mode = trigger_ctx.latency_wait_high ? ControlMode::Mixed : ControlMode::Latency;
            break;
        case GateState::Armed:
        case GateState::Normal:
        default:
            desired_mode = ControlMode::Normal;
            break;
        }
        current_mode = update_mode_with_hysteresis(current_mode, desired_mode, latency_streak, mixed_streak, normal_streak);
        assign_profiles(cycle_decisions, current_mode);
        for (auto &decision : cycle_decisions) {
            decision.adaptive_thresholds_enabled = thresholds.adaptive_enabled;
            decision.adaptive_thresholds_calibrated = thresholds.calibrated;
            decision.calibrated_latency_wait_threshold_ns = thresholds.wait_threshold_ns;
            decision.calibrated_background_runtime_threshold_ns = thresholds.background_runtime_threshold_ns;
            decision.calibrated_cpu_psi_threshold = thresholds.cpu_psi_threshold;
        }

        skill_context.resource_ops->apply_in_cycle(cycle_decisions, gate_decision, cycle_config);

        merged.insert(merged.end(), cycle_decisions.begin(), cycle_decisions.end());

        auto &m = global_metrics_state();
        m.cycles_total.store(m.cycles_total.load() + 1);
        m.observed_tasks.store(static_cast<std::uint64_t>(cycle_decisions.size()));
        m.psi_cpu_avg10.store(cpu_psi_high ? psi.cpu.some.avg10 : 0.0);
        m.gate_state.store(static_cast<int>(gate_decision.next_state));
        m.scx_ready.store(skill_context.scx_ready ? 1 : 0);

        std::uint64_t cl = 0, cb = 0, cu = 0, ca = 0;
        for (const auto &d : cycle_decisions) {
            if (d.action.applied) ca++;
            switch (d.klass) {
            case WorkloadClass::LatencySensitive: cl++; break;
            case WorkloadClass::BackgroundNoisy: cb++; break;
            default: cu++; break;
            }
        }
        m.classified_latency.store(cl);
        m.classified_background.store(cb);
        m.classified_unknown.store(cu);
        m.decisions_applied.store(ca);
    }

    workload_observer_bpf__destroy(skel);
    return merged;
}

void request_shutdown() {
    g_shutdown_requested = 1;
}

bool shutdown_requested() {
    return g_shutdown_requested != 0;
}

} // namespace eulerpilot
