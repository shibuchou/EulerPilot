#pragma once

#include "skill_registry.hpp"

namespace eulerpilot {

void register_resource_control_skill(SkillRegistry &registry);
void register_psi_gate_skill(SkillRegistry &registry);
void register_network_policy_skill(SkillRegistry &registry);
void register_network_qos_skill(SkillRegistry &registry);
void register_network_xdp_skill(SkillRegistry &registry);
void register_security_policy_skill(SkillRegistry &registry);
void register_policy_engine_skill(SkillRegistry &registry);

} // namespace eulerpilot
