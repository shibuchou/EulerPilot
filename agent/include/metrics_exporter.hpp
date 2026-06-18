#pragma once

#include <string>

namespace eulerpilot {

bool metrics_exporter_start(const std::string &listen_addr);
void metrics_exporter_stop();

} // namespace eulerpilot
