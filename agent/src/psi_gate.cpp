#include "psi_gate.hpp"

#include <bpf/bpf.h>

#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace eulerpilot {

namespace {

struct gate_state_value {
    std::uint32_t state;
    std::uint32_t generation;
    std::uint64_t updated_at_ns;
    std::uint32_t evidence_mask;
    std::uint32_t reserved;
};

constexpr std::uint32_t EVIDENCE_CPU_PSI = 1U << 0;
constexpr std::uint32_t EVIDENCE_LATENCY_WAIT = 1U << 1;
constexpr std::uint32_t EVIDENCE_BACKGROUND_RUNTIME = 1U << 2;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string getenv_or(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return value;
}

std::string make_agent_instance_id() {
    std::random_device rd;
    const auto now = now_ns();
    std::ostringstream out;
    out << std::hex << now << "-" << getpid() << "-" << rd();
    return out.str();
}

std::string trim_phase(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    value = value.substr(start);
    if (value.empty()) {
        return "unset";
    }
    return value;
}

int getenv_int(const char *name, int fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return std::atoi(value);
}

} // namespace

bool PsiGateSkill::init(ExecutorBackend backend, GateMode mode) {
    backend_ = backend;
    mode_ = mode;
    state_ = GateState::Normal;
    generation_ = 0;
    activation_streak_ = 0;
    recovery_streak_ = 0;
    cooldown_started_at_ns_ = 0;
    agent_instance_id_ = make_agent_instance_id();
    phase_ = getenv_or("EULERPILOT_PHASE", "unset");
    phase_control_path_ = getenv_or("EULERPILOT_PHASE_FILE", "");
    trace_path_ = getenv_or("EULERPILOT_PSI_GATE_TRACE", "/tmp/eulerpilot-psi-gate-trace.jsonl");
    trace_.open(trace_path_, std::ios::out | std::ios::app);
    if (!trace_.good()) {
        last_error_ = "psi-gate-trace-open-failed";
        return false;
    }

    if (mode_ == GateMode::Psi) {
        if (!open_trigger()) {
            if (last_error_.empty()) {
                last_error_ = "psi trigger unavailable";
            }
            return false;
        }
    }
    emit_phase_marker(phase_);
    return true;
}

bool PsiGateSkill::open_trigger() {
    trigger_fd_ = open("/proc/pressure/cpu", O_RDWR | O_NONBLOCK);
    if (trigger_fd_ < 0) {
        last_error_ = "psi-trigger-open-failed";
        return false;
    }

    const int stall_us = getenv_int("EULERPILOT_PSI_STALL_US", 100000);
    const int window_us = getenv_int("EULERPILOT_PSI_WINDOW_US", 1000000);
    std::string trigger = "some " + std::to_string(stall_us) + " " + std::to_string(window_us);
    if (write(trigger_fd_, trigger.c_str(), trigger.size()) < 0) {
        last_error_ = "psi-trigger-write-failed";
        close(trigger_fd_);
        trigger_fd_ = -1;
        return false;
    }

    pfd_.fd = trigger_fd_;
    pfd_.events = POLLPRI;
    pfd_.revents = 0;
    return true;
}

bool PsiGateSkill::poll_trigger() {
    if (mode_ != GateMode::Psi || trigger_fd_ < 0) {
        return false;
    }
    const int ret = poll(&pfd_, 1, 0);
    if (ret <= 0) {
        return false;
    }
    if (pfd_.revents & POLLPRI) {
        char buf[128];
        lseek(trigger_fd_, 0, SEEK_SET);
        read(trigger_fd_, buf, sizeof(buf));
        pfd_.revents = 0;
        return true;
    }
    return false;
}

std::string PsiGateSkill::gate_state_map_path() const {
    const char *value = std::getenv("EULERPILOT_GATE_STATE_MAP");
    if (value && *value) {
        return value;
    }
    return "/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map";
}

void PsiGateSkill::emit_trace(const GateDecision &decision) {
    if (!trace_.good()) {
        return;
    }
    const auto seq = ++event_seq_;
    trace_ << "{\"event_type\":\"gate\""
           << ",\"agent_instance_id\":\"" << agent_instance_id_ << "\""
           << ",\"event_seq\":" << seq
           << ",\"monotonic_timestamp_ns\":" << decision.timestamp_ns
           << ",\"timestamp_ns\":" << decision.timestamp_ns
           << ",\"phase\":\"" << phase_ << "\""
           << ",\"gate_state\":\"" << to_string(decision.next_state) << "\""
           << ",\"previous_state\":\"" << to_string(decision.previous_state) << "\""
           << ",\"next_state\":\"" << to_string(decision.next_state) << "\""
           << ",\"cpu_psi_triggered\":" << (decision.cpu_psi_triggered ? "true" : "false")
           << ",\"latency_wait_high\":" << (decision.latency_wait_high ? "true" : "false")
           << ",\"background_runtime_high\":" << (decision.background_runtime_high ? "true" : "false")
           << ",\"latency_workload_present\":" << (decision.latency_workload_present ? "true" : "false")
           << ",\"background_workload_present\":" << (decision.background_workload_present ? "true" : "false")
           << ",\"generation\":" << decision.generation
           << ",\"profile\":\"" << decision.profile << "\"}\n";
    trace_.flush();
}

void PsiGateSkill::emit_phase_marker(const std::string &phase) {
    if (!trace_.good()) {
        return;
    }
    const auto timestamp = now_ns();
    const auto seq = ++event_seq_;
    trace_ << "{\"event_type\":\"phase_marker\""
           << ",\"agent_instance_id\":\"" << agent_instance_id_ << "\""
           << ",\"event_seq\":" << seq
           << ",\"monotonic_timestamp_ns\":" << timestamp
           << ",\"timestamp_ns\":" << timestamp
           << ",\"phase\":\"" << phase << "\""
           << ",\"gate_state\":\"" << to_string(state_) << "\""
           << ",\"generation\":" << generation_ << "}\n";
    trace_.flush();
}

void PsiGateSkill::poll_phase_control() {
    if (phase_control_path_.empty()) {
        return;
    }
    std::ifstream phase_file(phase_control_path_);
    if (!phase_file.good()) {
        return;
    }
    std::stringstream buffer;
    buffer << phase_file.rdbuf();
    const std::string next_phase = trim_phase(buffer.str());
    if (next_phase != phase_) {
        phase_ = next_phase;
        emit_phase_marker(phase_);
    }
}

GateDecision PsiGateSkill::tick(const TriggerContext &ctx) {
    poll_phase_control();

    GateDecision decision;
    decision.previous_state = state_;
    decision.next_state = state_;
    decision.timestamp_ns = now_ns();
    decision.updated_at_ns = decision.timestamp_ns;
    decision.cpu_psi_triggered = mode_ == GateMode::Psi ? poll_trigger() : false;
    decision.latency_wait_high = ctx.latency_wait_high;
    decision.background_runtime_high = ctx.background_runtime_high;
    decision.latency_workload_present = ctx.latency_exists;
    decision.background_workload_present = ctx.background_exists;

    if (ctx.cpu_psi_high || decision.cpu_psi_triggered) {
        decision.evidence_mask |= EVIDENCE_CPU_PSI;
    }
    if (ctx.latency_wait_high) {
        decision.evidence_mask |= EVIDENCE_LATENCY_WAIT;
    }
    if (ctx.background_runtime_high) {
        decision.evidence_mask |= EVIDENCE_BACKGROUND_RUNTIME;
    }

    const int activation_windows = getenv_int("EULERPILOT_GATE_ACTIVATION_WINDOWS", 2);
    const int recovery_windows = getenv_int("EULERPILOT_GATE_RECOVERY_WINDOWS", 3);
    const std::uint64_t cooldown_ns =
        static_cast<std::uint64_t>(getenv_int("EULERPILOT_GATE_COOLDOWN_MS", 2000)) * 1000ULL * 1000ULL;

    int evidence_count = 0;
    evidence_count += (decision.evidence_mask & EVIDENCE_CPU_PSI) ? 1 : 0;
    evidence_count += (decision.evidence_mask & EVIDENCE_LATENCY_WAIT) ? 1 : 0;
    evidence_count += (decision.evidence_mask & EVIDENCE_BACKGROUND_RUNTIME) ? 1 : 0;

    switch (mode_) {
    case GateMode::AlwaysActive:
        decision.next_state = GateState::Active;
        break;
    case GateMode::Normal:
        decision.next_state = GateState::Normal;
        break;
    case GateMode::Psi:
        switch (state_) {
        case GateState::Normal:
            if (ctx.latency_exists && ctx.background_exists) {
                decision.next_state = GateState::Armed;
            }
            break;
        case GateState::Armed:
            if (evidence_count >= 2) {
                activation_streak_++;
                if (activation_streak_ >= static_cast<std::uint32_t>(activation_windows)) {
                    decision.next_state = GateState::Active;
                    activation_streak_ = 0;
                }
            } else {
                activation_streak_ = 0;
            }
            if (!ctx.latency_exists) {
                decision.next_state = GateState::Normal;
                activation_streak_ = 0;
            } else if (!ctx.background_exists && evidence_count < 2) {
                decision.next_state = GateState::Normal;
                activation_streak_ = 0;
            }
            break;
        case GateState::Active:
            if ((!ctx.background_exists && !(decision.evidence_mask & EVIDENCE_CPU_PSI)) ||
                (!ctx.background_exists && !ctx.background_runtime_high) ||
                (!(decision.evidence_mask & EVIDENCE_CPU_PSI) && !ctx.latency_wait_high)) {
                recovery_streak_++;
                if (recovery_streak_ >= static_cast<std::uint32_t>(recovery_windows)) {
                    decision.next_state = GateState::Cooldown;
                    cooldown_started_at_ns_ = decision.timestamp_ns;
                    recovery_streak_ = 0;
                }
            } else {
                recovery_streak_ = 0;
            }
            break;
        case GateState::Cooldown:
            if (ctx.background_exists && evidence_count >= 2) {
                decision.next_state = GateState::Active;
            } else if (decision.timestamp_ns - cooldown_started_at_ns_ >= cooldown_ns) {
                decision.next_state = GateState::Normal;
            }
            break;
        }
        break;
    }

    state_ = decision.next_state;
    decision.generation = ++generation_;
    switch (state_) {
    case GateState::Active:
    case GateState::Cooldown:
        decision.profile = "sched_ext_active";
        break;
    case GateState::Armed:
        decision.profile = "sched_ext_armed";
        break;
    default:
        decision.profile = "sched_ext_normal";
        break;
    }
    emit_trace(decision);
    return decision;
}

void PsiGateSkill::shutdown() {
    if (trigger_fd_ >= 0) {
        close(trigger_fd_);
        trigger_fd_ = -1;
    }
    if (trace_.is_open()) {
        trace_.close();
    }
}

const char *to_string(GateMode mode) {
    switch (mode) {
    case GateMode::AlwaysActive:
        return "always-active";
    case GateMode::Psi:
        return "psi";
    case GateMode::Normal:
    default:
        return "normal";
    }
}

const char *to_string(GateState state) {
    switch (state) {
    case GateState::Armed:
        return "ARMED";
    case GateState::Active:
        return "ACTIVE";
    case GateState::Cooldown:
        return "COOLDOWN";
    case GateState::Normal:
    default:
        return "NORMAL";
    }
}

} // namespace eulerpilot
