#include "skill_runtime_context.hpp"

namespace eulerpilot {

SkillRuntimeContext &global_skill_runtime_context() {
    static SkillRuntimeContext context;
    return context;
}

} // namespace eulerpilot
