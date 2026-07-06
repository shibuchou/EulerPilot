#include "builtin_skills.hpp"

#include "builtin_skills/factories.hpp"

namespace eulerpilot {

void register_builtin_skills(SkillRegistry &registry) {
    register_resource_control_skill(registry);
    register_psi_gate_skill(registry);
    register_network_policy_skill(registry);
    register_network_qos_skill(registry);
    register_network_xdp_skill(registry);
    register_security_policy_skill(registry);
    register_policy_engine_skill(registry);
}

} // namespace eulerpilot
