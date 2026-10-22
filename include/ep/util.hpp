#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace ep {

using Json = nlohmann::json;

// Lowercase hex SHA-256 of the raw bytes in `data`.
std::string sha256_hex(const std::string& data);

// Deterministic JSON encoding: object keys sorted by byte order, no
// insignificant whitespace. Used for policy digests. It is not a full RFC
// 8785 implementation (numbers keep nlohmann's representation).
std::string canonical_json(const Json& value);

// Current UTC time, e.g. "2026-10-21T03:40:00.123Z".
std::string now_iso8601();
std::string iso8601_from_ms(int64_t epoch_ms);
int64_t now_epoch_ms();

// Parses RFC 3339 timestamps with an explicit "Z" or "+HH:MM" offset.
// Fractional seconds are optional and truncated to milliseconds.
bool parse_iso8601_ms(const std::string& text, int64_t* out_ms);

// Hex token from /dev/urandom; `bytes` random bytes, so length is 2*bytes.
std::string random_hex(std::size_t bytes);

}  // namespace ep
