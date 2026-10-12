/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runs collectors each cycle, normalizes their output and tracks per-collector
 * heartbeats (spec sections 10.7 and 21).
 */

#ifndef EDGEML_AGENT_TELEMETRY_MANAGER_H
#define EDGEML_AGENT_TELEMETRY_MANAGER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "collectors/common/collector.h"
#include "core/common/clock.h"
#include "core/config/config.h"

namespace edgeml {

struct CollectorStatus {
  std::string name;
  bool initialized = false;
  bool healthy = false;
  bool heartbeat_required = true;
  uint64_t last_success_ms = 0;
  uint64_t last_heartbeat_ms = 0;
  uint64_t consecutive_failures = 0;
  uint64_t total_failures = 0;
};

void to_json(nlohmann::json &j, const CollectorStatus &status);

/** A collector transitioned between healthy and failing. */
struct CollectorTransition {
  std::string name;
  bool healthy;
};

class TelemetryManager {
public:
  TelemetryManager(const Config &config, const Clock &clock);

  void add_collector(std::unique_ptr<MetricCollector> collector);

  /** Initialize collectors; failures are reported but not fatal. */
  void initialize(std::vector<std::string> *warnings = nullptr);

  /**
   * Collect one batch. Appends security.agent_heartbeat and
   * security.telemetry_health.
   */
  TelemetryBatch
  collect(std::vector<CollectorTransition> *transitions = nullptr);

  /** Restart heartbeat timers (demo reset). */
  void reset_heartbeats();

  std::vector<CollectorStatus> status() const { return status_; }
  double heartbeat_age_seconds() const { return last_heartbeat_age_s_; }

private:
  Config config_;
  const Clock &clock_;
  std::vector<std::unique_ptr<MetricCollector>> collectors_;
  std::vector<CollectorStatus> status_;
  uint64_t cycle_ = 0;
  double last_heartbeat_age_s_ = 0.0;
};

} // namespace edgeml

#endif
