#include "executors.hpp"

#include "action_journal.hpp"
#include "audit_bus.hpp"
#include "target_resolver.hpp"

#include <bpf/bpf.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <signal.h>
#include <sstream>
#include <unordered_set>
#include <cerrno>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace eulerpilot {

namespace {

namespace fs = std::filesystem;

constexpr const char *kCgroupRoot = "/sys/fs/cgroup/eulerpilot";
constexpr const char *kResourceAuditPath = "reports/events/resource_control.jsonl";
constexpr const char *kResourceJournalPath = "run/eulerpilot/action_journal.jsonl";

struct ControlFileSnapshot {
    std::string path;
    std::string group;
    std::string file;
    std::string old_value;
    std::string restore_value;
    std::string target_ref;
};

void append_scx_session_log(const std::string &message) {
    std::ofstream out("/tmp/eulerpilot-scx-session.log", std::ios::app);
    if (!out.good()) {
        return;
    }
    out << message << '\n';
}

struct ProfileSettings {
    const char *name;
    const char *group;
    int cpu_weight;
    const char *cpuset_cpus;
};

struct ResourceSettings {
    std::string cpu_max = "max";
    std::string memory_high = "max";
    std::string memory_low = "0";
    std::string memory_max = "max";
    std::string memory_reclaim;
    std::string io_weight;
    std::string io_max;
    std::string mode = "normal";
};

int get_env_int(const char *name, int fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return std::atoi(value);
}

bool file_exists(const char *path) {
    std::ifstream file(path);
    return file.good();
}

bool executable_file(const std::string &path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

std::string read_file_trimmed(const char *path) {
    std::ifstream file(path);
    if (!file.good()) {
        return "";
    }

    std::string value;
    std::getline(file, value);
    return value;
}

bool read_control_file(const std::string &path, std::string &value) {
    std::ifstream file(path);
    if (!file.good()) {
        return false;
    }
    std::getline(file, value);
    return !file.bad();
}

bool write_pid_to_group(const std::string &group, int pid) {
    std::ofstream procs("/sys/fs/cgroup/eulerpilot/" + group + "/cgroup.procs");
    if (!procs.good()) {
        return false;
    }
    procs << pid;
    return procs.good();
}

std::string read_pid_cgroup_path(int pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/cgroup");
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.rfind(':');
        if (pos != std::string::npos && pos + 1 < line.size()) {
            const std::string relative = line.substr(pos + 1);
            return relative.empty() || relative == "/"
                ? std::string("/sys/fs/cgroup")
                : std::string("/sys/fs/cgroup") + relative;
        }
    }
    return {};
}

std::map<std::string, ControlFileSnapshot> &resource_control_snapshots() {
    static std::map<std::string, ControlFileSnapshot> snapshots;
    return snapshots;
}

std::string now_event_timestamp() {
    return std::to_string(static_cast<long long>(std::time(nullptr)));
}

void ensure_parent_dir(const fs::path &path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
}

std::string control_file_path(const std::string &cgroup_path, const std::string &file) {
    return cgroup_path + "/" + file;
}

bool valid_io_device(const std::string &value);

std::string root_block_device() {
    struct stat st {};
    if (stat("/", &st) != 0) {
        return "";
    }
    return std::to_string(major(st.st_dev)) + ":" + std::to_string(minor(st.st_dev));
}

std::string resolve_io_value_device(const std::string &value, const std::string &configured_device) {
    if (value.empty()) {
        return value;
    }

    std::istringstream in(value);
    std::string device;
    in >> device;
    if (device != "auto") {
        return value;
    }

    std::string resolved = configured_device == "auto" ? root_block_device() : configured_device;
    if (!valid_io_device(resolved)) {
        return value;
    }

    std::string rest;
    std::getline(in, rest);
    if (!rest.empty() && rest[0] == ' ') {
        rest.erase(0, 1);
    }
    return rest.empty() ? resolved : resolved + " " + rest;
}

bool valid_resource_group(const std::string &group) {
    return group == "latency" || group == "batch" || group == "background";
}

bool valid_resource_cgroup_path(const std::string &path) {
    if (path.rfind("/sys/fs/cgroup/", 0) != 0 && path != "/sys/fs/cgroup") {
        return false;
    }
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool write_file_value(const std::string &path, const std::string &value) {
    std::ofstream out(path);
    if (!out.good()) {
        return false;
    }
    out << value;
    return out.good();
}

bool parse_unsigned_value(const std::string &value, bool allow_zero) {
    if (value.empty()) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        return false;
    }
    return allow_zero || parsed > 0;
}

bool validate_cpu_max_value(const std::string &value) {
    if (value == "max") {
        return true;
    }

    std::istringstream in(value);
    std::string quota;
    std::string period;
    std::string extra;
    if (!(in >> quota >> period) || (in >> extra)) {
        return false;
    }
    return (quota == "max" || parse_unsigned_value(quota, false)) &&
           parse_unsigned_value(period, false);
}

bool valid_io_device(const std::string &value) {
    const auto sep = value.find(':');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= value.size()) {
        return false;
    }
    return parse_unsigned_value(value.substr(0, sep), true) &&
           parse_unsigned_value(value.substr(sep + 1), true);
}

bool valid_io_limit_key(const std::string &key) {
    return key == "rbps" || key == "wbps" || key == "riops" || key == "wiops";
}

bool validate_io_max_value(const std::string &value) {
    std::istringstream in(value);
    std::string device;
    if (!(in >> device) || !valid_io_device(device)) {
        return false;
    }

    bool has_limit = false;
    std::string token;
    while (in >> token) {
        const auto sep = token.find('=');
        if (sep == std::string::npos || sep == 0 || sep + 1 >= token.size()) {
            return false;
        }
        const std::string key = token.substr(0, sep);
        const std::string limit = token.substr(sep + 1);
        if (!valid_io_limit_key(key)) {
            return false;
        }
        if (limit != "max" && !parse_unsigned_value(limit, false)) {
            return false;
        }
        has_limit = true;
    }
    return has_limit;
}

bool validate_io_weight_value(const std::string &value) {
    std::istringstream in(value);
    std::string scope;
    std::string weight;
    std::string extra;
    if (!(in >> scope >> weight) || (in >> extra)) {
        return false;
    }
    if (scope != "default" && !valid_io_device(scope)) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(weight.c_str(), &end, 10);
    return errno == 0 && end != weight.c_str() && *end == '\0' &&
           parsed >= 1 && parsed <= 10000;
}

bool validate_control_value(const std::string &file, const std::string &value) {
    if (value.empty()) {
        return false;
    }
    if (file == "cpu.weight") {
        char *end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        return errno == 0 && end != value.c_str() && *end == '\0' &&
               parsed >= 1 && parsed <= 10000;
    }
    if (file == "cpu.max") {
        return validate_cpu_max_value(value);
    }
    if (file == "memory.high" || file == "memory.max") {
        return value == "max" || parse_unsigned_value(value, true);
    }
    if (file == "memory.low") {
        return parse_unsigned_value(value, true);
    }
    if (file == "memory.reclaim") {
        return parse_unsigned_value(value, false);
    }
    if (file == "io.max") {
        return validate_io_max_value(value);
    }
    if (file == "io.weight") {
        return validate_io_weight_value(value);
    }
    return !value.empty();
}

std::string first_token(const std::string &value) {
    std::istringstream in(value);
    std::string token;
    in >> token;
    return token;
}

std::string io_max_unlimited_value(const std::string &value) {
    const std::string device = first_token(value);
    if (!valid_io_device(device)) {
        return "";
    }
    return device + " rbps=max wbps=max riops=max wiops=max";
}

bool control_value_matches(const std::string &file,
                           const std::string &desired,
                           const std::string &actual) {
    if (desired == actual) {
        return true;
    }
    if (file == "cpu.max" && desired == "max") {
        return actual == "max" || actual.rfind("max ", 0) == 0;
    }
    if (file == "io.max") {
        const std::string desired_device = first_token(desired);
        const std::string actual_device = first_token(actual);
        if (desired_device.empty() || desired_device != actual_device) {
            return false;
        }

        std::istringstream in(desired);
        std::string token;
        in >> token; // device
        while (in >> token) {
            if (actual.find(token) == std::string::npos) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void append_resource_audit(const std::string &group,
                           const std::string &cgroup_path,
                           const std::string &target_ref,
                           const std::string &file,
                           const std::string &old_value,
                           const std::string &new_value,
                           const std::string &mode,
                           const std::string &result,
                           const std::string &reason,
                           int pid,
                           const std::string &profile) {
    ensure_parent_dir(kResourceAuditPath);

    AuditEvent event;
    event.timestamp = now_event_timestamp();
    event.event_id = "resource-control-" + event.timestamp + "-" + group + "-" + file + "-" + std::to_string(pid);
    event.skill = "resource_control";
    event.policy_id = "cgroup-v2-cpu-memory-io";
    event.rule_id = group + "." + file;
    event.mode = mode;
    event.target = {
        {"cgroup", cgroup_path},
        {"file", file},
        {"pid", std::to_string(pid)},
        {"profile", profile},
        {"target_ref", target_ref},
    };
    event.operation = "write-cgroup-file";
    event.evidence = {
        {"old_value", old_value},
        {"new_value", new_value},
        {"reason", reason},
    };
    event.action = "set-" + file;
    event.result = result;
    event.severity = result == "failed" ? "warning" : "info";

    std::string error;
    append_audit_event(kResourceAuditPath, event, &error);
}

void append_resource_journal(const std::string &group,
                             const std::string &file,
                             const std::string &path,
                             const std::string &old_value,
                             const std::string &new_value,
                             int pid,
                             const std::string &target_ref) {
    ensure_parent_dir(kResourceJournalPath);

    JournalAction action;
    action.action_id = "resource-control-" + now_event_timestamp() + "-" + group + "-" + file + "-" + std::to_string(pid);
    action.skill = "resource_control";
    action.target = path;
    action.operation = "write-cgroup-file";
    action.old_values = {{file, old_value}};
    action.new_values = {{file, new_value}};
    action.handles = {
        {"group", group},
        {"pid", std::to_string(pid)},
        {"path", path},
        {"target_ref", target_ref},
    };
    action.restored = false;

    std::string error;
    append_journal_action(kResourceJournalPath, action, &error);
}

bool apply_control_file_at(const std::string &group,
                           const std::string &cgroup_path,
                           const std::string &target_ref,
                           const std::string &file,
                           const std::string &value,
                           const std::string &mode,
                           int pid,
                           const std::string &profile,
                           std::string &reason) {
    if (!valid_resource_group(group)) {
        reason = "unsupported-cgroup";
        return false;
    }
    if (!valid_resource_cgroup_path(cgroup_path)) {
        reason = "unsupported-cgroup-path";
        return false;
    }
    if (!validate_control_value(file, value)) {
        reason = "invalid-control-value";
        append_resource_audit(group, cgroup_path, target_ref, file, "", value, mode,
                              "failed", reason, pid, profile);
        return false;
    }

    const std::string path = control_file_path(cgroup_path, file);
    const bool write_only = file == "memory.reclaim";
    std::string old_value;
    if (!write_only && !read_control_file(path, old_value)) {
        reason = "control-file-read-failed";
        append_resource_audit(group, cgroup_path, target_ref, file, "", value, mode,
                              "failed", reason, pid, profile);
        return false;
    }

    if (mode != "enforce") {
        reason = "audit-only";
        append_resource_audit(group, cgroup_path, target_ref, file, old_value, value,
                              mode, "audit-only", reason, pid, profile);
        return true;
    }

    if (!write_only && control_value_matches(file, value, old_value)) {
        reason = "unchanged";
        return true;
    }

    const std::string key = path;
    if (!write_only && resource_control_snapshots().find(key) == resource_control_snapshots().end()) {
        const std::string restore_value = file == "io.max" && old_value.empty()
            ? io_max_unlimited_value(value)
            : old_value;
        resource_control_snapshots()[key] = {
            path, group, file, old_value, restore_value, target_ref};
    }

    if (!write_file_value(path, value)) {
        reason = "control-file-write-failed";
        append_resource_audit(group, cgroup_path, target_ref, file, old_value, value,
                              mode, "failed", reason, pid, profile);
        return false;
    }

    if (!write_only) {
        std::string verified;
        if (!read_control_file(path, verified)) {
            reason = "control-file-verify-failed";
            append_resource_audit(group, cgroup_path, target_ref, file, old_value, value,
                                  mode, "failed", reason, pid, profile);
            return false;
        }
        if (!control_value_matches(file, value, verified)) {
            reason = "control-file-verify-failed";
            append_resource_audit(group, cgroup_path, target_ref, file, old_value, value,
                                  mode, "failed", reason, pid, profile);
            return false;
        }
    }

    reason = "applied";
    append_resource_audit(group, cgroup_path, target_ref, file, old_value, value,
                          mode, "applied", reason, pid, profile);
    append_resource_journal(group, file, path, old_value, value, pid, target_ref);
    return true;
}

bool is_benchmark_client(const WorkloadSample &sample) {
    return sample.comm.find("redis-benchmark") != std::string::npos ||
           sample.comm == "wrk";
}

bool looks_latency_service(const WorkloadSample &sample) {
    return sample.comm.find("redis-server") != std::string::npos ||
           sample.comm.find("nginx") != std::string::npos;
}

bool looks_background_noisy(const WorkloadSample &sample) {
    return sample.comm.find("stress") != std::string::npos ||
           sample.comm == "yes" ||
           sample.comm == "sleep";
}

bool looks_batch(const WorkloadSample &sample) {
    if (is_benchmark_client(sample)) {
        return false;
    }
    return sample.comm.find("make") != std::string::npos ||
           sample.comm.find("sysbench") != std::string::npos ||
           sample.comm.find("bench") != std::string::npos;
}

ProfileSettings select_profile_settings(const WorkloadDecision &decision) {
    const int latency_weight = get_env_int("EULERPILOT_LATENCY_WEIGHT", 1000);
    const int batch_weight = get_env_int("EULERPILOT_BATCH_WEIGHT", 100);
    const int background_weight = get_env_int("EULERPILOT_BACKGROUND_WEIGHT", 5);
    const int light_background_weight = get_env_int("EULERPILOT_BACKGROUND_LIGHT_WEIGHT", 100);

    if (decision.target_profile == "mixed_profile") {
        return {"mixed_profile", "background", background_weight, "4-7"};
    }

    if (decision.target_profile == "latency_profile") {
        if (decision.klass == WorkloadClass::LatencySensitive) {
            return {"latency_profile", "latency", latency_weight, "0-1"};
        }
        if (decision.klass == WorkloadClass::BackgroundNoisy || decision.klass == WorkloadClass::ThroughputBatch) {
            return {"latency_profile", "background", light_background_weight, "4-7"};
        }
    }

    if (decision.target_profile == "throughput_profile") {
        return {"throughput_profile", "batch", batch_weight, "2-3"};
    }

    return {"normal_profile", "", 100, ""};
}

ResourceSettings select_resource_settings(const ResourceControlPolicy &policy,
                                          const ProfileSettings &profile,
                                          bool pressure_mode) {
    ResourceSettings settings;
    settings.mode = pressure_mode ? "pressure" : "normal";

    if (std::strcmp(profile.group, "latency") == 0) {
        settings.cpu_max = policy.latency_cpu_max;
        settings.memory_high = policy.latency_memory_high;
        settings.memory_low = policy.latency_memory_low;
        settings.memory_max = policy.latency_memory_max;
        settings.io_weight = policy.latency_io_weight;
        settings.io_max = policy.latency_io_max;
        return settings;
    }

    if (std::strcmp(profile.group, "batch") == 0) {
        settings.cpu_max = policy.batch_cpu_max;
        settings.memory_high = policy.batch_memory_high;
        settings.memory_low = policy.batch_memory_low;
        settings.memory_max = policy.batch_memory_max;
        settings.io_weight = policy.batch_io_weight;
        settings.io_max = policy.batch_io_max;
        return settings;
    }

    if (std::strcmp(profile.group, "background") == 0) {
        settings.cpu_max = pressure_mode ? policy.background_cpu_max_pressure
                                         : policy.background_cpu_max_normal;
        settings.memory_high = pressure_mode ? policy.background_memory_high_pressure
                                             : policy.background_memory_high_normal;
        settings.memory_low = policy.background_memory_low;
        settings.memory_max = policy.background_memory_max;
        settings.memory_reclaim = pressure_mode ? policy.background_memory_reclaim_pressure : "";
        settings.io_weight = pressure_mode ? policy.background_io_weight_pressure
                                           : policy.background_io_weight_normal;
        settings.io_max = pressure_mode ? policy.background_io_max_pressure
                                        : policy.background_io_max_normal;
        return settings;
    }

    return settings;
}

const ResourceControlTargetSpec *find_resource_target(const ResourceControlPolicy &policy,
                                                      const std::string &target_ref) {
    for (const auto &target : policy.targets) {
        if (target.ref == target_ref) {
            return &target;
        }
    }
    return nullptr;
}

std::string target_ref_for_group(const ResourceControlPolicy &policy,
                                 const std::string &group) {
    if (group == "latency") {
        return policy.latency_target_ref;
    }
    if (group == "batch") {
        return policy.batch_target_ref;
    }
    if (group == "background") {
        return policy.background_target_ref;
    }
    return {};
}

TargetIdentity resolve_resource_target(const ResourceControlTargetSpec &spec) {
    if (spec.type == "cgroup") {
        const std::string path = !spec.cgroup_path.empty() ? spec.cgroup_path : spec.path;
        return resolve_cgroup_target(spec.ref, path);
    }

    if (spec.type == "pid") {
        return resolve_pid_target(spec.pid);
    }

    if (spec.type == "container_id") {
        return resolve_container_target(spec.ref, spec.container_id, spec.cgroup_root);
    }

    if (spec.type == "container") {
        ContainerTargetSpec container;
        container.name = spec.ref;
        container.container_id = spec.container_id;
        container.container_name = spec.container_name;
        container.runtime = spec.runtime;
        container.cgroup_root = spec.cgroup_root;
        container.crictl_path = spec.crictl_path;
        container.docker_path = spec.docker_path;
        container.podman_path = spec.podman_path;
        container.isula_path = spec.isula_path;
        return resolve_container_target(container);
    }

    if (spec.type == "k8s_pod" || spec.type == "pod") {
        K8sPodTargetSpec pod;
        pod.name = spec.ref;
        pod.pod_namespace = spec.pod_namespace;
        pod.pod_name = spec.pod_name;
        pod.pod_uid = spec.pod_uid;
        pod.container_id = spec.container_id;
        pod.container_name = spec.container_name;
        pod.cgroup_root = spec.cgroup_root;

        TargetResolverOptions options;
        options.allow_non_lab_pods = spec.allow_non_lab_pods;
        options.require_runtime_socket = spec.require_runtime_socket;
        options.lab_namespace = spec.lab_namespace;
        options.kubectl_path = spec.kubectl_path;
        options.crictl_path = spec.crictl_path;
        options.docker_path = spec.docker_path;
        options.podman_path = spec.podman_path;
        options.isula_path = spec.isula_path;
        return resolve_k8s_pod_cgroup_target(pod, options);
    }

    TargetIdentity target;
    target.name = spec.ref;
    target.type = spec.type;
    target.reason = spec.type.empty() ? "missing-target-type" : "unsupported-target-type";
    return target;
}

bool path_is_at_or_under(const std::string &path, const std::string &root) {
    if (path == root) {
        return true;
    }
    if (root.empty() || path.size() <= root.size() ||
        path.compare(0, root.size(), root) != 0) {
        return false;
    }
    return root.back() == '/' || path[root.size()] == '/';
}

struct ResourceApplyTarget {
    std::string group;
    std::string cgroup_path;
    std::string target_ref;
    std::string reason = "ok";
    bool explicit_target = false;
    bool resolved = true;
    bool sample_matches = true;
};

ResourceApplyTarget select_resource_apply_target(const ResourceControlPolicy &policy,
                                                 const ProfileSettings &profile,
                                                 const WorkloadSample &sample) {
    ResourceApplyTarget target;
    target.group = profile.group;
    target.cgroup_path = std::string(kCgroupRoot) + "/" + profile.group;
    target.target_ref = target_ref_for_group(policy, profile.group);

    if (target.target_ref.empty()) {
        return target;
    }

    target.explicit_target = true;
    const ResourceControlTargetSpec *spec = find_resource_target(policy, target.target_ref);
    if (!spec) {
        target.resolved = false;
        target.sample_matches = false;
        target.reason = "target-ref-not-found";
        return target;
    }

    const TargetIdentity identity = resolve_resource_target(*spec);
    if (!identity.resolved || identity.cgroup_path.empty()) {
        target.resolved = false;
        target.sample_matches = false;
        target.reason = identity.reason.empty() ? "target-not-resolved" : identity.reason;
        return target;
    }

    target.cgroup_path = identity.cgroup_path;
    if (identity.cgroup_id != 0 && sample.cgroup_id == identity.cgroup_id) {
        return target;
    }
    if (identity.pid > 0 && (sample.pid == identity.pid || sample.tgid == identity.pid)) {
        return target;
    }

    const std::string sample_cgroup_path = read_pid_cgroup_path(sample.pid);
    if (!sample_cgroup_path.empty() &&
        path_is_at_or_under(sample_cgroup_path, identity.cgroup_path)) {
        return target;
    }

    target.sample_matches = false;
    target.reason = "target-scope-mismatch";
    return target;
}

std::string find_in_path(const std::string &name) {
    const char *path_env = std::getenv("PATH");
    if (!path_env || !*path_env) {
        return "";
    }
    std::stringstream stream(path_env);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
        if (dir.empty()) {
            continue;
        }
        fs::path candidate = fs::path(dir) / name;
        if (executable_file(candidate.string())) {
            return candidate.string();
        }
    }
    return "";
}

ScxBinaryChoice make_scx_choice(std::string path, std::string source) {
    ScxBinaryChoice choice;
    choice.path = std::move(path);
    choice.source = std::move(source);
    choice.executable = executable_file(choice.path);
    return choice;
}

bool use_scx_fifo_mode() {
    return get_env_int("EULERPILOT_SCX_FIFO", 0) != 0;
}

std::string scx_class_map_path() {
    const char *value = std::getenv("EULERPILOT_SCX_CLASS_MAP");
    if (value && *value) {
        return value;
    }
    if (file_exists("/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map")) {
        return "/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map";
    }
    return "/sys/fs/bpf/class_map";
}

std::string scx_gate_state_map_path() {
    const char *value = std::getenv("EULERPILOT_GATE_STATE_MAP");
    if (value && *value) {
        return value;
    }
    if (file_exists("/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map")) {
        return "/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map";
    }
    return "/sys/fs/bpf/gate_state_map";
}

std::string scx_stats_map_path() {
    if (file_exists("/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats")) {
        return "/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats";
    }
    return "/sys/fs/bpf/stats";
}

bool ensure_scx_stats_map(ScxSession &session, std::string &reason) {
    if (session.stats_map_fd >= 0) {
        return true;
    }

    const std::string path = scx_stats_map_path();
    append_scx_session_log("ensure_scx_stats_map: trying path=" + path);
    session.stats_map_fd = bpf_obj_get(path.c_str());
    if (session.stats_map_fd < 0) {
        reason = "stats-map-not-pinned";
        append_scx_session_log("ensure_scx_stats_map: bpf_obj_get failed errno=" + std::to_string(errno));
        return false;
    }
    append_scx_session_log("ensure_scx_stats_map: success fd=" + std::to_string(session.stats_map_fd));
    return true;
}

__u32 decision_to_scx_class(const WorkloadDecision &decision) {
    switch (decision.klass) {
    case WorkloadClass::LatencySensitive:
        return 1;
    case WorkloadClass::ThroughputBatch:
        return 2;
    case WorkloadClass::BackgroundNoisy:
        return 3;
    default:
        return 0;
    }
}

bool is_process_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    return kill(pid, 0) == 0;
}

bool start_scx_session(const RuntimeConfig &config, ScxSession &session, bool fifo_mode, std::string &reason) {
    const ScxBinaryChoice binary = resolve_scx_binary(config);
    session.binary_path = binary.path;
    session.binary_source = binary.source;
    session.fifo_mode = fifo_mode;
    append_scx_session_log("start_scx_session: enter binary=" + session.binary_path +
                           " source=" + session.binary_source);

    if (!binary.executable) {
        reason = "scx-binary-not-executable:" + session.binary_source;
        append_scx_session_log("start_scx_session: " + reason);
        return false;
    }

    const int log_fd = open("/tmp/eulerpilot-scx.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        reason = "scx-log-open-failed";
        append_scx_session_log("start_scx_session: scx-log-open-failed");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(log_fd);
        reason = "scx-fork-failed";
        append_scx_session_log("start_scx_session: scx-fork-failed");
        return false;
    }

    if (pid == 0) {
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);

        if (fifo_mode) {
            execl(session.binary_path.c_str(), session.binary_path.c_str(), "-f", static_cast<char *>(nullptr));
        } else {
            execl(session.binary_path.c_str(), session.binary_path.c_str(), static_cast<char *>(nullptr));
        }
        _exit(127);
    }

    close(log_fd);
    session.pid = pid;
    append_scx_session_log("start_scx_session: forked pid=" + std::to_string(pid));

    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            session.pid = -1;
            reason = "scx-process-exited-early";
            append_scx_session_log("start_scx_session: scx-process-exited-early");
            return false;
        }

        if (read_file_trimmed("/sys/kernel/sched_ext/state") == "enabled") {
            reason = fifo_mode ? "scx-fifo-started" : "scx-started";
            append_scx_session_log("start_scx_session: enabled");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop_scx_session(session);
    reason = "scx-not-enabled";
    append_scx_session_log("start_scx_session: scx-not-enabled");
    return false;
}

bool ensure_scx_class_map(ScxSession &session, std::string &reason) {
    if (session.class_map_fd >= 0) {
        return true;
    }

    const std::string path = scx_class_map_path();
    session.class_map_fd = bpf_obj_get(path.c_str());
    if (session.class_map_fd < 0) {
        reason = "scx-class-map-missing";
        return false;
    }
    return true;
}

bool ensure_scx_gate_state_map(ScxSession &session, std::string &reason) {
    if (session.gate_state_map_fd >= 0) {
        return true;
    }

    const std::string path = scx_gate_state_map_path();
    session.gate_state_map_fd = bpf_obj_get(path.c_str());
    if (session.gate_state_map_fd < 0) {
        reason = "gate-state-map-not-pinned";
        return false;
    }
    return true;
}

bool tg_id_alive(pid_t tgid)
{
    if (tgid <= 0) {
        return false;
    }
    return kill(tgid, 0) == 0;
}

} // namespace

ScxBinaryChoice resolve_scx_binary(const RuntimeConfig &config) {
    if (!config.scheduler_binary_path.empty()) {
        return make_scx_choice(config.scheduler_binary_path, config.scheduler_binary_source.empty()
                                                         ? "yaml:scheduler.binary"
                                                         : config.scheduler_binary_source);
    }

    const char *env_binary = std::getenv("EULERPILOT_SCX_BINARY");
    if (env_binary && *env_binary) {
        return make_scx_choice(env_binary, "env:EULERPILOT_SCX_BINARY");
    }

    const std::string path_binary = find_in_path("scx_eulerpilot");
    if (!path_binary.empty()) {
        return make_scx_choice(path_binary, "PATH");
    }

    for (const auto &candidate : {
             std::string("/usr/local/bin/scx_eulerpilot"),
             std::string("/usr/bin/scx_eulerpilot"),
             (std::filesystem::current_path() / "build" / "scx_eulerpilot").string(),
             (std::filesystem::current_path() / "tools" / "sched_ext" / "build" / "bin" / "scx_eulerpilot").string(),
         }) {
        if (executable_file(candidate)) {
            return make_scx_choice(candidate, "auto");
        }
    }

    return make_scx_choice("scx_eulerpilot", "unresolved");
}

bool should_manage_sample(const WorkloadSample &sample) {
    if (is_benchmark_client(sample)) {
        return false;
    }
    return looks_latency_service(sample) || looks_background_noisy(sample) || looks_batch(sample);
}

void stop_scx_session(ScxSession &session) {
    if (session.pid <= 0) {
        return;
    }

    kill(session.pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t result = waitpid(session.pid, &status, WNOHANG);
        if (result == session.pid) {
            session.pid = -1;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    kill(session.pid, SIGKILL);
    waitpid(session.pid, nullptr, 0);
    session.pid = -1;
}

void close_scx_map(ScxSession &session) {
    if (session.class_map_fd >= 0) {
        close(session.class_map_fd);
        session.class_map_fd = -1;
    }
    if (session.gate_state_map_fd >= 0) {
        close(session.gate_state_map_fd);
        session.gate_state_map_fd = -1;
    }
    if (session.stats_map_fd >= 0) {
        close(session.stats_map_fd);
        session.stats_map_fd = -1;
    }
}

bool reconcile_scx_session(const RuntimeConfig &config, ControlMode mode, ScxSession &session, std::string &reason) {
    (void)mode;
    append_scx_session_log("reconcile_scx_session: enter backend=" + std::string(config.preferred_backend == ExecutorBackend::SchedExt ? "sched_ext" : "other") +
                           " dry_run=" + std::string(config.dry_run ? "true" : "false"));
    if (config.preferred_backend != ExecutorBackend::SchedExt) {
        reason = "backend-not-sched-ext";
        append_scx_session_log("reconcile_scx_session: backend-not-sched-ext");
        return false;
    }

    if (!file_exists("/sys/kernel/sched_ext")) {
        reason = "sched-ext-unavailable";
        append_scx_session_log("reconcile_scx_session: sched-ext-unavailable");
        return false;
    }

    const bool should_enable = !config.dry_run;
    const bool running = session.pid > 0 && is_process_alive(session.pid) &&
                         read_file_trimmed("/sys/kernel/sched_ext/state") == "enabled";

    if (!should_enable) {
        if (session.pid > 0) {
            stop_scx_session(session);
            reason = "scx-stopped";
            append_scx_session_log("reconcile_scx_session: scx-stopped");
        } else {
            reason = "scx-observe-only";
            append_scx_session_log("reconcile_scx_session: scx-observe-only");
        }
        close_scx_map(session);
        return false;
    }

    if (running) {
        std::string probe_reason;
        const bool class_map_ready = ensure_scx_class_map(session, probe_reason);
        const bool gate_map_ready = ensure_scx_gate_state_map(session, probe_reason);
        const bool stats_map_ready = ensure_scx_stats_map(session, probe_reason);
        append_scx_session_log("reconcile_scx_session: running state class_map=" +
                               std::string(class_map_ready ? "1" : "0") +
                               " gate_map=" + std::string(gate_map_ready ? "1" : "0") +
                               " stats_map=" + std::string(stats_map_ready ? "1" : "0"));

        if (!class_map_ready) {
            reason = "class-map-not-pinned";
            append_scx_session_log("reconcile_scx_session: class-map-not-pinned");
            return false;
        }
        if (!gate_map_ready) {
            reason = "gate-state-map-not-pinned";
            append_scx_session_log("reconcile_scx_session: gate-state-map-not-pinned");
            return false;
        }
        if (!stats_map_ready) {
            reason = "stats-map-not-pinned";
            append_scx_session_log("reconcile_scx_session: stats-map-not-pinned");
            return false;
        }
        reason = session.fifo_mode ? "scx-fifo-running" : "scx-running";
        append_scx_session_log("reconcile_scx_session: scx-running");
        return true;
    }

    if (!start_scx_session(config, session, use_scx_fifo_mode(), reason)) {
        append_scx_session_log("reconcile_scx_session: start failed reason=" + reason);
        return false;
    }

    for (int i = 0; i < 20; ++i) {
        const bool enabled = read_file_trimmed("/sys/kernel/sched_ext/state") == "enabled";
        std::string probe_reason;
        const bool class_map_ready = ensure_scx_class_map(session, probe_reason);
        const bool gate_map_ready = ensure_scx_gate_state_map(session, probe_reason);
        const bool stats_map_ready = ensure_scx_stats_map(session, probe_reason);
        append_scx_session_log("reconcile_scx_session: handshake iter=" + std::to_string(i) +
                               " enabled=" + std::string(enabled ? "1" : "0") +
                               " class_map=" + std::string(class_map_ready ? "1" : "0") +
                               " gate_map=" + std::string(gate_map_ready ? "1" : "0") +
                               " stats_map=" + std::string(stats_map_ready ? "1" : "0"));
        if (enabled && class_map_ready && gate_map_ready && stats_map_ready) {
            reason = "scx-session-ready";
            append_scx_session_log("reconcile_scx_session: scx-session-ready");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop_scx_session(session);
    close_scx_map(session);
    reason = "scx-session-handshake-failed";
    append_scx_session_log("reconcile_scx_session: scx-session-handshake-failed");
    return false;
}

bool update_scx_class_map(ScxSession &session, const std::vector<WorkloadDecision> &decisions, std::string &reason) {
    if (!ensure_scx_class_map(session, reason)) {
        return false;
    }

    std::unordered_set<__u32> live_keys;
    for (const auto &decision : decisions) {
        if (!should_manage_sample(decision.sample)) {
            continue;
        }
        __u32 key = static_cast<__u32>(decision.sample.tgid);
        __u32 value = decision_to_scx_class(decision);
        live_keys.insert(key);
        if (bpf_map_update_elem(session.class_map_fd, &key, &value, BPF_ANY) != 0) {
            reason = "scx-class-map-update-failed";
            return false;
        }
    }

    __u32 key = 0;
    __u32 next_key = 0;
    bool first = true;
    while (bpf_map_get_next_key(session.class_map_fd, first ? nullptr : &key, &next_key) == 0) {
        if (!live_keys.count(next_key) && !tg_id_alive(static_cast<pid_t>(next_key))) {
            bpf_map_delete_elem(session.class_map_fd, &next_key);
        }
        key = next_key;
        first = false;
    }

    reason = "scx-class-map-updated";
    return true;
}

bool update_scx_gate_state(ScxSession &session, GateState state, std::uint32_t generation,
                           std::uint64_t updated_at_ns, std::uint32_t evidence_mask,
                           std::string &reason) {
    struct gate_state_value {
        std::uint32_t state;
        std::uint32_t generation;
        std::uint64_t updated_at_ns;
        std::uint32_t evidence_mask;
        std::uint32_t reserved;
    };

    if (!ensure_scx_gate_state_map(session, reason)) {
        return false;
    }

    std::uint32_t key = 0;
    gate_state_value value = {
        static_cast<std::uint32_t>(state),
        generation,
        updated_at_ns,
        evidence_mask,
        0,
    };

    if (bpf_map_update_elem(session.gate_state_map_fd, &key, &value, BPF_ANY) != 0) {
        reason = "gate-state-map-init-failed";
        return false;
    }

    reason = "gate-state-map-updated";
    return true;
}

ExecutionAction apply_cgroup_assignment(const RuntimeConfig &config, const WorkloadDecision &decision) {
    ResourceControlPolicy policy;
    policy.mode = config.dry_run ? "audit" : "enforce";
    return apply_cgroup_assignment(config, decision, policy, decision.target_profile != "normal_profile");
}

ExecutionAction apply_cgroup_assignment(const RuntimeConfig &config,
                                        const WorkloadDecision &decision,
                                        const ResourceControlPolicy &policy,
                                        bool pressure_mode) {
    ExecutionAction action;
    action.executor = "observe-only";

    if (config.preferred_backend != ExecutorBackend::CgroupV2) {
        action.reason = "backend-not-cgroup";
        return action;
    }

    if (!should_manage_sample(decision.sample)) {
        action.reason = "non-target-process";
        return action;
    }

    auto profile = select_profile_settings(decision);
    action.target_profile = profile.name;
    action.cpu_weight = profile.cpu_weight;
    action.cpuset_cpus = profile.cpuset_cpus;
    const auto resources = select_resource_settings(policy, profile, pressure_mode);
    action.cpu_max = resources.cpu_max;
    action.memory_high = resources.memory_high;
    action.memory_low = resources.memory_low;
    action.memory_max = resources.memory_max;
    action.memory_reclaim = resources.memory_reclaim;
    action.io_weight = resolve_io_value_device(resources.io_weight, policy.io_device);
    action.io_max = resolve_io_value_device(resources.io_max, policy.io_device);
    action.resource_mode = resources.mode;

    if (std::strlen(profile.group) == 0) {
        action.reason = "no-group-for-class";
        return action;
    }

    const auto apply_target = select_resource_apply_target(policy, profile, decision.sample);
    action.target_ref = apply_target.target_ref;
    action.target_cgroup_path = apply_target.cgroup_path;
    if (!apply_target.resolved) {
        action.reason = apply_target.reason;
        return action;
    }
    if (!apply_target.sample_matches) {
        action.reason = apply_target.reason;
        return action;
    }

    action.executor = "cgroup_v2";
    action.target_group = profile.group;

    if (config.dry_run) {
        action.reason = "dry-run";
        return action;
    }

    if (policy.mode != "enforce") {
        action.reason = "audit-only";
        return action;
    }

    if (std::strlen(profile.cpuset_cpus) > 0) {
        std::string reason;
        if (!apply_control_file_at(profile.group, apply_target.cgroup_path,
                                   apply_target.target_ref, "cpuset.mems", "0",
                                   policy.mode, decision.sample.pid, profile.name, reason) ||
            !apply_control_file_at(profile.group, apply_target.cgroup_path,
                                   apply_target.target_ref, "cpuset.cpus",
                                   profile.cpuset_cpus, policy.mode,
                                   decision.sample.pid, profile.name, reason)) {
            action.reason = "cpuset-fallback-cpu-weight-only";
            action.cpuset_cpus.clear();
        }
    }

    std::string reason;
    if (!apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "cpu.weight",
                               std::to_string(profile.cpu_weight), policy.mode,
                               decision.sample.pid, profile.name, reason)) {
        action.reason = "cpu-weight-write-failed";
        return action;
    }

    if (policy.cpu_max_enabled &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "cpu.max", resources.cpu_max,
                               policy.mode, decision.sample.pid, profile.name, reason)) {
        action.reason = "cpu-max-write-failed";
        return action;
    }

    if (policy.memory_enabled && policy.memory_low_enabled &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "memory.low",
                               resources.memory_low, policy.mode, decision.sample.pid,
                               profile.name, reason)) {
        action.reason = "memory-low-write-failed";
        return action;
    }

    if (policy.memory_enabled && policy.memory_high_enabled &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "memory.high",
                               resources.memory_high, policy.mode, decision.sample.pid,
                               profile.name, reason)) {
        action.reason = "memory-high-write-failed";
        return action;
    }

    if (policy.memory_enabled && policy.memory_max_enabled &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "memory.max",
                               resources.memory_max, policy.mode, decision.sample.pid,
                               profile.name, reason)) {
        action.reason = "memory-max-write-failed";
        return action;
    }

    if (policy.memory_enabled && policy.memory_reclaim_enabled &&
        !resources.memory_reclaim.empty() &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "memory.reclaim",
                               resources.memory_reclaim, policy.mode,
                               decision.sample.pid, profile.name, reason)) {
        action.reason = "memory-reclaim-write-failed";
        return action;
    }

    if (policy.io_enabled && policy.io_weight_enabled && !action.io_weight.empty() &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "io.weight", action.io_weight,
                               policy.mode, decision.sample.pid, profile.name, reason)) {
        action.reason = "io-weight-write-failed";
        return action;
    }

    if (policy.io_enabled && policy.io_max_enabled && !action.io_max.empty() &&
        !apply_control_file_at(profile.group, apply_target.cgroup_path,
                               apply_target.target_ref, "io.max", action.io_max,
                               policy.mode, decision.sample.pid, profile.name, reason)) {
        action.reason = "io-max-write-failed";
        return action;
    }

    if (!apply_target.explicit_target && !write_pid_to_group(profile.group, decision.sample.pid)) {
        action.reason = "cgroup-write-failed";
        return action;
    }

    action.applied = true;
    if (action.reason == "cpuset-fallback-cpu-weight-only") {
        return action;
    }
    action.reason = "assigned";
    return action;
}

bool rollback_resource_control_state() {
    bool ok = true;
    for (const auto &item : resource_control_snapshots()) {
        const auto &snapshot = item.second;
        if (snapshot.restore_value.empty()) {
            continue;
        }
        if (!write_file_value(snapshot.path, snapshot.restore_value)) {
            ok = false;
            append_resource_audit(snapshot.group, fs::path(snapshot.path).parent_path().string(),
                                  snapshot.target_ref, snapshot.file, "", snapshot.restore_value,
                                  "rollback", "failed", "restore-failed", 0, "rollback");
            continue;
        }
        append_resource_audit(snapshot.group, fs::path(snapshot.path).parent_path().string(),
                              snapshot.target_ref, snapshot.file, "", snapshot.restore_value,
                              "rollback", "restored", "restored-old-value", 0, "rollback");
    }
    resource_control_snapshots().clear();
    return ok;
}

ExecutionAction apply_scx_assignment(const RuntimeConfig &config, const WorkloadDecision &decision,
                                     bool scheduler_active, const std::string &scheduler_reason) {
    ExecutionAction action;
    action.executor = "sched_ext";
    action.target_group = "global";
    action.target_profile = decision.target_profile;
    action.cpu_weight = 0;

    if (config.preferred_backend != ExecutorBackend::SchedExt) {
        action.executor = "observe-only";
        action.reason = "backend-not-sched-ext";
        return action;
    }

    if (!should_manage_sample(decision.sample)) {
        action.reason = "non-target-process";
        return action;
    }

    if (decision.target_profile == "normal_profile") {
        action.reason = scheduler_reason;
        return action;
    }

    if (!scheduler_active) {
        action.reason = scheduler_reason;
        return action;
    }

    action.applied = true;
    action.reason = scheduler_reason;
    return action;
}

} // namespace eulerpilot
