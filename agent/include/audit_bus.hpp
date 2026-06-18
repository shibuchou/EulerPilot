#pragma once

#include <map>
#include <string>

namespace eulerpilot {

struct AuditEvent {
    std::string timestamp;
    std::string event_id;
    std::string skill;
    std::string policy_id;
    std::string rule_id;
    std::string mode = "audit";
    std::map<std::string, std::string> target;
    std::string operation;
    std::map<std::string, std::string> evidence;
    std::string action;
    std::string result;
    std::string severity = "info";
};

bool append_audit_event(const std::string &path, const AuditEvent &event, std::string *error);

} // namespace eulerpilot
