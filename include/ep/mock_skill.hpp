#pragma once

#include <filesystem>
#include <string>

#include "ep/skill.hpp"

namespace ep {

// Test-only effect domain: a single integer in <state-dir>/mock/value.json.
// It is deliberately simple, compensable and observable so the transaction
// engine, journal and reconcile loop can be tested without touching the kernel.
class MockSkill : public Skill {
 public:
  explicit MockSkill(std::filesystem::path state_dir);

  std::string name() const override { return "mock"; }

  SkillProbe probe() override;
  bool plan(const Json& intent, Plan* out, std::string* error) override;
  bool snapshot(const Plan& plan, Snapshot* out, std::string* error) override;
  bool apply(const Plan& plan, const Snapshot& snapshot, const LeaseState& lease,
             Applied* out, std::string* error) override;
  VerifyResult verify(const Applied& applied, std::int64_t deadline_ms) override;
  RecoveryResult rollback(const Applied& applied, const Snapshot& snapshot,
                          std::string* error) override;
  VerifyResult verify_rolled_back(const Snapshot& snapshot,
                                  std::int64_t deadline_ms) override;
  RecoveryResult reconcile(const ReconcileContext& context, std::string* error) override;

 private:
  bool read_current(int* value, bool* exists, std::string* error) const;
  bool write_value(int value, std::string* error) const;
  bool restore(const Json& snapshot, std::string* error) const;

  std::filesystem::path dir_;
  std::filesystem::path file_;
};

}  // namespace ep
