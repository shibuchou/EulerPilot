#include "skill_manager.hpp"

#include <filesystem>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace eulerpilot {

namespace {

void flatten_config_node(const YAML::Node &node,
                         const std::string &prefix,
                         std::map<std::string, std::string> &out) {
    if (node.IsScalar()) {
        out[prefix] = node.as<std::string>();
        return;
    }
    if (node.IsMap()) {
        for (const auto &item : node) {
            const std::string key = item.first.as<std::string>();
            const std::string child_prefix = prefix.empty() ? key : prefix + "." + key;
            flatten_config_node(item.second, child_prefix, out);
        }
        return;
    }
    if (node.IsSequence()) {
        for (std::size_t i = 0; i < node.size(); ++i) {
            const std::string child_prefix = prefix + "." + std::to_string(i);
            flatten_config_node(node[i], child_prefix, out);
        }
    }
}

} // namespace

std::string resolve_skills_config_path(const std::string &agent_config_path, const std::string &skills_config_path) {
    fs::path agent_path = fs::weakly_canonical(fs::path(agent_config_path));
    fs::path resolved = agent_path.parent_path() / skills_config_path;
    return fs::weakly_canonical(resolved).string();
}

SkillsFileConfig parse_skills_file(const std::string &resolved_path) {
    YAML::Node root = YAML::LoadFile(resolved_path);
    SkillsFileConfig config;
    config.resolved_path = resolved_path;

    if (!root["schema_version"]) {
        throw std::runtime_error("skills.yaml missing schema_version");
    }
    config.schema_version = root["schema_version"].as<int>();
    if (config.schema_version != 1 && config.schema_version != 2) {
        throw std::runtime_error("unsupported skills.yaml schema_version: " + std::to_string(config.schema_version));
    }

    if (!root["skills"] || !root["skills"].IsSequence()) {
        throw std::runtime_error("skills.yaml missing skills sequence");
    }

    std::unordered_map<std::string, bool> seen;
    for (const auto &node : root["skills"]) {
        SkillSpec entry;
        entry.name = node["name"].as<std::string>();
        entry.kind = node["kind"].as<std::string>();
        entry.enabled = node["enabled"].as<bool>();
        if (!node["config"] || !node["config"].IsMap()) {
            throw std::runtime_error("skill '" + entry.name + "' missing config map");
        }
        if (!seen.emplace(entry.name, true).second) {
            throw std::runtime_error("duplicate skill name in skills.yaml: " + entry.name);
        }
        flatten_config_node(node["config"], "", entry.config);
        config.skills.push_back(std::move(entry));
    }

    return config;
}

bool SkillManager::load_from_yaml(const RuntimeConfig &runtime_config, const SkillRegistry &registry) {
    try {
        YAML::Node agent_root = YAML::LoadFile(runtime_config.config_path);
        auto skills_path_node = agent_root["skills_config_path"];
        if (!skills_path_node) {
            throw std::runtime_error("agent.yaml missing skills_config_path");
        }
        auto resolved_path = resolve_skills_config_path(runtime_config.config_path, skills_path_node.as<std::string>());
        config_ = parse_skills_file(resolved_path);

        created_skills_.clear();
        enabled_indices_.clear();
        start_order_.clear();
        for (const auto &entry : config_.skills) {
            if (entry.kind != "runtime") {
                throw std::runtime_error("skill '" + entry.name + "' has unsupported kind: " + entry.kind);
            }
            auto skill = registry.create(entry.name);
            if (!skill->configure(runtime_config, entry)) {
                throw std::runtime_error("failed to configure skill '" + entry.name + "': " + skill->last_error());
            }
            created_skills_.push_back(std::move(skill));
        }
        return validate_and_prepare();
    } catch (const std::exception &ex) {
        last_error_ = ex.what();
        created_skills_.clear();
        enabled_indices_.clear();
        start_order_.clear();
        return false;
    }
}

bool SkillManager::validate_and_prepare() {
    enabled_indices_.clear();
    for (std::size_t i = 0; i < config_.skills.size(); ++i) {
        if (config_.skills[i].enabled) {
            enabled_indices_.push_back(i);
        }
    }
    return topo_sort_enabled_skills();
}

bool SkillManager::topo_sort_enabled_skills() {
    start_order_.clear();
    std::unordered_map<std::string, std::size_t> enabled_lookup;
    for (auto index : enabled_indices_) {
        enabled_lookup.emplace(config_.skills[index].name, index);
    }

    std::unordered_map<std::size_t, int> indegree;
    std::unordered_map<std::size_t, std::vector<std::size_t>> graph;
    for (auto index : enabled_indices_) {
        indegree[index] = 0;
    }

    for (auto index : enabled_indices_) {
        for (const auto &dep : created_skills_[index]->dependencies()) {
            auto it = enabled_lookup.find(dep);
            if (it == enabled_lookup.end()) {
                last_error_ = "missing or disabled dependency '" + dep + "' for skill '" + config_.skills[index].name + "'";
                return false;
            }
            graph[it->second].push_back(index);
            indegree[index] += 1;
        }
    }

    std::queue<std::size_t> ready;
    for (auto index : enabled_indices_) {
        if (indegree[index] == 0) {
            ready.push(index);
        }
    }

    while (!ready.empty()) {
        auto index = ready.front();
        ready.pop();
        start_order_.push_back(index);
        for (auto next : graph[index]) {
            indegree[next] -= 1;
            if (indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (start_order_.size() != enabled_indices_.size()) {
        last_error_ = "cyclic skill dependency detected";
        return false;
    }
    return true;
}

bool SkillManager::start_enabled_skills() {
    for (auto index : start_order_) {
        auto &skill = created_skills_[index];
        if (!skill->probe() || !skill->init() || !skill->start()) {
            last_error_ = "failed to start skill '" + skill->name() + "': " + skill->last_error();
            rollback_all();
            stop_all();
            return false;
        }
    }
    return true;
}

int SkillManager::doctor_enabled_skills() {
    int exit_code = 0;
    for (std::size_t i = 0; i < created_skills_.size(); ++i) {
        auto &skill = created_skills_[i];
        bool enabled = config_.skills[i].enabled;
        bool ok = skill->probe();
        if (enabled && !ok) {
            exit_code = 1;
        }
    }
    return exit_code;
}

void SkillManager::rollback_all() {
    for (auto it = start_order_.rbegin(); it != start_order_.rend(); ++it) {
        created_skills_[*it]->rollback();
    }
}

void SkillManager::stop_all() {
    for (auto it = start_order_.rbegin(); it != start_order_.rend(); ++it) {
        created_skills_[*it]->stop();
    }
}

std::vector<SkillSnapshot> SkillManager::snapshots() const {
    std::vector<SkillSnapshot> out;
    out.reserve(created_skills_.size());
    for (const auto &skill : created_skills_) {
        out.push_back(skill->snapshot());
    }
    return out;
}

} // namespace eulerpilot
