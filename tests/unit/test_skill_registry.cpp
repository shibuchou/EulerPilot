#include "skill_registry.hpp"

#include <cassert>
#include <stdexcept>
#include <string>

namespace {

class DummySkill final : public eulerpilot::Skill {
public:
    std::string name() const override { return "dummy"; }
    bool configure(const eulerpilot::RuntimeConfig &, const eulerpilot::SkillSpec &) override { return true; }
    bool probe() override { return true; }
    bool init() override { return true; }
    bool start() override { return true; }
    eulerpilot::SkillSnapshot snapshot() const override {
        eulerpilot::SkillSnapshot snapshot;
        snapshot.skill_name = name();
        snapshot.available = true;
        snapshot.running = true;
        snapshot.state = "running";
        return snapshot;
    }
    bool rollback() override { return true; }
    void stop() override {}
    std::string last_error() const override { return {}; }
};

} // namespace

int main() {
    eulerpilot::SkillRegistry registry;
    registry.register_factory("z_dummy", [] { return std::make_unique<DummySkill>(); });
    registry.register_factory("a_dummy", [] { return std::make_unique<DummySkill>(); });

    // The registry is the contract used by --list-skills and by SkillManager
    // construction, so it must be deterministic and reject ambiguous entries.
    const auto names = registry.list();
    assert(names.size() == 2);
    assert(names[0] == "a_dummy");
    assert(names[1] == "z_dummy");
    assert(registry.create("a_dummy")->name() == "dummy");

    bool duplicate_rejected = false;
    try {
        registry.register_factory("a_dummy", [] { return std::make_unique<DummySkill>(); });
    } catch (const std::runtime_error &) {
        duplicate_rejected = true;
    }
    assert(duplicate_rejected);

    // Unknown Skill names should fail at construction time instead of silently
    // dropping a configured capability.
    bool unknown_rejected = false;
    try {
        (void)registry.create("missing");
    } catch (const std::runtime_error &) {
        unknown_rejected = true;
    }
    assert(unknown_rejected);

    return 0;
}
