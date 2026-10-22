#pragma once

#include <cstdint>
#include <string>

#include "ep/ownership.hpp"
#include "ep/util.hpp"

namespace ep {

struct SkillProbe {
  bool ok = false;
  std::string detail;
};

struct Plan {
  Json intent;
  Json params;
};

struct Snapshot {
  std::string id;
  Json data;
};

struct Applied {
  Json data;
};

struct VerifyResult {
  bool ok = false;
  std::string detail;
};

struct RecoveryResult {
  bool ok = false;
  bool conflict = false;
  std::string detail;
};

// What reconcile() needs to decide the fate of an interrupted transaction.
struct ReconcileContext {
  std::string txn;
  std::string action_id;
  std::string last_type;
  Json prepared;
};

// Per-executor identity needed by skills that own namespaced kernel resources.
struct SkillContext {
  std::string instance_id;
};

// A Skill owns one effect domain. The executor calls it only from the
// privileged process; plan() must be pure and side-effect free.
class Skill {
 public:
  virtual ~Skill() = default;

  // Called once after the executor acquires ownership, before any recovery.
  virtual void configure(const SkillContext& context) { (void)context; }

  virtual std::string name() const = 0;

  // Periodic refresh of time-limited kernel state while an action is active.
  // The default is a no-op; implementations must tolerate repeated calls.
  virtual void refresh(const Applied& applied) { (void)applied; }
  virtual SkillProbe probe() = 0;
  virtual bool plan(const Json& intent, Plan* out, std::string* error) = 0;
  virtual bool snapshot(const Plan& plan, Snapshot* out, std::string* error) = 0;
  virtual bool apply(const Plan& plan, const Snapshot& snapshot, const LeaseState& lease,
                     Applied* out, std::string* error) = 0;
  virtual VerifyResult verify(const Applied& applied, std::int64_t deadline_ms) = 0;
  virtual RecoveryResult rollback(const Applied& applied, const Snapshot& snapshot,
                                  std::string* error) = 0;
  // Post-rollback semantic check. A generic "snapshot again and compare" cannot
  // work because some domains have monotonic fields (for example sched_ext
  // enable_seq) that legitimately change across detach.
  virtual VerifyResult verify_rolled_back(const Snapshot& snapshot,
                                          std::int64_t deadline_ms) = 0;
  virtual RecoveryResult reconcile(const ReconcileContext& context, std::string* error) = 0;

  // Called once at executor startup, before journal reconciliation. It handles
  // resources owned by a previous executor process that are not tied to an
  // unfinished journal transaction (for example an orphan loader after a
  // committed apply). Default: nothing to recover.
  virtual RecoveryResult recover_orphan(std::string* error) {
    (void)error;
    return RecoveryResult{true, false, "no orphan recovery"};
  }
};

}  // namespace ep
