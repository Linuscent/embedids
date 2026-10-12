/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_CORE_INTEGRITY_HASH_UTIL_H
#define EDGEML_CORE_INTEGRITY_HASH_UTIL_H

#include <optional>
#include <string>

#include "core/integrity/sha256.h"

namespace edgeml {

inline std::optional<std::string> sha256_file(const std::string &path) {
  char hex[EDGEML_SHA256_HEX_LEN];
  if (edgeml_sha256_file(path.c_str(), hex) != 0) {
    return std::nullopt;
  }
  return std::string(hex);
}

inline std::string sha256_string(const std::string &data) {
  char hex[EDGEML_SHA256_HEX_LEN];
  edgeml_sha256_hex(data.data(), data.size(), hex);
  return hex;
}

/** Shorten a hash for display: "83ff1a2b...". */
inline std::string short_hash(const std::string &hash, size_t length = 12) {
  return hash.size() > length ? hash.substr(0, length) + "..." : hash;
}

} // namespace edgeml

#endif
