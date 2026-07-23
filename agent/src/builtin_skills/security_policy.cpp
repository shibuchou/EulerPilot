#include "common.hpp"
#include "factories.hpp"

namespace eulerpilot {
namespace {
class SecurityPolicySkill final : public Skill {
public:
    explicit SecurityPolicySkill(std::string skill_name = "security_policy_demo")
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
            const std::string default_mode = config_value_or(spec, "mode", "audit");
            mode_ = default_mode;
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
                const std::string rule_mode =
                    config_value_or(spec, rule_prefix + "mode", default_mode);
                if (rule_mode != "audit" && rule_mode != "enforce") {
                    last_error_ = "unsupported-rule-mode";
                    return false;
                }

                SecurityPolicyRule rule;
                rule.hook = hook;
                rule.mode = rule_mode;
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
                } else if (hook == "lsm_task_fix_setgroups") {
                    // Supplementary group changes must be scoped. A global
                    // setgroups deny would break service initialization paths.
                } else if (hook == "lsm_cred_prepare") {
                    // Credential allocation hooks are very hot and broad, so
                    // enforcement must always be scoped to an explicit workload.
                } else if (hook == "lsm_cred_alloc_blank") {
                    // Blank credential allocation is broad and must be scoped.
                    // It is primarily useful as audit/anomaly evidence.
                } else if (hook == "lsm_cred_transfer") {
                    // cred_transfer is a void LSM hook. It can provide
                    // lifecycle evidence but cannot enforce a denial.
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
                    target_spec.isula_path =
                        config_value_or(spec, target_prefix + "isula_path", "isula");
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
                last_error_ = "security-policy-legacy-config-missing";
                return false;
            }
            hook_ = hook->second;
            mode_ = mode->second;
            target_path_ = expand_project_root_token(target_path->second);
            exec_target_path_ = config_value_or(spec, "target_exec_path",
                                                eulerpilot_security_demo_path("deny_exec.sh"));
            action_ = config_value_or(spec, "action", "deny");
            target_ref_ = config_value_or(spec, "target_ref", "legacy_path");
            rule_id_ = config_value_or(spec, "rule_id", "deny-demo-secret-open");
            SecurityPolicyRule rule;
            rule.hook = hook_;
            rule.mode = mode_;
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
            if (rule.mode != "audit" && rule.mode != "enforce") {
                last_error_ = "unsupported-rule-mode";
                return false;
            }
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
            if (rule.hook == "lsm_task_fix_setgroups" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-setgroups-rule-scope-missing";
                return false;
            }
            if (rule.hook == "lsm_cred_prepare" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-cred-prepare-rule-scope-missing";
                return false;
            }
            if (rule.hook == "lsm_cred_alloc_blank" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-cred-alloc-blank-rule-scope-missing";
                return false;
            }
            if (rule.hook == "lsm_cred_transfer" && rule.cgroup_id == 0) {
                last_error_ = "security-policy-cred-transfer-rule-scope-missing";
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
        const std::string object_path = eulerpilot_bpf_object_path("security_policy.bpf.o");
        if (!file_exists(object_path.c_str())) {
            last_error_ = "security-policy-bpf-not-built";
            return false;
        }
        // Probe: load, attach, detach (no side effects)
        bpf_object *obj = bpf_object__open_file(object_path.c_str(), nullptr);
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
        if (!attach_security_programs(obj, probe_links, false, false, false,
                                      false, false, false, false, last_error_)) {
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
        const std::string object_path = eulerpilot_bpf_object_path("security_policy.bpf.o");
        bpf_object_ = bpf_object__open_file(object_path.c_str(), nullptr);
        if (!bpf_object_) {
            rollback();
            last_error_ = "security-policy-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(bpf_object_) != 0) {
            rollback();
            last_error_ = "security-policy-bpf-load-failed";
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
                                      has_rule_hook("lsm_task_fix_setgroups"),
                                      has_rule_hook("lsm_cred_prepare"),
                                      has_rule_hook("lsm_cred_alloc_blank"),
                                      has_rule_hook("lsm_cred_transfer"),
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

    static bool valid_security_comm_filter(const std::string &comm) {
        if (comm.empty() || comm.size() >= 16) {
            return false;
        }
        for (char ch : comm) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (!(std::isalnum(c) || ch == '_' || ch == '-' || ch == '.')) {
                return false;
            }
        }
        return true;
    }

    bool has_security_target_ref(const std::string &target_ref) const {
        for (const auto &rule : rules_) {
            if (rule.target_ref == target_ref) {
                return true;
            }
        }
        return false;
    }

    static bool is_supported_security_hook(const std::string &hook) {
        return hook == "lsm_file_open" || hook == "lsm_bprm_check_security" ||
               hook == "lsm_socket_connect" || hook == "lsm_ptrace_traceme" ||
               hook == "lsm_capable" || hook == "lsm_task_fix_setuid" ||
               hook == "lsm_task_fix_setgid" ||
               hook == "lsm_task_fix_setgroups" ||
               hook == "lsm_cred_prepare" ||
               hook == "lsm_cred_alloc_blank" ||
               hook == "lsm_cred_transfer";
    }

    static std::uint32_t security_hook_event_type(const std::string &hook) {
        if (hook == "lsm_file_open") {
            return 1;
        }
        if (hook == "lsm_bprm_check_security") {
            return 6;
        }
        if (hook == "lsm_socket_connect") {
            return 7;
        }
        if (hook == "lsm_ptrace_traceme") {
            return 8;
        }
        if (hook == "lsm_capable") {
            return 9;
        }
        if (hook == "lsm_task_fix_setuid") {
            return 10;
        }
        if (hook == "lsm_task_fix_setgid") {
            return 11;
        }
        if (hook == "lsm_task_fix_setgroups") {
            return 12;
        }
        if (hook == "lsm_cred_prepare") {
            return 13;
        }
        if (hook == "lsm_cred_alloc_blank") {
            return 14;
        }
        if (hook == "lsm_cred_transfer") {
            return 15;
        }
        return 0;
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
            const std::string target_ref =
                config_value_or(spec, prefix + "target_ref",
                                config_value_or(spec, prefix + "scope_target_ref", ""));
            const std::string path_prefix =
                config_value_or(spec, prefix + "path_prefix",
                                config_value_or(spec, prefix + "sensitive_path_prefix", ""));
            const std::string comm =
                config_value_or(spec, prefix + "comm",
                                config_value_or(spec, prefix + "process_comm", ""));
            const std::string comm_prefix =
                config_value_or(spec, prefix + "comm_prefix",
                                config_value_or(spec, prefix + "process_comm_prefix", ""));
            const std::string capability_text =
                config_value_or(spec, prefix + "capability",
                                config_value_or(spec, prefix + "cap", ""));
            if (name.empty() && type.empty() && syscall.empty() &&
                threshold_text.empty() && window_text.empty() &&
                target_ref.empty() && path_prefix.empty() && comm.empty() &&
                comm_prefix.empty() && capability_text.empty()) {
                continue;
            }

            SecurityAnomalyRule rule;
            rule.rule_id = name.empty() ? "burst_execve" : name;
            rule.type = type.empty() ? "rate" : type;
            rule.syscall = syscall.empty() ? "execve" : syscall;
            rule.severity = config_value_or(spec, prefix + "severity", "medium");
            rule.target_ref = target_ref;
            rule.path_prefix = path_prefix;
            rule.comm = comm;
            rule.comm_prefix = comm_prefix;
            if (rule.type != "rate") {
                last_error_ = "security-policy-anomaly-type-unsupported";
                return false;
            }
            if (rule.syscall == "sys_enter_execve") {
                rule.syscall = "execve";
            } else if (rule.syscall == "sys_enter_connect" ||
                       rule.syscall == "lsm_socket_connect") {
                rule.syscall = "connect";
            } else if (rule.syscall == "sys_enter_openat" ||
                       rule.syscall == "lsm_file_open") {
                rule.syscall = "openat";
            } else if (rule.syscall == "capable" ||
                       rule.syscall == "lsm_capable") {
                rule.syscall = "capability";
            } else if (rule.syscall == "cred" || rule.syscall == "credential" ||
                       rule.syscall == "lsm_cred_prepare" ||
                       rule.syscall == "lsm_cred_alloc_blank" ||
                       rule.syscall == "lsm_cred_transfer" ||
                       rule.syscall == "lsm_task_fix_setuid" ||
                       rule.syscall == "lsm_task_fix_setgid" ||
                       rule.syscall == "lsm_task_fix_setgroups") {
                rule.syscall = "credential";
            }
            if (rule.syscall != "execve" && rule.syscall != "connect" &&
                rule.syscall != "openat" && rule.syscall != "capability" &&
                rule.syscall != "credential") {
                last_error_ = "security-policy-anomaly-syscall-unsupported";
                return false;
            }
            if (!rule.path_prefix.empty() && !valid_security_path(rule.path_prefix)) {
                last_error_ = "security-policy-anomaly-path-prefix-invalid";
                return false;
            }
            if (!rule.target_ref.empty() && !has_security_target_ref(rule.target_ref)) {
                last_error_ = "security-policy-anomaly-target-ref-unknown";
                return false;
            }
            if (!rule.comm.empty() && !valid_security_comm_filter(rule.comm)) {
                last_error_ = "security-policy-anomaly-comm-invalid";
                return false;
            }
            if (!rule.comm_prefix.empty() &&
                !valid_security_comm_filter(rule.comm_prefix)) {
                last_error_ = "security-policy-anomaly-comm-prefix-invalid";
                return false;
            }
            if (!capability_text.empty() &&
                !parse_security_capability(capability_text, rule.capability)) {
                last_error_ = "security-policy-anomaly-capability-invalid";
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
                                         bool attach_task_fix_setgroups,
                                         bool attach_cred_prepare,
                                         bool attach_cred_alloc_blank,
                                         bool attach_cred_transfer,
                                         std::string &error) {
        bpf_program *lsm_prog = bpf_object__find_program_by_name(obj, "security_policy_file_open");
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

        if (attach_task_fix_setgroups) {
            bpf_program *setgroups_prog =
                bpf_object__find_program_by_name(obj, "security_policy_task_fix_setgroups");
            if (!setgroups_prog) {
                error = "security-policy-task-fix-setgroups-program-missing";
                return false;
            }
            bpf_link *setgroups_link = bpf_program__attach_lsm(setgroups_prog);
            if (!setgroups_link) {
                error = "security-policy-task-fix-setgroups-attach-failed";
                return false;
            }
            links.push_back(setgroups_link);
        }

        if (attach_cred_prepare) {
            bpf_program *cred_prepare_prog =
                bpf_object__find_program_by_name(obj, "security_policy_cred_prepare");
            if (!cred_prepare_prog) {
                error = "security-policy-cred-prepare-program-missing";
                return false;
            }
            bpf_link *cred_prepare_link = bpf_program__attach_lsm(cred_prepare_prog);
            if (!cred_prepare_link) {
                error = "security-policy-cred-prepare-attach-failed";
                return false;
            }
            links.push_back(cred_prepare_link);
        }

        if (attach_cred_alloc_blank) {
            bpf_program *cred_alloc_blank_prog =
                bpf_object__find_program_by_name(obj, "security_policy_cred_alloc_blank");
            if (!cred_alloc_blank_prog) {
                error = "security-policy-cred-alloc-blank-program-missing";
                return false;
            }
            bpf_link *cred_alloc_blank_link =
                bpf_program__attach_lsm(cred_alloc_blank_prog);
            if (!cred_alloc_blank_link) {
                error = "security-policy-cred-alloc-blank-attach-failed";
                return false;
            }
            links.push_back(cred_alloc_blank_link);
        }

        if (attach_cred_transfer) {
            bpf_program *cred_transfer_prog =
                bpf_object__find_program_by_name(obj, "security_policy_cred_transfer");
            if (!cred_transfer_prog) {
                error = "security-policy-cred-transfer-program-missing";
                return false;
            }
            bpf_link *cred_transfer_link =
                bpf_program__attach_lsm(cred_transfer_prog);
            if (!cred_transfer_link) {
                error = "security-policy-cred-transfer-attach-failed";
                return false;
            }
            links.push_back(cred_transfer_link);
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
                target.hook_type = security_hook_event_type(rules_[i].hook);
                target.enforce = rules_[i].mode == "enforce" ? 1 : 0;
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
        auto *self = static_cast<SecurityPolicySkill *>(ctx);
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
        case 12: return "lsm_task_fix_setgroups";
        case 13: return "lsm_cred_prepare";
        case 14: return "lsm_cred_alloc_blank";
        case 15: return "lsm_cred_transfer";
        default: return "unknown";
        }
    }

    static std::string credential_stage_from_hook(const std::string &hook_name) {
        if (hook_name == "lsm_cred_prepare") {
            return "prepare";
        }
        if (hook_name == "lsm_cred_alloc_blank") {
            return "alloc_blank";
        }
        if (hook_name == "lsm_cred_transfer") {
            return "transfer";
        }
        if (hook_name == "lsm_task_fix_setuid") {
            return "setuid";
        }
        if (hook_name == "lsm_task_fix_setgid") {
            return "setgid";
        }
        if (hook_name == "lsm_task_fix_setgroups") {
            return "setgroups";
        }
        return "unknown";
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
        event.mode = matched_rule ? matched_rule->mode
                                  : (hit.enforce ? "enforce" : "audit");
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
        if (hit.event_type == 12) {
            event.evidence["group_count"] = std::to_string(hit.group_count);
            event.evidence["old_group_count"] = std::to_string(hit.old_group_count);
        }
        if (hit.event_type == 13) {
            event.evidence["uid"] = std::to_string(hit.uid);
            event.evidence["euid"] = std::to_string(hit.euid);
            event.evidence["suid"] = std::to_string(hit.suid);
            event.evidence["gid"] = std::to_string(hit.gid);
            event.evidence["egid"] = std::to_string(hit.egid);
            event.evidence["sgid"] = std::to_string(hit.sgid);
            event.evidence["group_count"] = std::to_string(hit.group_count);
            event.evidence["old_group_count"] = std::to_string(hit.old_group_count);
            event.evidence["cred_gfp"] = std::to_string(hit.cred_gfp);
        }
        if (hit.event_type == 14 || hit.event_type == 15) {
            event.evidence["uid"] = std::to_string(hit.uid);
            event.evidence["euid"] = std::to_string(hit.euid);
            event.evidence["suid"] = std::to_string(hit.suid);
            event.evidence["gid"] = std::to_string(hit.gid);
            event.evidence["egid"] = std::to_string(hit.egid);
            event.evidence["sgid"] = std::to_string(hit.sgid);
        }
        if (hit.event_type == 14) {
            event.evidence["cred_gfp"] = std::to_string(hit.cred_gfp);
        }
        event.action = hit.decision < 0 ? "deny" : "audit-hit";
        event.result = hit.decision < 0 ? "blocked" : "observed";
        event.severity = hit.enforce ? "warning" : "info";
        std::string error;
        append_audit_event(audit_path.string(), event, &error);
        maybe_write_anomaly_event(hit, hook_name, audit_path);
    }

    void maybe_write_anomaly_event(const SecurityPolicyEvent &hit,
                                   const std::string &hook_name,
                                   const fs::path &audit_path) {
        if (anomaly_rules_.empty()) {
            return;
        }

        std::string observed_syscall;
        if (hook_name == "sys_enter_execve") {
            observed_syscall = "execve";
        } else if (hook_name == "sys_enter_connect" || hook_name == "lsm_socket_connect") {
            observed_syscall = "connect";
        } else if (hook_name == "sys_enter_openat" || hook_name == "lsm_file_open") {
            observed_syscall = "openat";
        } else if (hook_name == "lsm_capable") {
            observed_syscall = "capability";
        } else if (hook_name == "lsm_cred_prepare" ||
                   hook_name == "lsm_cred_alloc_blank" ||
                   hook_name == "lsm_cred_transfer" ||
                   hook_name == "lsm_task_fix_setuid" ||
                   hook_name == "lsm_task_fix_setgid" ||
                   hook_name == "lsm_task_fix_setgroups") {
            observed_syscall = "credential";
        } else {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const std::string path = bounded_string(hit.path, sizeof(hit.path));
        const std::string comm = bounded_string(hit.comm, sizeof(hit.comm));
        const SecurityPolicyRule *matched_rule = rule_for_target_index(hit.target_index);
        for (auto &rule : anomaly_rules_) {
            if (rule.type != "rate" || rule.syscall != observed_syscall) {
                continue;
            }
            if (!rule.target_ref.empty() &&
                (!matched_rule || matched_rule->target_ref != rule.target_ref)) {
                continue;
            }
            if (!rule.path_prefix.empty() && path.rfind(rule.path_prefix, 0) != 0) {
                continue;
            }
            if (!rule.comm.empty() && comm != rule.comm) {
                continue;
            }
            if (!rule.comm_prefix.empty() &&
                comm.rfind(rule.comm_prefix, 0) != 0) {
                continue;
            }
            if (rule.capability >= 0 && hit.capability != rule.capability) {
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
            event.mode = matched_rule ? matched_rule->mode : mode_;
            event.target = {
                {"target_ref", matched_rule ? matched_rule->target_ref : "syscall_trace"},
                {"syscall", rule.syscall},
                {"path", path},
            };
            if (matched_rule && matched_rule->cgroup_id != 0) {
                event.target["cgroup_id"] = std::to_string(matched_rule->cgroup_id);
                event.target["cgroup_path"] = matched_rule->cgroup_path;
            }
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
                {"target_index", hit.target_index == kSecurityTargetUnknown
                                     ? "unknown"
                                     : std::to_string(hit.target_index)},
            };
            if (!rule.target_ref.empty()) {
                event.evidence["target_ref_filter"] = rule.target_ref;
            }
            if (!rule.path_prefix.empty()) {
                event.evidence["path_prefix"] = rule.path_prefix;
            }
            if (!rule.comm.empty()) {
                event.evidence["comm_filter"] = rule.comm;
            }
            if (!rule.comm_prefix.empty()) {
                event.evidence["comm_prefix"] = rule.comm_prefix;
            }
            if (hit.daddr != 0) {
                const std::string dst_ip = ipv4_to_string(hit.daddr);
                if (!dst_ip.empty()) {
                    event.evidence["dst_ip"] = dst_ip;
                }
                event.evidence["dst_port"] = std::to_string(ntohs(hit.dport));
                event.evidence["protocol"] = hit.protocol == 6 ? "tcp" : std::to_string(hit.protocol);
            }
            if (hit.capability >= 0) {
                event.evidence["capability"] = security_capability_name(hit.capability);
            }
            if (observed_syscall == "credential") {
                event.evidence["credential_stage"] =
                    credential_stage_from_hook(hook_name);
                if (hook_name == "lsm_cred_prepare" ||
                    hook_name == "lsm_cred_alloc_blank" ||
                    hook_name == "lsm_cred_transfer" ||
                    hook_name == "lsm_task_fix_setuid") {
                    event.evidence["uid"] = std::to_string(hit.uid);
                    event.evidence["euid"] = std::to_string(hit.euid);
                    event.evidence["suid"] = std::to_string(hit.suid);
                }
                if (hook_name == "lsm_cred_prepare" ||
                    hook_name == "lsm_cred_alloc_blank" ||
                    hook_name == "lsm_cred_transfer" ||
                    hook_name == "lsm_task_fix_setgid") {
                    event.evidence["gid"] = std::to_string(hit.gid);
                    event.evidence["egid"] = std::to_string(hit.egid);
                    event.evidence["sgid"] = std::to_string(hit.sgid);
                }
                if (hook_name == "lsm_cred_prepare" ||
                    hook_name == "lsm_task_fix_setgroups") {
                    event.evidence["group_count"] =
                        std::to_string(hit.group_count);
                    event.evidence["old_group_count"] =
                        std::to_string(hit.old_group_count);
                }
                if (hook_name == "lsm_cred_prepare") {
                    event.evidence["cred_gfp"] = std::to_string(hit.cred_gfp);
                }
                if (hook_name == "lsm_cred_alloc_blank") {
                    event.evidence["cred_gfp"] = std::to_string(hit.cred_gfp);
                }
            }
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
    std::string target_path_ = eulerpilot_security_demo_path("secret.txt");
    std::string exec_target_path_ = eulerpilot_security_demo_path("deny_exec.sh");
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

void register_security_policy_skill(SkillRegistry &registry) {
    registry.register_factory("security_policy", [] {
        return std::make_unique<SecurityPolicySkill>("security_policy");
    });
    registry.register_factory("security_policy_demo", [] {
        return std::make_unique<SecurityPolicySkill>();
    });
}

} // namespace eulerpilot
