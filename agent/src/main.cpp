#include "eulerpilot.hpp"
#include "builtin_skills.hpp"
#include "skill_manager.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    try {
        auto config = eulerpilot::parse_args(argc, argv);
        auto env = eulerpilot::detect_environment();
        eulerpilot::SkillRegistry registry;
        eulerpilot::register_builtin_skills(registry);

        if (config.list_skills_only) {
            for (const auto &name : registry.list()) {
                std::cout << name << "\n";
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
            for (const auto &snapshot : manager.snapshots()) {
                std::cout << snapshot.skill_name
                          << " available=" << (snapshot.available ? "yes" : "no")
                          << " running=" << (snapshot.running ? "yes" : "no")
                          << " state=" << snapshot.state;
                for (const auto &evidence : snapshot.evidence) {
                    std::cout << " " << evidence.first << "=" << evidence.second;
                }
                std::cout << "\n";
            }
            return exit_code;
        }

        std::cout << "EulerPilot Agent starting\n";
        std::cout << "  config: " << config.config_path << "\n";
        std::cout << "  interval_ms: " << config.interval_ms << "\n";
        std::cout << "  duration_s: " << config.duration_s << "\n";
        std::cout << "  warmup_cycles: " << config.warmup_cycles << "\n";
        std::cout << "  mode: " << (config.dry_run ? "dry-run" : "active") << "\n";
        std::cout << "  preferred_backend: " << eulerpilot::to_string(config.preferred_backend) << "\n";
        std::cout << "  gate_mode: " << eulerpilot::to_string(config.gate_mode) << "\n";
        std::cout << "  env.psi: " << (env.psi_configured ? "enabled" : "disabled") << "\n";
        std::cout << "  env.cgroup_v2: " << (env.cgroup_v2_mounted ? "mounted" : "not-mounted") << "\n";
        std::cout << "  env.sched_ext: " << (env.sched_ext_available ? "available" : "not-available") << "\n";

        eulerpilot::SkillManager manager;
        if (!manager.load_from_yaml(config, registry)) {
            throw std::runtime_error(manager.last_error());
        }
        if (!manager.start_enabled_skills()) {
            throw std::runtime_error(manager.last_error());
        }

        auto decisions = eulerpilot::run_cycles(config);
        for (const auto &decision : decisions) {
            std::cout << "[Analyzer] " << decision.sample.comm
                      << " pid=" << decision.sample.pid
                      << " class=" << eulerpilot::to_string(decision.klass)
                      << " latency_score=" << decision.latency_score
                      << " batch_score=" << decision.batch_score
                      << " interference_score=" << decision.interference_score
                      << " managed_target=" << (decision.managed_target ? "yes" : "no")
                      << " gate_relevant=" << (decision.gate_relevant ? "yes" : "no")
                      << " gate_reason=" << decision.gate_reason
                      << " latency_exists=" << (decision.latency_exists ? "yes" : "no")
                      << " background_exists=" << (decision.background_exists ? "yes" : "no")
                      << " cpu_psi_high=" << (decision.cpu_psi_high ? "yes" : "no")
                      << " latency_wait_high=" << (decision.latency_wait_high ? "yes" : "no")
                      << " background_runtime_high=" << (decision.background_runtime_high ? "yes" : "no")
                      << " profile=" << decision.target_profile
                      << " trigger_reason=" << decision.trigger_reason
                      << " executor=" << decision.action.executor
                      << " group=" << decision.action.target_group
                      << " action_profile=" << decision.action.target_profile
                      << " cpu_weight=" << decision.action.cpu_weight
                      << " cpuset=" << decision.action.cpuset_cpus
                      << " applied=" << (decision.action.applied ? "yes" : "no")
                      << " reason=" << decision.action.reason
                      << "\n";
        }

        manager.stop_all();
        std::cout << "EulerPilot Agent run finished. Next step is to improve workload targeting and metrics export.\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "EulerPilot error: " << ex.what() << "\n";
        return 1;
    }
}
