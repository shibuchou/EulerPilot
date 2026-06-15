#include "metrics_state.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

namespace eulerpilot {

namespace {

std::atomic<bool> running{false};
std::thread exporter_thread;
int listen_fd = -1;

std::string build_response(const MetricsState &state) {
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: text/plain; version=0.0.4\r\n"
        << "\r\n";

    oss << "# HELP eulerpilot_up Agent is running (1=up)\n"
        << "# TYPE eulerpilot_up gauge\n"
        << "eulerpilot_up 1\n";

    oss << "# HELP eulerpilot_cycles_total Total observation cycles\n"
        << "# TYPE eulerpilot_cycles_total counter\n"
        << "eulerpilot_cycles_total " << state.cycles_total.load() << "\n";

    oss << "# HELP eulerpilot_observed_tasks Tasks observed in last cycle\n"
        << "# TYPE eulerpilot_observed_tasks gauge\n"
        << "eulerpilot_observed_tasks " << state.observed_tasks.load() << "\n";

    oss << "# HELP eulerpilot_classified_tasks Classified task count\n"
        << "# TYPE eulerpilot_classified_tasks gauge\n"
        << "eulerpilot_classified_tasks{class=\"LATENCY_SENSITIVE\"} " << state.classified_latency.load() << "\n"
        << "eulerpilot_classified_tasks{class=\"BACKGROUND_NOISY\"} " << state.classified_background.load() << "\n"
        << "eulerpilot_classified_tasks{class=\"UNKNOWN\"} " << state.classified_unknown.load() << "\n";

    oss << "# HELP eulerpilot_decisions_applied Decisions applied in last cycle\n"
        << "# TYPE eulerpilot_decisions_applied gauge\n"
        << "eulerpilot_decisions_applied " << state.decisions_applied.load() << "\n";

    oss << "# HELP eulerpilot_gate_state Current gate state (0=Normal,1=Armed,2=Active,3=Cooldown)\n"
        << "# TYPE eulerpilot_gate_state gauge\n"
        << "eulerpilot_gate_state " << state.gate_state.load() << "\n";

    oss << "# HELP eulerpilot_scx_ready Whether sched_ext backend is ready\n"
        << "# TYPE eulerpilot_scx_ready gauge\n"
        << "eulerpilot_scx_ready " << state.scx_ready.load() << "\n";

    oss << "# HELP eulerpilot_psi_cpu_avg10 PSI cpu.some.avg10 value\n"
        << "# TYPE eulerpilot_psi_cpu_avg10 gauge\n"
        << "eulerpilot_psi_cpu_avg10 " << state.psi_cpu_avg10.load() << "\n";

    oss << "# HELP eulerpilot_skill_running Whether a runtime skill is running\n"
        << "# TYPE eulerpilot_skill_running gauge\n"
        << "eulerpilot_skill_running{name=\"resource_control\"} " << state.skill_resource_control_running.load() << "\n"
        << "eulerpilot_skill_running{name=\"psi_gate\"} " << state.skill_psi_gate_running.load() << "\n";

    return oss.str();
}

void serve(const std::string &listen_addr) {
    std::string host = "127.0.0.1";
    int port = 9108;
    auto colon = listen_addr.find(':');
    if (colon != std::string::npos) {
        host = listen_addr.substr(0, colon);
        port = std::stoi(listen_addr.substr(colon + 1));
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "[metrics] socket create failed: " << std::strerror(errno) << "\n";
        return;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[metrics] bind failed: " << std::strerror(errno) << "\n";
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    if (listen(listen_fd, 4) < 0) {
        std::cerr << "[metrics] listen failed: " << std::strerror(errno) << "\n";
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    char buf[2048];
    while (running.load()) {
        pollfd fds[1] = {{listen_fd, POLLIN, 0}};
        int ret = poll(fds, 1, 1000);
        if (ret < 0) {
            if (errno != EINTR) break;
            continue;
        }
        if (ret == 0) continue;

        int client = accept(listen_fd, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }

        ssize_t n = read(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            if (std::strstr(buf, "GET /metrics")) {
                auto resp = build_response(global_metrics_state());
                write(client, resp.c_str(), resp.size());
            } else {
                const char *not_found = "HTTP/1.0 404 Not Found\r\n\r\n";
                write(client, not_found, std::strlen(not_found));
            }
        }
        close(client);
    }

    close(listen_fd);
    listen_fd = -1;
}

} // anonymous namespace

bool metrics_exporter_start(const std::string &listen_addr) {
    if (running.load()) return true;
    running.store(true);
    exporter_thread = std::thread(serve, listen_addr);
    return true;
}

void metrics_exporter_stop() {
    running.store(false);
    if (listen_fd >= 0) shutdown(listen_fd, SHUT_RDWR);
    if (exporter_thread.joinable()) exporter_thread.join();
}

} // namespace eulerpilot
