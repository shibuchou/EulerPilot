#include "ep/util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace ep {
namespace {

constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

void sha256_compress(std::uint32_t state[8], const unsigned char block[64]) {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
           (static_cast<std::uint32_t>(block[i * 4 + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t temp1 = h + s1 + ch + kSha256K[i] + w[i];
    const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void encode_string(const Json& value, std::string* out) {
  // nlohmann already emits a valid JSON string literal for strings.
  *out += value.dump();
}

void encode_canonical(const Json& value, std::string* out) {
  if (value.is_object()) {
    std::vector<std::string> keys;
    keys.reserve(value.size());
    for (auto it = value.begin(); it != value.end(); ++it) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    *out += '{';
    bool first = true;
    for (const auto& key : keys) {
      if (!first) *out += ',';
      first = false;
      encode_string(Json(key), out);
      *out += ':';
      encode_canonical(value.at(key), out);
    }
    *out += '}';
    return;
  }
  if (value.is_array()) {
    *out += '[';
    bool first = true;
    for (const auto& item : value) {
      if (!first) *out += ',';
      first = false;
      encode_canonical(item, out);
    }
    *out += ']';
    return;
  }
  *out += value.dump();
}

}  // namespace

std::string sha256_hex(const std::string& data) {
  std::uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::string message = data;
  const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8u;
  message.push_back(static_cast<char>(0x80));
  while (message.size() % 64 != 56) message.push_back('\0');
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<char>((bit_len >> shift) & 0xffu));
  }
  for (std::size_t offset = 0; offset < message.size(); offset += 64) {
    sha256_compress(state, reinterpret_cast<const unsigned char*>(message.data() + offset));
  }
  char hex[65];
  for (int i = 0; i < 8; ++i) {
    std::snprintf(hex + i * 8, 9, "%08x", state[i]);
  }
  return std::string(hex, 64);
}

std::string canonical_json(const Json& value) {
  std::string out;
  encode_canonical(value, &out);
  return out;
}

int64_t now_epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string now_iso8601() { return iso8601_from_ms(now_epoch_ms()); }

std::string iso8601_from_ms(int64_t epoch_ms) {
  const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
  std::tm tm{};
  gmtime_r(&seconds, &tm);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                tm.tm_sec, static_cast<int>(epoch_ms % 1000));
  return std::string(buffer);
}

bool parse_iso8601_ms(const std::string& text, int64_t* out_ms) {
  if (text.size() < 20) return false;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  int consumed = 0;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour,
                  &minute, &second, &consumed) != 6) {
    return false;
  }
  if (consumed < 19) return false;
  std::size_t pos = static_cast<std::size_t>(consumed);
  int millis = 0;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    int digits = 0;
    int value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && digits < 9) {
      if (digits < 3) value = value * 10 + (text[pos] - '0');
      ++digits;
      ++pos;
    }
    if (digits == 0) return false;
    while (digits < 3) {
      value *= 10;
      ++digits;
    }
    millis = value;
  }
  int offset_minutes = 0;
  if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
    ++pos;
  } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    const int sign = text[pos] == '+' ? 1 : -1;
    ++pos;
    if (pos + 5 > text.size()) return false;
    int oh = 0, om = 0;
    if (std::sscanf(text.c_str() + pos, "%2d:%2d", &oh, &om) != 2) return false;
    if (oh < 0 || oh > 23 || om < 0 || om > 59) return false;
    offset_minutes = sign * (oh * 60 + om);
    pos += 5;
  } else {
    return false;
  }
  if (pos != text.size()) return false;
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 60) {
    return false;
  }
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  const std::time_t seconds = timegm(&tm);
  if (out_ms != nullptr) {
    *out_ms = static_cast<int64_t>(seconds) * 1000 + millis -
              static_cast<int64_t>(offset_minutes) * 60 * 1000;
  }
  return true;
}

std::string random_hex(std::size_t bytes) {
  std::vector<unsigned char> buffer(bytes, 0);
  const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    std::size_t filled = 0;
    while (filled < bytes) {
      const ssize_t n = ::read(fd, buffer.data() + filled, bytes - filled);
      if (n <= 0) break;
      filled += static_cast<std::size_t>(n);
    }
    ::close(fd);
  }
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  for (unsigned char byte : buffer) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0f]);
  }
  return out;
}

}  // namespace ep
