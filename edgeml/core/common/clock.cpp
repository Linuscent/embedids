/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/common/clock.h"

#include <chrono>
#include <ctime>

namespace edgeml {

uint64_t SystemClock::now_ms() const {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count());
}

uint64_t monotonic_ms() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

std::string format_iso8601(uint64_t epoch_ms) {
  std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
  std::tm tm_utc{};
  gmtime_r(&seconds, &tm_utc);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buffer;
}

} // namespace edgeml
