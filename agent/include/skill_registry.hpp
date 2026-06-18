#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "skill.hpp"

namespace eulerpilot {

using SkillFactory = std::function<std::unique_ptr<Skill>()>;

class SkillRegistry {
public:
    void register_factory(const std::string &name, SkillFactory factory);
    std::unique_ptr<Skill> create(const std::string &name) const;
    std::vector<std::string> list() const;

private:
    std::unordered_map<std::string, SkillFactory> factories_;
};

} // namespace eulerpilot
