#include "builtin_skills.hpp"

#include "executors.hpp"
#include "psi_gate.hpp"
#include "skill_runtime_context.hpp"

#include <bpf/libbpf.h>
#include <iterator>

#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

namespace eulerpilot {

namespace {

namespace fs = std::filesystem;

bool file_exists(const char *path) {
    std::ifstream file(path);
    return file.good();
}

std::string getenv_or(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return value;
}

bool ensure_cgroup(const fs::path &path) {
    if (!fs::exists("/sys/fs/cgroup") || access("/sys/fs/cgroup", W_OK) != 0) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }
    // Enable controllers on parent cgroup so child cgroups can accept processes
    auto parent = path.parent_path();
    if (parent != "/sys/fs/cgroup") {
        auto sc_path = parent / "cgroup.subtree_control";
        if (fs::exists(sc_path)) {
            std::ofstream sc(sc_path);
            if (sc.good()) {
                sc << "+memory +pids";
            }
        }
    }
    fs::create_directories(path, ec);
    return !ec;
}

void cleanup_cgroup(const fs::path &path) {
    if (!fs::exists(path)) {
        return;
    }
    std::ifstream procs(path / "cgroup.procs");
    std::string pid;
    while (std::getline(procs, pid)) {
        if (pid.empty()) {
            continue;
        }
        std::ofstream root_procs("/sys/fs/cgroup/cgroup.procs");
        root_procs << pid;
    }
    std::error_code ec;
    fs::remove(path, ec);
}

class ResourceControlSkillAdapter final : public Skill {
public:
    std::string name() const override { return "resource_control"; }

    bool configure(const RuntimeConfig &runtime_config, const SkillSpec &) override {
        backend_ = runtime_config.preferred_backend;
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
        ctx.scx_ready = reconcile_scx_session(runtime_config_, ControlMode::Normal, ctx.scx_session, reason);
        ctx.scx_reason = reason;
        running_ = true;
        state_ = "started";
        return true;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["backend"] = "resource_control_adapter";
        snapshot.evidence["scheduler_backend"] = backend_ == ExecutorBackend::SchedExt ? "sched_ext" : "cgroup_v2";
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        auto &ctx = global_skill_runtime_context();
        stop_scx_session(ctx.scx_session);
        close_scx_map(ctx.scx_session);
        ctx.scx_ready = false;
        ctx.scx_reason = "rolled-back";
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        auto &ctx = global_skill_runtime_context();
        stop_scx_session(ctx.scx_session);
        close_scx_map(ctx.scx_session);
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
};

class PsiGateSkillAdapter final : public Skill {
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
        const bool sched_ext = backend_ == ExecutorBackend::SchedExt;
        available_ = file_exists("/proc/pressure/cpu");
        if (sched_ext) {
            available_ = available_ && file_exists("/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map");
        }
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
        running_ = true;
        state_ = "started";
        return true;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["gate_mode"] = "adapter";
        snapshot.evidence["backend"] = backend_ == ExecutorBackend::SchedExt ? "sched_ext" : "cgroup_v2";
        snapshot.evidence["requires_gate_state_map"] = backend_ == ExecutorBackend::SchedExt ? "yes" : "no";
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        auto &ctx = global_skill_runtime_context();
        ctx.psi_gate.shutdown();
        ctx.psi_gate_ready = false;
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        auto &ctx = global_skill_runtime_context();
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

class NetworkPolicySkill final : public Skill {
public:
    std::string name() const override { return "network_policy_demo"; }

    bool configure(const RuntimeConfig &, const SkillSpec &spec) override {
        auto hook = spec.config.find("hook");
        auto cgroup_path = spec.config.find("cgroup_path");
        auto mode = spec.config.find("mode");
        auto port = spec.config.find("dst_port");
        if (hook == spec.config.end() || cgroup_path == spec.config.end() ||
            mode == spec.config.end() || port == spec.config.end()) {
            last_error_ = "network-policy-demo-missing-required-config";
            return false;
        }
        hook_ = hook->second;
        cgroup_path_ = cgroup_path->second;
        mode_ = mode->second;
        dst_port_ = port->second;
        return true;
    }

    bool probe() override {
        available_ = false;
        if (!file_exists("/sys/fs/cgroup/cgroup.controllers")) {
            last_error_ = "cgroup-v2-missing";
            return false;
        }
        if (access("/sys/fs/cgroup", W_OK) != 0) {
            last_error_ = "cgroup-root-not-writable";
            return false;
        }
        if (!file_exists("/root/EulerPilot/build/network_policy_demo.bpf.o")) {
            last_error_ = "network-policy-demo-not-built";
            return false;
        }

        const fs::path probe_path = fs::path("/sys/fs/cgroup/eulerpilot") / (".probe-net-" + std::to_string(getpid()));
        if (!ensure_cgroup(probe_path)) {
            last_error_ = "probe-cgroup-create-failed";
            return false;
        }

        int probe_fd = open(probe_path.c_str(), O_RDONLY | O_DIRECTORY);
        if (probe_fd < 0) {
            cleanup_cgroup(probe_path);
            last_error_ = "probe-cgroup-open-failed";
            return false;
        }

        bpf_object *obj = bpf_object__open_file("/root/EulerPilot/build/network_policy_demo.bpf.o", nullptr);
        if (!obj) {
            close(probe_fd);
            cleanup_cgroup(probe_path);
            last_error_ = "probe-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(obj) != 0) {
            bpf_object__close(obj);
            close(probe_fd);
            cleanup_cgroup(probe_path);
            last_error_ = "probe-bpf-load-failed";
            return false;
        }

        bpf_program *prog = bpf_object__next_program(obj, nullptr);
        bpf_link *link = bpf_program__attach_cgroup(prog, probe_fd);
        if (!link) {
            bpf_object__close(obj);
            close(probe_fd);
            cleanup_cgroup(probe_path);
            last_error_ = "probe-bpf-attach-failed";
            return false;
        }

        bpf_link__destroy(link);
        bpf_object__close(obj);
        close(probe_fd);
        cleanup_cgroup(probe_path);
        available_ = true;
        last_error_.clear();
        return true;
    }

    bool init() override {
        running_ = false;
        return true;
    }

    bool start() override {
        if (!available_ && !probe()) {
            return false;
        }
        if (hook_ != "cgroup_connect4") {
            last_error_ = "unsupported-hook";
            return false;
        }
        if (!ensure_cgroup(cgroup_path_)) {
            last_error_ = "demo-cgroup-create-failed";
            return false;
        }
        cgroup_fd_ = open(cgroup_path_.c_str(), O_RDONLY | O_DIRECTORY);
        if (cgroup_fd_ < 0) {
            cleanup_cgroup(cgroup_path_);
            last_error_ = "demo-cgroup-open-failed";
            return false;
        }
        bpf_object_ = bpf_object__open_file("/root/EulerPilot/build/network_policy_demo.bpf.o", nullptr);
        if (!bpf_object_) {
            rollback();
            last_error_ = "demo-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(bpf_object_) != 0) {
            rollback();
            last_error_ = "demo-bpf-load-failed";
            return false;
        }
        bpf_program *prog = bpf_object__next_program(bpf_object_, nullptr);
        link_ = bpf_program__attach_cgroup(prog, cgroup_fd_);
        if (!link_) {
            rollback();
            last_error_ = "demo-bpf-attach-failed";
            return false;
        }
        bpf_link__pin(link_, "/sys/fs/bpf/network_policy_demo_link");
        running_ = true;
        state_ = "started";
        return true;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["hook"] = hook_;
        snapshot.evidence["dst_port"] = dst_port_;
        snapshot.evidence["mode"] = mode_;
        snapshot.evidence["cgroup_path"] = cgroup_path_;
        snapshot.evidence["allow_count"] = std::to_string(allow_count_);
        snapshot.evidence["deny_count"] = std::to_string(deny_count_);
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        if (link_) {
            bpf_link__unpin(link_);
            bpf_link__destroy(link_);
            link_ = nullptr;
        }
        if (bpf_object_) {
            bpf_object__close(bpf_object_);
            bpf_object_ = nullptr;
        }
        if (cgroup_fd_ >= 0) {
            close(cgroup_fd_);
            cgroup_fd_ = -1;
        }
        if (!cgroup_path_.empty()) {
            cleanup_cgroup(cgroup_path_);
        }
        allow_count_ = 0;
        deny_count_ = 0;
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        running_ = false;
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    bool available_ = false;
    bool running_ = false;
    std::string state_ = "created";
    std::string last_error_;
    std::string hook_ = "cgroup_connect4";
    std::string cgroup_path_ = "/sys/fs/cgroup/eulerpilot/demo-net";
    std::string mode_ = "enforce";
    std::string dst_port_ = "18080";
    int allow_count_ = 0;
    int deny_count_ = 0;
    int cgroup_fd_ = -1;
    bpf_object *bpf_object_ = nullptr;
    bpf_link *link_ = nullptr;
};

class SecurityPolicyDemoSkill final : public Skill {
public:
    std::string name() const override { return "security_policy_demo"; }

    bool configure(const RuntimeConfig &, const SkillSpec &spec) override {
        auto hook = spec.config.find("hook");
        auto mode = spec.config.find("mode");
        auto target_path = spec.config.find("target_path");
        if (hook == spec.config.end() || mode == spec.config.end() || target_path == spec.config.end()) {
            last_error_ = "security-policy-demo-missing-required-config";
            return false;
        }
        hook_ = hook->second;
        mode_ = mode->second;
        target_path_ = target_path->second;
        // Must match BPF hardcoded path
        if (target_path_ != "/root/EulerPilot/demo/security_policy_demo/secret.txt") {
            last_error_ = "target-path-mismatch: expected /root/EulerPilot/demo/security_policy_demo/secret.txt";
            return false;
        }
        return true;
    }

    bool probe() override {
        available_ = false;
        // Check LSM BPF capability
        std::ifstream lsm("/sys/kernel/security/lsm");
        if (!lsm.good()) {
            last_error_ = "lsm-sysfs-missing";
            return false;
        }
        std::string lsm_content((std::istreambuf_iterator<char>(lsm)), std::istreambuf_iterator<char>());
        if (lsm_content.find("bpf") == std::string::npos) {
            last_error_ = "bpf-lsm-not-available";
            return false;
        }
        // Check BPF fs mounted
        if (!file_exists("/sys/fs/bpf")) {
            last_error_ = "bpf-fs-not-mounted";
            return false;
        }
        // Check root
        if (geteuid() != 0) {
            last_error_ = "not-root";
            return false;
        }
        // Check BPF object built
        if (!file_exists("/root/EulerPilot/build/security_policy_demo.bpf.o")) {
            last_error_ = "security-policy-demo-not-built";
            return false;
        }
        // Probe: load, attach, detach (no side effects)
        bpf_object *obj = bpf_object__open_file("/root/EulerPilot/build/security_policy_demo.bpf.o", nullptr);
        if (!obj) {
            last_error_ = "probe-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(obj) != 0) {
            bpf_object__close(obj);
            last_error_ = "probe-bpf-load-failed";
            return false;
        }
        bpf_program *prog = bpf_object__next_program(obj, nullptr);
        bpf_link *link = bpf_program__attach_lsm(prog);
        if (!link) {
            bpf_object__close(obj);
            last_error_ = "probe-bpf-attach-failed";
            return false;
        }
        bpf_link__destroy(link);
        bpf_object__close(obj);
        available_ = true;
        last_error_.clear();
        return true;
    }

    bool init() override {
        running_ = false;
        return true;
    }

    bool start() override {
        if (!available_ && !probe()) {
            return false;
        }
        if (hook_ != "lsm_file_open") {
            last_error_ = "unsupported-hook";
            return false;
        }
        bpf_object_ = bpf_object__open_file("/root/EulerPilot/build/security_policy_demo.bpf.o", nullptr);
        if (!bpf_object_) {
            rollback();
            last_error_ = "demo-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(bpf_object_) != 0) {
            rollback();
            last_error_ = "demo-bpf-load-failed";
            return false;
        }
        bpf_program *prog = bpf_object__next_program(bpf_object_, nullptr);
        link_ = bpf_program__attach_lsm(prog);
        if (!link_) {
            rollback();
            last_error_ = "demo-bpf-attach-failed";
            return false;
        }
        // Do NOT pin link — LSM should not persist after agent exit
        running_ = true;
        state_ = "started";
        return true;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["hook"] = hook_;
        snapshot.evidence["target_path"] = target_path_;
        snapshot.evidence["mode"] = mode_;
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        if (link_) {
            bpf_link__destroy(link_);
            link_ = nullptr;
        }
        if (bpf_object_) {
            bpf_object__close(bpf_object_);
            bpf_object_ = nullptr;
        }
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        rollback();
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    bool available_ = false;
    bool running_ = false;
    std::string state_ = "created";
    std::string last_error_;
    std::string hook_ = "lsm_file_open";
    std::string target_path_ = "/root/EulerPilot/demo/security_policy_demo/secret.txt";
    std::string mode_ = "enforce";
    bpf_object *bpf_object_ = nullptr;
    bpf_link *link_ = nullptr;
};

} // namespace

void register_builtin_skills(SkillRegistry &registry) {
    registry.register_factory("resource_control", [] {
        return std::make_unique<ResourceControlSkillAdapter>();
    });
    registry.register_factory("psi_gate", [] {
        return std::make_unique<PsiGateSkillAdapter>();
    });
    registry.register_factory("network_policy_demo", [] {
        return std::make_unique<NetworkPolicySkill>();
    });
    registry.register_factory("security_policy_demo", [] {
        return std::make_unique<SecurityPolicyDemoSkill>();
    });
}

} // namespace eulerpilot
