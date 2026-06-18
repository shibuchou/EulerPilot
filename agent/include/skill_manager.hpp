#pragma once

#include <memory>
#include <map>
#include <string>
#include <vector>

#include "eulerpilot.hpp"
#include "skill.hpp"
#include "skill_registry.hpp"

namespace eulerpilot {

struct SkillsFileConfig {
    int schema_version = 0;
    std::string resolved_path;
    std::vector<SkillSpec> skills;
};

class SkillManager {
public:
    bool load_from_yaml(const RuntimeConfig &runtime_config, const SkillRegistry &registry);
    bool start_enabled_skills();
    int doctor_enabled_skills();
    void rollback_all();
    void stop_all();
    std::vector<SkillSnapshot> snapshots() const;
    const SkillsFileConfig &config() const { return config_; }
    std::string last_error() const { return last_error_; }

private:
    bool validate_and_prepare();
    bool topo_sort_enabled_skills();

    SkillsFileConfig config_;
    std::vector<std::unique_ptr<Skill>> created_skills_;
    std::vector<std::size_t> enabled_indices_;
    std::vector<std::size_t> start_order_;
    std::string last_error_;
};

std::string resolve_skills_config_path(const std::string &agent_config_path, const std::string &skills_config_path);
SkillsFileConfig parse_skills_file(const std::string &resolved_path);

} // namespace eulerpilot
