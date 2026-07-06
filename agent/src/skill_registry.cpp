#include "skill_registry.hpp"

#include <algorithm>
#include <stdexcept>

namespace eulerpilot {

void SkillRegistry::register_factory(const std::string &name, SkillFactory factory) {
    if (!factory) {
        throw std::runtime_error("skill factory for '" + name + "' is null");
    }
    auto inserted = factories_.emplace(name, std::move(factory));
    if (!inserted.second) {
        throw std::runtime_error("duplicate skill registration: " + name);
    }
}

std::unique_ptr<Skill> SkillRegistry::create(const std::string &name) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        throw std::runtime_error("unknown skill: " + name);
    }
    return it->second();
}

std::vector<std::string> SkillRegistry::list() const {
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto &entry : factories_) {
        names.push_back(entry.first);
    }
    // Keep CLI output and tests deterministic regardless of map iteration
    // order, especially after built-in Skills were split into separate files.
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace eulerpilot
