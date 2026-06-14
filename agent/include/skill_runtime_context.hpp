#pragma once

#include <string>

#include "executors.hpp"
#include "psi_gate.hpp"

namespace eulerpilot {

struct SkillRuntimeContext {
    ScxSession scx_session;
    bool scx_ready = false;
    std::string scx_reason = "backend-not-sched-ext";

    PsiGateSkill psi_gate;
    bool psi_gate_ready = false;
};

SkillRuntimeContext &global_skill_runtime_context();

} // namespace eulerpilot
