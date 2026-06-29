#include "action_journal.hpp"

#include <fstream>
#include <sstream>

namespace eulerpilot {

namespace {

std::string escape_json(const std::string &value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    return out.str();
}

void write_map(std::ostream &out, const std::map<std::string, std::string> &values) {
    bool first = true;
    out << "{";
    for (const auto &item : values) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << escape_json(item.first) << "\":\"" << escape_json(item.second) << "\"";
    }
    out << "}";
}

} // namespace

bool append_journal_action(const std::string &path, const JournalAction &action, std::string *error) {
    std::ofstream out(path, std::ios::app);
    if (!out.good()) {
        if (error) {
            *error = "failed-open-action-journal";
        }
        return false;
    }

    out << "{"
        << "\"action_id\":\"" << escape_json(action.action_id) << "\","
        << "\"skill\":\"" << escape_json(action.skill) << "\",";
    if (!action.transaction_id.empty()) {
        out << "\"transaction_id\":\"" << escape_json(action.transaction_id) << "\",";
    }
    if (!action.trigger_event_id.empty()) {
        out << "\"trigger_event_id\":\"" << escape_json(action.trigger_event_id) << "\",";
    }
    if (!action.policy_id.empty()) {
        out << "\"policy_id\":\"" << escape_json(action.policy_id) << "\",";
    }
    out
        << "\"target\":\"" << escape_json(action.target) << "\","
        << "\"operation\":\"" << escape_json(action.operation) << "\","
        << "\"old_values\":";
    write_map(out, action.old_values);
    out << ",\"new_values\":";
    write_map(out, action.new_values);
    out << ",\"handles\":";
    write_map(out, action.handles);
    out << ",\"restored\":" << (action.restored ? "true" : "false")
        << "}\n";
    return true;
}

} // namespace eulerpilot
