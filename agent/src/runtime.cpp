#include "eulerpilot.hpp"
#include "builtin_skills.hpp"
#include "metrics_state.hpp"
#include "skill_manager.hpp"
#include "skill_runtime_context.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "workload_observer.h"
#include "workload_observer.skel.h"
#include "psi_reader.h"

namespace eulerpilot {

namespace {

bool file_exists(const char *path) {
    std::ifstream file(path);
    return file.good();
}

bool looks_latency_service(const WorkloadSample &sample) {
    return sample.comm.find("redis-server") != std::string::npos ||
           sample.comm.find("nginx") != std::string::npos;
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

WorkloadSample to_sample(const task_metrics &metrics) {
    WorkloadSample sample;
    sample.pid = static_cast<int>(metrics.pid);
    sample.tgid = static_cast<int>(metrics.tgid);
    sample.comm = metrics.comm;
    sample.cgroup_id = metrics.cgroup_id;
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

    while (bpf_map_get_next_key(map_fd, first ? nullptr : &key, &next_key) == 0) {
        if (bpf_map_lookup_elem(map_fd, &next_key, &metrics) == 0) {
            samples.push_back(to_sample(metrics));
        }
        key = next_key;
        first = false;
    }

    return samples;
}

bool should_ignore_sample(const WorkloadSample &sample, int self_pid) {
    return sample.pid == 0 || sample.pid == self_pid || sample.comm.rfind("kworker", 0) == 0 ||
           sample.comm.rfind("migration", 0) == 0 || sample.comm.rfind("rcu_", 0) == 0 ||
           sample.comm.rfind("kcompactd", 0) == 0 || sample.comm.rfind("ksoftirqd", 0) == 0;
}

} // namespace

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
            throw std::runtime_error("usage: eulerpilot-agent [--config PATH] [--interval-ms N] [--duration-s N] [--warmup-cycles N] [--backend cgroup_v2|sched_ext] [--gate-mode always-active|psi|normal] [--active] [--verbose] [--jsonl] [--list-skills] [--doctor-skills]");
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
        } else if (std::strcmp(argv[i], "--jsonl") == 0) {
            config.jsonl = true;
        } else if (std::strcmp(argv[i], "--list-skills") == 0) {
            config.list_skills_only = true;
        } else if (std::strcmp(argv[i], "--doctor-skills") == 0) {
            config.doctor_skills_only = true;
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
    } else {
        decision.klass = WorkloadClass::Unknown;
        decision.managed_target = false;
        decision.gate_relevant = false;
        decision.gate_reason = "helper-process-not-in-managed-workload-set";
        decision.target_profile = "normal_profile";
    }

    return decision;
}

TriggerContext build_trigger_context(std::vector<WorkloadDecision> &decisions, bool cpu_psi_high, bool cpu_psi_triggered) {
    TriggerContext ctx;
    ctx.cpu_psi_high = cpu_psi_high;
    ctx.cpu_psi_triggered = cpu_psi_triggered;
    ctx.wait_threshold_ns = get_env_double("EULERPILOT_LATENCY_WAIT_THRESHOLD_NS", 5000000.0);
    ctx.background_runtime_threshold_ns = get_env_double("EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS", 4000000.0);

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
    static std::unordered_map<int, WorkloadSample> previous_samples;
    std::vector<WorkloadDecision> decisions;
    auto samples = read_samples(skel);
    std::unordered_map<int, WorkloadSample> current_samples;
    for (const auto &sample : samples) {
        if (should_ignore_sample(sample, self_pid)) {
            continue;
        }

        WorkloadSample current = sample;
        auto it = previous_samples.find(current.tgid);
        if (it != previous_samples.end()) {
            current.total_wait_ns_delta = current.total_wait_ns - it->second.total_wait_ns;
            current.runtime_ns_delta = current.runtime_ns - it->second.runtime_ns;
            current.wakeup_count_delta = current.wakeup_count - it->second.wakeup_count;
            current.ctx_switch_count_delta = current.ctx_switch_count - it->second.ctx_switch_count;
            current.migrate_count_delta = current.migrate_count - it->second.migrate_count;
        } else {
            current.total_wait_ns_delta = current.total_wait_ns;
            current.runtime_ns_delta = current.runtime_ns;
            current.wakeup_count_delta = current.wakeup_count;
            current.ctx_switch_count_delta = current.ctx_switch_count;
            current.migrate_count_delta = current.migrate_count;
        }

        current_samples[current.tgid] = current;
        decisions.push_back(classify_sample(current));
    }
    previous_samples = std::move(current_samples);
    return decisions;
}

std::vector<WorkloadDecision> run_once(const RuntimeConfig &config) {
    RuntimeConfig one_cycle = config;
    one_cycle.duration_s = 1;
    one_cycle.warmup_cycles = 0;
    return run_cycles(one_cycle);
}

std::vector<WorkloadDecision> run_cycles(const RuntimeConfig &config) {
    if (config.list_skills_only || config.doctor_skills_only) {
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

    for (std::uint32_t cycle = 0; cycle < cycle_count; ++cycle) {
        RuntimeConfig cycle_config = config;
        if (cycle < config.warmup_cycles) {
            cycle_config.dry_run = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config.interval_ms));
        auto cycle_decisions = collect_cycle_decisions(skel, self_pid);
        const PsiSnapshot psi = read_psi_snapshot();
        const double cpu_psi_threshold = get_env_double("EULERPILOT_CPU_PSI_THRESHOLD", 0.10);
        const bool cpu_psi_high = psi.cpu.some.avg10 >= cpu_psi_threshold;

        auto trigger_ctx = build_trigger_context(cycle_decisions, cpu_psi_high, false);
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

        skill_context.resource_ops->apply_in_cycle(cycle_decisions, gate_decision);

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

} // namespace eulerpilot
