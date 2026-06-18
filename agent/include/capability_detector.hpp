#pragma once

#include <map>
#include <string>

namespace eulerpilot {

struct CapabilityProbe {
    bool available = false;
    std::string evidence;
};

struct CapabilitySnapshot {
    std::map<std::string, CapabilityProbe> probes;
};

CapabilitySnapshot detect_capabilities();

} // namespace eulerpilot
