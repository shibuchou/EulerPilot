#include "psi_reader.h"

#include <fstream>
#include <sstream>
#include <string>

namespace eulerpilot {

namespace {

PsiWindowValue parse_line(const std::string &line) {
    PsiWindowValue value;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        if (token.rfind("avg10=", 0) == 0) {
            value.avg10 = std::stod(token.substr(6));
        } else if (token.rfind("avg60=", 0) == 0) {
            value.avg60 = std::stod(token.substr(6));
        } else if (token.rfind("avg300=", 0) == 0) {
            value.avg300 = std::stod(token.substr(7));
        } else if (token.rfind("total=", 0) == 0) {
            value.total = std::stoull(token.substr(6));
        }
    }
    return value;
}

PsiResourceSnapshot read_resource(const char *path) {
    PsiResourceSnapshot snapshot;
    std::ifstream file(path);
    if (!file.good()) {
        return snapshot;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("some ", 0) == 0) {
            snapshot.some = parse_line(line);
        } else if (line.rfind("full ", 0) == 0) {
            snapshot.full = parse_line(line);
        }
    }
    return snapshot;
}

} // namespace

PsiSnapshot read_psi_snapshot() {
    PsiSnapshot snapshot;
    snapshot.cpu = read_resource("/proc/pressure/cpu");
    snapshot.memory = read_resource("/proc/pressure/memory");
    snapshot.io = read_resource("/proc/pressure/io");
    return snapshot;
}

} // namespace eulerpilot
