/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/workload/model_file.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <sys/stat.h>

#include "core/integrity/hash_util.h"

namespace edgeml::sim {

namespace {

constexpr size_t kModelSize = 512 * 1024;
constexpr size_t kTamperOffset = 64 * 1024;

void make_parent_dirs(const std::string &path) {
  for (size_t pos = path.find('/', 1); pos != std::string::npos;
       pos = path.find('/', pos + 1)) {
    mkdir(path.substr(0, pos).c_str(), 0755);
  }
}

} // namespace

std::string approved_model_bytes() {
  std::string bytes = "EDGEML-TRT-ENGINE v1\n"
                      "network: yolo-edge-v8n\nprecision: int8\n"
                      "input: 1x3x640x640\n";
  bytes.reserve(kModelSize);
  uint64_t state = 0x5EED5EED12345678ULL;
  while (bytes.size() < kModelSize) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    bytes.push_back(static_cast<char>(state & 0xff));
  }
  return bytes;
}

std::string tampered_model_bytes() {
  std::string bytes = approved_model_bytes();
  static const char payload[] = "BACKDOOR-TRIGGER-PATCH:class_override=0;";
  for (size_t i = 0; i < 4096; i++) {
    bytes[kTamperOffset + i] = payload[i % (sizeof(payload) - 1)];
  }
  return bytes;
}

std::string approved_model_sha256() {
  static const std::string hash = sha256_string(approved_model_bytes());
  return hash;
}

bool write_model_file(const std::string &path, bool tampered,
                      std::string *error) {
  make_parent_dirs(path);
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) {
        *error = "cannot write " + tmp + ": " + std::strerror(errno);
      }
      return false;
    }
    const std::string bytes =
        tampered ? tampered_model_bytes() : approved_model_bytes();
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
      if (error) {
        *error = "short write to " + tmp;
      }
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) {
      *error = "cannot rename " + tmp + ": " + std::strerror(errno);
    }
    return false;
  }
  return true;
}

} // namespace edgeml::sim
