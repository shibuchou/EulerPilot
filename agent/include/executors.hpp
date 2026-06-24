#pragma once

#include <string>
#include <vector>

#include <bpf/libbpf.h>

#include "eulerpilot.hpp"

namespace eulerpilot {

struct ScxSession {
    pid_t pid = -1;
    std::string binary_path;
    bool fifo_mode = false;
    int class_map_fd = -1;
    int gate_state_map_fd = -1;
    int stats_map_fd = -1;
};

struct TriggerContext;

struct ResourceControlPolicy {
    std::string mode = "audit";
    bool cpu_max_enabled = true;
    bool memory_enabled = true;
    bool memory_high_enabled = true;
    bool memory_low_enabled = true;
    bool memory_max_enabled = true;
    bool memory_reclaim_enabled = false;

    std::string latency_cpu_max = "max";
    std::string batch_cpu_max = "max";
    std::string background_cpu_max_normal = "max";
    std::string background_cpu_max_pressure = "20000 100000";

    std::string latency_memory_high = "max";
    std::string latency_memory_low = "67108864";
    std::string latency_memory_max = "max";
    std::string batch_memory_high = "max";
    std::string batch_memory_low = "0";
    std::string batch_memory_max = "max";
    std::string background_memory_high_normal = "max";
    std::string background_memory_high_pressure = "134217728";
    std::string background_memory_low = "0";
    std::string background_memory_max = "max";
    std::string background_memory_reclaim_pressure = "";
};

bool should_manage_sample(const WorkloadSample &sample);
ExecutionAction apply_cgroup_assignment(const RuntimeConfig &config, const WorkloadDecision &decision);
ExecutionAction apply_cgroup_assignment(const RuntimeConfig &config, const WorkloadDecision &decision,
                                        const ResourceControlPolicy &policy, bool pressure_mode);
bool rollback_resource_control_state();
ExecutionAction apply_scx_assignment(const RuntimeConfig &config, const WorkloadDecision &decision,
                                     bool scheduler_active, const std::string &scheduler_reason);
bool reconcile_scx_session(const RuntimeConfig &config, ControlMode mode, ScxSession &session, std::string &reason);
bool update_scx_class_map(ScxSession &session, const std::vector<WorkloadDecision> &decisions, std::string &reason);
bool update_scx_gate_state(ScxSession &session, GateState state, std::uint32_t generation,
                           std::uint64_t updated_at_ns, std::uint32_t evidence_mask,
                           std::string &reason);
void stop_scx_session(ScxSession &session);
void close_scx_map(ScxSession &session);

} // namespace eulerpilot
