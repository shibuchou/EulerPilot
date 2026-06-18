#include "target_resolver.hpp"

#include <fstream>
#include <net/if.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace eulerpilot {

namespace {

bool path_exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

std::string read_first_cgroup_path(int pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/cgroup");
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.rfind(':');
        if (pos != std::string::npos && pos + 1 < line.size()) {
            return line.substr(pos + 1);
        }
    }
    return {};
}

std::uint64_t inode_as_stable_id(const std::string &path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(st.st_ino);
}

} // namespace

TargetIdentity resolve_pid_target(int pid) {
    TargetIdentity target;
    target.name = "pid:" + std::to_string(pid);
    target.type = "pid";
    target.pid = pid;

    if (pid <= 0) {
        target.reason = "invalid-pid";
        return target;
    }

    std::string relative = read_first_cgroup_path(pid);
    if (relative.empty()) {
        target.reason = "missing-proc-cgroup";
        return target;
    }

    target.cgroup_path = "/sys/fs/cgroup" + relative;
    target.cgroup_id = inode_as_stable_id(target.cgroup_path);
    target.resolved = target.cgroup_id != 0;
    target.reason = target.resolved ? "ok" : "cgroup-path-not-found";
    return target;
}

TargetIdentity resolve_cgroup_target(const std::string &name, const std::string &path) {
    TargetIdentity target;
    target.name = name;
    target.type = "cgroup";
    target.cgroup_path = path;
    target.cgroup_id = inode_as_stable_id(path);
    target.resolved = target.cgroup_id != 0;
    target.reason = target.resolved ? "ok" : "cgroup-path-not-found";
    return target;
}

TargetIdentity resolve_netdev_target(const std::string &name, const std::string &ifname) {
    TargetIdentity target;
    target.name = name;
    target.type = "netdev";
    target.ifname = ifname;
    target.ifindex = static_cast<int>(if_nametoindex(ifname.c_str()));
    target.resolved = target.ifindex > 0 && path_exists("/sys/class/net/" + ifname);
    target.reason = target.resolved ? "ok" : "netdev-not-found";
    return target;
}

} // namespace eulerpilot
