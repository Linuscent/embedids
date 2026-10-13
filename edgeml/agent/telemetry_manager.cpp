/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent/telemetry_manager.h"

#include <exception>

namespace edgeml {

void to_json(nlohmann::json &j, const CollectorStatus &status) {
  j = nlohmann::json{{"name", status.name},
                     {"initialized", status.initialized},
                     {"healthy", status.healthy},
                     {"heartbeat_required", status.heartbeat_required},
                     {"last_success_ms", status.last_success_ms},
                     {"last_heartbeat_ms", status.last_heartbeat_ms},
                     {"consecutive_failures", status.consecutive_failures},
                     {"total_failures", status.total_failures}};
}

TelemetryManager::TelemetryManager(const Config &config, const Clock &clock)
    : config_(config), clock_(clock) {}

void TelemetryManager::add_collector(
    std::unique_ptr<MetricCollector> collector) {
  CollectorStatus status;
  status.name = collector->name();
  status.heartbeat_required = collector->heartbeat_required();
  status_.push_back(status);
  collectors_.push_back(std::move(collector));
}

void TelemetryManager::initialize(std::vector<std::string> *warnings) {
  const uint64_t now = clock_.now_ms();
  for (size_t i = 0; i < collectors_.size(); i++) {
    bool ok = false;
    try {
      ok = collectors_[i]->initialize();
    } catch (const std::exception &e) {
      if (warnings) {
        warnings->push_back(status_[i].name + ": " + e.what());
      }
    }
    status_[i].initialized = ok;
    // Heartbeat clock starts at agent start so a dead source is detected.
    status_[i].last_heartbeat_ms = now;
    if (!ok && warnings) {
      warnings->push_back("collector " + status_[i].name +
                          " unavailable on this platform - disabled");
    }
  }
}

TelemetryBatch
TelemetryManager::collect(std::vector<CollectorTransition> *transitions) {
  TelemetryBatch batch;
  batch.cycle = ++cycle_;
  batch.timestamp_ms = clock_.now_ms();
  batch.device_id = config_.device.id;

  const uint64_t heartbeat_interval_ms =
      config_.heartbeat.interval_seconds * 1000ULL;
  size_t initialized = 0;
  size_t healthy = 0;

  for (size_t i = 0; i < collectors_.size(); i++) {
    CollectorStatus &status = status_[i];
    if (!status.initialized) {
      continue;
    }
    initialized++;
    const size_t first_new = batch.metrics.size();
    bool ok = false;
    try {
      ok = collectors_[i]->collect(batch);
    } catch (const std::exception &) {
      ok = false;
    }

    // Normalization: stamp identity and time on every new sample.
    for (size_t m = first_new; m < batch.metrics.size(); m++) {
      MetricSample &sample = batch.metrics[m];
      sample.device_id = batch.device_id;
      if (sample.timestamp_ms == 0) {
        sample.timestamp_ms = batch.timestamp_ms;
      }
    }

    const bool was_healthy = status.healthy || status.last_success_ms == 0;
    status.healthy = ok;
    if (ok) {
      healthy++;
      status.last_success_ms = batch.timestamp_ms;
      status.consecutive_failures = 0;
      if (batch.timestamp_ms - status.last_heartbeat_ms >=
          heartbeat_interval_ms) {
        status.last_heartbeat_ms = batch.timestamp_ms;
      }
    } else {
      status.consecutive_failures++;
      status.total_failures++;
    }
    if (transitions && ok != was_healthy) {
      transitions->push_back({status.name, ok});
    }
  }

  // Heartbeat age = oldest heartbeat among required collectors.
  double age_s = 0.0;
  std::string stale;
  for (const auto &status : status_) {
    if (!status.initialized || !status.heartbeat_required) {
      continue;
    }
    const double age =
        static_cast<double>(batch.timestamp_ms - status.last_heartbeat_ms) /
        1000.0;
    age_s = std::max(age_s, age);
    if (age > config_.heartbeat.timeout_seconds) {
      stale += (stale.empty() ? "" : ",") + status.name;
    }
  }
  last_heartbeat_age_s_ = age_s;

  batch.add("agent", metrics::kAgentHeartbeat, age_s, "seconds",
            {{"stale", stale}});
  batch.add("agent", metrics::kTelemetryHealth,
            initialized == 0 ? 0.0
                             : 100.0 * static_cast<double>(healthy) /
                                   static_cast<double>(initialized),
            "percent");
  return batch;
}

void TelemetryManager::reset_heartbeats() {
  const uint64_t now = clock_.now_ms();
  for (auto &status : status_) {
    status.last_heartbeat_ms = now;
  }
}

} // namespace edgeml
