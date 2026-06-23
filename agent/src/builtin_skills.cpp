#include "builtin_skills.hpp"

#include "action_journal.hpp"
#include "audit_bus.hpp"
#include "executors.hpp"
#include "psi_gate.hpp"
#include "skill_runtime_context.hpp"
#include "target_resolver.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <deque>
#include <iterator>

#include <arpa/inet.h>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <linux/if_link.h>
#include <map>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

bool parse_ipv4_address(const std::string &value, std::uint32_t &addr) {
    in_addr parsed{};
    if (inet_pton(AF_INET, value.c_str(), &parsed) != 1) {
        return false;
    }
    addr = parsed.s_addr;
    return true;
}

bool parse_pid_value(const std::string &value, int &pid) {
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' ||
        parsed <= 0 || parsed > 4194304) {
        return false;
    }
    pid = static_cast<int>(parsed);
    return true;
}

bool parse_uint32_range(const std::string &value,
                        std::uint32_t min_value,
                        std::uint32_t max_value,
                        std::uint32_t &out) {
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' ||
        parsed < min_value || parsed > max_value) {
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_security_file_access(const std::string &value, std::uint32_t &access) {
    if (value.empty() || value == "any" || value == "all") {
        access = 0;
        return true;
    }
    if (value == "read" || value == "read_only" || value == "readonly") {
        access = 1;
        return true;
    }
    if (value == "write" || value == "write_only" || value == "writeonly") {
        access = 2;
        return true;
    }
    return false;
}

std::string normalize_security_token(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ch == '-') {
            ch = '_';
        }
    }
    if (value.rfind("cap_", 0) == 0) {
        value = value.substr(4);
    }
    return value;
}

bool parse_security_capability(const std::string &value, std::int32_t &capability) {
    if (value.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno == 0 && end != value.c_str() && *end == '\0' &&
        parsed >= 0 && parsed <= 63) {
        capability = static_cast<std::int32_t>(parsed);
        return true;
    }

    const std::string token = normalize_security_token(value);
    static const std::map<std::string, std::int32_t> caps = {
        {"chown", 0},
        {"dac_override", 1},
        {"dac_read_search", 2},
        {"fowner", 3},
        {"fsetid", 4},
        {"kill", 5},
        {"setgid", 6},
        {"setuid", 7},
        {"setpcap", 8},
        {"linux_immutable", 9},
        {"net_bind_service", 10},
        {"net_broadcast", 11},
        {"net_admin", 12},
        {"net_raw", 13},
        {"ipc_lock", 14},
        {"ipc_owner", 15},
        {"sys_module", 16},
        {"sys_rawio", 17},
        {"sys_chroot", 18},
        {"sys_ptrace", 19},
        {"sys_pacct", 20},
        {"sys_admin", 21},
        {"sys_boot", 22},
        {"sys_nice", 23},
        {"sys_resource", 24},
        {"sys_time", 25},
        {"sys_tty_config", 26},
        {"mknod", 27},
        {"lease", 28},
        {"audit_write", 29},
        {"audit_control", 30},
        {"setfcap", 31},
        {"mac_override", 32},
        {"mac_admin", 33},
        {"syslog", 34},
        {"wake_alarm", 35},
        {"block_suspend", 36},
        {"audit_read", 37},
        {"perfmon", 38},
        {"bpf", 39},
        {"checkpoint_restore", 40},
    };
    const auto it = caps.find(token);
    if (it == caps.end()) {
        return false;
    }
    capability = it->second;
    return true;
}

std::string security_capability_name(std::int32_t capability) {
    switch (capability) {
    case 12: return "CAP_NET_ADMIN";
    case 19: return "CAP_SYS_PTRACE";
    case 21: return "CAP_SYS_ADMIN";
    case 38: return "CAP_PERFMON";
    case 39: return "CAP_BPF";
    default: return capability >= 0 ? std::to_string(capability) : "unset";
    }
}

std::string security_file_access_name(std::uint32_t access) {
    switch (access) {
    case 1: return "read";
    case 2: return "write";
    default: return "any";
    }
}

const std::string *find_config_value(const SkillSpec &spec, const std::string &key) {
    auto it = spec.config.find(key);
    return it == spec.config.end() ? nullptr : &it->second;
}

std::string config_value_or(const SkillSpec &spec,
                            const std::string &key,
                            const std::string &fallback) {
    const auto *value = find_config_value(spec, key);
    return value ? *value : fallback;
}

int find_rule_by_hook(const SkillSpec &spec, const std::string &hook) {
    for (int i = 0; i < 128; ++i) {
        const std::string prefix = "rules." + std::to_string(i) + ".";
        const auto *rule_hook = find_config_value(spec, prefix + "hook");
        if (!rule_hook) {
            continue;
        }
        if (*rule_hook == hook) {
            return i;
        }
    }
    return -1;
}

int find_first_rule(const SkillSpec &spec) {
    for (int i = 0; i < 128; ++i) {
        const std::string prefix = "rules." + std::to_string(i) + ".";
        if (find_config_value(spec, prefix + "hook")) {
            return i;
        }
    }
    return -1;
}

bool config_bool_or(const SkillSpec &spec,
                    const std::string &key,
                    bool fallback) {
    const auto *value = find_config_value(spec, key);
    if (!value) {
        return fallback;
    }
    return *value == "1" || *value == "true" || *value == "yes" ||
           *value == "on";
}

bool resolve_network_target_ifname(const SkillSpec &spec,
                                   const std::string &target_prefix,
                                   const std::string &target_ref,
                                   const std::string &error_prefix,
                                   std::string &ifname,
                                   std::string &last_error) {
    const std::string target_type =
        config_value_or(spec, target_prefix + "type", "");
    if (target_type == "netdev") {
        ifname = config_value_or(spec, target_prefix + "ifname", "");
        if (ifname.empty()) {
            last_error = error_prefix + "-v2-ifname-missing";
            return false;
        }
        return true;
    }

    if (target_type == "k8s_pod" || target_type == "pod") {
        K8sPodTargetSpec target_spec;
        target_spec.name = target_ref;
        target_spec.pod_namespace =
            config_value_or(spec, target_prefix + "namespace",
                            config_value_or(spec, target_prefix + "pod_namespace", ""));
        target_spec.pod_name =
            config_value_or(spec, target_prefix + "pod_name",
                            config_value_or(spec, target_prefix + "name", ""));
        target_spec.pod_uid = config_value_or(spec, target_prefix + "pod_uid", "");
        target_spec.container_id =
            config_value_or(spec, target_prefix + "container_id", "");
        target_spec.container_name =
            config_value_or(spec, target_prefix + "container_name", "");
        target_spec.cgroup_root =
            config_value_or(spec, target_prefix + "cgroup_root", "/sys/fs/cgroup");

        TargetResolverOptions options;
        options.allow_non_lab_pods =
            config_bool_or(spec, target_prefix + "allow_non_lab_pods", false);
        options.allow_host_network_pods =
            config_bool_or(spec, target_prefix + "allow_host_network_pods", false);
        options.require_runtime_socket =
            config_bool_or(spec, target_prefix + "require_runtime_socket", true);
        options.lab_namespace =
            config_value_or(spec, target_prefix + "lab_namespace", "eulerpilot-lab");
        options.kubectl_path =
            config_value_or(spec, target_prefix + "kubectl_path", "kubectl");
        options.crictl_path =
            config_value_or(spec, target_prefix + "crictl_path", "crictl");
        options.docker_path =
            config_value_or(spec, target_prefix + "docker_path", "docker");
        options.podman_path =
            config_value_or(spec, target_prefix + "podman_path", "podman");
        options.ip_path = config_value_or(spec, target_prefix + "ip_path", "ip");
        options.nsenter_path =
            config_value_or(spec, target_prefix + "nsenter_path", "nsenter");

        const auto target = resolve_k8s_pod_target(target_spec, options);
        if (!target.resolved || target.ifname.empty()) {
            last_error = error_prefix + "-pod-veth-" + target.reason;
            return false;
        }
        ifname = target.ifname;
        return true;
    }

    if (target_type == "container_id" || target_type == "container") {
        ContainerTargetSpec target_spec;
        target_spec.name = target_ref;
        target_spec.container_id =
            config_value_or(spec, target_prefix + "container_id", "");
        target_spec.container_name =
            config_value_or(spec, target_prefix + "container_name",
                            config_value_or(spec, target_prefix + "name", ""));
        target_spec.runtime =
            config_value_or(spec, target_prefix + "runtime", "auto");
        target_spec.cgroup_root =
            config_value_or(spec, target_prefix + "cgroup_root", "/sys/fs/cgroup");
        target_spec.crictl_path =
            config_value_or(spec, target_prefix + "crictl_path", "crictl");
        target_spec.docker_path =
            config_value_or(spec, target_prefix + "docker_path", "docker");
        target_spec.podman_path =
            config_value_or(spec, target_prefix + "podman_path", "podman");

        TargetResolverOptions options;
        options.require_runtime_socket =
            config_bool_or(spec, target_prefix + "require_runtime_socket", false);
        options.allow_host_network_pods =
            config_bool_or(spec, target_prefix + "allow_host_network",
                           config_bool_or(spec,
                                          target_prefix + "allow_host_network_containers",
                                          false));
        options.crictl_path = target_spec.crictl_path;
        options.docker_path = target_spec.docker_path;
        options.podman_path = target_spec.podman_path;
        options.ip_path = config_value_or(spec, target_prefix + "ip_path", "ip");
        options.nsenter_path =
            config_value_or(spec, target_prefix + "nsenter_path", "nsenter");

        const auto target = resolve_container_netdev_target(target_spec, options);
        if (!target.resolved || target.ifname.empty()) {
            last_error = error_prefix + "-container-veth-" + target.reason;
            return false;
        }
        ifname = target.ifname;
        return true;
    }

    last_error = error_prefix + "-v2-target-not-netdev-container-or-pod";
    return false;
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

struct NetworkXdpConfig {
    std::uint16_t dst_port = 0;
    std::uint8_t protocol = 0;
    std::uint8_t action = 1;
    std::uint8_t enabled = 0;
    std::uint8_t reserved[3] = {};
};

struct NetworkXdpStats {
    std::uint64_t pass_count = 0;
    std::uint64_t drop_count = 0;
    std::uint64_t byte_count = 0;
};

struct NetworkXdpCounters {
    std::uint64_t pass_count = 0;
    std::uint64_t drop_count = 0;
    std::uint64_t byte_count = 0;
};

struct SecurityPolicyConfig {
    std::uint32_t enforce = 0;
    std::uint32_t target_count = 0;
};

struct SecurityPolicyTarget {
    char file_path[256] = {};
    char file_prefix[256] = {};
    char exec_path[256] = {};
    char exec_prefix[256] = {};
    std::uint64_t cgroup_id = 0;
    std::uint32_t connect_daddr = 0;
    std::uint16_t connect_dport = 0;
    std::uint16_t connect_protocol = 0;
    std::uint32_t file_access = 0;
    std::int32_t capability = -1;
};

struct SecurityPolicyRule {
    std::string rule_id;
    std::string hook;
    std::string target_ref;
    std::string file_path;
    std::string file_prefix;
    std::string exec_path;
    std::string exec_prefix;
    std::string file_access = "any";
    std::string cgroup_path;
    std::string connect_ip;
    std::string connect_port;
    std::uint64_t cgroup_id = 0;
    std::uint32_t file_access_value = 0;
    std::uint32_t connect_daddr = 0;
    std::uint16_t connect_dport = 0;
    std::uint16_t connect_protocol = 0;
    std::int32_t capability = -1;
};

struct SecurityAnomalyRule {
    std::string rule_id;
    std::string type = "rate";
    std::string syscall = "execve";
    std::string severity = "medium";
    std::uint32_t threshold = 5;
    std::uint32_t window_ms = 1000;
    std::deque<std::chrono::steady_clock::time_point> hits;
};

struct SecurityPolicyEvent {
    std::uint32_t event_type = 0;
    std::uint32_t pid = 0;
    std::uint32_t tgid = 0;
    std::uint32_t enforce = 0;
    std::int32_t decision = 0;
    std::uint32_t target_index = 0;
    char comm[16] = {};
    char path[256] = {};
    std::uint32_t daddr = 0;
    std::uint16_t dport = 0;
    std::uint16_t protocol = 0;
    std::uint32_t file_flags = 0;
    std::uint32_t file_access = 0;
    std::int32_t capability = -1;
    std::uint32_t uid = 0;
    std::uint32_t euid = 0;
    std::uint32_t suid = 0;
    std::uint32_t setuid_flags = 0;
    std::uint32_t gid = 0;
    std::uint32_t egid = 0;
    std::uint32_t sgid = 0;
    std::uint32_t setgid_flags = 0;
};

struct NetworkXdpRule {
    std::string rule_id;
    std::string protocol;
    std::string dst_port;
    std::string action;
    std::string target_ref;
    std::uint16_t dst_port_value = 0;
    std::uint8_t protocol_value = 0;
    std::uint8_t action_value = 1;
};

constexpr std::size_t kNetworkXdpMaxRules = 8;
constexpr std::size_t kSecurityPolicyMaxTargets = 8;
constexpr std::uint32_t kSecurityTargetUnknown = 0xffffffffu;

static_assert(sizeof(NetworkXdpConfig) == 8,
              "network xdp config map layout must match BPF");
static_assert(sizeof(NetworkXdpStats) == 24,
              "network xdp stats map layout must match BPF");
static_assert(sizeof(SecurityPolicyConfig) == 8,
              "security policy config map layout must match BPF");
static_assert(sizeof(SecurityPolicyTarget) == 1048,
              "security policy target map layout must match BPF");
static_assert(sizeof(SecurityPolicyEvent) == 348,
              "security policy ringbuf event layout must match BPF");

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
        const int rule_index = find_rule_by_hook(spec, "cgroup_connect4");
        if (rule_index >= 0) {
            const std::string rule_prefix = "rules." + std::to_string(rule_index) + ".";
            hook_ = config_value_or(spec, rule_prefix + "hook", "cgroup_connect4");
            mode_ = config_value_or(spec, rule_prefix + "mode",
                                    config_value_or(spec, "mode", "audit"));
            dst_port_ = config_value_or(spec, rule_prefix + "dst_port", "18080");
            protocol_ = config_value_or(spec, rule_prefix + "protocol", "tcp");
            action_ = config_value_or(spec, rule_prefix + "action", "deny");
            rule_id_ = config_value_or(spec, rule_prefix + "name",
                                       "connect4-deny-port-" + dst_port_);
            target_ref_ = config_value_or(spec, rule_prefix + "target_ref", "");
            if (target_ref_.empty()) {
                last_error_ = "network-policy-v2-missing-target-ref";
                return false;
            }
            const std::string target_prefix = "targets." + target_ref_ + ".";
            const std::string target_type = config_value_or(spec, target_prefix + "type", "");
            if (target_type != "cgroup") {
                last_error_ = "network-policy-v2-target-not-cgroup";
                return false;
            }
            cgroup_path_ = config_value_or(spec, target_prefix + "path", "");
            if (cgroup_path_.empty()) {
                last_error_ = "network-policy-v2-target-path-missing";
                return false;
            }
        } else {
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
            protocol_ = config_value_or(spec, "protocol", "tcp");
            action_ = config_value_or(spec, "action", "deny");
            target_ref_ = config_value_or(spec, "target_ref", "legacy_cgroup");
            rule_id_ = "connect4-deny-port-" + dst_port_;
        }

        if (mode_ != "audit" && mode_ != "enforce") {
            last_error_ = "unsupported-mode";
            return false;
        }
        if (protocol_ != "tcp") {
            last_error_ = "unsupported-protocol";
            return false;
        }
        if (action_ != "deny") {
            last_error_ = "unsupported-action";
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
        snapshot.evidence["target_ref"] = target_ref_;
        snapshot.evidence["rule_id"] = rule_id_;
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
        event.rule_id = rule_id_;
        event.mode = mode_;
        event.target = {
            {"cgroup_id", std::to_string(cgroup_id_)},
            {"cgroup_path", cgroup_path_},
            {"target_ref", target_ref_},
        };
        event.operation = operation;
        event.evidence = {
            {"hook", hook_},
            {"protocol", protocol_},
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
            {"target_ref", target_ref_},
            {"rule_id", rule_id_},
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
    std::string protocol_ = "tcp";
    std::string action_ = "deny";
    std::string target_ref_ = "legacy_cgroup";
    std::string rule_id_ = "connect4-deny-port-18080";
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
        const int rule_index = find_rule_by_hook(spec, "tc_egress");
        if (rule_index >= 0) {
            const std::string rule_prefix = "rules." + std::to_string(rule_index) + ".";
            hook_ = config_value_or(spec, rule_prefix + "hook", "tc_egress");
            mode_ = config_value_or(spec, rule_prefix + "mode",
                                    config_value_or(spec, "mode", "audit"));
            protocol_ = config_value_or(spec, rule_prefix + "protocol", "any");
            dst_port_ = config_value_or(spec, rule_prefix + "dst_port", "0");
            rate_ = config_value_or(spec, rule_prefix + "rate", "1mbit");
            burst_ = config_value_or(spec, rule_prefix + "burst", "32kb");
            latency_ = config_value_or(spec, rule_prefix + "latency", "50ms");
            action_ = config_value_or(spec, rule_prefix + "action", "limit");
            rule_id_ = config_value_or(spec, rule_prefix + "name", "tc-egress-qos");
            target_ref_ = config_value_or(spec, rule_prefix + "target_ref", "");
            if (target_ref_.empty()) {
                last_error_ = "network-qos-v2-missing-target-ref";
                return false;
            }
            const std::string target_prefix = "targets." + target_ref_ + ".";
            if (!resolve_network_target_ifname(spec, target_prefix, target_ref_,
                                               "network-qos", ifname_, last_error_)) {
                return false;
            }
        } else {
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
            action_ = config_value_or(spec, "action", "limit");
            target_ref_ = config_value_or(spec, "target_ref", "legacy_netdev");
            rule_id_ = "tc-egress-qos-" + ifname_;
        }

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
        if (action_ != "limit") {
            last_error_ = "unsupported-action";
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
        snapshot.evidence["target_ref"] = target_ref_;
        snapshot.evidence["rule_id"] = rule_id_;
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
        event.rule_id = rule_id_;
        event.mode = mode_;
        event.target = {
            {"ifname", ifname_},
            {"ifindex", std::to_string(ifindex_)},
            {"target_ref", target_ref_},
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
            {"target_ref", target_ref_},
            {"rule_id", rule_id_},
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
    std::string target_ref_ = "legacy_netdev";
    std::string rule_id_ = "tc-egress-qos-ep-veth-qos0";
    std::string protocol_ = "any";
    std::string dst_port_ = "0";
    std::string rate_ = "1mbit";
    std::string burst_ = "32kb";
    std::string latency_ = "50ms";
    std::string action_ = "limit";
    std::uint16_t dst_port_value_ = 0;
    std::uint8_t protocol_value_ = 0;
    std::uint64_t packet_count_ = 0;
    std::uint64_t byte_count_ = 0;
    int ifindex_ = 0;
    bpf_object *bpf_object_ = nullptr;
    bpf_tc_hook tc_hook_ = {};
    bpf_tc_opts tc_opts_ = {};
};

class NetworkXdpSkill final : public Skill {
public:
    std::string name() const override { return "network_xdp"; }

    bool configure(const RuntimeConfig &, const SkillSpec &spec) override {
        rules_.clear();
        mode_ = config_value_or(spec, "mode", "audit");
        hook_ = "xdp";
        ifname_.clear();
        target_ref_.clear();

        bool saw_v2_rule = false;
        for (int i = 0; i < 128; ++i) {
            const std::string rule_prefix = "rules." + std::to_string(i) + ".";
            const auto *rule_hook = find_config_value(spec, rule_prefix + "hook");
            if (!rule_hook || *rule_hook != "xdp") {
                continue;
            }
            saw_v2_rule = true;
            if (rules_.size() >= kNetworkXdpMaxRules) {
                last_error_ = "network-xdp-too-many-rules";
                return false;
            }

            NetworkXdpRule rule;
            rule.rule_id = config_value_or(spec, rule_prefix + "name",
                                           "xdp-rule-" + std::to_string(rules_.size()));
            rule.protocol = config_value_or(spec, rule_prefix + "protocol", "icmp");
            rule.dst_port = config_value_or(spec, rule_prefix + "dst_port", "0");
            rule.action = config_value_or(spec, rule_prefix + "action", "drop");
            rule.target_ref = config_value_or(spec, rule_prefix + "target_ref", "");
            mode_ = config_value_or(spec, rule_prefix + "mode", mode_);
            if (rule.target_ref.empty()) {
                last_error_ = "network-xdp-v2-missing-target-ref";
                return false;
            }
            const std::string target_prefix = "targets." + rule.target_ref + ".";
            std::string ifname;
            if (!resolve_network_target_ifname(spec, target_prefix, rule.target_ref,
                                               "network-xdp", ifname, last_error_)) {
                return false;
            }
            if (ifname_.empty()) {
                ifname_ = ifname;
                target_ref_ = rule.target_ref;
            } else if (ifname_ != ifname) {
                last_error_ = "network-xdp-multiple-ifnames-unsupported";
                return false;
            }
            if (!parse_and_add_rule(rule)) {
                return false;
            }
        }

        if (saw_v2_rule) {
            if (rules_.empty()) {
                last_error_ = "network-xdp-no-rules";
                return false;
            }
        } else {
            auto hook = spec.config.find("hook");
            auto mode = spec.config.find("mode");
            auto ifname = spec.config.find("ifname");
            auto protocol = spec.config.find("protocol");
            auto port = spec.config.find("dst_port");
            auto action = spec.config.find("action");
            if (hook == spec.config.end() || mode == spec.config.end() ||
                ifname == spec.config.end() || protocol == spec.config.end() ||
                port == spec.config.end() || action == spec.config.end()) {
                last_error_ = "network-xdp-missing-required-config";
                return false;
            }

            hook_ = hook->second;
            mode_ = mode->second;
            ifname_ = ifname->second;
            NetworkXdpRule rule;
            rule.protocol = protocol->second;
            rule.dst_port = port->second;
            rule.action = action->second;
            target_ref_ = config_value_or(spec, "target_ref", "legacy_netdev");
            rule.target_ref = target_ref_;
            rule.rule_id = "xdp-" + rule.action + "-" + rule.protocol + "-" + ifname_;
            if (!parse_and_add_rule(rule)) {
                return false;
            }
        }

        if (hook_ != "xdp") {
            last_error_ = "unsupported-hook";
            return false;
        }

        if (mode_ != "audit" && mode_ != "enforce") {
            last_error_ = "unsupported-mode";
            return false;
        }
        if (!valid_tc_token(ifname_)) {
            last_error_ = "invalid-ifname";
            return false;
        }
        if (rules_.empty()) {
            last_error_ = "network-xdp-no-rules";
            return false;
        }
        rule_ids_ = joined_rule_ids();
        return true;
    }

    bool probe() override {
        available_ = false;
        if (!command_available("ip")) {
            last_error_ = "iproute2-missing";
            return false;
        }
        if (!file_exists("/root/EulerPilot/build/network_xdp_demo.bpf.o")) {
            last_error_ = "network-xdp-demo-not-built";
            return false;
        }
        const unsigned int ifindex = if_nametoindex(ifname_.c_str());
        if (ifindex == 0) {
            last_error_ = "xdp-ifname-missing";
            return false;
        }
        ifindex_ = static_cast<int>(ifindex);

        bpf_object *obj = bpf_object__open_file("/root/EulerPilot/build/network_xdp_demo.bpf.o", nullptr);
        if (!obj) {
            last_error_ = "probe-xdp-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(obj) != 0) {
            bpf_object__close(obj);
            last_error_ = "probe-xdp-bpf-load-failed";
            return false;
        }
        bpf_program *prog = bpf_object__find_program_by_name(obj, "network_xdp_filter");
        if (!prog) {
            bpf_object__close(obj);
            last_error_ = "probe-xdp-program-missing";
            return false;
        }
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

        ifindex_ = static_cast<int>(if_nametoindex(ifname_.c_str()));
        if (ifindex_ <= 0) {
            last_error_ = "xdp-ifindex-resolve-failed";
            return false;
        }

        if (mode_ == "audit") {
            running_ = true;
            state_ = "audit-only";
            write_audit_event("start", "audit-only", "success");
            write_journal_action("start-audit", "audit-only", "none");
            return true;
        }

        bpf_object_ = bpf_object__open_file("/root/EulerPilot/build/network_xdp_demo.bpf.o", nullptr);
        if (!bpf_object_) {
            last_error_ = "xdp-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(bpf_object_) != 0) {
            rollback();
            last_error_ = "xdp-bpf-load-failed";
            return false;
        }
        if (!install_xdp_config()) {
            rollback();
            return false;
        }

        bpf_program *prog = bpf_object__find_program_by_name(bpf_object_, "network_xdp_filter");
        if (!prog) {
            rollback();
            last_error_ = "xdp-program-missing";
            return false;
        }

        const std::uint32_t flags = XDP_FLAGS_SKB_MODE | XDP_FLAGS_UPDATE_IF_NOEXIST;
        if (bpf_xdp_attach(ifindex_, bpf_program__fd(prog), flags, nullptr) != 0) {
            rollback();
            last_error_ = "xdp-attach-failed";
            return false;
        }
        xdp_attached_ = true;
        running_ = true;
        state_ = "started";
        write_audit_event("start", "attach-xdp-generic", "success");
        write_journal_action("start-enforce", "attach-xdp-generic", ifname_);
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
        snapshot.evidence["target_ref"] = target_ref_;
        snapshot.evidence["rule_ids"] = rule_ids_;
        snapshot.evidence["rule_count"] = std::to_string(rules_.size());
        const auto stats = read_xdp_stats();
        snapshot.evidence["pass_count"] = std::to_string(stats.pass_count);
        snapshot.evidence["drop_count"] = std::to_string(stats.drop_count);
        snapshot.evidence["byte_count"] = std::to_string(stats.byte_count);
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        update_cached_stats();
        if (running_) {
            write_audit_event("rollback", "detach-xdp-generic", "success");
            write_journal_action("rollback", "detach-xdp-generic", ifname_);
        }
        if (xdp_attached_) {
            bpf_xdp_detach(ifindex_, XDP_FLAGS_SKB_MODE, nullptr);
            xdp_attached_ = false;
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
            std::cerr << "[network_xdp] stop cleanup failed: "
                      << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[network_xdp] stop cleanup failed: unknown\n";
        }
        running_ = false;
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    bool parse_and_add_rule(NetworkXdpRule &rule) {
        if (rule.protocol != "any" && rule.protocol != "tcp" &&
            rule.protocol != "udp" && rule.protocol != "icmp") {
            last_error_ = "unsupported-protocol";
            return false;
        }
        if (rule.action != "drop" && rule.action != "pass") {
            last_error_ = "unsupported-action";
            return false;
        }
        if (rule.dst_port == "0") {
            rule.dst_port_value = 0;
        } else if (!parse_tcp_port(rule.dst_port, rule.dst_port_value)) {
            last_error_ = "invalid-dst-port";
            return false;
        }
        rule.protocol_value = protocol_id(rule.protocol);
        rule.action_value = rule.action == "drop" ? 1 : 0;
        rules_.push_back(rule);
        return true;
    }

    std::string joined_rule_ids() const {
        std::string joined;
        for (std::size_t i = 0; i < rules_.size(); ++i) {
            if (i != 0) {
                joined += ",";
            }
            joined += rules_[i].rule_id;
        }
        return joined;
    }

    bool install_xdp_config() {
        const int config_fd = bpf_object__find_map_fd_by_name(bpf_object_, "xdp_config_map");
        if (config_fd < 0) {
            last_error_ = "xdp-config-map-missing";
            return false;
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "xdp_stats_map");
        if (stats_fd < 0) {
            last_error_ = "xdp-stats-map-missing";
            return false;
        }
        for (std::uint32_t key = 0; key < kNetworkXdpMaxRules; ++key) {
            NetworkXdpConfig config;
            if (key < rules_.size()) {
                const auto &rule = rules_[key];
                config.dst_port = rule.dst_port_value;
                config.protocol = rule.protocol_value;
                config.action = rule.action_value;
                config.enabled = 1;
            }
            if (bpf_map_update_elem(config_fd, &key, &config, BPF_ANY) != 0) {
                last_error_ = "xdp-config-map-update-failed";
                return false;
            }

            NetworkXdpStats stats;
            if (bpf_map_update_elem(stats_fd, &key, &stats, BPF_ANY) != 0) {
                last_error_ = "xdp-stats-map-reset-failed";
                return false;
            }
        }
        pass_count_ = 0;
        drop_count_ = 0;
        byte_count_ = 0;
        return true;
    }

    NetworkXdpCounters read_xdp_stats() const {
        NetworkXdpCounters counters;
        counters.pass_count = pass_count_;
        counters.drop_count = drop_count_;
        counters.byte_count = byte_count_;
        if (!bpf_object_) {
            return counters;
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "xdp_stats_map");
        if (stats_fd < 0) {
            return counters;
        }
        counters = {};
        for (std::uint32_t key = 0; key < kNetworkXdpMaxRules; ++key) {
            NetworkXdpStats stats;
            if (bpf_map_lookup_elem(stats_fd, &key, &stats) != 0) {
                continue;
            }
            counters.pass_count += stats.pass_count;
            counters.drop_count += stats.drop_count;
            counters.byte_count += stats.byte_count;
        }
        return counters;
    }

    void update_cached_stats() {
        const auto stats = read_xdp_stats();
        pass_count_ = stats.pass_count;
        drop_count_ = stats.drop_count;
        byte_count_ = stats.byte_count;
    }

    void write_audit_event(const std::string &operation,
                           const std::string &action,
                           const std::string &result) const {
        const fs::path audit_path = "reports/events/network_policy.jsonl";
        ensure_parent_dir(audit_path);

        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = "network_xdp-" + operation + "-" + event.timestamp;
        event.skill = "network_xdp";
        event.policy_id = "network_policy";
        event.rule_id = rule_ids_;
        event.mode = mode_;
        event.target = {
            {"ifname", ifname_},
            {"ifindex", std::to_string(ifindex_)},
            {"target_ref", target_ref_},
        };
        event.operation = operation;
        event.evidence = {
            {"hook", hook_},
            {"rule_count", std::to_string(rules_.size())},
            {"rule_ids", rule_ids_},
            {"pass_count", std::to_string(pass_count_)},
            {"drop_count", std::to_string(drop_count_)},
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
        entry.action_id = "network_xdp-" + operation + "-" + now_event_timestamp();
        entry.skill = "network_xdp";
        entry.target = ifname_;
        entry.operation = operation;
        entry.new_values = {
            {"mode", mode_},
            {"hook", hook_},
            {"target_ref", target_ref_},
            {"rule_ids", rule_ids_},
            {"rule_count", std::to_string(rules_.size())},
            {"action", action},
        };
        entry.handles = {
            {"ifname", ifname_},
            {"xdp_mode", "generic"},
            {"handle", handle},
        };
        entry.restored = operation == "rollback";
        std::string error;
        append_journal_action(journal_path.string(), entry, &error);
    }

    bool available_ = false;
    bool running_ = false;
    bool xdp_attached_ = false;
    std::string state_ = "created";
    std::string last_error_;
    std::string hook_ = "xdp";
    std::string mode_ = "audit";
    std::string ifname_ = "ep-veth-xdp0";
    std::string target_ref_ = "legacy_netdev";
    std::string rule_ids_ = "xdp-drop-icmp-lab";
    std::vector<NetworkXdpRule> rules_;
    std::uint64_t pass_count_ = 0;
    std::uint64_t drop_count_ = 0;
    std::uint64_t byte_count_ = 0;
    int ifindex_ = 0;
    bpf_object *bpf_object_ = nullptr;
};

class SecurityPolicyDemoSkill final : public Skill {
public:
    explicit SecurityPolicyDemoSkill(std::string skill_name = "security_policy_demo")
        : skill_name_(std::move(skill_name)) {}

    std::string name() const override { return skill_name_; }

    bool configure(const RuntimeConfig &, const SkillSpec &spec) override {
        rules_.clear();
        anomaly_rules_.clear();
        anomaly_alert_count_.store(0);
        exec_prefix_.clear();
        file_prefix_.clear();
        file_access_ = "any";
        const int rule_index = find_first_rule(spec);
        if (rule_index >= 0) {
            mode_ = config_value_or(spec, "mode", "audit");
            hook_ = config_value_or(spec, "rules." + std::to_string(rule_index) + ".hook",
                                    "lsm_file_open");
            action_ = "deny";

            for (std::size_t i = 0; i < kSecurityPolicyMaxTargets; ++i) {
                const std::string rule_prefix = "rules." + std::to_string(i) + ".";
                const std::string hook = config_value_or(spec, rule_prefix + "hook", "");
                if (hook.empty()) {
                    continue;
                }
                if (!is_supported_security_hook(hook)) {
                    last_error_ = "unsupported-hook";
                    return false;
                }
                const std::string action = config_value_or(spec, rule_prefix + "action", "deny");
                if (action != "deny") {
                    last_error_ = "unsupported-action";
                    return false;
                }
                mode_ = config_value_or(spec, rule_prefix + "mode", mode_);

                SecurityPolicyRule rule;
                rule.hook = hook;
                rule.rule_id = config_value_or(spec, rule_prefix + "name",
                                               "deny-security-target-" + std::to_string(i));
                rule.target_ref = config_value_or(spec, rule_prefix + "target_ref", "");
                if (rule.target_ref.empty()) {
                    last_error_ = "security-policy-v2-missing-target-ref";
                    return false;
                }
                const std::string target_prefix = "targets." + rule.target_ref + ".";
                const std::string target_type = config_value_or(spec, target_prefix + "type", "");
                if (target_type != "path" && target_type != "cgroup" &&
                    target_type != "pid" &&
                    target_type != "container_id" && target_type != "container" &&
                    target_type != "k8s_pod" && target_type != "pod") {
                    last_error_ = "security-policy-v2-target-not-path-cgroup-pid-container-or-pod";
                    return false;
                }
                rule.file_path = config_value_or(spec, target_prefix + "path", "");
                rule.file_prefix =
                    config_value_or(spec, target_prefix + "path_prefix",
                                    config_value_or(spec, target_prefix + "file_prefix",
                                                    config_value_or(spec, rule_prefix + "path_prefix",
                                                                    config_value_or(spec, rule_prefix + "file_prefix", ""))));
                rule.exec_path = config_value_or(spec, target_prefix + "exec_path", "");
                rule.exec_prefix = config_value_or(spec, target_prefix + "exec_prefix", "");
                rule.file_access =
                    config_value_or(spec, target_prefix + "file_access",
                                    config_value_or(spec, rule_prefix + "file_access", "any"));
                if (!parse_security_file_access(rule.file_access, rule.file_access_value)) {
                    last_error_ = "security-policy-v2-target-file-access-invalid";
                    return false;
                }
                if (hook == "lsm_capable") {
                    const std::string capability_text =
                        config_value_or(spec, target_prefix + "capability",
                                        config_value_or(spec, target_prefix + "cap",
                                                        config_value_or(spec, rule_prefix + "capability",
                                                                        config_value_or(spec, rule_prefix + "cap", ""))));
                    if (!parse_security_capability(capability_text, rule.capability)) {
                        last_error_ = "security-policy-v2-target-capability-invalid";
                        return false;
                    }
                }
                if (hook == "lsm_socket_connect") {
                    rule.connect_ip =
                        config_value_or(spec, target_prefix + "dst_ip",
                                        config_value_or(spec, target_prefix + "connect_ip", ""));
                    if (rule.connect_ip.empty() ||
                        !parse_ipv4_address(rule.connect_ip, rule.connect_daddr)) {
                        last_error_ = "security-policy-v2-target-dst-ip-invalid";
                        return false;
                    }
                    rule.connect_port =
                        config_value_or(spec, target_prefix + "dst_port",
                                        config_value_or(spec, target_prefix + "connect_port", ""));
                    std::uint16_t host_port = 0;
                    if (!parse_tcp_port(rule.connect_port, host_port)) {
                        last_error_ = "security-policy-v2-target-dst-port-invalid";
                        return false;
                    }
                    rule.connect_dport = htons(host_port);
                    rule.connect_protocol = 6;
                    rule.connect_port = std::to_string(host_port);
                } else if (hook == "lsm_bprm_check_security") {
                    if (rule.exec_path.empty() && rule.exec_prefix.empty()) {
                        last_error_ = "security-policy-v2-target-exec-matcher-missing";
                        return false;
                    }
                } else if (hook == "lsm_ptrace_traceme") {
                    // Ptrace enforcement must be scoped. A global ptrace deny
                    // would be too easy to use incorrectly on the host.
                } else if (hook == "lsm_task_fix_setuid") {
                    // Credential transition enforcement must be scoped. A
                    // global setuid deny would break host administration paths.
                } else if (hook == "lsm_task_fix_setgid") {
                    // Group credential transition enforcement must be scoped.
                    // A global setgid deny would break host administration paths.
                } else if (hook == "lsm_capable") {
                    // Capability enforcement must be scoped. A global capable
                    // deny would break host administration paths.
                } else {
                    if (rule.file_path.empty() && rule.file_prefix.empty()) {
                        last_error_ = "security-policy-v2-target-path-missing";
                        return false;
                    }
                }
                if (target_type == "pid") {
                    int target_pid = 0;
                    if (!parse_pid_value(config_value_or(spec, target_prefix + "pid", ""), target_pid)) {
                        last_error_ = "security-policy-v2-target-pid-invalid";
                        return false;
                    }
                    const auto target = resolve_pid_target(target_pid);
                    if (!target.resolved) {
                        last_error_ = "security-policy-target-pid-resolve-failed:" + target.reason;
                        return false;
                    }
                    rule.cgroup_path = target.cgroup_path;
                    rule.cgroup_id = target.cgroup_id;
                } else if (target_type == "container_id" || target_type == "container") {
                    ContainerTargetSpec target_spec;
                    target_spec.name = rule.target_ref;
                    target_spec.container_id =
                        config_value_or(spec, target_prefix + "container_id", "");
                    target_spec.container_name =
                        config_value_or(spec, target_prefix + "container_name",
                                        config_value_or(spec, target_prefix + "name", ""));
                    target_spec.runtime =
                        config_value_or(spec, target_prefix + "runtime", "auto");
                    target_spec.cgroup_root =
                        config_value_or(spec, target_prefix + "cgroup_root", "/sys/fs/cgroup");
                    target_spec.crictl_path =
                        config_value_or(spec, target_prefix + "crictl_path", "crictl");
                    target_spec.docker_path =
                        config_value_or(spec, target_prefix + "docker_path", "docker");
                    target_spec.podman_path =
                        config_value_or(spec, target_prefix + "podman_path", "podman");
                    const auto target = resolve_container_target(target_spec);
                    if (!target.resolved) {
                        last_error_ = "security-policy-target-container-resolve-failed:" + target.reason;
                        return false;
                    }
                    rule.cgroup_path = target.cgroup_path;
                    rule.cgroup_id = target.cgroup_id;
                } else if (target_type == "k8s_pod" || target_type == "pod") {
                    K8sPodTargetSpec target_spec;
                    target_spec.name = rule.target_ref;
                    target_spec.pod_namespace =
                        config_value_or(spec, target_prefix + "namespace",
                                        config_value_or(spec, target_prefix + "pod_namespace", ""));
                    target_spec.pod_name =
                        config_value_or(spec, target_prefix + "pod_name",
                                        config_value_or(spec, target_prefix + "name", ""));
                    target_spec.pod_uid =
                        config_value_or(spec, target_prefix + "pod_uid", "");
                    target_spec.container_id =
                        config_value_or(spec, target_prefix + "container_id", "");
                    target_spec.container_name =
                        config_value_or(spec, target_prefix + "container_name", "");
                    target_spec.cgroup_root =
                        config_value_or(spec, target_prefix + "cgroup_root", "/sys/fs/cgroup");
                    TargetResolverOptions options;
                    options.kubectl_path =
                        config_value_or(spec, target_prefix + "kubectl_path", "kubectl");
                    const auto target = resolve_k8s_pod_cgroup_target(target_spec, options);
                    if (!target.resolved) {
                        last_error_ = "security-policy-target-pod-resolve-failed:" + target.reason;
                        return false;
                    }
                    rule.cgroup_path = target.cgroup_path;
                    rule.cgroup_id = target.cgroup_id;
                } else {
                    rule.cgroup_path = config_value_or(spec, target_prefix + "cgroup_path", "");
                    if (!rule.cgroup_path.empty()) {
                        const auto target = resolve_cgroup_target(rule.target_ref, rule.cgroup_path);
                        if (!target.resolved) {
                            last_error_ = "security-policy-target-cgroup-resolve-failed:" + target.reason;
                            return false;
                        }
                        rule.cgroup_id = target.cgroup_id;
                    }
                }
                if (!rule.file_path.empty() && !valid_security_path(rule.file_path)) {
                    last_error_ = "invalid-target-path";
                    return false;
                }
                if (!rule.file_prefix.empty() && !valid_security_path(rule.file_prefix)) {
                    last_error_ = "invalid-target-path-prefix";
                    return false;
                }
                if (!rule.exec_path.empty() && !valid_security_path(rule.exec_path)) {
                    last_error_ = "invalid-exec-target-path";
                    return false;
                }
                if (!rule.exec_prefix.empty() && !valid_security_path(rule.exec_prefix)) {
                    last_error_ = "invalid-exec-prefix";
                    return false;
                }
                rules_.push_back(std::move(rule));
            }
            if (!config_value_or(spec, "rules." + std::to_string(kSecurityPolicyMaxTargets) + ".hook", "").empty()) {
                last_error_ = "security-policy-too-many-targets";
                return false;
            }
            if (rules_.empty()) {
                last_error_ = "security-policy-v2-no-supported-rules";
                return false;
            }
        } else {
            auto hook = spec.config.find("hook");
            auto mode = spec.config.find("mode");
            auto target_path = spec.config.find("target_path");
            if (hook == spec.config.end() || mode == spec.config.end() ||
                target_path == spec.config.end()) {
                last_error_ = "security-policy-demo-missing-required-config";
                return false;
            }
            hook_ = hook->second;
            mode_ = mode->second;
            target_path_ = target_path->second;
            exec_target_path_ = config_value_or(spec, "target_exec_path",
                                                "/root/EulerPilot/demo/security_policy_demo/deny_exec.sh");
            action_ = config_value_or(spec, "action", "deny");
            target_ref_ = config_value_or(spec, "target_ref", "legacy_path");
            rule_id_ = config_value_or(spec, "rule_id", "deny-demo-secret-open");
            SecurityPolicyRule rule;
            rule.hook = hook_;
            rule.rule_id = rule_id_;
            rule.target_ref = target_ref_;
            rule.file_path = target_path_;
            rule.exec_path = exec_target_path_;
            rule.file_access = config_value_or(spec, "file_access", "any");
            if (!parse_security_file_access(rule.file_access, rule.file_access_value)) {
                last_error_ = "security-policy-target-file-access-invalid";
                return false;
            }
            rule.cgroup_path = config_value_or(spec, "target_cgroup_path",
                                               config_value_or(spec, "cgroup_path", ""));
            if (!rule.cgroup_path.empty()) {
                const auto target = resolve_cgroup_target(rule.target_ref, rule.cgroup_path);
                if (!target.resolved) {
                    last_error_ = "security-policy-target-cgroup-resolve-failed:" + target.reason;
                    return false;
                }
                rule.cgroup_id = target.cgroup_id;
            }
            rules_.push_back(std::move(rule));
        }
        if (!parse_anomaly_rules(spec)) {
            return false;
        }
        if (mode_ != "audit" && mode_ != "enforce") {
            last_error_ = "unsupported-mode";
            return false;
        }
        if (action_ != "deny") {
            last_error_ = "unsupported-action";
            return false;
        }
        if (rules_.empty()) {
            last_error_ = "security-policy-no-targets";
            return false;
        }
        for (const auto &rule : rules_) {
            if (!is_supported_security_hook(rule.hook)) {
                last_error_ = "unsupported-hook";
                return false;
            }
            if (!rule.file_path.empty() && !valid_security_path(rule.file_path)) {
                last_error_ = "invalid-target-path";
                return false;
            }
            if (!rule.file_prefix.empty() && !valid_security_path(rule.file_prefix)) {
                last_error_ = "invalid-target-path-prefix";
                return false;
            }
            if (!rule.exec_path.empty() && !valid_security_path(rule.exec_path)) {
                last_error_ = "invalid-exec-target-path";
                return false;
            }
            if (!rule.exec_prefix.empty() && !valid_security_path(rule.exec_prefix)) {
                last_error_ = "invalid-exec-prefix";
                return false;
            }
            if (rule.hook == "lsm_file_open" &&
                rule.file_path.empty() && rule.file_prefix.empty()) {
                last_error_ = "security-policy-file-rule-target-missing";
                return false;
            }
            if (rule.hook == "lsm_bprm_check_security" &&
                rule.exec_path.empty() && rule.exec_prefix.empty()) {
                last_error_ = "security-policy-bprm-rule-target-missing";
                return false;
            }
            if (rule.hook == "lsm_socket_connect" &&
                (rule.connect_daddr == 0 || rule.connect_dport == 0)) {
                last_error_ = "security-policy-socket-rule-target-missing";
                return false;
            }
            if (rule.hook == "lsm_ptrace_traceme" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-ptrace-rule-scope-missing";
                return false;
            }
            if (rule.hook == "lsm_task_fix_setuid" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-setuid-rule-scope-missing";
                return false;
            }
            if (rule.hook == "lsm_task_fix_setgid" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-setgid-rule-scope-missing";
                return false;
            }
            if (rule.hook == "lsm_capable") {
                if (rule.cgroup_id == 0) {
                    last_error_ = "security-policy-capable-rule-scope-missing";
                    return false;
                }
                if (rule.capability < 0) {
                    last_error_ = "security-policy-capable-rule-capability-missing";
                    return false;
                }
            }
        }
        target_path_ = rules_.front().file_path;
        exec_target_path_ = rules_.front().exec_path;
        for (const auto &rule : rules_) {
            if (target_path_.empty() && !rule.file_path.empty()) {
                target_path_ = rule.file_path;
            }
            if (file_prefix_.empty() && !rule.file_prefix.empty()) {
                file_prefix_ = rule.file_prefix;
            }
            if (exec_target_path_.empty() && !rule.exec_path.empty()) {
                exec_target_path_ = rule.exec_path;
            }
            if (exec_prefix_.empty() && !rule.exec_prefix.empty()) {
                exec_prefix_ = rule.exec_prefix;
            }
            if (file_access_ == "any" && rule.file_access != "any") {
                file_access_ = rule.file_access;
            }
        }
        hook_ = join_security_field(rules_, "hook");
        target_ref_ = join_security_field(rules_, "target_ref");
        rule_id_ = join_security_field(rules_, "rule_id");
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
        std::vector<bpf_link *> probe_links;
        if (!attach_security_programs(obj, probe_links, false, false, false, last_error_)) {
            for (auto *probe_link : probe_links) {
                bpf_link__destroy(probe_link);
            }
            bpf_object__close(obj);
            return false;
        }
        for (auto *probe_link : probe_links) {
            bpf_link__destroy(probe_link);
        }
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
        if (!install_policy_config()) {
            rollback();
            return false;
        }
        if (!start_event_reader()) {
            rollback();
            return false;
        }
        if (!attach_security_programs(bpf_object_, links_, has_rule_hook("lsm_capable"),
                                      has_rule_hook("lsm_task_fix_setuid"),
                                      has_rule_hook("lsm_task_fix_setgid"),
                                      last_error_)) {
            rollback();
            return false;
        }
        // Do NOT pin link — LSM should not persist after agent exit
        running_ = true;
        state_ = mode_ == "audit" ? "audit-attached" : "started";
        write_audit_event("start",
                          mode_ == "audit" ? "attach-security-policy-audit"
                                           : "attach-security-policy",
                          "success");
        write_journal_action(mode_ == "audit" ? "start-audit" : "start-enforce",
                             mode_ == "audit" ? "attach-security-policy-audit"
                                              : "attach-security-policy",
                             "fd-owned-link");
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
        snapshot.evidence["exec_target_path"] = exec_target_path_;
        snapshot.evidence["exec_prefix"] = exec_prefix_;
        snapshot.evidence["path_prefix"] = file_prefix_;
        snapshot.evidence["file_access"] = file_access_;
        snapshot.evidence["mode"] = mode_;
        snapshot.evidence["target_ref"] = target_ref_;
        snapshot.evidence["rule_id"] = rule_id_;
        snapshot.evidence["target_count"] = std::to_string(rules_.size());
        snapshot.evidence["action"] = action_;
        snapshot.evidence["hit_count"] = std::to_string(hit_count_.load());
        snapshot.evidence["deny_count"] = std::to_string(deny_count_.load());
        snapshot.evidence["anomaly_rule_count"] = std::to_string(anomaly_rules_.size());
        snapshot.evidence["anomaly_alert_count"] = std::to_string(anomaly_alert_count_.load());
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        if (running_) {
            write_audit_event("rollback",
                              mode_ == "audit" ? "detach-security-policy-audit"
                                               : "detach-security-policy",
                              "success");
            write_journal_action("rollback",
                                 mode_ == "audit" ? "detach-security-policy-audit"
                                                  : "detach-security-policy",
                                 "fd-owned-link");
        }
        stop_event_reader();
        for (auto *link : links_) {
            bpf_link__destroy(link);
        }
        links_.clear();
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
    static bool valid_security_path(const std::string &path) {
        return !path.empty() && path[0] == '/' && path.size() < 256;
    }

    static bool is_supported_security_hook(const std::string &hook) {
        return hook == "lsm_file_open" || hook == "lsm_bprm_check_security" ||
               hook == "lsm_socket_connect" || hook == "lsm_ptrace_traceme" ||
               hook == "lsm_capable" || hook == "lsm_task_fix_setuid" ||
               hook == "lsm_task_fix_setgid";
    }

    bool has_rule_hook(const std::string &hook) const {
        for (const auto &rule : rules_) {
            if (rule.hook == hook) {
                return true;
            }
        }
        return false;
    }

    bool parse_anomaly_rules(const SkillSpec &spec) {
        anomaly_rules_.clear();
        for (std::size_t i = 0; i < kSecurityPolicyMaxTargets; ++i) {
            const std::string prefix = "anomaly_rules." + std::to_string(i) + ".";
            const std::string name = config_value_or(spec, prefix + "name", "");
            const std::string type = config_value_or(spec, prefix + "type", "");
            const std::string syscall = config_value_or(spec, prefix + "syscall", "");
            const std::string threshold_text = config_value_or(spec, prefix + "threshold", "");
            const std::string window_text = config_value_or(spec, prefix + "window_ms", "");
            if (name.empty() && type.empty() && syscall.empty() &&
                threshold_text.empty() && window_text.empty()) {
                continue;
            }

            SecurityAnomalyRule rule;
            rule.rule_id = name.empty() ? "burst_execve" : name;
            rule.type = type.empty() ? "rate" : type;
            rule.syscall = syscall.empty() ? "execve" : syscall;
            rule.severity = config_value_or(spec, prefix + "severity", "medium");
            if (rule.type != "rate") {
                last_error_ = "security-policy-anomaly-type-unsupported";
                return false;
            }
            if (rule.syscall == "sys_enter_execve") {
                rule.syscall = "execve";
            }
            if (rule.syscall != "execve") {
                last_error_ = "security-policy-anomaly-syscall-unsupported";
                return false;
            }
            if (!threshold_text.empty() &&
                !parse_uint32_range(threshold_text, 1, 100000, rule.threshold)) {
                last_error_ = "security-policy-anomaly-threshold-invalid";
                return false;
            }
            if (!window_text.empty() &&
                !parse_uint32_range(window_text, 1, 600000, rule.window_ms)) {
                last_error_ = "security-policy-anomaly-window-invalid";
                return false;
            }
            anomaly_rules_.push_back(std::move(rule));
        }
        if (!config_value_or(spec,
                             "anomaly_rules." +
                                 std::to_string(kSecurityPolicyMaxTargets) + ".name",
                             "")
                 .empty()) {
            last_error_ = "security-policy-too-many-anomaly-rules";
            return false;
        }
        return true;
    }

    static std::string join_security_field(const std::vector<SecurityPolicyRule> &rules,
                                           const char *field) {
        std::ostringstream out;
        for (std::size_t i = 0; i < rules.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            if (std::string(field) == "hook") {
                out << rules[i].hook;
            } else if (std::string(field) == "target_ref") {
                out << rules[i].target_ref;
            } else {
                out << rules[i].rule_id;
            }
        }
        return out.str();
    }

    static bool copy_security_path(const std::string &src,
                                   char *dst,
                                   std::size_t dst_len,
                                   std::string &error,
                                   const char *field) {
        if (src.empty() || src.size() >= dst_len) {
            error = std::string("security-policy-invalid-") + field;
            return false;
        }
        std::memset(dst, 0, dst_len);
        std::memcpy(dst, src.data(), src.size());
        return true;
    }

    static bool attach_security_programs(bpf_object *obj,
                                         std::vector<bpf_link *> &links,
                                         bool attach_capable,
                                         bool attach_task_fix_setuid,
                                         bool attach_task_fix_setgid,
                                         std::string &error) {
        bpf_program *lsm_prog = bpf_object__find_program_by_name(obj, "security_policy_demo");
        if (!lsm_prog) {
            error = "security-policy-lsm-program-missing";
            return false;
        }
        bpf_link *lsm_link = bpf_program__attach_lsm(lsm_prog);
        if (!lsm_link) {
            error = "security-policy-lsm-attach-failed";
            return false;
        }
        links.push_back(lsm_link);

        bpf_program *bprm_prog = bpf_object__find_program_by_name(obj, "security_policy_bprm");
        if (!bprm_prog) {
            error = "security-policy-bprm-program-missing";
            return false;
        }
        bpf_link *bprm_link = bpf_program__attach_lsm(bprm_prog);
        if (!bprm_link) {
            error = "security-policy-bprm-attach-failed";
            return false;
        }
        links.push_back(bprm_link);

        bpf_program *socket_connect_prog =
            bpf_object__find_program_by_name(obj, "security_policy_socket_connect");
        if (!socket_connect_prog) {
            error = "security-policy-socket-connect-program-missing";
            return false;
        }
        bpf_link *socket_connect_link = bpf_program__attach_lsm(socket_connect_prog);
        if (!socket_connect_link) {
            error = "security-policy-socket-connect-attach-failed";
            return false;
        }
        links.push_back(socket_connect_link);

        bpf_program *ptrace_traceme_prog =
            bpf_object__find_program_by_name(obj, "security_policy_ptrace_traceme");
        if (!ptrace_traceme_prog) {
            error = "security-policy-ptrace-traceme-program-missing";
            return false;
        }
        bpf_link *ptrace_traceme_link = bpf_program__attach_lsm(ptrace_traceme_prog);
        if (!ptrace_traceme_link) {
            error = "security-policy-ptrace-traceme-attach-failed";
            return false;
        }
        links.push_back(ptrace_traceme_link);

        if (attach_capable) {
            bpf_program *capable_prog =
                bpf_object__find_program_by_name(obj, "security_policy_capable");
            if (!capable_prog) {
                error = "security-policy-capable-program-missing";
                return false;
            }
            bpf_link *capable_link = bpf_program__attach_lsm(capable_prog);
            if (!capable_link) {
                error = "security-policy-capable-attach-failed";
                return false;
            }
            links.push_back(capable_link);
        }

        if (attach_task_fix_setuid) {
            bpf_program *setuid_prog =
                bpf_object__find_program_by_name(obj, "security_policy_task_fix_setuid");
            if (!setuid_prog) {
                error = "security-policy-task-fix-setuid-program-missing";
                return false;
            }
            bpf_link *setuid_link = bpf_program__attach_lsm(setuid_prog);
            if (!setuid_link) {
                error = "security-policy-task-fix-setuid-attach-failed";
                return false;
            }
            links.push_back(setuid_link);
        }

        if (attach_task_fix_setgid) {
            bpf_program *setgid_prog =
                bpf_object__find_program_by_name(obj, "security_policy_task_fix_setgid");
            if (!setgid_prog) {
                error = "security-policy-task-fix-setgid-program-missing";
                return false;
            }
            bpf_link *setgid_link = bpf_program__attach_lsm(setgid_prog);
            if (!setgid_link) {
                error = "security-policy-task-fix-setgid-attach-failed";
                return false;
            }
            links.push_back(setgid_link);
        }

        bpf_program *execve_prog = bpf_object__find_program_by_name(obj, "trace_execve");
        if (!execve_prog) {
            error = "security-policy-execve-program-missing";
            return false;
        }
        bpf_link *execve_link = bpf_program__attach(execve_prog);
        if (!execve_link) {
            error = "security-policy-execve-attach-failed";
            return false;
        }
        links.push_back(execve_link);

        bpf_program *openat_prog = bpf_object__find_program_by_name(obj, "trace_openat");
        if (!openat_prog) {
            error = "security-policy-openat-program-missing";
            return false;
        }
        bpf_link *openat_link = bpf_program__attach(openat_prog);
        if (!openat_link) {
            error = "security-policy-openat-attach-failed";
            return false;
        }
        links.push_back(openat_link);

        bpf_program *connect_prog = bpf_object__find_program_by_name(obj, "trace_connect");
        if (!connect_prog) {
            error = "security-policy-connect-program-missing";
            return false;
        }
        bpf_link *connect_link = bpf_program__attach(connect_prog);
        if (!connect_link) {
            error = "security-policy-connect-attach-failed";
            return false;
        }
        links.push_back(connect_link);

        bpf_program *ptrace_prog = bpf_object__find_program_by_name(obj, "trace_ptrace");
        if (!ptrace_prog) {
            error = "security-policy-ptrace-program-missing";
            return false;
        }
        bpf_link *ptrace_link = bpf_program__attach(ptrace_prog);
        if (!ptrace_link) {
            error = "security-policy-ptrace-attach-failed";
            return false;
        }
        links.push_back(ptrace_link);
        return true;
    }

    bool install_policy_config() {
        const int config_fd = bpf_object__find_map_fd_by_name(bpf_object_, "policy_map");
        if (config_fd < 0) {
            last_error_ = "security-policy-config-map-missing";
            return false;
        }

        std::uint32_t key = 0;
        SecurityPolicyConfig config;
        config.enforce = mode_ == "enforce" ? 1 : 0;
        config.target_count = static_cast<std::uint32_t>(rules_.size());
        if (bpf_map_update_elem(config_fd, &key, &config, BPF_ANY) != 0) {
            last_error_ = "security-policy-config-map-update-failed";
            return false;
        }

        const int target_fd = bpf_object__find_map_fd_by_name(bpf_object_, "target_map");
        if (target_fd < 0) {
            last_error_ = "security-policy-target-map-missing";
            return false;
        }

        for (std::size_t i = 0; i < kSecurityPolicyMaxTargets; ++i) {
            SecurityPolicyTarget target;
            if (i < rules_.size()) {
                if (rules_[i].hook == "lsm_file_open" && !rules_[i].file_path.empty()) {
                    if (!copy_security_path(rules_[i].file_path, target.file_path, sizeof(target.file_path),
                                            last_error_, "file-path")) {
                        return false;
                    }
                }
                if (rules_[i].hook == "lsm_file_open" && !rules_[i].file_prefix.empty()) {
                    if (!copy_security_path(rules_[i].file_prefix, target.file_prefix,
                                            sizeof(target.file_prefix),
                                            last_error_, "file-prefix")) {
                        return false;
                    }
                }
                if (rules_[i].hook == "lsm_bprm_check_security" && !rules_[i].exec_path.empty()) {
                    if (!copy_security_path(rules_[i].exec_path, target.exec_path, sizeof(target.exec_path),
                                            last_error_, "exec-path")) {
                        return false;
                    }
                }
                if (rules_[i].hook == "lsm_bprm_check_security" && !rules_[i].exec_prefix.empty()) {
                    if (!copy_security_path(rules_[i].exec_prefix, target.exec_prefix,
                                            sizeof(target.exec_prefix),
                                            last_error_, "exec-prefix")) {
                        return false;
                    }
                }
                target.cgroup_id = rules_[i].cgroup_id;
                target.connect_daddr = rules_[i].connect_daddr;
                target.connect_dport = rules_[i].connect_dport;
                target.connect_protocol = rules_[i].connect_protocol;
                target.file_access = rules_[i].hook == "lsm_file_open"
                                         ? rules_[i].file_access_value
                                         : 0;
                target.capability = rules_[i].hook == "lsm_capable"
                                        ? rules_[i].capability
                                        : -1;
            }
            std::uint32_t target_key = static_cast<std::uint32_t>(i);
            if (bpf_map_update_elem(target_fd, &target_key, &target, BPF_ANY) != 0) {
                last_error_ = "security-policy-target-map-update-failed";
                return false;
            }
        }

        hit_count_.store(0);
        deny_count_.store(0);
        return true;
    }

    bool start_event_reader() {
        const int events_fd = bpf_object__find_map_fd_by_name(bpf_object_, "events");
        if (events_fd < 0) {
            last_error_ = "security-policy-events-map-missing";
            return false;
        }

        ring_buffer_ = ring_buffer__new(events_fd, handle_ringbuf_event, this, nullptr);
        if (!ring_buffer_) {
            last_error_ = "security-policy-ringbuf-create-failed";
            return false;
        }

        event_thread_stop_.store(false);
        try {
            event_thread_ = std::thread([this]() { poll_event_loop(); });
        } catch (...) {
            ring_buffer__free(ring_buffer_);
            ring_buffer_ = nullptr;
            last_error_ = "security-policy-ringbuf-thread-failed";
            return false;
        }
        return true;
    }

    void stop_event_reader() {
        event_thread_stop_.store(true);
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
        if (ring_buffer_) {
            ring_buffer__free(ring_buffer_);
            ring_buffer_ = nullptr;
        }
    }

    void poll_event_loop() {
        while (!event_thread_stop_.load()) {
            const int rc = ring_buffer__poll(ring_buffer_, 100);
            if (rc < 0 && rc != -EINTR) {
                // Keep the Agent alive: a transient ringbuf poll failure should
                // be visible in counters/logs later, but must not bypass rollback.
                continue;
            }
        }
    }

    static int handle_ringbuf_event(void *ctx, void *data, size_t size) {
        auto *self = static_cast<SecurityPolicyDemoSkill *>(ctx);
        if (!self || size < sizeof(SecurityPolicyEvent)) {
            return 0;
        }
        const auto *event = static_cast<const SecurityPolicyEvent *>(data);
        self->write_hit_event(*event);
        return 0;
    }

    static std::string bounded_string(const char *value, std::size_t max_len) {
        std::size_t len = 0;
        while (len < max_len && value[len] != '\0') {
            ++len;
        }
        return std::string(value, len);
    }

    static std::string ipv4_to_string(std::uint32_t daddr) {
        char text[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &daddr, text, sizeof(text)) == nullptr) {
            return "";
        }
        return std::string(text);
    }

    static std::string security_event_hook(std::uint32_t event_type) {
        switch (event_type) {
        case 1: return "lsm_file_open";
        case 2: return "sys_enter_execve";
        case 3: return "sys_enter_openat";
        case 4: return "sys_enter_connect";
        case 5: return "sys_enter_ptrace";
        case 6: return "lsm_bprm_check_security";
        case 7: return "lsm_socket_connect";
        case 8: return "lsm_ptrace_traceme";
        case 9: return "lsm_capable";
        case 10: return "lsm_task_fix_setuid";
        case 11: return "lsm_task_fix_setgid";
        default: return "unknown";
        }
    }

    const SecurityPolicyRule *rule_for_target_index(std::uint32_t target_index) const {
        if (target_index == kSecurityTargetUnknown || target_index >= rules_.size()) {
            return nullptr;
        }
        return &rules_[target_index];
    }

    void write_hit_event(const SecurityPolicyEvent &hit) {
        const auto hit_index = hit_count_.fetch_add(1) + 1;
        if (hit.decision < 0) {
            deny_count_.fetch_add(1);
        }
        const std::string hook_name = security_event_hook(hit.event_type);
        const SecurityPolicyRule *matched_rule = rule_for_target_index(hit.target_index);

        const fs::path audit_path = "reports/events/security_policy.jsonl";
        ensure_parent_dir(audit_path);

        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = skill_name_ + "-hit-" + std::to_string(hit_index) + "-" + event.timestamp;
        event.skill = skill_name_;
        event.policy_id = "security_policy";
        event.rule_id = matched_rule ? matched_rule->rule_id : rule_id_;
        event.mode = mode_;
        event.target = {
            {"target_ref", matched_rule ? matched_rule->target_ref : target_ref_},
            {"path", bounded_string(hit.path, sizeof(hit.path))},
        };
        if (matched_rule && matched_rule->cgroup_id != 0) {
            event.target["cgroup_id"] = std::to_string(matched_rule->cgroup_id);
            event.target["cgroup_path"] = matched_rule->cgroup_path;
        }
        if (matched_rule && !matched_rule->connect_ip.empty()) {
            event.target["dst_ip"] = matched_rule->connect_ip;
            event.target["dst_port"] = matched_rule->connect_port;
        }
        if (matched_rule && !matched_rule->exec_prefix.empty()) {
            event.target["exec_prefix"] = matched_rule->exec_prefix;
        }
        if (matched_rule && !matched_rule->file_prefix.empty()) {
            event.target["path_prefix"] = matched_rule->file_prefix;
        }
        if (matched_rule && matched_rule->hook == "lsm_file_open") {
            event.target["file_access"] = matched_rule->file_access;
        }
        if (matched_rule && matched_rule->hook == "lsm_capable") {
            event.target["capability"] =
                security_capability_name(matched_rule->capability);
        }
        event.operation = "hit";
        event.evidence = {
            {"hook", matched_rule ? matched_rule->hook : hook_},
            {"action", action_},
            {"event_type", std::to_string(hit.event_type)},
            {"event_hook", hook_name},
            {"pid", std::to_string(hit.pid)},
            {"tgid", std::to_string(hit.tgid)},
            {"comm", bounded_string(hit.comm, sizeof(hit.comm))},
            {"enforce", std::to_string(hit.enforce)},
            {"decision", std::to_string(hit.decision)},
            {"target_index", hit.target_index == kSecurityTargetUnknown
                                 ? "unknown"
                                 : std::to_string(hit.target_index)},
        };
        if (hit.daddr != 0) {
            const std::string dst_ip = ipv4_to_string(hit.daddr);
            if (!dst_ip.empty()) {
                event.evidence["dst_ip"] = dst_ip;
            }
            event.evidence["dst_port"] = std::to_string(ntohs(hit.dport));
            event.evidence["protocol"] = hit.protocol == 6 ? "tcp" : std::to_string(hit.protocol);
        }
        if (hit.event_type == 1) {
            event.evidence["file_access"] = security_file_access_name(hit.file_access);
            event.evidence["file_flags"] = std::to_string(hit.file_flags);
        }
        if (hit.capability >= 0) {
            event.evidence["capability"] =
                security_capability_name(hit.capability);
        }
        if (hit.event_type == 10) {
            event.evidence["uid"] = std::to_string(hit.uid);
            event.evidence["euid"] = std::to_string(hit.euid);
            event.evidence["suid"] = std::to_string(hit.suid);
            event.evidence["setuid_flags"] = std::to_string(hit.setuid_flags);
        }
        if (hit.event_type == 11) {
            event.evidence["gid"] = std::to_string(hit.gid);
            event.evidence["egid"] = std::to_string(hit.egid);
            event.evidence["sgid"] = std::to_string(hit.sgid);
            event.evidence["setgid_flags"] = std::to_string(hit.setgid_flags);
        }
        event.action = hit.decision < 0 ? "deny" : "audit-hit";
        event.result = hit.decision < 0 ? "blocked" : "observed";
        event.severity = hit.decision < 0 ? "warning" : "info";
        std::string error;
        append_audit_event(audit_path.string(), event, &error);
        maybe_write_anomaly_event(hit, hook_name, audit_path);
    }

    void maybe_write_anomaly_event(const SecurityPolicyEvent &hit,
                                   const std::string &hook_name,
                                   const fs::path &audit_path) {
        if (hook_name != "sys_enter_execve" || anomaly_rules_.empty()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const std::string path = bounded_string(hit.path, sizeof(hit.path));
        const std::string comm = bounded_string(hit.comm, sizeof(hit.comm));
        for (auto &rule : anomaly_rules_) {
            if (rule.type != "rate" || rule.syscall != "execve") {
                continue;
            }
            rule.hits.push_back(now);
            const auto window = std::chrono::milliseconds(rule.window_ms);
            while (!rule.hits.empty() && now - rule.hits.front() > window) {
                rule.hits.pop_front();
            }
            if (rule.hits.size() < rule.threshold) {
                continue;
            }

            const auto alert_index = anomaly_alert_count_.fetch_add(1) + 1;
            const auto window_hit_count = rule.hits.size();
            AuditEvent event;
            event.timestamp = now_event_timestamp();
            event.event_id = skill_name_ + "-anomaly-" + rule.rule_id + "-" +
                             std::to_string(alert_index) + "-" + event.timestamp;
            event.skill = skill_name_;
            event.policy_id = "security_policy";
            event.rule_id = rule.rule_id;
            event.mode = mode_;
            event.target = {
                {"target_ref", "syscall_trace"},
                {"syscall", rule.syscall},
                {"path", path},
            };
            event.operation = "anomaly";
            event.evidence = {
                {"event_hook", hook_name},
                {"anomaly_type", rule.type},
                {"threshold", std::to_string(rule.threshold)},
                {"window_ms", std::to_string(rule.window_ms)},
                {"hit_count", std::to_string(window_hit_count)},
                {"pid", std::to_string(hit.pid)},
                {"tgid", std::to_string(hit.tgid)},
                {"comm", comm},
            };
            event.action = "alert";
            event.result = "observed";
            event.severity = rule.severity;
            std::string error;
            append_audit_event(audit_path.string(), event, &error);
            rule.hits.clear();
        }
    }

    void write_audit_event(const std::string &operation,
                           const std::string &action,
                           const std::string &result) const {
        const fs::path audit_path = "reports/events/security_policy.jsonl";
        ensure_parent_dir(audit_path);

        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = skill_name_ + "-" + operation + "-" + event.timestamp;
        event.skill = skill_name_;
        event.policy_id = "security_policy";
        event.rule_id = rule_id_;
        event.mode = mode_;
        event.target = {
            {"target_ref", target_ref_},
            {"path", target_path_},
            {"exec_path", exec_target_path_},
            {"exec_prefix", exec_prefix_},
            {"path_prefix", file_prefix_},
            {"file_access", file_access_},
            {"target_count", std::to_string(rules_.size())},
            {"anomaly_rule_count", std::to_string(anomaly_rules_.size())},
        };
        event.operation = operation;
        event.evidence = {
            {"hook", hook_},
            {"action", action_},
            {"current_scope", "map-configured-demo-paths"},
        };
        event.action = action;
        event.result = result;
        event.severity = mode_ == "enforce" ? "warning" : "info";
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
        entry.target = target_path_;
        entry.operation = operation;
        entry.new_values = {
            {"mode", mode_},
            {"hook", hook_},
            {"target_ref", target_ref_},
            {"rule_id", rule_id_},
            {"target_count", std::to_string(rules_.size())},
            {"path", target_path_},
            {"exec_path", exec_target_path_},
            {"exec_prefix", exec_prefix_},
            {"path_prefix", file_prefix_},
            {"file_access", file_access_},
            {"action", action},
        };
        entry.handles = {
            {"path", target_path_},
            {"exec_path", exec_target_path_},
            {"exec_prefix", exec_prefix_},
            {"path_prefix", file_prefix_},
            {"file_access", file_access_},
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
    std::string hook_ = "lsm_file_open";
    std::string target_path_ = "/root/EulerPilot/demo/security_policy_demo/secret.txt";
    std::string exec_target_path_ = "/root/EulerPilot/demo/security_policy_demo/deny_exec.sh";
    std::string exec_prefix_;
    std::string file_prefix_;
    std::string file_access_ = "any";
    std::string mode_ = "enforce";
    std::string action_ = "deny";
    std::string target_ref_ = "legacy_path";
    std::string rule_id_ = "deny-demo-secret-open";
    std::vector<SecurityPolicyRule> rules_;
    std::vector<SecurityAnomalyRule> anomaly_rules_;
    std::atomic<std::uint64_t> hit_count_{0};
    std::atomic<std::uint64_t> deny_count_{0};
    std::atomic<std::uint64_t> anomaly_alert_count_{0};
    std::atomic<bool> event_thread_stop_{false};
    std::thread event_thread_;
    bpf_object *bpf_object_ = nullptr;
    std::vector<bpf_link *> links_;
    ring_buffer *ring_buffer_ = nullptr;
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
    registry.register_factory("network_xdp", [] {
        return std::make_unique<NetworkXdpSkill>();
    });
    registry.register_factory("security_policy", [] {
        return std::make_unique<SecurityPolicyDemoSkill>("security_policy");
    });
    registry.register_factory("security_policy_demo", [] {
        return std::make_unique<SecurityPolicyDemoSkill>();
    });
}

} // namespace eulerpilot
