#include "eulerpilot.hpp"
#include "builtin_skills.hpp"
#include "capability_detector.hpp"
#include "metrics_exporter.hpp"
#include "metrics_state.hpp"
#include "skill_manager.hpp"

#include "color.hpp"

#include <yaml-cpp/yaml.h>

#include <exception>
#include <csignal>
#include <iomanip>
#include <iostream>

namespace clr = eulerpilot::color;

namespace {

void handle_shutdown_signal(int) {
    // Signal handlers only set a flag; the main flow still owns Skill cleanup.
    eulerpilot::request_shutdown();
}

void install_shutdown_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
}

struct MetricsExporterGuard {
    bool started = false;

    explicit MetricsExporterGuard(const eulerpilot::RuntimeConfig &config) {
        if (config.metrics_enabled && !config.metrics_listen.empty()) {
            started = eulerpilot::metrics_exporter_start(config.metrics_listen);
        }
    }

    ~MetricsExporterGuard() {
        if (started) {
            eulerpilot::metrics_exporter_stop();
        }
    }
};

std::string bar(std::size_t width = 50) {
    return std::string(width, '-');
}

std::string escape_json(const std::string &value) {
    std::string out;
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

void apply_backend_from_yaml(eulerpilot::RuntimeConfig &config, const std::string &backend) {
    if (backend == "cgroup_v2") {
        config.preferred_backend = eulerpilot::ExecutorBackend::CgroupV2;
    } else if (backend == "sched_ext") {
        config.preferred_backend = eulerpilot::ExecutorBackend::SchedExt;
    } else {
        throw std::runtime_error("unknown scheduler.type in config: " + backend);
    }
}

void apply_agent_yaml_config(eulerpilot::RuntimeConfig &config) {
    YAML::Node root = YAML::LoadFile(config.config_path);
    if (root["scheduler"]) {
        const auto scheduler = root["scheduler"];
        if (scheduler["type"] && !config.backend_cli_set) {
            apply_backend_from_yaml(config, scheduler["type"].as<std::string>());
        }
        if (scheduler["binary"]) {
            config.scheduler_binary_path = scheduler["binary"].as<std::string>();
            config.scheduler_binary_source = "yaml:scheduler.binary";
        }
    }
    if (root["exporter"] && root["exporter"]["prometheus"]) {
        const auto pm = root["exporter"]["prometheus"];
        if (pm["enabled"]) config.metrics_enabled = pm["enabled"].as<bool>();
        if (pm["listen"]) config.metrics_listen = pm["listen"].as<std::string>();
    }
}

void print_status_json(const std::vector<eulerpilot::SkillSnapshot> &snapshots) {
    std::cout << "{\"skills\":[";
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const auto &snapshot = snapshots[i];
        if (i > 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":\"" << escape_json(snapshot.skill_name) << "\","
                  << "\"available\":" << (snapshot.available ? "true" : "false") << ","
                  << "\"running\":" << (snapshot.running ? "true" : "false") << ","
                  << "\"state\":\"" << escape_json(snapshot.state) << "\","
                  << "\"evidence\":{";
        bool first = true;
        for (const auto &item : snapshot.evidence) {
            if (!first) {
                std::cout << ",";
            }
            first = false;
            std::cout << "\"" << escape_json(item.first) << "\":\""
                      << escape_json(item.second) << "\"";
        }
        std::cout << "}}";
    }
    std::cout << "]}\n";
}

void print_doctor_json(const eulerpilot::RuntimeConfig &config,
                       const eulerpilot::CapabilitySnapshot &capabilities,
                       const std::vector<eulerpilot::SkillSnapshot> &snapshots,
                       int exit_code) {
    std::cout << "{\"strict\":" << (config.strict ? "true" : "false")
              << ",\"doctor_mode\":\""
              << (config.doctor_safe_only ? "safe" : "live-probe") << "\""
              << ",\"exit_code\":" << exit_code
              << ",\"backend\":\"" << escape_json(eulerpilot::to_string(config.preferred_backend)) << "\""
              << ",\"capabilities\":{";
    bool first = true;
    for (const auto &probe : capabilities.probes) {
        if (!first) {
            std::cout << ",";
        }
        first = false;
        std::cout << "\"" << escape_json(probe.first) << "\":{"
                  << "\"available\":" << (probe.second.available ? "true" : "false") << ","
                  << "\"evidence\":\"" << escape_json(probe.second.evidence) << "\"}";
    }
    std::cout << "},\"skills\":[";
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const auto &snapshot = snapshots[i];
        if (i > 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":\"" << escape_json(snapshot.skill_name) << "\","
                  << "\"available\":" << (snapshot.available ? "true" : "false") << ","
                  << "\"running\":" << (snapshot.running ? "true" : "false") << ","
                  << "\"state\":\"" << escape_json(snapshot.state) << "\","
                  << "\"evidence\":{";
        bool first_evidence = true;
        for (const auto &item : snapshot.evidence) {
            if (!first_evidence) {
                std::cout << ",";
            }
            first_evidence = false;
            std::cout << "\"" << escape_json(item.first) << "\":\""
                      << escape_json(item.second) << "\"";
        }
        std::cout << "}}";
    }
    std::cout << "]}\n";
}

void print_status_text(const std::vector<eulerpilot::SkillSnapshot> &snapshots) {
    for (const auto &snapshot : snapshots) {
        std::cout << snapshot.skill_name << " " << snapshot.state;
        auto reason = snapshot.evidence.find("reason");
        if (reason != snapshot.evidence.end() && reason->second != "ok") {
            std::cout << " [" << reason->second << "]";
        }
        std::cout << "\n";
    }
}

void print_banner(const eulerpilot::RuntimeConfig &config, const eulerpilot::EnvironmentStatus &env) {
    std::cout << "\n"
              << clr::cyan_() << clr::b() << "  * EulerPilot Agent " << clr::r() << "\n"
              << clr::dim_() << "  " << bar() << clr::r() << "\n";

    auto tick = [](bool ok) -> const char* {
        return ok ? "ok" : "--";
    };

    std::cout << "  " << clr::dim_() << "config:     " << clr::r() << config.config_path << "\n"
              << "  " << clr::dim_() << "interval:   " << clr::r() << config.interval_ms << "ms  "
              << clr::dim_() << "duration:   " << clr::r() << config.duration_s << "s  "
              << clr::dim_() << "warmup: " << clr::r() << config.warmup_cycles << "\n"
              << "  " << clr::dim_() << "mode:       " << clr::r()
              << (config.dry_run ? clr::yellow_() : clr::green_())
              << (config.dry_run ? "dry-run" : "active") << clr::r() << "  "
              << clr::dim_() << "backend:    " << clr::r()
              << (config.preferred_backend == eulerpilot::ExecutorBackend::SchedExt ? clr::magenta_() : clr::blue_())
              << eulerpilot::to_string(config.preferred_backend) << clr::r() << "\n"
              << "  " << clr::dim_() << "scx binary: " << clr::r()
              << (config.scheduler_binary_path.empty() ? "auto" : config.scheduler_binary_path)
              << clr::dim_() << " (" << config.scheduler_binary_source << ")" << clr::r() << "\n"
              << "  " << clr::dim_() << "gate:       " << clr::r()
              << eulerpilot::to_string(config.gate_mode) << "\n"
              << "  " << clr::dim_() << "env:        " << clr::r()
              << " psi:"   << (env.psi_configured ? clr::green_() : clr::red_()) << tick(env.psi_configured) << clr::r()
              << "  cg:" << (env.cgroup_v2_mounted ? clr::green_() : clr::red_()) << tick(env.cgroup_v2_mounted) << clr::r()
              << "  scx:" << (env.sched_ext_available ? clr::green_() : clr::red_()) << tick(env.sched_ext_available) << clr::r()
              << "\n";

    std::cout << clr::dim_() << "  " << bar() << clr::r() << "\n\n";
}

void print_decision(const eulerpilot::WorkloadDecision &d, bool verbose, bool jsonl) {
    using eulerpilot::WorkloadClass;

    if (jsonl) {
        std::cout << "{"
                  << "\"comm\":\"" << d.sample.comm << "\""
                  << ",\"pid\":" << d.sample.pid
                  << ",\"tgid\":" << d.sample.tgid
                  << ",\"start_boottime_ns\":" << d.sample.start_boottime_ns
                  << ",\"generation_cookie\":" << d.sample.generation_cookie
                  << ",\"identity_source\":\"" << d.sample.identity_source << "\""
                  << ",\"class\":\"" << eulerpilot::to_string(d.klass) << "\""
                  << ",\"latency_score\":" << d.latency_score
                  << ",\"batch_score\":" << d.batch_score
                  << ",\"interference_score\":" << d.interference_score
                  << ",\"profile\":\"" << d.target_profile << "\""
                  << ",\"cpu_weight\":" << d.action.cpu_weight
                  << ",\"applied\":" << (d.action.applied ? "true" : "false")
                  << ",\"reason\":\"" << d.action.reason << "\""
                  << ",\"managed_target\":" << (d.managed_target ? "true" : "false")
                  << ",\"gate_relevant\":" << (d.gate_relevant ? "true" : "false")
                  << ",\"gate_reason\":\"" << d.gate_reason << "\""
                  << ",\"latency_exists\":" << (d.latency_exists ? "true" : "false")
                  << ",\"background_exists\":" << (d.background_exists ? "true" : "false")
                  << ",\"cpu_psi_high\":" << (d.cpu_psi_high ? "true" : "false")
                  << ",\"latency_wait_high\":" << (d.latency_wait_high ? "true" : "false")
                  << ",\"background_runtime_high\":" << (d.background_runtime_high ? "true" : "false")
                  << ",\"trigger_reason\":\"" << d.trigger_reason << "\""
                  << ",\"adaptive_thresholds_enabled\":" << (d.adaptive_thresholds_enabled ? "true" : "false")
                  << ",\"adaptive_thresholds_calibrated\":" << (d.adaptive_thresholds_calibrated ? "true" : "false")
                  << ",\"calibrated_latency_wait_threshold_ns\":" << d.calibrated_latency_wait_threshold_ns
                  << ",\"calibrated_background_runtime_threshold_ns\":" << d.calibrated_background_runtime_threshold_ns
                  << ",\"calibrated_cpu_psi_threshold\":" << d.calibrated_cpu_psi_threshold
                  << ",\"executor\":\"" << d.action.executor << "\""
                  << ",\"group\":\"" << d.action.target_group << "\""
                  << ",\"target_ref\":\"" << d.action.target_ref << "\""
                  << ",\"target_cgroup_path\":\"" << d.action.target_cgroup_path << "\""
                  << "}\n";
        return;
    }

    const char *cc = clr::dim_();
    const char *icon = " ";
    switch (d.klass) {
    case WorkloadClass::LatencySensitive:  cc = clr::green_();  icon = "L"; break;
    case WorkloadClass::BackgroundNoisy:   cc = clr::red_();    icon = "B"; break;
    case WorkloadClass::ThroughputBatch:   cc = clr::yellow_(); icon = "T"; break;
    default: icon = "."; break;
    }

    const char *ac = clr::dim_();
    const char *ai = "-";
    if (d.action.applied) {
        ac = clr::green_();
        ai = "+";
    } else if (d.managed_target) {
        ac = clr::yellow_();
        ai = "~";
    }

    std::cout << "  " << cc << icon << clr::r() << " "
              << clr::b() << std::left << std::setw(18) << d.sample.comm.substr(0, 17) << clr::r()
              << std::right << std::setw(6) << d.sample.pid
              << " " << cc << std::left << std::setw(12) << eulerpilot::to_string(d.klass) << clr::r()
              << " |" << ac << ai << clr::r()
              << " " << std::left << std::setw(10) << d.action.target_profile
              << clr::dim_() << " w:" << d.action.cpu_weight << clr::r()
              << " " << clr::dim_() << d.action.reason << clr::r()
              << "\n";

    if (verbose) {
        std::cout << clr::dim_()
                  << "    scores: latency=" << d.latency_score
                  << " batch=" << d.batch_score
                  << " interference=" << d.interference_score << "\n"
                  << "    gate: relevant=" << (d.gate_relevant ? "yes" : "no")
                  << " reason=" << d.gate_reason << "\n"
                  << "    identity: tgid=" << d.sample.tgid
                  << " start_boottime_ns=" << d.sample.start_boottime_ns
                  << " generation_cookie=" << d.sample.generation_cookie
                  << " source=" << d.sample.identity_source << "\n"
                  << "    evidence: latency_exists=" << (d.latency_exists ? "yes" : "no")
                  << " background_exists=" << (d.background_exists ? "yes" : "no")
                  << " cpu_psi_high=" << (d.cpu_psi_high ? "yes" : "no")
                  << " latency_wait_high=" << (d.latency_wait_high ? "yes" : "no")
                  << " background_runtime_high=" << (d.background_runtime_high ? "yes" : "no") << "\n"
                  << "    adaptive_thresholds: enabled=" << (d.adaptive_thresholds_enabled ? "yes" : "no")
                  << " calibrated=" << (d.adaptive_thresholds_calibrated ? "yes" : "no")
                  << " latency_wait_ns=" << d.calibrated_latency_wait_threshold_ns
                  << " background_runtime_ns=" << d.calibrated_background_runtime_threshold_ns
                  << " cpu_psi=" << d.calibrated_cpu_psi_threshold << "\n"
                  << "    action: executor=" << d.action.executor
                  << " group=" << d.action.target_group
                  << " applied=" << (d.action.applied ? "yes" : "no")
                  << " cpuset=" << d.action.cpuset_cpus << "\n"
                  << clr::r();
    }
}

void print_summary(const std::vector<eulerpilot::WorkloadDecision> &decisions) {
    int latency = 0, batch = 0, noisy = 0, unknown = 0, applied = 0;
    for (const auto &d : decisions) {
        if (d.action.applied) applied++;
        switch (d.klass) {
        case eulerpilot::WorkloadClass::LatencySensitive: latency++; break;
        case eulerpilot::WorkloadClass::ThroughputBatch:  batch++;   break;
        case eulerpilot::WorkloadClass::BackgroundNoisy:  noisy++;   break;
        default: unknown++; break;
        }
    }

    std::cout << clr::dim_() << "  " << bar() << clr::r() << "\n"
              << "  " << clr::b() << "DONE" << clr::r()
              << "  total=" << decisions.size()
              << "  applied=" << clr::green_() << applied << clr::r()
              << "  L=" << clr::green_() << latency << clr::r()
              << "  T=" << clr::yellow_() << batch << clr::r()
              << "  B=" << clr::red_() << noisy << clr::r()
              << "  U=" << clr::dim_() << unknown << clr::r()
              << "\n\n";
}

} // anonymous namespace

int main(int argc, char **argv) {
    install_shutdown_signal_handlers();
    eulerpilot::SkillManager manager;
    try {
        auto config = eulerpilot::parse_args(argc, argv);
        auto env = eulerpilot::detect_environment();
        eulerpilot::SkillRegistry registry;
        eulerpilot::register_builtin_skills(registry);

        if (config.list_skills_only) {
            for (const auto &name : registry.list()) {
                std::cout << clr::cyan_() << name << clr::r() << "\n";
            }
            return 0;
        }

        apply_agent_yaml_config(config);

        if (config.validate_config_only) {
            if (!manager.load_from_yaml(config, registry)) {
                std::cerr << "EulerPilot config invalid: " << manager.last_error() << "\n";
                return 1;
            }
            if (config.jsonl) {
                std::cout << "{\"config\":\"" << escape_json(config.config_path)
                          << "\",\"result\":\"valid\"}\n";
            } else {
                std::cout << "EulerPilot config valid: " << config.config_path << "\n";
            }
            return 0;
        }

        if (config.status_only) {
            if (!manager.load_from_yaml(config, registry)) {
                std::cerr << "EulerPilot error: " << manager.last_error() << "\n";
                return 1;
            }
            if (config.jsonl) {
                print_status_json(manager.snapshots());
            } else {
                print_status_text(manager.snapshots());
            }
            return 0;
        }

        if (config.doctor_safe_only || config.doctor_skills_only) {
            eulerpilot::SkillManager manager;
            if (!manager.load_from_yaml(config, registry)) {
                std::cerr << "EulerPilot error: " << manager.last_error() << "\n";
                return 1;
            }
            auto capabilities = eulerpilot::detect_capabilities();
            int exit_code = 0;
            if (config.doctor_skills_only) {
                exit_code = manager.doctor_enabled_skills();
            }
            if (config.jsonl) {
                print_doctor_json(config, capabilities, manager.snapshots(), exit_code);
                return exit_code;
            }
            std::cout << "\n" << clr::yellow_() << clr::b()
                      << "  Doctor mode: "
                      << (config.doctor_safe_only ? "safe (no BPF/TC/XDP/LSM probe)"
                                                  : "live-probe (may load short-lived probes)")
                      << clr::r() << "\n";
            std::cout << "\n" << clr::cyan_() << clr::b() << "  Capability Detector" << clr::r() << "\n"
                      << clr::dim_() << "  " << bar() << clr::r() << "\n";
            for (const auto &probe : capabilities.probes) {
                const char *pc = probe.second.available ? clr::green_() : clr::yellow_();
                const char *pi = probe.second.available ? "+" : "!";
                std::cout << "  " << pc << pi << clr::r() << " "
                          << clr::b() << std::left << std::setw(18) << probe.first << clr::r()
                          << " " << (probe.second.available ? "available" : "missing")
                          << " " << clr::dim_() << probe.second.evidence << clr::r() << "\n";
            }
            std::cout << clr::dim_() << "  " << bar() << clr::r() << "\n";
            std::cout << "\n" << clr::cyan_() << clr::b() << "  Skills Doctor" << clr::r() << "\n"
                      << clr::dim_() << "  " << bar() << clr::r() << "\n";
            for (const auto &snapshot : manager.snapshots()) {
                const bool safe_skip = config.doctor_safe_only;
                const char *sc = safe_skip ? clr::yellow_()
                                           : (snapshot.available ? clr::green_() : clr::red_());
                const char *si = safe_skip ? "!" : (snapshot.available ? "+" : "x");
                std::cout << "  " << sc << si << clr::r() << " "
                          << clr::b() << std::left << std::setw(22) << snapshot.skill_name << clr::r()
                          << " " << (safe_skip ? "safe-not-probed" : snapshot.state);
                auto reason = snapshot.evidence.find("reason");
                if (!safe_skip && reason != snapshot.evidence.end() && reason->second != "ok") {
                    std::cout << " " << clr::red_() << "[" << reason->second << "]" << clr::r();
                }
                std::cout << "\n";
            }
            std::cout << clr::dim_() << "  " << bar() << clr::r() << "\n\n";
            return exit_code;
        }

        if (!config.jsonl) {
            print_banner(config, env);
        }

        {
            YAML::Node agent_root = YAML::LoadFile(config.config_path);
            if (agent_root["exporter"] && agent_root["exporter"]["prometheus"]) {
                auto pm = agent_root["exporter"]["prometheus"];
                if (pm["enabled"]) config.metrics_enabled = pm["enabled"].as<bool>();
                if (pm["listen"]) config.metrics_listen = pm["listen"].as<std::string>();
            }
        }

        if (!manager.load_from_yaml(config, registry)) {
            throw std::runtime_error(manager.last_error());
        }
        if (!manager.start_enabled_skills()) {
            throw std::runtime_error(manager.last_error());
        }

        auto &metrics = eulerpilot::global_metrics_state();
        for (const auto &snap : manager.snapshots()) {
            if (snap.skill_name == "resource_control")
                metrics.skill_resource_control_running.store(snap.running ? 1 : 0);
            else if (snap.skill_name == "psi_gate")
                metrics.skill_psi_gate_running.store(snap.running ? 1 : 0);
        }

        if (!config.jsonl) {
            std::cout << clr::cyan_() << "  --- legend ---" << clr::r() << "\n"
                      << "  " << clr::dim_() << "mark: " << clr::r()
                      << ". UNKNOWN  " << clr::green_() << "L" << clr::r() << " LATENCY_SENSITIVE  "
                      << clr::red_() << "B" << clr::r() << " BACKGROUND_NOISY  "
                      << clr::yellow_() << "T" << clr::r() << " THROUGHPUT_BATCH\n"
                      << "  " << clr::dim_() << "act:  " << clr::r()
                      << clr::green_() << "+" << clr::r() << " applied  "
                      << clr::yellow_() << "~" << clr::r() << " managed  "
                      << "- none\n";

            std::cout << clr::cyan_() << "  --- observing ---" << clr::r() << "\n"
                      << clr::dim_()
                      << "  mark name             pid  class        act profile    w:weight reason"
                      << clr::r() << "\n";
        }

        MetricsExporterGuard metrics_guard(config);

        auto decisions = eulerpilot::run_cycles(config);
        for (const auto &decision : decisions) {
            if (!config.verbose && !config.jsonl &&
                decision.klass == eulerpilot::WorkloadClass::Unknown &&
                !decision.gate_relevant) {
                continue;
            }
            print_decision(decision, config.verbose, config.jsonl);
        }

        manager.stop_all();
        if (!config.jsonl) {
            print_summary(decisions);
        }
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << clr::red_() << "EulerPilot error: " << ex.what() << clr::r() << "\n";
        try {
            manager.stop_all();
        } catch (...) {
            // cleanup failure must not hide original error
        }
        return 1;
    }
}
