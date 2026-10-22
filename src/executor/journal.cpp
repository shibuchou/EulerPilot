#include "ep/journal.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace ep {
namespace {

bool write_all(int fd, const std::string& data, std::string* error) {
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (error != nullptr) *error = std::string("write failed: ") + std::strerror(errno);
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  return true;
}

bool is_terminal(const std::string& type) {
  return type == "COMMITTED" || type == "ROLLED_BACK" || type == "RECOVERY_REQUIRED";
}

bool is_incomplete(const std::string& type) { return !is_terminal(type) && type != "RESULT"; }

}  // namespace

Json JournalRecord::to_json() const {
  Json value{{"seq", seq},         {"ts", ts},       {"txn", txn},
             {"action_id", action_id}, {"type", type}, {"generation", generation},
             {"owner", owner},     {"fence", fence},  {"data", data}};
  return value;
}

std::optional<JournalRecord> JournalRecord::from_json(const Json& value) {
  if (!value.is_object()) return std::nullopt;
  JournalRecord record;
  try {
    record.seq = value.at("seq").get<std::uint64_t>();
    record.ts = value.value("ts", "");
    record.txn = value.value("txn", "");
    record.action_id = value.value("action_id", "");
    record.type = value.at("type").get<std::string>();
    record.generation = value.value("generation", 0ULL);
    record.owner = value.value("owner", "");
    record.fence = value.value("fence", 0ULL);
    record.data = value.value("data", Json::object());
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return record;
}

Journal::Journal(std::filesystem::path dir) : dir_(std::move(dir)) {
  path_ = dir_ / "journal.jsonl";
}

Journal::~Journal() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool Journal::open(std::string* error) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    if (error != nullptr) *error = "cannot create journal dir: " + ec.message();
    return false;
  }
  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd_ < 0) {
    if (error != nullptr) *error = std::string("cannot open journal: ") + std::strerror(errno);
    return false;
  }

  std::ifstream input(path_, std::ios::binary);
  std::stringstream buffer;
  buffer << input.rdbuf();
  std::string content = buffer.str();

  // A crash can leave a partial last line. Drop it and make the file end at the
  // last complete newline so that appends start on a record boundary.
  if (!content.empty() && content.back() != '\n') {
    const std::size_t newline = content.find_last_of('\n');
    const std::size_t keep = (newline == std::string::npos) ? 0 : newline + 1;
    if (::ftruncate(fd_, static_cast<off_t>(keep)) != 0) {
      if (error != nullptr) *error = std::string("cannot truncate torn journal: ") + std::strerror(errno);
      return false;
    }
    content.resize(keep);
  }

  records_.clear();
  std::size_t start = 0;
  while (start < content.size()) {
    const std::size_t end = content.find('\n', start);
    if (end == std::string::npos) break;
    const std::string line = content.substr(start, end - start);
    start = end + 1;
    if (line.empty()) continue;
    Json parsed;
    try {
      parsed = Json::parse(line);
    } catch (const std::exception&) {
      if (error != nullptr) *error = "journal contains an unparsable record at seq " +
                                     std::to_string(records_.size() + 1);
      return false;
    }
    auto record = JournalRecord::from_json(parsed);
    if (!record.has_value()) {
      if (error != nullptr) *error = "journal record missing required fields";
      return false;
    }
    records_.push_back(*record);
  }
  next_seq_ = records_.empty() ? 1 : records_.back().seq + 1;
  last_good_size_ = static_cast<std::uint64_t>(content.size());
  return true;
}

bool Journal::rewind_to_last_good() {
  if (::ftruncate(fd_, static_cast<off_t>(last_good_size_)) != 0) return false;
  if (::lseek(fd_, 0, SEEK_END) < 0) return false;
  ::fsync(fd_);
  return true;
}

bool Journal::append(JournalRecord record, std::string* error) {
  if (fd_ < 0) {
    if (error != nullptr) *error = "journal is not open";
    return false;
  }
  if (broken_) {
    if (error != nullptr) *error = "journal is broken; restart and reconcile before appending";
    return false;
  }
  if (fault_after_ >= 0 && append_attempts_ >= fault_after_) {
    broken_ = true;
    if (error != nullptr) *error = "injected journal append failure";
    return false;
  }
  ++append_attempts_;
  record.seq = next_seq_;
  if (record.ts.empty()) record.ts = now_iso8601();
  std::string line = record.to_json().dump();
  line.push_back('\n');
  if (!write_all(fd_, line, error)) {
    broken_ = true;
    rewind_to_last_good();
    return false;
  }
  if (::fsync(fd_) != 0) {
    if (error != nullptr) *error = std::string("fsync failed: ") + std::strerror(errno);
    broken_ = true;
    rewind_to_last_good();
    return false;
  }
  ++next_seq_;
  last_good_size_ += line.size();
  records_.push_back(std::move(record));
  return true;
}

std::optional<JournalRecord> Journal::find_result(
    const std::string& idempotency_key) const {
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    if (it->type != "RESULT") continue;
    if (it->data.value("idempotency_key", "") == idempotency_key) return *it;
  }
  return std::nullopt;
}

bool Journal::append_result(const std::string& idempotency_key,
                            const std::string& request_sha256, const Json& response,
                            std::string* error) {
  JournalRecord record;
  record.type = "RESULT";
  record.data = Json{{"idempotency_key", idempotency_key},
                     {"request_sha256", request_sha256},
                     {"response", response}};
  return append(std::move(record), error);
}

std::uint64_t Journal::max_committed_generation() const {
  std::uint64_t generation = 0;
  for (const auto& record : records_) {
    if (record.type == "COMMITTED") {
      generation = std::max(generation, record.generation);
    } else if (record.type == "ROLLED_BACK" &&
               record.data.value("generation_advanced", false)) {
      generation = std::max(generation, record.generation);
    }
  }
  return generation;
}

std::vector<std::string> Journal::incomplete_transactions() const {
  std::map<std::string, std::string> last_type;
  std::vector<std::string> order;
  for (const auto& record : records_) {
    if (record.txn.empty()) continue;
    if (last_type.find(record.txn) == last_type.end()) order.push_back(record.txn);
    last_type[record.txn] = record.type;
  }
  std::vector<std::string> result;
  for (const auto& txn : order) {
    if (is_incomplete(last_type[txn])) result.push_back(txn);
  }
  return result;
}

std::vector<std::string> Journal::transactions_needing_reconcile() const {
  std::map<std::string, std::string> last_type;
  std::vector<std::string> order;
  for (const auto& record : records_) {
    if (record.txn.empty()) continue;
    if (last_type.find(record.txn) == last_type.end()) order.push_back(record.txn);
    last_type[record.txn] = record.type;
  }
  std::vector<std::string> result;
  for (const auto& txn : order) {
    const std::string& type = last_type[txn];
    if (is_incomplete(type) || type == "RECOVERY_REQUIRED") result.push_back(txn);
  }
  return result;
}

std::vector<JournalRecord> Journal::transaction_records(const std::string& txn) const {
  std::vector<JournalRecord> result;
  for (const auto& record : records_) {
    if (record.txn == txn) result.push_back(record);
  }
  return result;
}

}  // namespace ep
