#include "common.hpp"
#include "factories.hpp"

namespace eulerpilot {
namespace {
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
            rule.src_ip = config_value_or(spec, rule_prefix + "src_ip", "");
            rule.dst_ip = config_value_or(spec, rule_prefix + "dst_ip", "");
            rule.src_port = config_value_or(spec, rule_prefix + "src_port", "0");
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
            rule.src_ip = config_value_or(spec, "src_ip", "");
            rule.dst_ip = config_value_or(spec, "dst_ip", "");
            rule.src_port = config_value_or(spec, "src_port", "0");
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
        const std::string object_path = eulerpilot_bpf_object_path("network_xdp.bpf.o");
        if (!file_exists(object_path.c_str())) {
            last_error_ = "network-xdp-bpf-not-built";
            return false;
        }
        const unsigned int ifindex = if_nametoindex(ifname_.c_str());
        if (ifindex == 0) {
            last_error_ = "xdp-ifname-missing";
            return false;
        }
        ifindex_ = static_cast<int>(ifindex);

        bpf_object *obj = bpf_object__open_file(object_path.c_str(), nullptr);
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

        const std::string object_path = eulerpilot_bpf_object_path("network_xdp.bpf.o");
        bpf_object_ = bpf_object__open_file(object_path.c_str(), nullptr);
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
        append_rule_stats(snapshot.evidence);
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
        if (!rule.src_ip.empty() &&
            !parse_ipv4_address(rule.src_ip, rule.src_addr_value)) {
            last_error_ = "invalid-src-ip";
            return false;
        }
        if (!rule.dst_ip.empty() &&
            !parse_ipv4_address(rule.dst_ip, rule.dst_addr_value)) {
            last_error_ = "invalid-dst-ip";
            return false;
        }
        if (rule.src_port == "0") {
            rule.src_port_value = 0;
        } else if (!parse_tcp_port(rule.src_port, rule.src_port_value)) {
            last_error_ = "invalid-src-port";
            return false;
        }
        if (rule.dst_port == "0") {
            rule.dst_port_value = 0;
        } else if (!parse_tcp_port(rule.dst_port, rule.dst_port_value)) {
            last_error_ = "invalid-dst-port";
            return false;
        }
        if ((rule.src_port_value != 0 || rule.dst_port_value != 0) &&
            rule.protocol != "tcp" && rule.protocol != "udp") {
            last_error_ = "network-xdp-port-requires-tcp-or-udp";
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
                config.src_addr = rule.src_addr_value;
                config.dst_addr = rule.dst_addr_value;
                config.src_port = rule.src_port_value;
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

    NetworkXdpRuleStats read_xdp_rule_stats() const {
        NetworkXdpRuleStats rule_stats(kNetworkXdpMaxRules);
        if (!bpf_object_) {
            return rule_stats;
        }
        const int stats_fd = bpf_object__find_map_fd_by_name(bpf_object_, "xdp_stats_map");
        if (stats_fd < 0) {
            return rule_stats;
        }
        for (std::uint32_t key = 0; key < kNetworkXdpMaxRules; ++key) {
            NetworkXdpStats stats;
            if (bpf_map_lookup_elem(stats_fd, &key, &stats) == 0) {
                rule_stats[key] = stats;
            }
        }
        return rule_stats;
    }

    void append_rule_stats(std::map<std::string, std::string> &evidence) const {
        const auto rule_stats = read_xdp_rule_stats();
        std::string summary;
        for (std::size_t i = 0; i < rules_.size() && i < rule_stats.size(); ++i) {
            const auto &rule = rules_[i];
            const auto &stats = rule_stats[i];
            const std::string prefix = "rule." + rule.rule_id + ".";
            evidence[prefix + "protocol"] = rule.protocol;
            evidence[prefix + "src_ip"] = rule.src_ip.empty() ? "any" : rule.src_ip;
            evidence[prefix + "dst_ip"] = rule.dst_ip.empty() ? "any" : rule.dst_ip;
            evidence[prefix + "src_port"] = rule.src_port;
            evidence[prefix + "dst_port"] = rule.dst_port;
            evidence[prefix + "pass_count"] = std::to_string(stats.pass_count);
            evidence[prefix + "drop_count"] = std::to_string(stats.drop_count);
            evidence[prefix + "byte_count"] = std::to_string(stats.byte_count);
            if (!summary.empty()) {
                summary += ";";
            }
            summary += rule.rule_id + ":proto=" + rule.protocol +
                       ",src_ip=" + (rule.src_ip.empty() ? "any" : rule.src_ip) +
                       ",dst_ip=" + (rule.dst_ip.empty() ? "any" : rule.dst_ip) +
                       ",src_port=" + rule.src_port +
                       ",dst_port=" + rule.dst_port +
                       ",pass=" + std::to_string(stats.pass_count) +
                       ",drop=" + std::to_string(stats.drop_count) +
                       ",bytes=" + std::to_string(stats.byte_count);
        }
        evidence["rule_stats"] = summary;
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
        append_rule_stats(event.evidence);
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


} // namespace

void register_network_xdp_skill(SkillRegistry &registry) {
    registry.register_factory("network_xdp", [] {
        return std::make_unique<NetworkXdpSkill>();
    });
}

} // namespace eulerpilot
