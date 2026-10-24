#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "ep/cgroup_policy.hpp"
#include "ep/skill.hpp"

namespace ep {

struct CgroupPaths {
  std::filesystem::path state_dir;
  // Production is the unified hierarchy; tests point it at a directory of plain files
  // so the write/read-back/compensate path can be exercised without root.
  std::string cgroup_root = "/sys/fs/cgroup";
  std::string declared_prefix = "/sys/fs/cgroup";
};

// One knob this skill owns: where it lives, what the policy wants, and what was there
// before we touched it. The previous value is what makes the effect compensable.
struct CgroupSkillEffect {
  std::string path;       // declared path, under cgroup_root after resolution
  std::string file;       // absolute path of the knob file (cpu.weight)
  long long desired = 0;
  std::string previous;   // value read before we touched it
};

// Declared paths are absolute in the contract; the root is swapped so a test can run the
// same code against a directory of plain files.
std::string cgroup_knob_file(const std::string& cgroup_root, const std::string& declared_prefix,
                             const std::string& declared_path, const std::string& knob);
std::vector<CgroupSkillEffect> cgroup_effects(const std::vector<CgroupEffect>& declared,
                                              const std::string& cgroup_root,
                                              const std::string& declared_prefix);

// P4 effect domain: moves cgroup v2 cpu.weight knobs and nothing else. It is a real
// writer (the value is read back after every write, and verify() re-reads), it is
// compensable (the pre-apply value is captured first), and it is recoverable (a marker
// file lists what to restore if this process dies with the effect in place).
class CgroupSkill : public Skill {
 public:
  explicit CgroupSkill(CgroupPaths paths);

  std::string name() const override { return "cgroup"; }

  SkillProbe probe() override;
  bool plan(const Json& intent, Plan* out, std::string* error) override;
  bool snapshot(const Plan& plan, Snapshot* out, std::string* error) override;
  bool apply(const Plan& plan, const Snapshot& snapshot, const LeaseState& lease,
             Applied* out, std::string* error) override;
  VerifyResult verify(const Applied& applied, std::int64_t deadline_ms) override;
  bool refresh(const Applied& applied, const std::vector<TargetBinding>& bindings,
               const RefreshContext& context, std::string* error) override;
  RecoveryResult rollback(const Applied& applied, const Snapshot& snapshot,
                          std::string* error) override;
  VerifyResult verify_rolled_back(const Snapshot& snapshot, std::int64_t deadline_ms) override;
  RecoveryResult reconcile(const ReconcileContext& context, std::string* error) override;
  RecoveryResult recover_orphan(std::string* error) override;

 private:
  std::filesystem::path marker_path() const;
  bool read_knob(const std::string& file, std::string* value, std::string* error) const;
  bool write_knob(const std::string& file, long long value, std::string* error) const;
  // Writes then reads back: a cgroup file accepts a value and silently keeps another
  // one if something else races us, so the read is part of the write.
  bool set_knob(const std::string& file, long long value, std::string* error) const;
  bool write_marker(const std::vector<CgroupSkillEffect>& effects, const std::string& action_id,
                    std::string* error) const;
  bool read_marker(std::vector<CgroupSkillEffect>* effects, std::string* error) const;
  void remove_marker() const;
  bool restore(const std::vector<CgroupSkillEffect>& effects, std::string* error) const;
  std::string drift_report(const std::vector<CgroupSkillEffect>& effects) const;

  CgroupPaths paths_;
};

}  // namespace ep
