#pragma once

#include <string>
#include <vector>

#include "eulerpilot.hpp"
#include "executors.hpp"
#include "psi_gate.hpp"

namespace eulerpilot {

struct PsiGateRuntimeOps {
    virtual ~PsiGateRuntimeOps() = default;
    virtual GateDecision tick_gate(const TriggerContext &ctx) = 0;
};

struct ResourceControlRuntimeOps {
    virtual ~ResourceControlRuntimeOps() = default;
    virtual void apply_in_cycle(std::vector<WorkloadDecision> &decisions,
                                const GateDecision &gate,
                                const RuntimeConfig &cycle_config) = 0;
};

struct SkillRuntimeContext {
    ScxSession scx_session;
    bool scx_ready = false;
    std::string scx_reason = "backend-not-sched-ext";

    PsiGateSkill psi_gate;
    bool psi_gate_ready = false;

    PsiGateRuntimeOps         *psi_gate_ops = nullptr;
    ResourceControlRuntimeOps *resource_ops = nullptr;
};

SkillRuntimeContext &global_skill_runtime_context();

} // namespace eulerpilot
