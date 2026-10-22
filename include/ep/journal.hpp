#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ep/util.hpp"

namespace ep {

// One durable journal record. `type` is one of:
// PREPARED, APPLYING, VERIFIED, COMMITTED, COMPENSATING, ROLLED_BACK,
// RECOVERY_REQUIRED, ROLLBACK_PREPARED, RESULT.
struct JournalRecord {
  std::uint64_t seq = 0;
  std::string ts;
  std::string txn;
  std::string action_id;
  std::string type;
  std::uint64_t generation = 0;
  std::string owner;
  std::uint64_t fence = 0;
  Json data = Json::object();

  Json to_json() const;
  static std::optional<JournalRecord> from_json(const Json& value);
};

// Append-only write-ahead log. Every append is fsynced before it returns.
// A torn trailing line from a crash is truncated on open.
class Journal {
 public:
  explicit Journal(std::filesystem::path dir);
  ~Journal();
  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;

  bool open(std::string* error);
  bool append(JournalRecord record, std::string* error);

  const std::vector<JournalRecord>& records() const { return records_; }
  std::size_t size() const { return records_.size(); }

  std::optional<JournalRecord> find_result(const std::string& idempotency_key) const;
  bool append_result(const std::string& idempotency_key,
                     const std::string& request_sha256, const Json& response,
                     std::string* error);

  std::uint64_t max_committed_generation() const;
  std::vector<std::string> incomplete_transactions() const;
  std::vector<std::string> transactions_needing_reconcile() const;
  std::vector<JournalRecord> transaction_records(const std::string& txn) const;

 private:
  std::filesystem::path dir_;
  std::filesystem::path path_;
  int fd_ = -1;
  std::uint64_t next_seq_ = 1;
  std::vector<JournalRecord> records_;
};

}  // namespace ep
