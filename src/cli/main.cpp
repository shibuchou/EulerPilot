#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ep/domain.hpp"
#include "ep/util.hpp"

namespace {

std::string default_socket() {
  if (const char* env = ::getenv("EP_SOCKET"); env != nullptr && *env != '\0') return env;
  if (::geteuid() == 0) return "/run/eulerpilot-v2/executor/executor.sock";
  if (const char* xdg = ::getenv("XDG_RUNTIME_DIR"); xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/eulerpilot-v2/executor/executor.sock";
  }
  return "/tmp/eulerpilot-v2-" + std::to_string(::geteuid()) + "/executor.sock";
}

struct Options {
  std::string command;
  std::string socket_path = default_socket();
  std::string file;
  std::string intent;
  std::string intent_file;
  std::string rollback_of;
  std::string original_transaction;
  std::string snapshot_id;
  std::string policy_sha256;
  std::string idempotency_key;
  std::string expert;
  std::string lease_owner;
  std::uint64_t ttl_ms = 30000;
};

void print_usage() {
  std::cout << "usage:\n"
               "  ep status [--socket PATH]\n"
               "  ep features [--socket PATH]\n"
               "  ep route [--socket PATH]\n"
               "  ep slo --expert NAME [--socket PATH]\n"
               "  ep observe --file OBSERVATION.json [--socket PATH]\n"
               "  ep lease [--ttl-ms N] [--owner NAME] [--socket PATH]\n"
               "  ep validate --file POLICY.json [--socket PATH]\n"
               "  ep apply (--file POLICY.json | --intent JSON | --intent-file FILE)\n"
               "           [--idempotency-key KEY] [--ttl-ms N] [--socket PATH]\n"
               "  ep rollback --of ACTION --transaction TXN --snapshot SNAP "
               "--policy-sha256 HEX [--socket PATH]\n"
               "  ep reconcile [--socket PATH]\n";
}

bool write_all(int fd, const std::string& data) {
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  return true;
}

bool send_request(const std::string& socket_path, const ep::Json& request,
                  ep::Json* response, std::string* error) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    *error = std::string("socket: ") + std::strerror(errno);
    return false;
  }
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    *error = "connect " + socket_path + ": " + std::strerror(errno);
    ::close(fd);
    return false;
  }
  if (!write_all(fd, request.dump() + "\n")) {
    *error = "write failed";
    ::close(fd);
    return false;
  }
  std::string data;
  char buffer[4096];
  while (data.size() < (1u << 20)) {
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) continue;
      *error = std::string("read failed: ") + std::strerror(errno);
      ::close(fd);
      return false;
    }
    if (n == 0) break;
    data.append(buffer, static_cast<std::size_t>(n));
    if (data.find('\n') != std::string::npos) break;
  }
  ::close(fd);
  if (data.empty()) {
    *error = "empty response";
    return false;
  }
  try {
    *response = ep::Json::parse(data);
  } catch (const std::exception& exception) {
    *error = std::string("invalid response JSON: ") + exception.what();
    return false;
  }
  return true;
}

bool read_json_file(const std::string& path, ep::Json* value, std::string* error) {
  std::ifstream input(path);
  if (!input) {
    *error = "cannot open " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  try {
    *value = ep::Json::parse(buffer.str());
  } catch (const std::exception& exception) {
    *error = "invalid JSON in " + path + ": " + exception.what();
    return false;
  }
  return true;
}

ep::Json make_envelope(const std::string& operation, const std::string& policy_sha256,
                       const std::string& lease_owner, std::uint64_t fence,
                       std::uint64_t generation, const std::string& idempotency_key,
                       const ep::Json& rollback_of) {
  ep::Json envelope{{"apiVersion", ep::kApiVersion},
                    {"request_id", "req-" + ep::random_hex(12)},
                    {"transaction_id", "txn-" + ep::random_hex(12)},
                    {"idempotency_key", idempotency_key},
                    {"operation", operation},
                    {"policy_sha256", policy_sha256},
                    {"lease_owner", lease_owner},
                    {"fence_epoch", fence},
                    {"expected_generation", generation},
                    {"issued_at", ep::now_iso8601()},
                    {"deadline", ep::iso8601_from_ms(ep::now_epoch_ms() + 60000)}};
  if (!rollback_of.empty()) envelope["rollback_of"] = rollback_of;
  return envelope;
}

bool acquire_lease(const std::string& socket_path, std::uint64_t ttl_ms, const std::string& owner,
                   ep::Json* lease, std::string* error) {
  // An explicit owner is how an operator hands the lease to the autopilot identity;
  // without it each call gets a fresh cli identity.
  const std::string resolved_owner = owner.empty() ? "cli-" + ep::random_hex(8) : owner;
  ep::Json response;
  const ep::Json request{{"operation", "lease.acquire"},
                         {"request_id", "req-" + ep::random_hex(12)},
                         {"lease_owner", resolved_owner},
                         {"lease_ttl_ms", ttl_ms}};
  if (!send_request(socket_path, request, &response, error)) return false;
  if (response.value("status", "") != "OK") {
    *error = "lease.acquire failed: " + response.dump();
    return false;
  }
  *lease = response;
  return true;
}

int print_response(const ep::Json& response) {
  std::cout << response.dump(2) << "\n";
  const std::string status = response.value("status", "");
  if (status == "OK" || status == "VERIFIED" || status == "APPLIED" ||
      status == "ROLLED_BACK") {
    return 0;
  }
  return 1;
}

bool parse_args(int argc, char** argv, Options* options, std::string* error) {
  if (argc < 2) {
    print_usage();
    *error = "missing command";
    return false;
  }
  options->command = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](std::string* out) -> bool {
      if (i + 1 >= argc) {
        *error = "missing value for " + arg;
        return false;
      }
      *out = argv[++i];
      return true;
    };
    if (arg == "--socket") {
      if (!next(&options->socket_path)) return false;
    } else if (arg == "--file") {
      if (!next(&options->file)) return false;
    } else if (arg == "--intent") {
      if (!next(&options->intent)) return false;
    } else if (arg == "--intent-file") {
      if (!next(&options->intent_file)) return false;
    } else if (arg == "--of") {
      if (!next(&options->rollback_of)) return false;
    } else if (arg == "--transaction") {
      if (!next(&options->original_transaction)) return false;
    } else if (arg == "--snapshot") {
      if (!next(&options->snapshot_id)) return false;
    } else if (arg == "--policy-sha256") {
      if (!next(&options->policy_sha256)) return false;
    } else if (arg == "--idempotency-key") {
      if (!next(&options->idempotency_key)) return false;
    } else if (arg == "--expert") {
      if (!next(&options->expert)) return false;
    } else if (arg == "--owner") {
      if (!next(&options->lease_owner)) return false;
    } else if (arg == "--ttl-ms") {
      std::string value;
      if (!next(&value)) return false;
      options->ttl_ms = std::strtoull(value.c_str(), nullptr, 10);
    } else if (arg == "-h" || arg == "--help") {
      print_usage();
      std::exit(0);
    } else {
      *error = "unknown argument: " + arg;
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!parse_args(argc, argv, &options, &error)) {
    std::cerr << "ep: " << error << "\n";
    return 2;
  }

  if (options.command == "status") {
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "policy.status"},
                               {"request_id", "req-" + ep::random_hex(8)}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "features") {
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "feature.snapshot"},
                               {"request_id", "req-" + ep::random_hex(8)}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "route") {
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "route.decide"},
                               {"request_id", "req-" + ep::random_hex(8)}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "slo") {
    if (options.expert.empty()) {
      std::cerr << "ep: slo needs --expert NAME\n";
      return 2;
    }
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "slo.evaluate"},
                               {"request_id", "req-" + ep::random_hex(8)},
                               {"expert", options.expert}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "observe") {
    if (options.file.empty()) {
      std::cerr << "ep: observe needs --file OBSERVATION.json\n";
      return 2;
    }
    ep::Json observation;
    if (!read_json_file(options.file, &observation, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "service.observe"},
                               {"request_id", "req-" + ep::random_hex(8)},
                               {"observation", observation}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "reconcile") {
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "transaction.reconcile"},
                               {"request_id", "req-" + ep::random_hex(8)}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "validate") {
    if (options.file.empty()) {
      std::cerr << "ep: validate needs --file POLICY.json\n";
      return 2;
    }
    ep::Json policy;
    if (!read_json_file(options.file, &policy, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "policy.validate"},
                               {"request_id", "req-" + ep::random_hex(8)},
                               {"policy", policy}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "lease") {
    ep::Json lease;
    if (!acquire_lease(options.socket_path, options.ttl_ms, options.lease_owner, &lease, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(lease);
  }

  if (options.command == "apply") {
    ep::Json payload;
    std::string payload_key;
    if (!options.file.empty()) {
      if (!read_json_file(options.file, &payload, &error)) {
        std::cerr << "ep: " << error << "\n";
        return 1;
      }
      payload_key = "policy";
    } else if (!options.intent.empty()) {
      try {
        payload = ep::Json::parse(options.intent);
      } catch (const std::exception& exception) {
        std::cerr << "ep: invalid --intent JSON: " << exception.what() << "\n";
        return 1;
      }
      payload_key = "intent";
    } else if (!options.intent_file.empty()) {
      if (!read_json_file(options.intent_file, &payload, &error)) {
        std::cerr << "ep: " << error << "\n";
        return 1;
      }
      payload_key = "intent";
    } else {
      std::cerr << "ep: apply needs --file, --intent or --intent-file\n";
      return 2;
    }

    ep::Json lease;
    if (!acquire_lease(options.socket_path, options.ttl_ms, options.lease_owner, &lease, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    const std::string digest = ep::sha256_hex(ep::canonical_json(payload));
    const std::string idempotency_key =
        options.idempotency_key.empty() ? "idem-" + ep::random_hex(12) : options.idempotency_key;
    const ep::Json envelope =
        make_envelope("policy.apply", digest, lease.at("lease_owner").get<std::string>(),
                      lease.at("fence_epoch").get<std::uint64_t>(),
                      lease.at("observed_generation").get<std::uint64_t>(),
                      idempotency_key, ep::Json::object());
    ep::Json request{{"operation", "policy.apply"}, {"envelope", envelope}};
    request[payload_key] = payload;
    ep::Json response;
    if (!send_request(options.socket_path, request, &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  if (options.command == "rollback") {
    if (options.rollback_of.empty() || options.original_transaction.empty() ||
        options.snapshot_id.empty() || options.policy_sha256.empty()) {
      std::cerr << "ep: rollback needs --of, --transaction, --snapshot and --policy-sha256\n";
      return 2;
    }
    ep::Json lease;
    if (!acquire_lease(options.socket_path, options.ttl_ms, options.lease_owner, &lease, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    const ep::Json rollback_of{{"apply_action_id", options.rollback_of},
                               {"original_transaction_id", options.original_transaction},
                               {"snapshot_id", options.snapshot_id}};
    const std::string idempotency_key =
        options.idempotency_key.empty() ? "idem-" + ep::random_hex(12) : options.idempotency_key;
    const ep::Json envelope =
        make_envelope("policy.rollback", options.policy_sha256,
                      lease.at("lease_owner").get<std::string>(),
                      lease.at("fence_epoch").get<std::uint64_t>(),
                      lease.at("observed_generation").get<std::uint64_t>(),
                      idempotency_key, rollback_of);
    ep::Json response;
    if (!send_request(options.socket_path,
                      ep::Json{{"operation", "policy.rollback"}, {"envelope", envelope}},
                      &response, &error)) {
      std::cerr << "ep: " << error << "\n";
      return 1;
    }
    return print_response(response);
  }

  std::cerr << "ep: unknown command " << options.command << "\n";
  print_usage();
  return 2;
}
