#pragma once

// Field-level JSON checks shared by the policy validators. Both the scheduling
// contract and the cgroup contract enforce the same rules (unknown keys rejected,
// integers never accepted as floats, cgroup paths normalized), so one copy of each
// rule is what keeps their error messages and their holes identical.

#include <cstddef>
#include <initializer_list>
#include <regex>
#include <string>

#include "ep/util.hpp"

namespace ep {

inline bool check_keys(const Json& object, std::initializer_list<const char*> allowed,
                       const std::string& path, std::string* error) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    bool known = false;
    for (const char* key : allowed) {
      if (it.key() == key) {
        known = true;
        break;
      }
    }
    if (!known) {
      if (error != nullptr) *error = path + " has unknown field '" + it.key() + "'";
      return false;
    }
  }
  return true;
}

inline bool get_string(const Json& object, const char* key, const std::string& path,
                       std::size_t min_len, std::size_t max_len, std::string* out,
                       std::string* error) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    if (error != nullptr) *error = path + "." + key + " must be a string";
    return false;
  }
  *out = object.at(key).get<std::string>();
  if (out->size() < min_len || out->size() > max_len) {
    if (error != nullptr) {
      *error = path + "." + key + " length must be " + std::to_string(min_len) + ".." +
               std::to_string(max_len);
    }
    return false;
  }
  return true;
}

inline bool get_int(const Json& object, const char* key, const std::string& path,
                    long long minimum, long long maximum, long long* out, std::string* error) {
  if (!object.contains(key) ||
      (!object.at(key).is_number_integer() && !object.at(key).is_number_unsigned())) {
    if (error != nullptr) *error = path + "." + key + " must be an integer";
    return false;
  }
  const long long value = object.at(key).get<long long>();
  if (value < minimum || value > maximum) {
    if (error != nullptr) {
      *error = path + "." + key + " out of range [" + std::to_string(minimum) + "," +
               std::to_string(maximum) + "]";
    }
    return false;
  }
  *out = value;
  return true;
}

inline bool get_number(const Json& object, const char* key, const std::string& path,
                       double minimum, double maximum, bool minimum_exclusive, double* out,
                       std::string* error) {
  if (!object.contains(key) || !object.at(key).is_number()) {
    if (error != nullptr) *error = path + "." + key + " must be a number";
    return false;
  }
  const double value = object.at(key).get<double>();
  const bool below = minimum_exclusive ? value <= minimum : value < minimum;
  if (below || value > maximum) {
    if (error != nullptr) *error = path + "." + key + " out of range";
    return false;
  }
  *out = value;
  return true;
}

inline bool get_enum(const Json& object, const char* key, const std::string& path,
                     std::initializer_list<const char*> allowed, std::string* out,
                     std::string* error) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    if (error != nullptr) *error = path + "." + key + " must be a string";
    return false;
  }
  const std::string value = object.at(key).get<std::string>();
  for (const char* candidate : allowed) {
    if (value == candidate) {
      *out = value;
      return true;
    }
  }
  if (error != nullptr) *error = path + "." + key + " has invalid value '" + value + "'";
  return false;
}

inline bool valid_cgroup_path(const std::string& path) {
  static const std::regex pattern("^/sys/fs/cgroup/[A-Za-z0-9_./-]+$");
  if (path.size() > 1024 || !std::regex_match(path, pattern)) return false;
  // Reject "." and ".." segments even though the regex admits dots.
  std::string segment;
  for (std::size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (segment == "." || segment == "..") return false;
      segment.clear();
    } else {
      segment.push_back(path[i]);
    }
  }
  return true;
}

}  // namespace ep
