#include "common.hpp"
#include "factories.hpp"

namespace eulerpilot {
namespace {
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
                last_error_ = "network-policy-legacy-config-missing";
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
        const std::string object_path = eulerpilot_bpf_object_path("network_policy.bpf.o");
        if (!file_exists(object_path.c_str())) {
            last_error_ = "network-policy-bpf-not-built";
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

        bpf_object *obj = bpf_object__open_file(object_path.c_str(), nullptr);
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
            last_error_ = "network-policy-cgroup-create-failed";
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
            last_error_ = "network-policy-cgroup-open-failed";
            return false;
        }
        const std::string object_path = eulerpilot_bpf_object_path("network_policy.bpf.o");
        bpf_object_ = bpf_object__open_file(object_path.c_str(), nullptr);
        if (!bpf_object_) {
            rollback();
            last_error_ = "network-policy-bpf-open-failed";
            return false;
        }
        if (bpf_object__load(bpf_object_) != 0) {
            rollback();
            last_error_ = "network-policy-bpf-load-failed";
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
            last_error_ = "network-policy-bpf-attach-failed";
            return false;
        }
        std::error_code ec;
        fs::remove(link_pin_path(), ec);
        if (bpf_link__pin(link_, link_pin_path().c_str()) != 0) {
            rollback();
            last_error_ = "network-policy-bpf-pin-failed";
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


} // namespace

void register_network_policy_skill(SkillRegistry &registry) {
    registry.register_factory("network_policy_demo", [] {
        return std::make_unique<NetworkPolicySkill>("network_policy_demo");
    });
    registry.register_factory("network_policy", [] {
        return std::make_unique<NetworkPolicySkill>("network_policy");
    });
}

} // namespace eulerpilot
