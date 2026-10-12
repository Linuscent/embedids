/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_COLLECTORS_COMMON_FILE_UTIL_H
#define EDGEML_COLLECTORS_COMMON_FILE_UTIL_H

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace edgeml {

inline std::optional<std::string> read_text_file(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream content;
  content << in.rdbuf();
  return content.str();
}

} // namespace edgeml

#endif
