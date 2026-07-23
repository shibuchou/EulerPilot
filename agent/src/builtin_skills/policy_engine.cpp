#include "common.hpp"
#include "factories.hpp"

#include <functional>
#include <unordered_set>

namespace eulerpilot {
namespace {
// One PolicyEngineAction is a single whitelisted side effect. The engine keeps
// the previous value beside the resolved target so a multi-action transaction
// can roll back already-applied changes if a later action fails.
struct PolicyEngineAction {
    std::string name;
    std::string target_ref;
    std::string target_type = "cgroup";
    std::string resolved_target_type = "cgroup";
    std::string target_path;
    std::string target_ifname;
    std::string pod_namespace;
    std::string pod_name;
    std::string pod_uid;
    std::string file;
    std::string value;
    std::string burst = "32kb";
    std::string latency = "50ms";
    std::string old_value;
    std::string skip_reason;
    bool memory_high_guard = true;
    bool applied = false;
};

class PolicyEngineSkill final : public Skill {
public:
    std::string name() const override { return "policy_engine"; }

    std::vector<std::string> dependencies() const override {
        std::vector<std::string> deps;
        if (require_security_policy_) {
            deps.push_back("security_policy");
        }
        if (require_resource_control_) {
            deps.push_back("resource_control");
        }
        if (require_network_qos_) {
            deps.push_back("network_qos");
        }
        return deps;
    }

    bool configure(const RuntimeConfig &, const SkillSpec &spec) override {
        mode_ = config_value_or(spec, "mode", "enforce");
        policy_id_ = config_value_or(spec, "policy_id", "cross_skill_response");
        source_path_ = config_value_or(spec, "source.audit_path",
                                       config_value_or(spec, "watch.audit_path",
                                                       "reports/events/security_policy.jsonl"));
        audit_path_ = config_value_or(spec, "audit_path",
                                      "reports/events/policy_engine.jsonl");
        journal_path_ = config_value_or(spec, "journal_path",
                                        "run/eulerpilot/action_journal.jsonl");
        watch_skill_ = config_value_or(spec, "watch.skill", "security_policy");
        watch_operation_ = config_value_or(spec, "watch.operation", "anomaly");
        watch_rule_id_ = config_value_or(spec, "watch.rule_id", "burst_execve");
        watch_result_ = config_value_or(spec, "watch.result", "observed");
        require_security_policy_ =
            config_bool_or(spec, "dependencies.security_policy", true);
        require_resource_control_ =
            config_bool_or(spec, "dependencies.resource_control", true);
        require_network_qos_ =
            config_bool_or(spec, "dependencies.network_qos", false);
        poll_interval_ms_ = 100;
        parse_uint32_range(config_value_or(spec, "poll_interval_ms", "100"),
                           20, 5000, poll_interval_ms_);
        const bool default_memory_high_guard =
            config_bool_or(spec, "guards.memory_high", true);

        actions_.clear();
        for (int i = 0; i < 16; ++i) {
            const std::string prefix = "actions." + std::to_string(i) + ".";
            const std::string file = config_value_or(spec, prefix + "file", "");
            const std::string value = config_value_or(spec, prefix + "value", "");
            const std::string target_ref =
                config_value_or(spec, prefix + "target_ref", "");
            if (file.empty() && value.empty() && target_ref.empty()) {
                continue;
            }
            if (file.empty() || value.empty() || target_ref.empty()) {
                last_error_ = "policy-engine-action-incomplete";
                return false;
            }

            const std::string target_prefix = "targets." + target_ref + ".";
            PolicyEngineAction action;
            action.name = config_value_or(spec, prefix + "name",
                                          "policy-action-" + std::to_string(i));
            action.target_ref = target_ref;
            action.target_type =
                config_value_or(spec, target_prefix + "type", "cgroup");
            action.target_path =
                config_value_or(spec, target_prefix + "path",
                                config_value_or(spec, target_prefix + "cgroup_path", ""));
            action.target_ifname =
                config_value_or(spec, target_prefix + "ifname",
                                config_value_or(spec, prefix + "ifname", ""));
            action.file = file;
            action.value = value;
            action.burst = config_value_or(spec, prefix + "burst",
                                           config_value_or(spec, target_prefix + "burst", "32kb"));
            action.latency = config_value_or(spec, prefix + "latency",
                                             config_value_or(spec, target_prefix + "latency", "50ms"));
            action.memory_high_guard =
                config_bool_or(spec, prefix + "memory_high_guard", default_memory_high_guard);
            if (!resolve_action_target(spec, target_prefix, action)) {
                return false;
            }
            if (!validate_action(action)) {
                return false;
            }
            actions_.push_back(std::move(action));
        }

        if (actions_.empty()) {
            last_error_ = "policy-engine-no-actions";
            return false;
        }
        if (mode_ != "audit" && mode_ != "enforce") {
            last_error_ = "policy-engine-unsupported-mode";
            return false;
        }
        return true;
    }

    bool probe() override {
        if (actions_.empty()) {
            last_error_ = "policy-engine-no-actions";
            available_ = false;
            return false;
        }
        for (const auto &action : actions_) {
            if (!validate_action(action)) {
                available_ = false;
                return false;
            }
            if (action.resolved_target_type == "cgroup") {
                if (!fs::exists(action.target_path)) {
                    last_error_ = "policy-engine-target-missing:" + action.target_ref;
                    available_ = false;
                    return false;
                }
                if (!fs::exists(fs::path(action.target_path) / action.file)) {
                    last_error_ = "policy-engine-control-file-missing:" + action.file;
                    available_ = false;
                    return false;
                }
            } else if (action.resolved_target_type == "netdev") {
                if (!command_available("tc") || !command_available("ip")) {
                    last_error_ = "policy-engine-iproute2-missing";
                    available_ = false;
                    return false;
                }
                if (if_nametoindex(action.target_ifname.c_str()) == 0) {
                    last_error_ = "policy-engine-netdev-missing:" + action.target_ifname;
                    available_ = false;
                    return false;
                }
            }
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
        ensure_parent_dir(audit_path_);
        ensure_parent_dir(journal_path_);
        ensure_parent_dir(source_path_);
        {
            std::ofstream source(source_path_, std::ios::app);
        }
        source_offset_ = current_source_size();
        event_thread_stop_.store(false);
        event_thread_ = std::thread([this] { event_loop(); });
        running_ = true;
        state_ = "watching";
        write_policy_event("start", "watch-security-anomaly", "success", "", "", "", nullptr);
        return true;
    }

    SkillSnapshot snapshot() const override {
        SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = available_;
        snapshot.running = running_;
        snapshot.state = state_;
        snapshot.evidence["source_path"] = source_path_;
        snapshot.evidence["watch_skill"] = watch_skill_;
        snapshot.evidence["watch_operation"] = watch_operation_;
        snapshot.evidence["watch_rule_id"] = watch_rule_id_;
        snapshot.evidence["policy_id"] = policy_id_;
        snapshot.evidence["mode"] = mode_;
        snapshot.evidence["action_count"] = std::to_string(actions_.size());
        snapshot.evidence["trigger_count"] = std::to_string(trigger_count_.load());
        snapshot.evidence["last_transaction_id"] = last_transaction_id_;
        snapshot.evidence["reason"] = last_error_.empty() ? "ok" : last_error_;
        return snapshot;
    }

    bool rollback() override {
        stop_thread();
        restore_actions(last_transaction_id_, last_trigger_event_id_);
        running_ = false;
        state_ = "rolled-back";
        return true;
    }

    void stop() override {
        stop_thread();
        restore_actions(last_transaction_id_, last_trigger_event_id_);
        running_ = false;
        state_ = "stopped";
    }

    std::string last_error() const override { return last_error_; }

private:
    bool is_pod_target_type(const std::string &target_type) const {
        return target_type == "k8s_pod" || target_type == "pod";
    }

    TargetResolverOptions policy_target_resolver_options(
        const SkillSpec &spec,
        const std::string &target_prefix) const {
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
        return options;
    }

    K8sPodTargetSpec policy_k8s_pod_target_spec(
        const SkillSpec &spec,
        const std::string &target_prefix,
        const std::string &target_ref) const {
        K8sPodTargetSpec target_spec;
        target_spec.name = target_ref;
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
        return target_spec;
    }

    bool resolve_action_target(const SkillSpec &spec,
                               const std::string &target_prefix,
                               PolicyEngineAction &action) const {
        if (action.target_type == "cgroup" || action.target_type == "netdev") {
            action.resolved_target_type = action.target_type;
            return true;
        }
        if (!is_pod_target_type(action.target_type)) {
            action.resolved_target_type = action.target_type;
            return true;
        }

        const K8sPodTargetSpec target_spec =
            policy_k8s_pod_target_spec(spec, target_prefix, action.target_ref);
        const TargetResolverOptions options =
            policy_target_resolver_options(spec, target_prefix);

        if (is_allowed_control_file(action.file)) {
            const auto target = resolve_k8s_pod_cgroup_target(target_spec, options);
            if (!target.resolved || target.cgroup_path.empty()) {
                last_error_ = "policy-engine-target-pod-cgroup-resolve-failed:" +
                              target.reason;
                return false;
            }
            action.resolved_target_type = "cgroup";
            action.target_path = target.cgroup_path;
            action.pod_namespace = target.pod_namespace;
            action.pod_name = target.pod_name;
            action.pod_uid = target.pod_uid;
            return true;
        }

        if (is_allowed_network_qos_file(action.file)) {
            const auto target = resolve_k8s_pod_target(target_spec, options);
            if (!target.resolved || target.ifname.empty()) {
                last_error_ = "policy-engine-target-pod-veth-resolve-failed:" +
                              target.reason;
                return false;
            }
            action.resolved_target_type = "netdev";
            action.target_ifname = target.ifname;
            action.pod_namespace = target.pod_namespace;
            action.pod_name = target.pod_name;
            action.pod_uid = target.pod_uid;
            return true;
        }

        last_error_ = "policy-engine-action-file-not-allowed:" + action.file;
        return false;
    }

    bool validate_action(const PolicyEngineAction &action) const {
        if (action.value.empty() || action.value.find('\n') != std::string::npos ||
            action.value.find('\r') != std::string::npos) {
            last_error_ = "policy-engine-invalid-value";
            return false;
        }
        if (action.resolved_target_type == "cgroup") {
            if (action.target_path.empty() ||
                action.target_path.rfind("/sys/fs/cgroup/", 0) != 0) {
                last_error_ = "policy-engine-target-outside-cgroup";
                return false;
            }
            if (!is_allowed_control_file(action.file)) {
                last_error_ = "policy-engine-control-file-not-allowed:" + action.file;
                return false;
            }
            return true;
        }
        if (action.resolved_target_type == "netdev") {
            if (!is_allowed_network_qos_file(action.file)) {
                last_error_ = "policy-engine-network-action-not-allowed:" + action.file;
                return false;
            }
            if (!valid_tc_token(action.target_ifname) || !valid_tc_token(action.value) ||
                !valid_tc_token(action.burst) || !valid_tc_token(action.latency)) {
                last_error_ = "policy-engine-invalid-tc-token";
                return false;
            }
            const bool resolved_lab_pod_veth =
                is_pod_target_type(action.target_type) &&
                !is_denied_host_netdev_name(action.target_ifname);
            if (!is_allowed_lab_netdev_name(action.target_ifname) &&
                !resolved_lab_pod_veth) {
                last_error_ = "policy-engine-non-lab-netdev-denied:" + action.target_ifname;
                return false;
            }
            return true;
        }
        last_error_ = "policy-engine-target-type-unsupported:" + action.target_type;
        return false;
    }

    bool is_allowed_control_file(const std::string &file) const {
        return file == "cpu.max" || file == "cpu.weight" ||
               file == "memory.high" || file == "memory.low" ||
               file == "memory.max" || file == "io.max" ||
               file == "io.weight";
    }

    bool is_allowed_network_qos_file(const std::string &file) const {
        return file == "network_qos.rate" || file == "tc.tbf.rate" ||
               file == "tc.rate";
    }

    std::uint64_t current_source_size() const {
        std::error_code ec;
        const auto size = fs::file_size(source_path_, ec);
        return ec ? 0 : static_cast<std::uint64_t>(size);
    }

    bool line_matches(const std::string &line) const {
        return contains_json_string(line, "skill", watch_skill_) &&
               contains_json_string(line, "operation", watch_operation_) &&
               contains_json_string(line, "rule_id", watch_rule_id_) &&
               contains_json_string(line, "result", watch_result_);
    }

    bool contains_json_string(const std::string &line,
                              const std::string &key,
                              const std::string &value) const {
        return line.find("\"" + key + "\":\"" + value + "\"") != std::string::npos;
    }

    std::string extract_json_string(const std::string &line,
                                    const std::string &key) const {
        const std::string marker = "\"" + key + "\":\"";
        const auto start = line.find(marker);
        if (start == std::string::npos) {
            return "";
        }
        const auto value_start = start + marker.size();
        const auto value_end = line.find('"', value_start);
        if (value_end == std::string::npos) {
            return "";
        }
        return line.substr(value_start, value_end - value_start);
    }

    std::string request_id_for_source(const std::string &source_line) const {
        const std::string event_id = extract_json_string(source_line, "event_id");
        if (!event_id.empty()) {
            return event_id;
        }
        return "source-hash-" + std::to_string(std::hash<std::string>{}(source_line));
    }

    std::string desired_state_key(const PolicyEngineAction &action) const {
        const std::string target_identity = action.resolved_target_type == "netdev"
            ? action.target_ifname
            : action.target_path;
        return policy_id_ + "|" + action.resolved_target_type + "|" +
               target_identity + "|" + action.file + "|" + action.value + "|" +
               action.burst + "|" + action.latency;
    }

    void event_loop() {
        while (!event_thread_stop_.load()) {
            std::error_code ec;
            if (fs::exists(source_path_, ec)) {
                const auto size = current_source_size();
                if (size < source_offset_) {
                    source_offset_ = 0;
                }
                if (size > source_offset_) {
                    std::ifstream in(source_path_);
                    in.seekg(static_cast<std::streamoff>(source_offset_));
                    std::string line;
                    while (std::getline(in, line)) {
                        if (line_matches(line)) {
                            trigger_count_.fetch_add(1);
                            apply_actions(line);
                        }
                    }
                    source_offset_ = current_source_size();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
        }
    }

    void stop_thread() {
        event_thread_stop_.store(true);
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
    }

    bool read_value(const fs::path &path, std::string &value) const {
        std::ifstream in(path);
        if (!in.good()) {
            return false;
        }
        std::getline(in, value);
        return true;
    }

    bool write_value(const fs::path &path, const std::string &value) const {
        std::ofstream out(path);
        if (!out.good()) {
            return false;
        }
        out << value << "\n";
        return out.good();
    }

    bool parse_uint64_value(const std::string &value, std::uint64_t &out) const {
        char *end = nullptr;
        errno = 0;
        const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
        if (errno != 0 || end == value.c_str() || *end != '\0') {
            return false;
        }
        out = static_cast<std::uint64_t>(parsed);
        return true;
    }

    bool memory_high_allowed(const PolicyEngineAction &action,
                             std::string &reason) const {
        if (action.file != "memory.high" || !action.memory_high_guard ||
            action.value == "max") {
            return true;
        }
        std::uint64_t high_value = 0;
        if (!parse_uint64_value(action.value, high_value)) {
            reason = "memory-high-value-not-numeric";
            return false;
        }
        std::string max_value;
        if (!read_value(fs::path(action.target_path) / "memory.max", max_value)) {
            return true;
        }
        if (max_value == "max") {
            return true;
        }
        std::uint64_t max_numeric = 0;
        if (!parse_uint64_value(max_value, max_numeric)) {
            return true;
        }
        if (max_numeric <= high_value) {
            reason = "memory-max-below-memory-high:" + max_value;
            return false;
        }
        return true;
    }

    std::string tc_qdisc_show(const std::string &ifname) const {
        const std::string command = "tc qdisc show dev " + ifname + " 2>/dev/null";
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return "";
        }
        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        pclose(pipe);
        return output;
    }

    std::string apply_action(PolicyEngineAction &action,
                             const std::string &transaction_id,
                             const std::string &trigger_event_id,
                             const std::string &source_line) {
        action.skip_reason.clear();
        if (action.resolved_target_type == "cgroup") {
            return apply_cgroup_action(action, transaction_id, trigger_event_id, source_line);
        }
        if (action.resolved_target_type == "netdev") {
            return apply_netdev_action(action, transaction_id, trigger_event_id, source_line);
        }
        last_error_ = "policy-engine-unsupported-action-target:" +
                      action.target_type + "->" + action.resolved_target_type;
        return "failed";
    }

    std::string apply_cgroup_action(PolicyEngineAction &action,
                                    const std::string &,
                                    const std::string &,
                                    const std::string &) {
        const fs::path control_path = fs::path(action.target_path) / action.file;
        std::string old_value;
        if (!read_value(control_path, old_value)) {
            last_error_ = "policy-engine-read-old-failed:" + action.file;
            return "failed";
        }
        action.old_value = old_value;
        std::string guard_reason;
        if (!memory_high_allowed(action, guard_reason)) {
            action.skip_reason = guard_reason;
            return "skipped";
        }
        if (mode_ != "enforce") {
            return "observed";
        }
        if (!write_value(control_path, action.value)) {
            last_error_ = "policy-engine-write-failed:" + action.file;
            return "failed";
        }
        std::string verify_value;
        if (!read_value(control_path, verify_value) || verify_value != action.value) {
            last_error_ = "policy-engine-verify-failed:" + action.file;
            return "failed";
        }
        action.applied = true;
        return "applied";
    }

    std::string apply_netdev_action(PolicyEngineAction &action,
                                    const std::string &,
                                    const std::string &,
                                    const std::string &) {
        action.old_value = tc_qdisc_show(action.target_ifname);
        if (mode_ != "enforce") {
            return "observed";
        }
        if (if_nametoindex(action.target_ifname.c_str()) == 0) {
            last_error_ = "policy-engine-netdev-missing-at-apply:" + action.target_ifname;
            return "failed";
        }
        if (tc_has_existing_root_qdisc(action.target_ifname)) {
            last_error_ = "policy-engine-existing-root-qdisc-denied:" + action.target_ifname;
            return "failed";
        }
        const std::string command = "tc qdisc replace dev " + action.target_ifname +
                                    " root tbf rate " + action.value +
                                    " burst " + action.burst +
                                    " latency " + action.latency;
        if (!run_tc_command(command)) {
            last_error_ = "policy-engine-network-qos-apply-failed:" + action.target_ifname;
            return "failed";
        }
        const std::string after = tc_qdisc_show(action.target_ifname);
        if (after.find("tbf") == std::string::npos) {
            last_error_ = "policy-engine-network-qos-verify-failed:" + action.target_ifname;
            return "failed";
        }
        action.applied = true;
        return "applied";
    }

    void apply_actions(const std::string &source_line) {
        const std::string request_id = request_id_for_source(source_line);
        if (successful_request_ids_.find(request_id) != successful_request_ids_.end()) {
            write_policy_event("decision", policy_id_, "deduplicated", source_line,
                               last_transaction_id_, last_trigger_event_id_, nullptr);
            return;
        }
        // The transaction id is written to Policy Engine, child Skill audit
        // events, and ActionJournal entries. This gives the Web Console and
        // final evidence scripts one stable key for cross-Skill reconstruction.
        const std::string trigger_event_id = extract_json_string(source_line, "event_id");
        const std::string transaction_id = "pe-v3-1-" +
            std::to_string(trigger_count_.load()) + "-" + now_event_timestamp();
        last_transaction_id_ = transaction_id;
        last_trigger_event_id_ = trigger_event_id;
        write_policy_event("decision", policy_id_, "decision", source_line,
                           transaction_id, trigger_event_id, nullptr);

        for (auto &action : actions_) {
            std::string result;
            const std::string desired_key = desired_state_key(action);
            if (effective_desired_states_.find(desired_key) != effective_desired_states_.end()) {
                result = "converged";
            } else {
                result = apply_action(action, transaction_id, trigger_event_id, source_line);
            }
            write_policy_event("apply", action.name, result, source_line,
                               transaction_id, trigger_event_id, &action);
            write_skill_action_event(action, "apply", result, transaction_id,
                                     trigger_event_id, source_line);
            if (result == "applied" || result == "observed" || result == "skipped") {
                write_policy_journal(action, "apply", false,
                                     transaction_id, trigger_event_id);
            }
            if (result == "applied" || result == "converged") {
                effective_desired_states_.insert(desired_key);
            }
            if (result == "failed") {
                restore_actions(transaction_id, trigger_event_id);
                state_ = "failed";
                return;
            }
        }
        successful_request_ids_.insert(request_id);
        state_ = "applied";
    }

    void restore_actions(const std::string &transaction_id,
                         const std::string &trigger_event_id) {
        // Restore in reverse order so dependent actions unwind like a stack:
        // network qdisc changes are removed before the cgroup controls that
        // may have narrowed the same workload.
        for (std::size_t i = actions_.size(); i > 0; --i) {
            auto &action = actions_[i - 1];
            if (!action.applied) {
                continue;
            }
            bool restored = false;
            if (action.resolved_target_type == "cgroup") {
                const fs::path control_path = fs::path(action.target_path) / action.file;
                restored = !action.old_value.empty() && write_value(control_path, action.old_value);
            } else if (action.resolved_target_type == "netdev") {
                restored = run_tc_command("tc qdisc del dev " + action.target_ifname + " root");
            }
            if (restored) {
                effective_desired_states_.erase(desired_state_key(action));
                write_policy_event("rollback", action.name, "restored", "",
                                   transaction_id, trigger_event_id, &action);
                write_skill_action_event(action, "rollback", "restored",
                                         transaction_id, trigger_event_id, "");
                write_policy_journal(action, "rollback", true,
                                     transaction_id, trigger_event_id);
                action.applied = false;
            } else {
                last_error_ = "policy-engine-restore-failed:" + action.name;
                write_policy_event("rollback", action.name, "failed", "",
                                   transaction_id, trigger_event_id, &action);
                write_skill_action_event(action, "rollback", "failed",
                                         transaction_id, trigger_event_id, "");
            }
        }
    }

    std::string stage_for_result(const std::string &operation,
                                 const std::string &result) const {
        if (operation == "decision") {
            return "decision";
        }
        if (result == "applied") {
            return "applied";
        }
        if (result == "restored") {
            return "restored";
        }
        if (result == "failed") {
            return "failed";
        }
        if (result == "converged" || result == "deduplicated") {
            return "no-op";
        }
        return result;
    }

    void fill_action_target(AuditEvent &event,
                            const PolicyEngineAction *action) const {
        if (!action) {
            event.target["source_audit_path"] = source_path_;
            return;
        }
        event.target["target_ref"] = action->target_ref;
        event.target["target_type"] = action->target_type;
        event.target["resolved_target_type"] = action->resolved_target_type;
        if (!action->pod_namespace.empty()) {
            event.target["pod_namespace"] = action->pod_namespace;
        }
        if (!action->pod_name.empty()) {
            event.target["pod_name"] = action->pod_name;
        }
        if (!action->pod_uid.empty()) {
            event.target["pod_uid"] = action->pod_uid;
        }
        if (action->resolved_target_type == "cgroup") {
            event.target["cgroup_path"] = action->target_path;
            event.target["file"] = action->file;
        } else if (action->resolved_target_type == "netdev") {
            event.target["ifname"] = action->target_ifname;
            event.target["file"] = action->file;
        }
    }

    void write_policy_event(const std::string &operation,
                            const std::string &action_name,
                            const std::string &result,
                            const std::string &source_line,
                            const std::string &transaction_id,
                            const std::string &trigger_event_id,
                            const PolicyEngineAction *action) const {
        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = "policy_engine-" + operation + "-" +
                         std::to_string(trigger_count_.load()) + "-" + event.timestamp;
        event.skill = "policy_engine";
        event.policy_id = policy_id_;
        event.rule_id = watch_rule_id_;
        event.transaction_id = transaction_id;
        event.trigger_event_id = trigger_event_id;
        event.mode = mode_;
        event.operation = operation == "apply" ? "cross_skill_response" : operation;
        event.action = action_name;
        event.result = result;
        event.severity = result == "failed" ? "error" : "info";
        event.evidence = {
            {"source_skill", watch_skill_},
            {"source_operation", watch_operation_},
            {"source_rule_id", watch_rule_id_},
            {"source_seen", source_line.empty() ? "false" : "true"},
            {"stage", stage_for_result(operation, result)},
        };
        if (action) {
            event.evidence["old_value"] = action->old_value;
            event.evidence["new_value"] = action->value;
            if (!action->skip_reason.empty()) {
                event.evidence["reason"] = action->skip_reason;
            }
        }
        fill_action_target(event, action);
        std::string error;
        append_audit_event(audit_path_, event, &error);
    }

    void write_skill_action_event(const PolicyEngineAction &action,
                                  const std::string &operation,
                                  const std::string &result,
                                  const std::string &transaction_id,
                                  const std::string &trigger_event_id,
                                  const std::string &source_line) const {
        const bool is_netdev = action.resolved_target_type == "netdev";
        const fs::path path = is_netdev ? fs::path("reports/events/network_policy.jsonl")
                                        : fs::path("reports/events/resource_control.jsonl");
        ensure_parent_dir(path);
        AuditEvent event;
        event.timestamp = now_event_timestamp();
        event.event_id = (is_netdev ? "network_qos" : "resource_control") +
                         std::string("-") + operation + "-" + event.timestamp;
        event.skill = is_netdev ? "network_qos" : "resource_control";
        event.policy_id = policy_id_;
        event.rule_id = watch_rule_id_;
        event.transaction_id = transaction_id;
        event.trigger_event_id = trigger_event_id;
        event.mode = mode_;
        event.operation = is_netdev ? "write-tc-qdisc" : "write-cgroup-file";
        event.action = action.name;
        event.result = result;
        event.severity = result == "failed" ? "error" : "info";
        if (is_netdev) {
            event.target = {
                {"target_ref", action.target_ref},
                {"target_type", action.target_type},
                {"resolved_target_type", action.resolved_target_type},
                {"ifname", action.target_ifname},
                {"file", action.file},
            };
            event.evidence = {
                {"old_value", action.old_value},
                {"new_value", action.value},
                {"rate", action.value},
                {"burst", action.burst},
                {"latency", action.latency},
                {"stage", stage_for_result(operation, result)},
                {"source_seen", source_line.empty() ? "false" : "true"},
            };
        } else {
            event.target = {
                {"target_ref", action.target_ref},
                {"target_type", action.target_type},
                {"resolved_target_type", action.resolved_target_type},
                {"cgroup", action.target_path},
                {"file", action.file},
            };
            event.evidence = {
                {"old_value", action.old_value},
                {"new_value", action.value},
                {"stage", stage_for_result(operation, result)},
                {"source_seen", source_line.empty() ? "false" : "true"},
            };
        }
        if (!action.skip_reason.empty()) {
            event.evidence["reason"] = action.skip_reason;
        }
        std::string error;
        append_audit_event(path.string(), event, &error);
    }

    void write_policy_journal(const PolicyEngineAction &action,
                              const std::string &operation,
                              bool restored,
                              const std::string &transaction_id,
                              const std::string &trigger_event_id) const {
        JournalAction entry;
        entry.action_id = "policy_engine-" + operation + "-" + action.name;
        entry.skill = "policy_engine";
        entry.transaction_id = transaction_id;
        entry.trigger_event_id = trigger_event_id;
        entry.policy_id = policy_id_;
        entry.target = action.resolved_target_type == "netdev" ? action.target_ifname : action.target_path;
        entry.operation = operation;
        entry.old_values = {{action.file, action.old_value}};
        entry.new_values = {{action.file, action.value}};
        entry.handles = {
            {"target_ref", action.target_ref},
            {"target_type", action.target_type},
            {"resolved_target_type", action.resolved_target_type},
            {"control_file", action.file},
            {"source_audit_path", source_path_},
        };
        if (!action.pod_namespace.empty()) {
            entry.handles["pod_namespace"] = action.pod_namespace;
        }
        if (!action.pod_name.empty()) {
            entry.handles["pod_name"] = action.pod_name;
        }
        if (!action.pod_uid.empty()) {
            entry.handles["pod_uid"] = action.pod_uid;
        }
        if (action.resolved_target_type == "netdev") {
            entry.handles["ifname"] = action.target_ifname;
            entry.handles["qdisc"] = "tbf";
        }
        entry.restored = restored;
        std::string error;
        append_journal_action(journal_path_, entry, &error);
    }

    bool available_ = false;
    bool running_ = false;
    mutable std::string last_error_;
    std::string state_ = "created";
    std::string mode_ = "enforce";
    std::string policy_id_ = "cross_skill_response";
    std::string source_path_ = "reports/events/security_policy.jsonl";
    std::string audit_path_ = "reports/events/policy_engine.jsonl";
    std::string journal_path_ = "run/eulerpilot/action_journal.jsonl";
    std::string watch_skill_ = "security_policy";
    std::string watch_operation_ = "anomaly";
    std::string watch_rule_id_ = "burst_execve";
    std::string watch_result_ = "observed";
    bool require_security_policy_ = true;
    bool require_resource_control_ = true;
    bool require_network_qos_ = false;
    std::uint32_t poll_interval_ms_ = 100;
    std::vector<PolicyEngineAction> actions_;
    std::atomic<bool> event_thread_stop_{false};
    std::atomic<std::uint64_t> trigger_count_{0};
    std::thread event_thread_;
    std::uint64_t source_offset_ = 0;
    std::string last_transaction_id_;
    std::string last_trigger_event_id_;
    std::unordered_set<std::string> successful_request_ids_;
    std::unordered_set<std::string> effective_desired_states_;
};

} // namespace

void register_policy_engine_skill(SkillRegistry &registry) {
    registry.register_factory("policy_engine", [] {
        return std::make_unique<PolicyEngineSkill>();
    });
}

} // namespace eulerpilot
