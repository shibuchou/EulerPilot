#include "builtin_skills.hpp"

#include "action_journal.hpp"
#include "audit_bus.hpp"
#include "executors.hpp"
#include "psi_gate.hpp"
#include "skill_runtime_context.hpp"
#include "target_resolver.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iterator>

#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <map>
#include <net/if.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <utility>

namespace eulerpilot {

namespace {

namespace fs = std::filesystem;

bool file_exists(const char *path) {
    std::ifstream file(path);
    return file.good();
}

std::string now_event_timestamp() {
    return std::to_string(static_cast<long long>(time(nullptr)));
}

void ensure_parent_dir(const fs::path &path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
}

std::string sanitize_for_id(std::string value) {
    for (char &ch : value) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) {
            ch = '_';
        }
    }
    return value;
}

bool parse_tcp_port(const std::string &value, std::uint16_t &port) {
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' ||
        parsed == 0 || parsed > 65535) {
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

struct NetworkPolicyMapConfig {
    std::uint16_t deny_port = 18080;
    std::uint8_t enforce = 1;
    std::uint8_t reserved[5] = {};
};

struct NetworkPolicyMapStats {
    std::uint64_t allow_count = 0;
    std::uint64_t deny_count = 0;
};

static_assert(sizeof(NetworkPolicyMapConfig) == 8,
              "network policy config map layout must match BPF");
static_assert(sizeof(NetworkPolicyMapStats) == 16,
              "network policy stats map layout must match BPF");

struct NetworkQosTcConfig {
    std::uint16_t dst_port = 0;
    std::uint8_t protocol = 0;
    std::uint8_t enabled = 1;
    std::uint32_t reserved = 0;
};

struct NetworkQosTcStats {
    std::uint64_t packet_count = 0;
    std::uint64_t byte_count = 0;
};

static_assert(sizeof(NetworkQosTcConfig) == 8,
              "network qos config map layout must match BPF");
static_assert(sizeof(NetworkQosTcStats) == 16,
              "network qos stats map layout must match BPF");

bool valid_tc_token(const std::string &value) {
    if (value.empty()) {
        return false;
    }
    for (const char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.' ||
              ch == ':' || ch == '/' || ch == '%')) {
            return false;
        }
    }
    return true;
}

bool command_available(const char *command) {
    const std::string check = std::string("command -v ") + command + " >/dev/null 2>&1";
    return std::system(check.c_str()) == 0;
}

bool run_tc_command(const std::string &command) {
    return std::system((command + " >/dev/null 2>&1").c_str()) == 0;
}

std::uint8_t protocol_id(const std::string &protocol) {
    if (protocol == "tcp") {
        return 6;
    }
    if (protocol == "udp") {
        return 17;
    }
    if (protocol == "icmp") {
        return 1;
    }
    return 0;
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

class ResourceControlSkillAdapter final : public Skill, public ResourceControlRuntimeOps {
public:
    std::string name() const override { return "resource_control"; }

    bool configure(const RuntimeConfig &runtime_config, const SkillSpec &) override {
        backend_ = runtime_config.preferred_backend;
        runtime_config_ = runtime_config;
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
        global_skill_runtime_context().resource_ops = this;
        running_ = true;
        state_ = "started";
        return true;
    }

    void apply_in_cycle(std::vector<WorkloadDecision> &decisions,
                        const GateDecision &gate) override {
        auto &ctx = global_skill_runtime_context();
        bool scx_active = ctx.scx_ready;
        std::string scx_reason = ctx.scx_reason;

        if (backend_ == ExecutorBackend::SchedExt) {
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
                d.action = apply_scx_assignment(runtime_config_, d, scx_active, scx_reason);
            }
        } else {
            for (auto &d : decisions) {
                d.action = apply_cgroup_assignment(runtime_config_, d);
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
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        auto &ctx = global_skill_runtime_context();
        if (ctx.resource_ops == this) ctx.resource_ops = nullptr;
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
        if (ctx.resource_ops == this) ctx.resource_ops = nullptr;
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
        snapshot.evidence["requires_gate_state_map"] = backend_ == ExecutorBackend::SchedExt ? "yes" : "no";
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

class NetworkPolicySkill final : public Skill {
public:
    explicit NetworkPolicySkill(std::string skill_name = "network_policy_demo")
        : skill_name_(std::move(skill_name)) {}

    std::string name() const override { return skill_name_; }

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
        if (mode_ != "audit" && mode_ != "enforce") {
            last_error_ = "unsupported-mode";
            return false;
        }
        if (!parse_tcp_port(dst_port_, dst_port_value_)) {
            last_error_ = "invalid-dst-port";
            return false;
        }
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
        auto target = resolve_cgroup_target(skill_name_, cgroup_path_);
        if (!target.resolved) {
            last_error_ = "target-resolve-failed:" + target.reason;
            return false;
        }
        cgroup_id_ = target.cgroup_id;

        if (mode_ == "audit") {
            // In audit mode, the NetworkPolicySkill only records that the policy
            // matched configuration. It intentionally does not attach BPF, so it
            // cannot block traffic or affect SSH/host networking.
            running_ = true;
            state_ = "audit-only";
            write_audit_event("start", "audit-only", "success");
            write_journal_action("start-audit", "audit-only", "none");
            return true;
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
        if (!install_policy_config()) {
            rollback();
            return false;
        }
        bpf_program *prog = bpf_object__next_program(bpf_object_, nullptr);
        link_ = bpf_program__attach_cgroup(prog, cgroup_fd_);
        if (!link_) {
            rollback();
            last_error_ = "demo-bpf-attach-failed";
            return false;
        }
        std::error_code ec;
        fs::remove(link_pin_path(), ec);
        if (bpf_link__pin(link_, link_pin_path().c_str()) != 0) {
            rollback();
            last_error_ = "demo-bpf-pin-failed";
            return false;
        }
        running_ = true;
        state_ = "started";
        write_audit_event("start", "attach-cgroup-connect4", "success");
        write_journal_action("start-enforce", "attach-cgroup-connect4", link_pin_path());
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
        snapshot.evidence["cgroup_id"] = std::to_string(cgroup_id_);
        snapshot.evidence["link_pin_path"] = link_pin_path();
        const auto stats = read_stats_counts();
        snapshot.evidence["allow_count"] = std::to_string(stats.first);
        snapshot.evidence["deny_count"] = std::to_string(stats.second);
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        update_cached_stats();
        if (running_) {
            write_audit_event("rollback", "detach-cgroup-connect4", "success");
            write_journal_action("rollback", "detach-cgroup-connect4", link_pin_path());
        }
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
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        try {
            rollback();
        } catch (const std::exception &ex) {
            std::cerr << "[network_policy_demo] stop cleanup failed: "
                      << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[network_policy_demo] stop cleanup failed: unknown\n";
        }
        running_ = false;
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    std::string link_pin_path() const {
        return "/sys/fs/bpf/eulerpilot_" + sanitize_for_id(skill_name_) + "_link";
    }

    bool install_policy_config() {
        const int policy_fd = bpf_object__find_map_fd_by_name(bpf_object_, "policy_map");
        if (policy_fd < 0) {
            last_error_ = "policy-map-missing";
            return false;
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "stats_map");
        if (stats_fd < 0) {
            last_error_ = "stats-map-missing";
            return false;
        }

        std::uint32_t key = 0;
        NetworkPolicyMapConfig config;
        config.deny_port = dst_port_value_;
        config.enforce = mode_ == "enforce" ? 1 : 0;
        if (bpf_map_update_elem(policy_fd, &key, &config, BPF_ANY) != 0) {
            last_error_ = "policy-map-update-failed";
            return false;
        }

        NetworkPolicyMapStats stats;
        if (bpf_map_update_elem(stats_fd, &key, &stats, BPF_ANY) != 0) {
            last_error_ = "stats-map-reset-failed";
            return false;
        }
        allow_count_ = 0;
        deny_count_ = 0;
        return true;
    }

    std::pair<std::uint64_t, std::uint64_t> read_stats_counts() const {
        if (!bpf_object_) {
            return {allow_count_, deny_count_};
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "stats_map");
        if (stats_fd < 0) {
            return {allow_count_, deny_count_};
        }
        std::uint32_t key = 0;
        NetworkPolicyMapStats stats;
        if (bpf_map_lookup_elem(stats_fd, &key, &stats) != 0) {
            return {allow_count_, deny_count_};
        }
        return {stats.allow_count, stats.deny_count};
    }

    void update_cached_stats() {
        const auto stats = read_stats_counts();
        allow_count_ = stats.first;
        deny_count_ = stats.second;
    }

    void write_audit_event(const std::string &operation,
                           const std::string &action,
                           const std::string &result) const {
        const fs::path audit_path = "reports/events/network_policy.jsonl";
        ensure_parent_dir(audit_path);

        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = skill_name_ + "-" + operation + "-" + event.timestamp;
        event.skill = skill_name_;
        event.policy_id = "network_policy";
        event.rule_id = "connect4-deny-port-" + dst_port_;
        event.mode = mode_;
        event.target = {
            {"cgroup_id", std::to_string(cgroup_id_)},
            {"cgroup_path", cgroup_path_},
        };
        event.operation = operation;
        event.evidence = {
            {"hook", hook_},
            {"dst_port", dst_port_},
            {"allow_count", std::to_string(allow_count_)},
            {"deny_count", std::to_string(deny_count_)},
        };
        event.action = action;
        event.result = result;
        event.severity = "info";
        std::string error;
        append_audit_event(audit_path.string(), event, &error);
    }

    void write_journal_action(const std::string &operation,
                              const std::string &action,
                              const std::string &handle) const {
        const fs::path journal_path = "run/eulerpilot/action_journal.jsonl";
        ensure_parent_dir(journal_path);

        JournalAction entry;
        entry.action_id = skill_name_ + "-" + operation + "-" + now_event_timestamp();
        entry.skill = skill_name_;
        entry.target = cgroup_path_;
        entry.operation = operation;
        entry.new_values = {
            {"mode", mode_},
            {"hook", hook_},
            {"dst_port", dst_port_},
            {"action", action},
        };
        entry.handles = {
            {"cgroup_path", cgroup_path_},
            {"bpf_link", handle},
        };
        entry.restored = operation == "rollback";
        std::string error;
        append_journal_action(journal_path.string(), entry, &error);
    }

    std::string skill_name_;
    bool available_ = false;
    bool running_ = false;
    std::string state_ = "created";
    std::string last_error_;
    std::string hook_ = "cgroup_connect4";
    std::string cgroup_path_ = "/sys/fs/cgroup/eulerpilot/demo-net";
    std::string mode_ = "enforce";
    std::string dst_port_ = "18080";
    std::uint16_t dst_port_value_ = 18080;
    std::uint64_t cgroup_id_ = 0;
    std::uint64_t allow_count_ = 0;
    std::uint64_t deny_count_ = 0;
    int cgroup_fd_ = -1;
    bpf_object *bpf_object_ = nullptr;
    bpf_link *link_ = nullptr;
};

class NetworkQosSkill final : public Skill {
public:
    std::string name() const override { return "network_qos"; }

    bool configure(const RuntimeConfig &, const SkillSpec &spec) override {
        auto hook = spec.config.find("hook");
        auto mode = spec.config.find("mode");
        auto ifname = spec.config.find("ifname");
        auto protocol = spec.config.find("protocol");
        auto port = spec.config.find("dst_port");
        auto rate = spec.config.find("rate");
        auto burst = spec.config.find("burst");
        auto latency = spec.config.find("latency");
        if (hook == spec.config.end() || mode == spec.config.end() ||
            ifname == spec.config.end() || protocol == spec.config.end() ||
            port == spec.config.end() || rate == spec.config.end() ||
            burst == spec.config.end() || latency == spec.config.end()) {
            last_error_ = "network-qos-missing-required-config";
            return false;
        }

        hook_ = hook->second;
        mode_ = mode->second;
        ifname_ = ifname->second;
        protocol_ = protocol->second;
        dst_port_ = port->second;
        rate_ = rate->second;
        burst_ = burst->second;
        latency_ = latency->second;

        if (hook_ != "tc_egress") {
            last_error_ = "unsupported-hook";
            return false;
        }
        if (mode_ != "audit" && mode_ != "enforce") {
            last_error_ = "unsupported-mode";
            return false;
        }
        if (protocol_ != "any" && protocol_ != "tcp" &&
            protocol_ != "udp" && protocol_ != "icmp") {
            last_error_ = "unsupported-protocol";
            return false;
        }
        if (dst_port_ == "0") {
            dst_port_value_ = 0;
        } else if (!parse_tcp_port(dst_port_, dst_port_value_)) {
            last_error_ = "invalid-dst-port";
            return false;
        }
        if (!valid_tc_token(ifname_) || !valid_tc_token(rate_) ||
            !valid_tc_token(burst_) || !valid_tc_token(latency_)) {
            last_error_ = "invalid-tc-token";
            return false;
        }
        protocol_value_ = protocol_id(protocol_);
        return true;
    }

    bool probe() override {
        available_ = false;
        if (!command_available("tc") || !command_available("ip")) {
            last_error_ = "iproute2-missing";
            return false;
        }
        if (!file_exists("/root/EulerPilot/build/network_qos_tc.bpf.o")) {
            last_error_ = "network-qos-tc-not-built";
            return false;
        }
        const unsigned int ifindex = if_nametoindex(ifname_.c_str());
        if (ifindex == 0) {
            last_error_ = "tc-ifname-missing";
            return false;
        }
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

        if (mode_ == "audit") {
            running_ = true;
            state_ = "audit-only";
            write_audit_event("start", "audit-only", "success");
            write_journal_action("start-audit", "audit-only", "none");
            return true;
        }

        ifindex_ = static_cast<int>(if_nametoindex(ifname_.c_str()));
        if (ifindex_ <= 0) {
            last_error_ = "tc-ifindex-resolve-failed";
            return false;
        }

        bpf_object_ = bpf_object__open_file("/root/EulerPilot/build/network_qos_tc.bpf.o", nullptr);
        if (!bpf_object_) {
            last_error_ = "tc-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(bpf_object_) != 0) {
            rollback();
            last_error_ = "tc-bpf-load-failed";
            return false;
        }
        if (!install_tc_config()) {
            rollback();
            return false;
        }

        bpf_program *prog = bpf_object__find_program_by_name(bpf_object_, "network_qos_classifier");
        if (!prog) {
            rollback();
            last_error_ = "tc-bpf-program-missing";
            return false;
        }

        tc_hook_ = {};
        tc_hook_.sz = sizeof(tc_hook_);
        tc_hook_.ifindex = ifindex_;
        tc_hook_.attach_point = BPF_TC_EGRESS;
        int err = bpf_tc_hook_create(&tc_hook_);
        if (err != 0 && err != -EEXIST) {
            rollback();
            last_error_ = "tc-hook-create-failed";
            return false;
        }
        tc_hook_created_ = true;

        tc_opts_ = {};
        tc_opts_.sz = sizeof(tc_opts_);
        tc_opts_.prog_fd = bpf_program__fd(prog);
        tc_opts_.flags = BPF_TC_F_REPLACE;
        tc_opts_.handle = 1;
        tc_opts_.priority = 1;
        if (bpf_tc_attach(&tc_hook_, &tc_opts_) != 0) {
            rollback();
            last_error_ = "tc-bpf-attach-failed";
            return false;
        }
        tc_attached_ = true;

        if (!apply_tbf_qdisc()) {
            rollback();
            last_error_ = "tc-tbf-apply-failed";
            return false;
        }
        tbf_attached_ = true;
        running_ = true;
        state_ = "started";
        write_audit_event("start", "attach-tc-egress", "success");
        write_journal_action("start-enforce", "attach-tc-egress", ifname_);
        return true;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["hook"] = hook_;
        snapshot.evidence["mode"] = mode_;
        snapshot.evidence["ifname"] = ifname_;
        snapshot.evidence["ifindex"] = std::to_string(ifindex_);
        snapshot.evidence["protocol"] = protocol_;
        snapshot.evidence["dst_port"] = dst_port_;
        snapshot.evidence["rate"] = rate_;
        snapshot.evidence["burst"] = burst_;
        snapshot.evidence["latency"] = latency_;
        const auto stats = read_tc_stats();
        snapshot.evidence["packet_count"] = std::to_string(stats.first);
        snapshot.evidence["byte_count"] = std::to_string(stats.second);
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        update_cached_stats();
        if (running_) {
            write_audit_event("rollback", "detach-tc-egress", "success");
            write_journal_action("rollback", "detach-tc-egress", ifname_);
        }
        if (tbf_attached_) {
            run_tc_command("tc qdisc del dev " + ifname_ + " root");
            tbf_attached_ = false;
        }
        if (tc_attached_) {
            bpf_tc_detach(&tc_hook_, &tc_opts_);
            tc_attached_ = false;
        }
        if (tc_hook_created_) {
            bpf_tc_hook_destroy(&tc_hook_);
            run_tc_command("tc qdisc del dev " + ifname_ + " clsact");
            tc_hook_created_ = false;
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
        try {
            rollback();
        } catch (const std::exception &ex) {
            std::cerr << "[network_qos] stop cleanup failed: "
                      << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[network_qos] stop cleanup failed: unknown\n";
        }
        running_ = false;
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    bool install_tc_config() {
        const int config_fd = bpf_object__find_map_fd_by_name(bpf_object_, "qos_config_map");
        if (config_fd < 0) {
            last_error_ = "tc-config-map-missing";
            return false;
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "qos_stats_map");
        if (stats_fd < 0) {
            last_error_ = "tc-stats-map-missing";
            return false;
        }
        std::uint32_t key = 0;
        NetworkQosTcConfig config;
        config.dst_port = dst_port_value_;
        config.protocol = protocol_value_;
        config.enabled = 1;
        if (bpf_map_update_elem(config_fd, &key, &config, BPF_ANY) != 0) {
            last_error_ = "tc-config-map-update-failed";
            return false;
        }
        NetworkQosTcStats stats;
        if (bpf_map_update_elem(stats_fd, &key, &stats, BPF_ANY) != 0) {
            last_error_ = "tc-stats-map-reset-failed";
            return false;
        }
        packet_count_ = 0;
        byte_count_ = 0;
        return true;
    }

    bool apply_tbf_qdisc() const {
        return run_tc_command("tc qdisc replace dev " + ifname_ +
                              " root tbf rate " + rate_ +
                              " burst " + burst_ +
                              " latency " + latency_);
    }

    std::pair<std::uint64_t, std::uint64_t> read_tc_stats() const {
        if (!bpf_object_) {
            return {packet_count_, byte_count_};
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "qos_stats_map");
        if (stats_fd < 0) {
            return {packet_count_, byte_count_};
        }
        std::uint32_t key = 0;
        NetworkQosTcStats stats;
        if (bpf_map_lookup_elem(stats_fd, &key, &stats) != 0) {
            return {packet_count_, byte_count_};
        }
        return {stats.packet_count, stats.byte_count};
    }

    void update_cached_stats() {
        const auto stats = read_tc_stats();
        packet_count_ = stats.first;
        byte_count_ = stats.second;
    }

    void write_audit_event(const std::string &operation,
                           const std::string &action,
                           const std::string &result) const {
        const fs::path audit_path = "reports/events/network_policy.jsonl";
        ensure_parent_dir(audit_path);

        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = "network_qos-" + operation + "-" + event.timestamp;
        event.skill = "network_qos";
        event.policy_id = "network_policy";
        event.rule_id = "tc-egress-qos-" + ifname_;
        event.mode = mode_;
        event.target = {
            {"ifname", ifname_},
            {"ifindex", std::to_string(ifindex_)},
        };
        event.operation = operation;
        event.evidence = {
            {"hook", hook_},
            {"protocol", protocol_},
            {"dst_port", dst_port_},
            {"rate", rate_},
            {"packet_count", std::to_string(packet_count_)},
            {"byte_count", std::to_string(byte_count_)},
        };
        event.action = action;
        event.result = result;
        event.severity = "info";
        std::string error;
        append_audit_event(audit_path.string(), event, &error);
    }

    void write_journal_action(const std::string &operation,
                              const std::string &action,
                              const std::string &handle) const {
        const fs::path journal_path = "run/eulerpilot/action_journal.jsonl";
        ensure_parent_dir(journal_path);

        JournalAction entry;
        entry.action_id = "network_qos-" + operation + "-" + now_event_timestamp();
        entry.skill = "network_qos";
        entry.target = ifname_;
        entry.operation = operation;
        entry.new_values = {
            {"mode", mode_},
            {"hook", hook_},
            {"protocol", protocol_},
            {"dst_port", dst_port_},
            {"rate", rate_},
            {"burst", burst_},
            {"latency", latency_},
            {"action", action},
        };
        entry.handles = {
            {"ifname", ifname_},
            {"tc_hook", handle},
            {"qdisc", "tbf"},
        };
        entry.restored = operation == "rollback";
        std::string error;
        append_journal_action(journal_path.string(), entry, &error);
    }

    bool available_ = false;
    bool running_ = false;
    bool tc_hook_created_ = false;
    bool tc_attached_ = false;
    bool tbf_attached_ = false;
    std::string state_ = "created";
    std::string last_error_;
    std::string hook_ = "tc_egress";
    std::string mode_ = "audit";
    std::string ifname_ = "ep-veth-qos0";
    std::string protocol_ = "any";
    std::string dst_port_ = "0";
    std::string rate_ = "1mbit";
    std::string burst_ = "32kb";
    std::string latency_ = "50ms";
    std::uint16_t dst_port_value_ = 0;
    std::uint8_t protocol_value_ = 0;
    std::uint64_t packet_count_ = 0;
    std::uint64_t byte_count_ = 0;
    int ifindex_ = 0;
    bpf_object *bpf_object_ = nullptr;
    bpf_tc_hook tc_hook_ = {};
    bpf_tc_opts tc_opts_ = {};
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
        return std::make_unique<NetworkPolicySkill>("network_policy_demo");
    });
    registry.register_factory("network_policy", [] {
        return std::make_unique<NetworkPolicySkill>("network_policy");
    });
    registry.register_factory("network_qos", [] {
        return std::make_unique<NetworkQosSkill>();
    });
    registry.register_factory("security_policy_demo", [] {
        return std::make_unique<SecurityPolicyDemoSkill>();
    });
}

} // namespace eulerpilot
