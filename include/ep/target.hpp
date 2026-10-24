#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ep/policy.hpp"
#include "ep/util.hpp"

namespace ep {

// Resolved target identity. cgroup: kernfs inode/device plus the kernel cgroup
// id. pid: pidfd + starttime; the pidfd is held for the lifetime of the
// bindings so the process identity cannot be recycled under the transaction.
struct TargetBinding {
  std::string kind;
  std::string path;
  std::string role;
  std::string role_source;
  std::string runtime_class{};  // declared class, or "" when the policy declared none
  std::string boot_id;
  std::uint64_t inode = 0;
  std::uint64_t device = 0;
  std::uint64_t cgroup_id = 0;  // 0 = not available on this kernel/mount
  std::int64_t pid = 0;
  std::uint64_t starttime = 0;
  std::shared_ptr<int> pidfd;  // pid targets; shared by copies, closes on last
  std::shared_ptr<int> cgroup_fd;  // cgroup targets; O_PATH directory, held

  Json to_json() const;
};

// The class the data plane should file this target under, or "" when nothing should
// be written to class_map at all. One function holds the role default so the apply
// path and the 2s refresh path can never disagree about who is BG today. The pair
// overload exists because the apply path only has the validated policy text, while
// the refresh path has live bindings; both must mean the same thing.
std::string effective_runtime_class(const std::string& role, const std::string& declared);
std::string effective_runtime_class(const TargetBinding& binding);

// Opens every target as a cgroup v2 directory (O_PATH|O_NOFOLLOW) and records
// path + inode + device + boot_id. Fails closed on a missing/non-v2/symlink path.
bool resolve_targets(const std::vector<PolicyTarget>& targets,
                     std::vector<TargetBinding>* bindings, std::string* error);

// Re-checks boot_id and every path/inode/device before or after a write.
bool revalidate_targets(const std::vector<TargetBinding>& bindings, std::string* error);

// Re-checks one held binding. A cgroup binding uses the held directory FD for
// identity and members, and also compares the current path to detect replacement.
bool revalidate_binding(const TargetBinding& binding, std::string* error);

// Enumerates current pids from the held cgroup directory FD (not the path).
bool read_cgroup_members(const TargetBinding& binding, std::vector<std::int64_t>* pids,
                         std::string* error);

}  // namespace ep
