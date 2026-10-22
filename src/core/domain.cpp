#include "ep/domain.hpp"

namespace ep {

std::string to_string(Status status) {
  switch (status) {
    case Status::kPlanned:
      return "PLANNED";
    case Status::kApplied:
      return "APPLIED";
    case Status::kVerified:
      return "VERIFIED";
    case Status::kRejected:
      return "REJECTED";
    case Status::kRolledBack:
      return "ROLLED_BACK";
    case Status::kDegraded:
      return "DEGRADED";
    case Status::kRecoveryRequired:
      return "RECOVERY_REQUIRED";
  }
  return "REJECTED";
}

std::string to_string(ErrorCode code) {
  switch (code) {
    case ErrorCode::kNone:
      return "NONE";
    case ErrorCode::kSchemaInvalid:
      return "SCHEMA_INVALID";
    case ErrorCode::kUnsupported:
      return "UNSUPPORTED";
    case ErrorCode::kPermissionDenied:
      return "PERMISSION_DENIED";
    case ErrorCode::kTargetStale:
      return "TARGET_STALE";
    case ErrorCode::kOwnershipConflict:
      return "OWNERSHIP_CONFLICT";
    case ErrorCode::kGenerationConflict:
      return "GENERATION_CONFLICT";
    case ErrorCode::kDeadlineExceeded:
      return "DEADLINE_EXCEEDED";
    case ErrorCode::kSloDenied:
      return "SLO_DENIED";
    case ErrorCode::kApplyFailed:
      return "APPLY_FAILED";
    case ErrorCode::kVerifyFailed:
      return "VERIFY_FAILED";
    case ErrorCode::kRollbackFailed:
      return "ROLLBACK_FAILED";
    case ErrorCode::kRecoveryRequired:
      return "RECOVERY_REQUIRED";
    case ErrorCode::kIdempotencyConflict:
      return "IDEMPOTENCY_CONFLICT";
    case ErrorCode::kInternal:
      return "INTERNAL";
  }
  return "INTERNAL";
}

Json Error::to_json() const {
  if (code == ErrorCode::kNone) return nullptr;
  return Json{{"code", to_string(code)}, {"message", message}, {"detail", detail}};
}

Json ActionResult::to_json() const {
  Json bindings = Json::array();
  for (const auto& item : target_bindings) bindings.push_back(item);
  Json check_array = Json::array();
  for (const auto& item : checks) check_array.push_back(item.to_json());
  return Json{{"request_id", request_id},
              {"transaction_id", transaction_id},
              {"action_id", action_id},
              {"snapshot_id", snapshot_id},
              {"status", to_string(status)},
              {"observed_generation", observed_generation},
              {"policy_sha256", policy_sha256},
              {"target_bindings", bindings},
              {"checks", check_array},
              {"rollback", rollback.to_json()},
              {"error", error.to_json()}};
}

ActionResult make_rejection(const std::string& request_id, ErrorCode code,
                            const std::string& message, const std::string& detail) {
  ActionResult result;
  result.request_id = request_id;
  result.status = Status::kRejected;
  result.error.code = code;
  result.error.message = message;
  result.error.detail = detail;
  return result;
}

}  // namespace ep
