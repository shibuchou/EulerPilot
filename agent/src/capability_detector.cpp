#include "capability_detector.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace eulerpilot {

namespace {

bool path_exists(const char *path) {
    return access(path, F_OK) == 0;
}

bool command_available(const char *name) {
    std::string command = "command -v ";
    command += name;
    command += " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

bool file_contains(const char *path, const char *needle) {
    std::ifstream file(path);
    if (!file.good()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str().find(needle) != std::string::npos;
}

void put(CapabilitySnapshot &snapshot, const std::string &name, bool available, const std::string &evidence) {
    snapshot.probes[name] = CapabilityProbe{available, evidence};
}

} // namespace

CapabilitySnapshot detect_capabilities() {
    CapabilitySnapshot snapshot;

    put(snapshot, "btf", path_exists("/sys/kernel/btf/vmlinux"), "/sys/kernel/btf/vmlinux");
    put(snapshot, "ringbuf", path_exists("/sys/kernel/btf/vmlinux"), "libbpf ringbuf requires BTF/libbpf runtime");
    put(snapshot, "bpf_lsm", file_contains("/sys/kernel/security/lsm", "bpf"), "/sys/kernel/security/lsm");
    put(snapshot, "xdp", path_exists("/sys/class/net/lo"), "/sys/class/net/* with generic fallback");
    put(snapshot, "tc", command_available("tc"), "tc command");
    put(snapshot, "cgroup_v2", path_exists("/sys/fs/cgroup/cgroup.controllers"), "/sys/fs/cgroup/cgroup.controllers");
    put(snapshot, "psi_cpu", path_exists("/proc/pressure/cpu"), "/proc/pressure/cpu");
    put(snapshot, "psi_memory", path_exists("/proc/pressure/memory"), "/proc/pressure/memory");
    put(snapshot, "psi_io", path_exists("/proc/pressure/io"), "/proc/pressure/io");
    put(snapshot, "sched_ext", path_exists("/sys/kernel/sched_ext"), "/sys/kernel/sched_ext");
    put(snapshot, "memory_reclaim", path_exists("/sys/fs/cgroup/memory.reclaim"), "/sys/fs/cgroup/memory.reclaim");
    put(snapshot, "kubernetes", command_available("kubectl"), "kubectl command");

    return snapshot;
}

} // namespace eulerpilot
