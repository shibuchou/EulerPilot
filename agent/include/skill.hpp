#pragma once

#include <map>
#include <string>
#include <vector>

namespace eulerpilot {

struct RuntimeConfig;

struct SkillSpec {
    std::string name;
    std::string kind;
    bool enabled = false;
    std::map<std::string, std::string> config;
};

struct SkillSnapshot {
    std::string skill_name;
    bool available = false;
    bool running = false;
    std::string state = "unknown";
    std::map<std::string, std::string> evidence;
};

class Skill {
public:
    virtual ~Skill() = default;
    virtual std::string name() const = 0;
    virtual std::vector<std::string> dependencies() const { return {}; }
    virtual bool configure(const RuntimeConfig &runtime_config, const SkillSpec &spec) = 0;
    virtual bool probe() = 0;
    virtual bool init() = 0;
    virtual bool start() = 0;
    virtual SkillSnapshot snapshot() const = 0;
    virtual bool rollback() = 0;
    virtual void stop() = 0;
    virtual std::string last_error() const = 0;
};

} // namespace eulerpilot
