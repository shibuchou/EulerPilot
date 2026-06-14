#pragma once

#include <cstdint>
#include <fstream>
#include <poll.h>
#include <string>

#include "eulerpilot.hpp"

namespace eulerpilot {

class PsiGateSkill {
public:
    bool init(ExecutorBackend backend, GateMode mode);
    GateDecision tick(const TriggerContext &ctx);
    GateState state() const { return state_; }
    std::string last_error() const { return last_error_; }
    bool write_gate_state(const GateDecision &decision);
    void shutdown();

private:
    bool open_trigger();
    bool poll_trigger();
    void emit_trace(const GateDecision &decision);
    std::string gate_state_map_path() const;

    ExecutorBackend backend_ = ExecutorBackend::CgroupV2;
    GateMode mode_ = GateMode::AlwaysActive;
    GateState state_ = GateState::Normal;
    std::uint32_t generation_ = 0;
    int trigger_fd_ = -1;
    pollfd pfd_ = {};
    std::uint32_t activation_streak_ = 0;
    std::uint32_t recovery_streak_ = 0;
    std::uint64_t cooldown_started_at_ns_ = 0;
    std::string trace_path_ = "/tmp/eulerpilot-psi-gate-trace.jsonl";
    std::string last_error_;
    std::ofstream trace_;
};

} // namespace eulerpilot
