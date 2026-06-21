#include "target_resolver.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <net/if.h>
#include <sstream>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace eulerpilot {

namespace {

namespace fs = std::filesystem;

bool path_exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

bool executable_exists(const std::string &path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

bool contains_path_separator(const std::string &value) {
    return value.find('/') != std::string::npos;
}

std::vector<std::string> split_colon_list(const std::string &value) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(':', start);
        const std::string item = value.substr(start, end == std::string::npos
                                                       ? std::string::npos
                                                       : end - start);
        if (!item.empty()) {
            items.push_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return items;
}

bool command_exists(const std::string &command) {
    if (command.empty()) {
        return false;
    }
    if (contains_path_separator(command)) {
        return executable_exists(command);
    }

    const char *path_env = std::getenv("PATH");
    if (!path_env) {
        return false;
    }
    for (const std::string &dir : split_colon_list(path_env)) {
        if (executable_exists(dir + "/" + command)) {
            return true;
        }
    }
    return false;
}

bool valid_ifname(const std::string &ifname) {
    if (ifname.empty() || ifname.size() >= IF_NAMESIZE) {
        return false;
    }
    for (const char ch : ifname) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' ||
              ch == '.' || ch == ':')) {
            return false;
        }
    }
    return true;
}

bool valid_container_id(const std::string &container_id) {
    if (container_id.size() < 8 || container_id.size() > 128) {
        return false;
    }
    for (const char ch : container_id) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

bool valid_name_token(const std::string &value) {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    for (const char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

bool valid_lookup_token(const std::string &value) {
    if (value.size() < 8 || value.size() > 128) {
        return false;
    }
    for (const char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

std::string trim_copy(const std::string &value) {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string first_nonempty_line(const std::string &value) {
    std::istringstream in(value);
    std::string line;
    while (std::getline(in, line)) {
        line = trim_copy(line);
        if (!line.empty()) {
            return line;
        }
    }
    return {};
}

std::string capture_command_stdout(const std::vector<std::string> &args,
                                   int &exit_code) {
    exit_code = -1;
    if (args.empty()) {
        return {};
    }

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        return {};
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {};
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[256];
    constexpr std::size_t kMaxCommandOutput = 8192;
    while (output.size() < kMaxCommandOutput) {
        const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        output.append(buffer, static_cast<std::size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return output;
    }
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }
    return output;
}

std::string resolve_container_id_with_command(const std::vector<std::string> &args) {
    int rc = -1;
    const std::string output = capture_command_stdout(args, rc);
    if (rc != 0) {
        return {};
    }
    const std::string id = first_nonempty_line(output);
    return valid_container_id(id) ? id : std::string{};
}

std::string runtime_container_id(const ContainerTargetSpec &spec,
                                 std::string &reason) {
    if (!valid_name_token(spec.container_name)) {
        reason = "invalid-container-name";
        return {};
    }

    const bool auto_runtime = spec.runtime.empty() || spec.runtime == "auto";
    if ((auto_runtime || spec.runtime == "crictl") && command_exists(spec.crictl_path)) {
        std::string id = resolve_container_id_with_command(
            {spec.crictl_path, "ps", "-a", "--name", spec.container_name, "-q"});
        if (!id.empty()) {
            return id;
        }
        if (!auto_runtime) {
            reason = "crictl-container-id-not-found";
            return {};
        }
    }

    if ((auto_runtime || spec.runtime == "docker") && command_exists(spec.docker_path)) {
        std::string id = resolve_container_id_with_command(
            {spec.docker_path, "inspect", "-f", "{{.Id}}", spec.container_name});
        if (!id.empty()) {
            return id;
        }
        if (!auto_runtime) {
            reason = "docker-container-id-not-found";
            return {};
        }
    }

    if ((auto_runtime || spec.runtime == "podman") && command_exists(spec.podman_path)) {
        std::string id = resolve_container_id_with_command(
            {spec.podman_path, "inspect", "-f", "{{.Id}}", spec.container_name});
        if (!id.empty()) {
            return id;
        }
        if (!auto_runtime) {
            reason = "podman-container-id-not-found";
            return {};
        }
    }

    reason = auto_runtime ? "container-runtime-id-not-found"
                          : "missing-runtime-command";
    return {};
}

std::string normalize_pod_uid_systemd(const std::string &pod_uid) {
    std::string normalized = pod_uid;
    for (char &ch : normalized) {
        if (ch == '-') {
            ch = '_';
        }
    }
    return normalized;
}

std::string strip_pod_uid_dashes(const std::string &pod_uid) {
    std::string stripped;
    stripped.reserve(pod_uid.size());
    for (const char ch : pod_uid) {
        if (ch != '-') {
            stripped.push_back(ch);
        }
    }
    return stripped;
}

std::vector<std::string> pod_uid_needles(const std::string &pod_uid) {
    const std::string systemd_uid = normalize_pod_uid_systemd(pod_uid);
    const std::string compact_uid = strip_pod_uid_dashes(pod_uid);
    return {
        pod_uid,
        systemd_uid,
        compact_uid,
        "pod" + pod_uid,
        "pod" + systemd_uid,
        "pod" + compact_uid,
    };
}

bool path_contains_any(const std::string &path,
                       const std::vector<std::string> &needles) {
    for (const std::string &needle : needles) {
        if (!needle.empty() && path.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string find_cgroup_by_needles(const std::string &cgroup_root,
                                   const std::vector<std::string> &needles,
                                   std::string &reason) {
    if (!path_exists(cgroup_root)) {
        reason = "cgroup-root-not-found";
        return {};
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(
        cgroup_root,
        fs::directory_options::skip_permission_denied,
        ec);
    fs::recursive_directory_iterator end;
    if (ec) {
        reason = "cgroup-scan-failed";
        return {};
    }

    std::string best_path;
    int visited = 0;
    constexpr int kMaxCgroupScanEntries = 4096;
    for (; it != end && visited < kMaxCgroupScanEntries; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        ++visited;
        if (!it->is_directory(ec)) {
            ec.clear();
            continue;
        }
        const std::string path = it->path().string();
        if (!path_contains_any(path, needles)) {
            continue;
        }
        if (best_path.empty() || path.size() < best_path.size()) {
            best_path = path;
        }
    }

    if (best_path.empty()) {
        reason = visited >= kMaxCgroupScanEntries
                     ? "cgroup-scan-limit"
                     : "cgroup-not-found";
    }
    return best_path;
}

std::string kubectl_pod_uid(const K8sPodTargetSpec &spec,
                            const TargetResolverOptions &options,
                            std::string &reason) {
    if (!command_exists(options.kubectl_path)) {
        reason = "missing-kubectl";
        return {};
    }
    int rc = -1;
    const std::string output = capture_command_stdout(
        {options.kubectl_path, "-n", spec.pod_namespace, "get", "pod",
         spec.pod_name, "-o", "jsonpath={.metadata.uid}"},
        rc);
    if (rc != 0) {
        reason = "kubectl-pod-query-failed";
        return {};
    }
    std::string uid = trim_copy(output);
    if (!valid_lookup_token(uid)) {
        reason = "kubectl-pod-uid-invalid";
        return {};
    }
    return uid;
}

bool socket_exists(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

std::vector<std::string> runtime_socket_candidates(const TargetResolverOptions &options) {
    const char *override_list = std::getenv("EULERPILOT_TARGET_RUNTIME_SOCKETS");
    if (override_list && *override_list) {
        return split_colon_list(override_list);
    }
    return options.runtime_socket_paths;
}

bool has_runtime_socket(const TargetResolverOptions &options) {
    for (const std::string &path : runtime_socket_candidates(options)) {
        if (socket_exists(path)) {
            return true;
        }
    }
    return false;
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

TargetIdentity resolve_container_target(const std::string &name,
                                        const std::string &container_id,
                                        const std::string &cgroup_root) {
    ContainerTargetSpec spec;
    spec.name = name;
    spec.container_id = container_id;
    spec.cgroup_root = cgroup_root;
    return resolve_container_target(spec);
}

TargetIdentity resolve_container_target(const ContainerTargetSpec &spec) {
    TargetIdentity target;
    target.name = spec.name;
    target.type = "container_id";
    target.container_id = spec.container_id;
    target.container_name = spec.container_name;

    if (target.container_id.empty() && !spec.container_name.empty()) {
        std::string reason;
        target.container_id = runtime_container_id(spec, reason);
        if (target.container_id.empty()) {
            target.reason = reason.empty() ? "container-runtime-id-not-found" : reason;
            return target;
        }
    }

    if (!valid_container_id(target.container_id)) {
        target.reason = "invalid-container-id";
        return target;
    }

    std::string reason;
    const std::string cgroup_path = find_cgroup_by_needles(
        spec.cgroup_root,
        {target.container_id},
        reason);
    if (cgroup_path.empty()) {
        target.reason = reason == "cgroup-not-found"
                            ? "container-cgroup-not-found"
                            : reason;
        return target;
    }

    target.cgroup_path = cgroup_path;
    target.cgroup_id = inode_as_stable_id(cgroup_path);
    target.resolved = target.cgroup_id != 0;
    target.reason = target.resolved ? "ok" : "cgroup-path-not-found";
    return target;
}

TargetIdentity resolve_netdev_target(const std::string &name, const std::string &ifname) {
    TargetIdentity target;
    target.name = name;
    target.type = "netdev";
    target.ifname = ifname;

    if (!valid_ifname(ifname)) {
        target.reason = "invalid-ifname";
        return target;
    }

    target.ifindex = static_cast<int>(if_nametoindex(ifname.c_str()));
    target.resolved = target.ifindex > 0 && path_exists("/sys/class/net/" + ifname);
    target.reason = target.resolved ? "ok" : "netdev-not-found";
    return target;
}

TargetIdentity resolve_k8s_pod_cgroup_target(const K8sPodTargetSpec &spec,
                                             const TargetResolverOptions &options) {
    TargetIdentity target;
    target.name = spec.name.empty()
                      ? "pod:" + spec.pod_namespace + "/" + spec.pod_name
                      : spec.name;
    target.type = "k8s_pod";
    target.pod_namespace = spec.pod_namespace;
    target.pod_name = spec.pod_name;
    target.pod_uid = spec.pod_uid;
    target.container_id = spec.container_id;
    target.container_name = spec.container_name;

    if (!valid_name_token(spec.pod_namespace)) {
        target.reason = "missing-or-invalid-pod-namespace";
        return target;
    }
    if (!valid_name_token(spec.pod_name)) {
        target.reason = "missing-or-invalid-pod-name";
        return target;
    }
    if (!options.allow_non_lab_pods &&
        spec.pod_namespace != options.lab_namespace) {
        target.reason = "unsupported-namespace";
        return target;
    }

    if (!spec.container_id.empty()) {
        ContainerTargetSpec container_spec;
        container_spec.name = target.name;
        container_spec.container_id = spec.container_id;
        container_spec.container_name = spec.container_name;
        container_spec.cgroup_root = spec.cgroup_root;
        TargetIdentity container = resolve_container_target(container_spec);
        container.type = "k8s_pod";
        container.pod_namespace = spec.pod_namespace;
        container.pod_name = spec.pod_name;
        container.pod_uid = spec.pod_uid;
        return container;
    }

    std::string pod_uid = spec.pod_uid;
    if (pod_uid.empty()) {
        std::string reason;
        pod_uid = kubectl_pod_uid(spec, options, reason);
        if (pod_uid.empty()) {
            target.reason = reason.empty() ? "missing-pod-uid" : reason;
            return target;
        }
    }
    if (!valid_lookup_token(pod_uid)) {
        target.reason = "invalid-pod-uid";
        return target;
    }
    target.pod_uid = pod_uid;

    std::string reason;
    const std::string cgroup_path = find_cgroup_by_needles(
        spec.cgroup_root,
        pod_uid_needles(pod_uid),
        reason);
    if (cgroup_path.empty()) {
        target.reason = reason == "cgroup-not-found"
                            ? "pod-cgroup-not-found"
                            : reason;
        return target;
    }

    target.cgroup_path = cgroup_path;
    target.cgroup_id = inode_as_stable_id(cgroup_path);
    target.resolved = target.cgroup_id != 0;
    target.reason = target.resolved ? "ok" : "cgroup-path-not-found";
    return target;
}

TargetIdentity resolve_k8s_pod_target(const K8sPodTargetSpec &spec,
                                      const TargetResolverOptions &options) {
    TargetIdentity target;
    target.name = spec.name.empty()
                      ? "pod:" + spec.pod_namespace + "/" + spec.pod_name
                      : spec.name;
    target.type = "k8s_pod";
    target.pod_namespace = spec.pod_namespace;
    target.pod_name = spec.pod_name;

    if (spec.pod_namespace.empty()) {
        target.reason = "missing-pod-namespace";
        return target;
    }
    if (spec.pod_name.empty()) {
        target.reason = "missing-pod-name";
        return target;
    }
    if (!options.allow_non_lab_pods &&
        spec.pod_namespace != options.lab_namespace) {
        target.reason = "unsupported-namespace";
        return target;
    }
    if (!command_exists(options.kubectl_path)) {
        target.reason = "missing-kubectl";
        return target;
    }
    if (!has_runtime_socket(options)) {
        target.reason = "missing-runtime";
        return target;
    }

    target.reason = "unsupported-pod-veth-resolution";
    return target;
}

TargetIdentity resolve_k8s_pod_target(const std::string &name,
                                      const std::string &pod_namespace,
                                      const std::string &pod_name) {
    K8sPodTargetSpec spec;
    spec.name = name;
    spec.pod_namespace = pod_namespace;
    spec.pod_name = pod_name;
    return resolve_k8s_pod_target(spec);
}

} // namespace eulerpilot
