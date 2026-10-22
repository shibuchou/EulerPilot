#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ep/util.hpp"

namespace ep {

inline constexpr const char* kApiVersion = "eulerpilot.io/v1alpha1";

enum class Status {
  kPlanned,
  kApplied,
  kVerified,
  kRejected,
  kRolledBack,
  kDegraded,
  kRecoveryRequired,
};

std::string to_string(Status status);

enum class ErrorCode {
  kNone,
  kSchemaInvalid,
  kUnsupported,
  kPermissionDenied,
  kTargetStale,
  kOwnershipConflict,
  kGenerationConflict,
  kDeadlineExceeded,
  kSloDenied,
  kApplyFailed,
  kVerifyFailed,
  kRollbackFailed,
  kRecoveryRequired,
  kIdempotencyConflict,
  kInternal,
};

std::string to_string(ErrorCode code);

struct Error {
  ErrorCode code = ErrorCode::kNone;
  std::string message;
  std::string detail;

  bool ok() const { return code == ErrorCode::kNone; }
  Json to_json() const;
};

struct Check {
  std::string name;
  bool ok = false;
  std::string detail;

  Json to_json() const {
    return Json{{"name", name}, {"ok", ok}, {"detail", detail}};
  }
};

struct RollbackInfo {
  bool attempted = false;
  std::string status = "NOT_NEEDED";
  std::string detail;

  Json to_json() const {
    return Json{{"attempted", attempted}, {"status", status}, {"detail", detail}};
  }
};

struct ActionResult {
  std::string request_id;
  std::string transaction_id;
  std::string action_id;
  std::string snapshot_id;
  Status status = Status::kRejected;
  std::uint64_t observed_generation = 0;
  std::string policy_sha256;
  std::vector<Json> target_bindings;
  std::vector<Check> checks;
  RollbackInfo rollback;
  Error error;

  Json to_json() const;
};

// Fixed-shape rejection used before a transaction can start.
ActionResult make_rejection(const std::string& request_id, ErrorCode code,
                            const std::string& message, const std::string& detail = "");

}  // namespace ep
