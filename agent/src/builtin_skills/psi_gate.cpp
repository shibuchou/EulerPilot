#include "common.hpp"
#include "factories.hpp"

namespace eulerpilot {
namespace {
class PsiGateSkillAdapter final : public Skill, public PsiGateRuntimeOps {
public:
    std::string name() const override { return "psi_gate"; }

    std::vector<std::string> dependencies() const override {
        return {"resource_control"};
    }

    bool configure(const RuntimeConfig &runtime_config, const SkillSpec &) override {
        backend_ = runtime_config.preferred_backend;
        runtime_config_ = runtime_config;
        return true;
    }

    bool probe() override {
        available_ = file_exists("/proc/pressure/cpu");
        last_error_ = available_ ? "" : "psi-gate-prerequisites-missing";
        return available_;
    }

    bool init() override {
        running_ = false;
        return true;
    }

    bool start() override {
        if (!available_ && !probe()) {
            return false;
        }
        auto &ctx = global_skill_runtime_context();
        ctx.psi_gate_ready = ctx.psi_gate.init(runtime_config_.preferred_backend, runtime_config_.gate_mode);
        if (!ctx.psi_gate_ready && runtime_config_.gate_mode == GateMode::Psi) {
            last_error_ = ctx.psi_gate.last_error();
            return false;
        }
        global_skill_runtime_context().psi_gate_ops = this;
        running_ = true;
        state_ = "started";
        return true;
    }

    GateDecision tick_gate(const TriggerContext &ctx) override {
        auto &rc = global_skill_runtime_context();
        if (rc.psi_gate_ready) {
            return rc.psi_gate.tick(ctx);
        }
        GateDecision gd{};
        gd.previous_state = GateState::Normal;
        gd.next_state = GateState::Normal;
        gd.profile = "sched_ext_normal";
        return gd;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["gate_mode"] = "adapter";
        snapshot.evidence["backend"] = backend_ == ExecutorBackend::SchedExt ? "sched_ext" : "cgroup_v2";
        snapshot.evidence["requires_gate_state_map"] = "executor-managed";
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        auto &ctx = global_skill_runtime_context();
        if (ctx.psi_gate_ops == this) ctx.psi_gate_ops = nullptr;
        ctx.psi_gate.shutdown();
        ctx.psi_gate_ready = false;
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        auto &ctx = global_skill_runtime_context();
        if (ctx.psi_gate_ops == this) ctx.psi_gate_ops = nullptr;
        ctx.psi_gate.shutdown();
        ctx.psi_gate_ready = false;
        running_ = false;
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    bool available_ = false;
    bool running_ = false;
    std::string state_ = "created";
    std::string last_error_;
    ExecutorBackend backend_ = ExecutorBackend::CgroupV2;
    RuntimeConfig runtime_config_;
};


} // namespace

void register_psi_gate_skill(SkillRegistry &registry) {
    registry.register_factory("psi_gate", [] {
        return std::make_unique<PsiGateSkillAdapter>();
    });
}

} // namespace eulerpilot
