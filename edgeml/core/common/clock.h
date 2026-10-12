/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_CORE_COMMON_CLOCK_H
#define EDGEML_CORE_COMMON_CLOCK_H

#include <atomic>
#include <cstdint>
#include <string>

namespace edgeml {

/** Source of wall-clock time in milliseconds since the Unix epoch. */
class Clock {
public:
  virtual ~Clock() = default;
  virtual uint64_t now_ms() const = 0;
};

class SystemClock : public Clock {
public:
  uint64_t now_ms() const override;
};

/** Manually advanced clock used by tests and deterministic simulations. */
class ManualClock : public Clock {
public:
  explicit ManualClock(uint64_t start_ms = 1790000000000ULL) : now_(start_ms) {}
  uint64_t now_ms() const override { return now_.load(); }
  void advance_ms(uint64_t delta) { now_ += delta; }
  void set_ms(uint64_t value) { now_ = value; }

private:
  std::atomic<uint64_t> now_;
};

/** Monotonic milliseconds, for measuring durations only. */
uint64_t monotonic_ms();

/** Format epoch milliseconds as ISO-8601 UTC, e.g. 2026-10-06T22:00:00Z. */
std::string format_iso8601(uint64_t epoch_ms);

} // namespace edgeml

#endif
