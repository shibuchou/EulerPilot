#pragma once

#include <cstdint>
#include <string>

namespace eulerpilot {

struct TargetIdentity {
    std::string name;
    std::string type;
    int pid = -1;
    std::uint64_t cgroup_id = 0;
    std::string cgroup_path;
    std::string container_id;
    std::string pod_namespace;
    std::string pod_name;
    std::string netns_path;
    std::string ifname;
    int ifindex = 0;
    bool resolved = false;
    std::string reason;
};

TargetIdentity resolve_pid_target(int pid);
TargetIdentity resolve_cgroup_target(const std::string &name, const std::string &path);
TargetIdentity resolve_netdev_target(const std::string &name, const std::string &ifname);

} // namespace eulerpilot
