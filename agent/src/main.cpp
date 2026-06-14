#include "eulerpilot.hpp"
#include "builtin_skills.hpp"
#include "skill_manager.hpp"

#include "color.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

namespace clr = eulerpilot::color;

namespace {

std::string bar(std::size_t width = 50) {
    return std::string(width, '-');
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
                  << ",\"executor\":\"" << d.action.executor << "\""
                  << ",\"group\":\"" << d.action.target_group << "\""
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
                  << "    evidence: latency_exists=" << (d.latency_exists ? "yes" : "no")
                  << " background_exists=" << (d.background_exists ? "yes" : "no")
                  << " cpu_psi_high=" << (d.cpu_psi_high ? "yes" : "no")
                  << " latency_wait_high=" << (d.latency_wait_high ? "yes" : "no")
                  << " background_runtime_high=" << (d.background_runtime_high ? "yes" : "no") << "\n"
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
              << "  u=" << clr::dim_() << unknown << clr::r()
              << "\n\n";
}

} // anonymous namespace

int main(int argc, char **argv) {
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

        if (config.doctor_skills_only) {
            eulerpilot::SkillManager manager;
            if (!manager.load_from_yaml(config, registry)) {
                std::cerr << "EulerPilot error: " << manager.last_error() << "\n";
                return 1;
            }
            int exit_code = manager.doctor_enabled_skills();
            std::cout << "\n" << clr::cyan_() << clr::b() << "  Skills Doctor" << clr::r() << "\n"
                      << clr::dim_() << "  " << bar() << clr::r() << "\n";
            for (const auto &snapshot : manager.snapshots()) {
                const char *sc = snapshot.available ? clr::green_() : clr::red_();
                const char *si = snapshot.available ? "+" : "x";
                std::cout << "  " << sc << si << clr::r() << " "
                          << clr::b() << std::left << std::setw(22) << snapshot.skill_name << clr::r()
                          << " " << snapshot.state;
                auto reason = snapshot.evidence.find("reason");
                if (reason != snapshot.evidence.end() && reason->second != "ok") {
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

        eulerpilot::SkillManager manager;
        if (!manager.load_from_yaml(config, registry)) {
            throw std::runtime_error(manager.last_error());
        }
        if (!manager.start_enabled_skills()) {
            throw std::runtime_error(manager.last_error());
        }

        if (!config.jsonl) {
            std::cout << clr::cyan_() << "  --- observing ---" << clr::r() << "\n"
                      << clr::dim_() << "  " << " mark name             pid  class        act profile     w:weight reason" << clr::r() << "\n"
                      << "  " << clr::dim_() << " legend: " << clr::r()
                      << ". observe  " << clr::green_() << "+ apply" << clr::r()
                      << "  " << clr::green_() << "L" << clr::r() << " latency  "
                      << clr::red_() << "B" << clr::r() << " bg  "
                      << clr::yellow_() << "T" << clr::r() << " batch\n";
        }

        auto decisions = eulerpilot::run_cycles(config);
        for (const auto &decision : decisions) {
            print_decision(decision, config.verbose, config.jsonl);
        }

        manager.stop_all();
        if (!config.jsonl) {
            print_summary(decisions);
        }
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << clr::red_() << "EulerPilot error: " << ex.what() << clr::r() << "\n";
        return 1;
    }
}
