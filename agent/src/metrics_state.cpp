#include "metrics_state.hpp"

namespace eulerpilot {

MetricsState &global_metrics_state() {
    static MetricsState state;
    return state;
}

} // namespace eulerpilot
