#pragma once

#include <map>
#include <string>

namespace eulerpilot {

struct JournalAction {
    std::string action_id;
    std::string skill;
    std::string transaction_id;
    std::string trigger_event_id;
    std::string policy_id;
    std::string target;
    std::string operation;
    std::string state;
    std::map<std::string, std::string> old_values;
    std::map<std::string, std::string> new_values;
    std::map<std::string, std::string> handles;
    bool restored = false;
};

bool append_journal_action(const std::string &path, const JournalAction &action, std::string *error);

} // namespace eulerpilot
