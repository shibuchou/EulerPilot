#pragma once

#include <atomic>
#include <cstdint>

namespace eulerpilot {

struct MetricsState {
    std::atomic<std::uint64_t> cycles_total{0};
    std::atomic<std::uint64_t> observed_tasks{0};
    std::atomic<std::uint64_t> classified_latency{0};
    std::atomic<std::uint64_t> classified_background{0};
    std::atomic<std::uint64_t> classified_unknown{0};
    std::atomic<std::uint64_t> decisions_applied{0};
    std::atomic<int> gate_state{0};
    std::atomic<int> scx_ready{0};
    std::atomic<double> psi_cpu_avg10{0.0};
    std::atomic<int> skill_resource_control_running{0};
    std::atomic<int> skill_psi_gate_running{0};
};

MetricsState &global_metrics_state();

} // namespace eulerpilot
