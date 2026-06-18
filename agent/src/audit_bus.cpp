#include "audit_bus.hpp"

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

bool append_audit_event(const std::string &path, const AuditEvent &event, std::string *error) {
    std::ofstream out(path, std::ios::app);
    if (!out.good()) {
        if (error) {
            *error = "failed-open-audit-log";
        }
        return false;
    }

    out << "{"
        << "\"timestamp\":\"" << escape_json(event.timestamp) << "\","
        << "\"event_id\":\"" << escape_json(event.event_id) << "\","
        << "\"skill\":\"" << escape_json(event.skill) << "\","
        << "\"policy_id\":\"" << escape_json(event.policy_id) << "\","
        << "\"rule_id\":\"" << escape_json(event.rule_id) << "\","
        << "\"mode\":\"" << escape_json(event.mode) << "\","
        << "\"target\":";
    write_map(out, event.target);
    out << ",\"operation\":\"" << escape_json(event.operation) << "\","
        << "\"evidence\":";
    write_map(out, event.evidence);
    out << ",\"action\":\"" << escape_json(event.action) << "\","
        << "\"result\":\"" << escape_json(event.result) << "\","
        << "\"severity\":\"" << escape_json(event.severity) << "\""
        << "}\n";
    return true;
}

} // namespace eulerpilot
