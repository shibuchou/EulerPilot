#include "executors.hpp"

#include <bpf/bpf.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <unordered_set>
#include <cerrno>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace eulerpilot {

namespace {

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

std::string read_file_trimmed(const char *path) {
    std::ifstream file(path);
    if (!file.good()) {
        return "";
    }

    std::string value;
    std::getline(file, value);
    return value;
}

bool write_group_value(const std::string &group, const char *file, const std::string &value) {
    std::ofstream out("/sys/fs/cgroup/eulerpilot/" + group + "/" + file);
    if (!out.good()) {
        return false;
    }
    out << value;
    return out.good();
}

bool write_group_value(const std::string &group, const char *file, int value) {
    return write_group_value(group, file, std::to_string(value));
}

bool write_pid_to_group(const std::string &group, int pid) {
    std::ofstream procs("/sys/fs/cgroup/eulerpilot/" + group + "/cgroup.procs");
    if (!procs.good()) {
        return false;
    }
    procs << pid;
    return procs.good();
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

std::string default_scx_binary() {
    const char *value = std::getenv("EULERPILOT_SCX_BINARY");
    if (value && *value) {
        return value;
    }
    return "/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot";
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

bool start_scx_session(ScxSession &session, bool fifo_mode, std::string &reason) {
    session.binary_path = default_scx_binary();
    session.fifo_mode = fifo_mode;
    append_scx_session_log("start_scx_session: enter binary=" + session.binary_path);

    if (!file_exists(session.binary_path.c_str())) {
        reason = "scx-binary-missing";
        append_scx_session_log("start_scx_session: scx-binary-missing");
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

    if (!start_scx_session(session, use_scx_fifo_mode(), reason)) {
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

    if (std::strlen(profile.group) == 0) {
        action.reason = "no-group-for-class";
        return action;
    }

    action.executor = "cgroup_v2";
    action.target_group = profile.group;

    if (config.dry_run) {
        action.reason = "dry-run";
        return action;
    }

    if (std::strlen(profile.cpuset_cpus) > 0) {
        if (!write_group_value(profile.group, "cpuset.mems", "0") ||
            !write_group_value(profile.group, "cpuset.cpus", profile.cpuset_cpus)) {
            action.reason = "cpuset-fallback-cpu-weight-only";
            action.cpuset_cpus.clear();
        }
    }

    if (!write_group_value(profile.group, "cpu.weight", profile.cpu_weight)) {
        action.reason = "cpu-weight-write-failed";
        return action;
    }

    if (!write_pid_to_group(profile.group, decision.sample.pid)) {
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
