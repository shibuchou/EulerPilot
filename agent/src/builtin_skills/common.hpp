#pragma once

// Shared implementation helpers for built-in Skill adapters.
// Keep this header private to agent/src/builtin_skills/*.cpp; public Skill
// interfaces stay in agent/include/.
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
#include <cstdio>
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

#if defined(__GNUC__)
// This private helper header is included by several Skill implementation
// units. Some helpers are intentionally used by only one Skill, so suppress the
// compiler warning locally instead of exporting unrelated declarations.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

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

ResourceControlTargetSpec parse_resource_control_target(const SkillSpec &spec,
                                                        const std::string &prefix,
                                                        const std::string &ref) {
    ResourceControlTargetSpec target;
    target.ref = ref;
    target.type = config_value_or(spec, prefix + "type", "");
    target.path = config_value_or(spec, prefix + "path", "");
    target.cgroup_path = config_value_or(spec, prefix + "cgroup_path", "");
    target.cgroup_root = config_value_or(spec, prefix + "cgroup_root", target.cgroup_root);
    target.container_id = config_value_or(spec, prefix + "container_id", "");
    target.container_name =
        config_value_or(spec, prefix + "container_name",
                        config_value_or(spec, prefix + "container", ""));
    target.runtime = config_value_or(spec, prefix + "runtime", target.runtime);
    target.pod_namespace =
        config_value_or(spec, prefix + "namespace",
                        config_value_or(spec, prefix + "pod_namespace", ""));
    target.pod_name =
        config_value_or(spec, prefix + "pod_name",
                        config_value_or(spec, prefix + "pod", ""));
    target.pod_uid = config_value_or(spec, prefix + "pod_uid", "");
    target.crictl_path = config_value_or(spec, prefix + "crictl_path", target.crictl_path);
    target.docker_path = config_value_or(spec, prefix + "docker_path", target.docker_path);
    target.podman_path = config_value_or(spec, prefix + "podman_path", target.podman_path);
    target.isula_path = config_value_or(spec, prefix + "isula_path", target.isula_path);
    target.kubectl_path = config_value_or(spec, prefix + "kubectl_path", target.kubectl_path);
    target.lab_namespace = config_value_or(spec, prefix + "lab_namespace", target.lab_namespace);
    target.allow_non_lab_pods =
        config_bool_or(spec, prefix + "allow_non_lab_pods", target.allow_non_lab_pods);
    target.require_runtime_socket =
        config_bool_or(spec, prefix + "require_runtime_socket", target.require_runtime_socket);
    if (!config_value_or(spec, prefix + "pid", "").empty() &&
        !parse_pid_value(config_value_or(spec, prefix + "pid", ""), target.pid)) {
        target.type.clear();
    }
    return target;
}

void add_resource_target_if_valid(ResourceControlPolicy &policy,
                                  const ResourceControlTargetSpec &target) {
    if (target.ref.empty() || target.type.empty()) {
        return;
    }
    for (const auto &existing : policy.targets) {
        if (existing.ref == target.ref) {
            return;
        }
    }
    policy.targets.push_back(target);
}

ResourceControlPolicy parse_resource_control_policy(const RuntimeConfig &runtime_config,
                                                    const SkillSpec &spec) {
    ResourceControlPolicy policy;
    policy.mode = config_value_or(spec, "mode", runtime_config.dry_run ? "audit" : "enforce");

    policy.cpu_max_enabled = config_bool_or(spec, "controllers.cpu.max.enabled", policy.cpu_max_enabled);
    policy.memory_enabled = config_bool_or(spec, "controllers.memory.enabled", policy.memory_enabled);
    policy.memory_high_enabled = config_bool_or(spec, "controllers.memory.high.enabled", policy.memory_high_enabled);
    policy.memory_low_enabled = config_bool_or(spec, "controllers.memory.low.enabled", policy.memory_low_enabled);
    policy.memory_max_enabled = config_bool_or(spec, "controllers.memory.max.enabled", policy.memory_max_enabled);
    policy.memory_reclaim_enabled = config_bool_or(spec, "controllers.memory.reclaim.enabled", policy.memory_reclaim_enabled);
    policy.io_enabled = config_bool_or(spec, "controllers.io.enabled", policy.io_enabled);
    policy.io_weight_enabled = config_bool_or(spec, "controllers.io.weight.enabled", policy.io_weight_enabled);
    policy.io_max_enabled = config_bool_or(spec, "controllers.io.max.enabled", policy.io_max_enabled);
    policy.io_device = config_value_or(spec, "controllers.io.device", policy.io_device);

    policy.latency_cpu_max =
        config_value_or(spec, "profiles.latency.cpu_max",
                        config_value_or(spec, "profiles.latency.pressure.cpu_max", policy.latency_cpu_max));
    policy.batch_cpu_max =
        config_value_or(spec, "profiles.batch.cpu_max",
                        config_value_or(spec, "profiles.batch.pressure.cpu_max", policy.batch_cpu_max));
    policy.background_cpu_max_normal =
        config_value_or(spec, "profiles.background.normal.cpu_max", policy.background_cpu_max_normal);
    policy.background_cpu_max_pressure =
        config_value_or(spec, "profiles.background.pressure.cpu_max", policy.background_cpu_max_pressure);

    policy.latency_memory_high =
        config_value_or(spec, "profiles.latency.memory_high",
                        config_value_or(spec, "profiles.latency.pressure.memory_high", policy.latency_memory_high));
    policy.latency_memory_low =
        config_value_or(spec, "profiles.latency.memory_low",
                        config_value_or(spec, "profiles.latency.pressure.memory_low", policy.latency_memory_low));
    policy.latency_memory_max =
        config_value_or(spec, "profiles.latency.memory_max",
                        config_value_or(spec, "profiles.latency.pressure.memory_max", policy.latency_memory_max));

    policy.batch_memory_high =
        config_value_or(spec, "profiles.batch.memory_high",
                        config_value_or(spec, "profiles.batch.pressure.memory_high", policy.batch_memory_high));
    policy.batch_memory_low =
        config_value_or(spec, "profiles.batch.memory_low",
                        config_value_or(spec, "profiles.batch.pressure.memory_low", policy.batch_memory_low));
    policy.batch_memory_max =
        config_value_or(spec, "profiles.batch.memory_max",
                        config_value_or(spec, "profiles.batch.pressure.memory_max", policy.batch_memory_max));

    policy.background_memory_high_normal =
        config_value_or(spec, "profiles.background.normal.memory_high", policy.background_memory_high_normal);
    policy.background_memory_high_pressure =
        config_value_or(spec, "profiles.background.pressure.memory_high", policy.background_memory_high_pressure);
    policy.background_memory_low =
        config_value_or(spec, "profiles.background.memory_low",
                        config_value_or(spec, "profiles.background.pressure.memory_low", policy.background_memory_low));
    policy.background_memory_max =
        config_value_or(spec, "profiles.background.memory_max",
                        config_value_or(spec, "profiles.background.pressure.memory_max", policy.background_memory_max));
    policy.background_memory_reclaim_pressure =
        config_value_or(spec, "profiles.background.pressure.memory_reclaim", policy.background_memory_reclaim_pressure);
    policy.latency_io_weight =
        config_value_or(spec, "profiles.latency.io_weight",
                        config_value_or(spec, "profiles.latency.pressure.io_weight", policy.latency_io_weight));
    policy.latency_io_max =
        config_value_or(spec, "profiles.latency.io_max",
                        config_value_or(spec, "profiles.latency.pressure.io_max", policy.latency_io_max));
    policy.batch_io_weight =
        config_value_or(spec, "profiles.batch.io_weight",
                        config_value_or(spec, "profiles.batch.pressure.io_weight", policy.batch_io_weight));
    policy.batch_io_max =
        config_value_or(spec, "profiles.batch.io_max",
                        config_value_or(spec, "profiles.batch.pressure.io_max", policy.batch_io_max));
    policy.background_io_weight_normal =
        config_value_or(spec, "profiles.background.normal.io_weight", policy.background_io_weight_normal);
    policy.background_io_weight_pressure =
        config_value_or(spec, "profiles.background.pressure.io_weight", policy.background_io_weight_pressure);
    policy.background_io_max_normal =
        config_value_or(spec, "profiles.background.normal.io_max", policy.background_io_max_normal);
    policy.background_io_max_pressure =
        config_value_or(spec, "profiles.background.pressure.io_max", policy.background_io_max_pressure);

    policy.latency_target_ref =
        config_value_or(spec, "profiles.latency.target_ref", policy.latency_target_ref);
    policy.batch_target_ref =
        config_value_or(spec, "profiles.batch.target_ref", policy.batch_target_ref);
    policy.background_target_ref =
        config_value_or(spec, "profiles.background.target_ref",
                        config_value_or(spec, "profiles.background.normal.target_ref",
                                        policy.background_target_ref));
    policy.background_target_ref =
        config_value_or(spec, "profiles.background.pressure.target_ref",
                        policy.background_target_ref);

    add_resource_target_if_valid(
        policy,
        parse_resource_control_target(spec, "targets." + policy.latency_target_ref + ".",
                                      policy.latency_target_ref));
    add_resource_target_if_valid(
        policy,
        parse_resource_control_target(spec, "targets." + policy.batch_target_ref + ".",
                                      policy.batch_target_ref));
    add_resource_target_if_valid(
        policy,
        parse_resource_control_target(spec, "targets." + policy.background_target_ref + ".",
                                      policy.background_target_ref));

    for (int i = 0; i < 32; ++i) {
        const std::string prefix = "targets." + std::to_string(i) + ".";
        add_resource_target_if_valid(
            policy,
            parse_resource_control_target(spec, prefix, config_value_or(spec, prefix + "name", "")));
    }
    return policy;
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
        options.isula_path =
            config_value_or(spec, target_prefix + "isula_path", "isula");
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
        target_spec.isula_path =
            config_value_or(spec, target_prefix + "isula_path", "isula");

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
        options.isula_path = target_spec.isula_path;
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
    std::uint32_t src_addr = 0;
    std::uint32_t dst_addr = 0;
    std::uint16_t src_port = 0;
    std::uint16_t dst_port = 0;
    std::uint8_t protocol = 0;
    std::uint8_t action = 1;
    std::uint8_t enabled = 0;
    std::uint8_t reserved = 0;
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

using NetworkXdpRuleStats = std::vector<NetworkXdpStats>;

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
    std::uint32_t hook_type = 0;
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
    std::string target_ref;
    std::string path_prefix;
    std::string comm;
    std::string comm_prefix;
    std::int32_t capability = -1;
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
    std::uint32_t group_count = 0;
    std::uint32_t old_group_count = 0;
    std::uint32_t cred_gfp = 0;
};

struct NetworkXdpRule {
    std::string rule_id;
    std::string protocol;
    std::string src_ip;
    std::string dst_ip;
    std::string src_port = "0";
    std::string dst_port;
    std::string action;
    std::string target_ref;
    std::uint32_t src_addr_value = 0;
    std::uint32_t dst_addr_value = 0;
    std::uint16_t src_port_value = 0;
    std::uint16_t dst_port_value = 0;
    std::uint8_t protocol_value = 0;
    std::uint8_t action_value = 1;
};

constexpr std::size_t kNetworkXdpMaxRules = 8;
// Keep the Security target map intentionally small. Each entry is 1056 bytes in
// the BPF ARRAY map, so eight entries cover the current demo/competition rules
// while keeping verifier state and map memory bounded on small SP3/SP4 VMs.
constexpr std::size_t kSecurityPolicyMaxTargets = 8;
constexpr std::uint32_t kSecurityTargetUnknown = 0xffffffffu;

static_assert(sizeof(NetworkXdpConfig) == 16,
              "network xdp config map layout must match BPF");
static_assert(sizeof(NetworkXdpStats) == 24,
              "network xdp stats map layout must match BPF");
static_assert(sizeof(SecurityPolicyConfig) == 8,
              "security policy config map layout must match BPF");
static_assert(sizeof(SecurityPolicyTarget) == 1056,
              "security policy target map layout must match BPF");
static_assert(sizeof(SecurityPolicyEvent) == 360,
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

bool has_prefix(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool is_lab_netdev_name(const std::string &ifname) {
    return has_prefix(ifname, "ep-") || has_prefix(ifname, "eulerpilot-") ||
           has_prefix(ifname, "lab-");
}

bool is_denied_host_netdev_name(const std::string &ifname) {
    return has_prefix(ifname, "eth") || has_prefix(ifname, "ens") ||
           has_prefix(ifname, "eno") || has_prefix(ifname, "wlan") ||
           has_prefix(ifname, "bond") || has_prefix(ifname, "br") ||
           has_prefix(ifname, "cni") || has_prefix(ifname, "flannel");
}

bool is_allowed_lab_netdev_name(const std::string &ifname) {
    return is_lab_netdev_name(ifname) && !is_denied_host_netdev_name(ifname);
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


} // namespace
} // namespace eulerpilot

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
