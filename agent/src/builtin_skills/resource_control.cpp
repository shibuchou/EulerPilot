#include "common.hpp"
#include "factories.hpp"

namespace eulerpilot {
namespace {
class ResourceControlSkillAdapter final : public Skill, public ResourceControlRuntimeOps {
public:
    std::string name() const override { return "resource_control"; }

    bool configure(const RuntimeConfig &runtime_config, const SkillSpec &spec) override {
        backend_ = runtime_config.preferred_backend;
        runtime_config_ = runtime_config;
        policy_ = parse_resource_control_policy(runtime_config, spec);
        return true;
    }

    bool probe() override {
        available_ = file_exists("/proc/pressure/cpu") && file_exists("/sys/fs/cgroup/cgroup.controllers");
        last_error_ = available_ ? "" : "resource-control-prerequisites-missing";
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
        std::string reason = "backend-not-sched-ext";
        RuntimeConfig start_config = runtime_config_;
        start_config.dry_run = true;
        ctx.scx_ready = reconcile_scx_session(start_config, ControlMode::Normal, ctx.scx_session, reason);
        ctx.scx_reason = reason;
        global_skill_runtime_context().resource_ops = this;
        running_ = true;
        state_ = "started";
        return true;
    }

    void apply_in_cycle(std::vector<WorkloadDecision> &decisions,
                        const GateDecision &gate,
                        const RuntimeConfig &cycle_config) override {
        auto &ctx = global_skill_runtime_context();
        bool scx_active = ctx.scx_ready;
        std::string scx_reason = ctx.scx_reason;

        if (backend_ == ExecutorBackend::SchedExt) {
            const ControlMode scx_mode = (gate.next_state == GateState::Active ||
                                          gate.next_state == GateState::Cooldown)
                                             ? ControlMode::Latency
                                             : ControlMode::Normal;
            scx_active = reconcile_scx_session(cycle_config, scx_mode, ctx.scx_session, scx_reason);
            ctx.scx_ready = scx_active;
            ctx.scx_reason = scx_reason;
            if (scx_active) {
                if (update_scx_gate_state(ctx.scx_session, gate.next_state, gate.generation,
                                          gate.updated_at_ns, gate.evidence_mask, scx_reason)) {
                    ctx.scx_reason = scx_reason;
                }
                std::string cm_reason = scx_reason;
                if (update_scx_class_map(ctx.scx_session, decisions, cm_reason)) {
                    ctx.scx_reason = cm_reason;
                }
            }
            for (auto &d : decisions) {
                d.action = apply_scx_assignment(cycle_config, d, scx_active, scx_reason);
            }
        } else {
            const bool gate_pressure = gate.next_state == GateState::Active ||
                                       gate.next_state == GateState::Cooldown;
            for (auto &d : decisions) {
                const bool pressure_mode = gate_pressure || d.target_profile != "normal_profile";
                d.action = apply_cgroup_assignment(cycle_config, d, policy_, pressure_mode);
            }
        }
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["backend"] = "resource_control_adapter";
        snapshot.evidence["scheduler_backend"] = backend_ == ExecutorBackend::SchedExt ? "sched_ext" : "cgroup_v2";
        snapshot.evidence["mode"] = policy_.mode;
        snapshot.evidence["memory"] = policy_.memory_enabled ? "enabled" : "disabled";
        snapshot.evidence["cpu_max"] = policy_.cpu_max_enabled ? "enabled" : "disabled";
        snapshot.evidence["io"] = policy_.io_enabled ? "enabled" : "disabled";
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        auto &ctx = global_skill_runtime_context();
        if (ctx.resource_ops == this) ctx.resource_ops = nullptr;
        stop_scx_session(ctx.scx_session);
        close_scx_map(ctx.scx_session);
        rollback_resource_control_state();
        ctx.scx_ready = false;
        ctx.scx_reason = "rolled-back";
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        auto &ctx = global_skill_runtime_context();
        if (ctx.resource_ops == this) ctx.resource_ops = nullptr;
        stop_scx_session(ctx.scx_session);
        close_scx_map(ctx.scx_session);
        rollback_resource_control_state();
        ctx.scx_ready = false;
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
    ResourceControlPolicy policy_;
};


} // namespace

void register_resource_control_skill(SkillRegistry &registry) {
    registry.register_factory("resource_control", [] {
        return std::make_unique<ResourceControlSkillAdapter>();
    });
}

} // namespace eulerpilot
