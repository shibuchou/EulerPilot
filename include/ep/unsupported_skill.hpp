#pragma once

#include <string>
#include <utility>

#include "ep/skill.hpp"

namespace ep {

// Placeholder for a real effect domain that is not implemented yet. It probes
// as unavailable so apply requests fail with UNSUPPORTED instead of pretending
// to have changed the system.
class UnsupportedSkill : public Skill {
 public:
  explicit UnsupportedSkill(std::string name) : name_(std::move(name)) {}

  std::string name() const override { return name_; }

  SkillProbe probe() override {
    return SkillProbe{false, name_ + " skill is not implemented in the P1 skeleton"};
  }
  bool plan(const Json&, Plan*, std::string* error) override {
    if (error != nullptr) *error = name_ + " skill cannot plan";
    return false;
  }
  bool snapshot(const Plan&, Snapshot*, std::string* error) override {
    if (error != nullptr) *error = name_ + " skill cannot snapshot";
    return false;
  }
  bool apply(const Plan&, const Snapshot&, const LeaseState&, Applied*,
             std::string* error) override {
    if (error != nullptr) *error = name_ + " skill cannot apply";
    return false;
  }
  VerifyResult verify(const Applied&, std::int64_t) override {
    return VerifyResult{false, name_ + " skill cannot verify"};
  }
  RecoveryResult rollback(const Applied&, const Snapshot&, std::string* error) override {
    if (error != nullptr) *error = name_ + " skill cannot rollback";
    return RecoveryResult{false, true, "unsupported"};
  }
  VerifyResult verify_rolled_back(const Snapshot&, std::int64_t) override {
    return VerifyResult{false, name_ + " skill cannot verify rollback"};
  }
  RecoveryResult reconcile(const ReconcileContext&, std::string* error) override {
    if (error != nullptr) *error = name_ + " skill cannot reconcile";
    return RecoveryResult{false, true, "unsupported"};
  }

 private:
  std::string name_;
};

}  // namespace ep
