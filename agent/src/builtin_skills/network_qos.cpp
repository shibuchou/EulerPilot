#include "common.hpp"
#include "factories.hpp"

namespace eulerpilot {
namespace {
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
            target_type_ = config_value_or(spec, target_prefix + "type", "");
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
            target_type_ = "netdev";
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
        const bool resolved_lab_pod_veth =
            (target_type_ == "k8s_pod" || target_type_ == "pod") &&
            !is_denied_host_netdev_name(ifname_);
        if (!is_allowed_lab_netdev_name(ifname_) && !resolved_lab_pod_veth) {
            last_error_ = "network-qos-non-lab-netdev-denied:" + ifname_;
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
        const std::string object_path = eulerpilot_bpf_object_path("network_qos_tc.bpf.o");
        if (!file_exists(object_path.c_str())) {
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
        if (tc_has_existing_root_qdisc(ifname_)) {
            last_error_ = "tc-existing-root-qdisc-denied";
            return false;
        }

        const std::string object_path = eulerpilot_bpf_object_path("network_qos_tc.bpf.o");
        bpf_object_ = bpf_object__open_file(object_path.c_str(), nullptr);
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
        tc_hook_created_ = err == 0;
        tc_hook_preexisting_ = err == -EEXIST;

        tc_opts_ = {};
        tc_opts_.sz = sizeof(tc_opts_);
        tc_opts_.prog_fd = bpf_program__fd(prog);
        tc_opts_.flags = 0;
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
        snapshot.evidence["target_type"] = target_type_;
        snapshot.evidence["tc_hook_created"] = tc_hook_created_ ? "true" : "false";
        snapshot.evidence["tc_hook_preexisting"] = tc_hook_preexisting_ ? "true" : "false";
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
            tc_hook_created_ = false;
        }
        tc_hook_preexisting_ = false;
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
            {"target_type", target_type_},
            {"tc_hook_created", tc_hook_created_ ? "true" : "false"},
            {"tc_hook_preexisting", tc_hook_preexisting_ ? "true" : "false"},
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
            {"target_type", target_type_},
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
            {"tc_filter_handle", std::to_string(tc_opts_.handle)},
            {"tc_filter_priority", std::to_string(tc_opts_.priority)},
            {"qdisc", "tbf"},
        };
        entry.restored = operation == "rollback";
        std::string error;
        append_journal_action(journal_path.string(), entry, &error);
    }

    bool available_ = false;
    bool running_ = false;
    bool tc_hook_created_ = false;
    bool tc_hook_preexisting_ = false;
    bool tc_attached_ = false;
    bool tbf_attached_ = false;
    std::string state_ = "created";
    std::string last_error_;
    std::string hook_ = "tc_egress";
    std::string mode_ = "audit";
    std::string ifname_ = "ep-veth-qos0";
    std::string target_ref_ = "legacy_netdev";
    std::string target_type_ = "netdev";
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


} // namespace

void register_network_qos_skill(SkillRegistry &registry) {
    registry.register_factory("network_qos", [] {
        return std::make_unique<NetworkQosSkill>();
    });
}

} // namespace eulerpilot
