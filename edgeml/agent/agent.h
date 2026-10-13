/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * The EmbedIDS EdgeML security agent: collect -> normalize -> detect ->
 * correlate -> alert, plus the thread-safe views served by the REST API.
 */

#ifndef EDGEML_AGENT_AGENT_H
#define EDGEML_AGENT_AGENT_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/alert_manager.h"
#include "agent/event_store.h"
#include "agent/telemetry_manager.h"
#include "collectors/collector_factory.h"
#include "core/common/clock.h"
#include "core/config/config.h"
#include "core/correlation/correlator.h"
#include "core/detection/detection_engine.h"

namespace edgeml {

inline constexpr const char *kAgentVersion = "0.1.0";

class Agent {
public:
  Agent(Config config, const Clock &clock);
  ~Agent();

  Agent(const Agent &) = delete;
  Agent &operator=(const Agent &) = delete;

  /** Add an extra sink (tests, embedding). Call before initialize(). */
  void add_alert_sink(std::unique_ptr<AlertSink> sink);

  /** Replace the collector set (tests). Call before initialize(). */
  void set_collectors(CollectorSet collectors);

  bool initialize(std::string *error = nullptr);

  /** Execute one sampling/detection cycle. */
  void run_cycle();

  /** Loop at sampling.interval_ms until stop becomes true. */
  void run(const std::atomic<bool> &stop);

  // ---- Thread-safe views (REST API) ----
  nlohmann::json health() const;
  nlohmann::json status() const;
  nlohmann::json metrics(uint32_t history_seconds) const;
  std::string prometheus() const;
  nlohmann::json alerts(bool active_only, size_t limit) const;
  std::optional<nlohmann::json> alert(const std::string &alert_id) const;
  nlohmann::json events(size_t limit) const;
  nlohmann::json rules() const;

  bool demo_enabled() const;
  bool demo_scenario(const std::string &name, nlohmann::json *result,
                     std::string *error);
  bool demo_reset(nlohmann::json *result, std::string *error);

  std::vector<Alert> alert_snapshot() const;
  SecurityState security_state() const;
  const Config &config() const { return config_; }
  uint64_t cycles() const { return cycles_; }

private:
  void record_event_locked(Event event);
  void handle_alert_updates_locked(const std::vector<AlertUpdate> &updates);
  void update_history_locked(const TelemetryBatch &batch);

  Config config_;
  const Clock &clock_;

  // Pipeline (guarded by pipeline_mutex_).
  std::mutex pipeline_mutex_;
  CollectorSet collector_set_;
  bool collectors_injected_ = false;
  std::unique_ptr<TelemetryManager> telemetry_;
  std::unique_ptr<DetectionEngine> detection_;
  std::unique_ptr<Correlator> correlator_;
  MetricSummaryAggregator summary_;

  // Shared state (guarded by state_mutex_).
  mutable std::mutex state_mutex_;
  std::vector<std::unique_ptr<AlertSink>> pending_sinks_;
  std::unique_ptr<AlertManager> alerts_;
  std::unique_ptr<EventStore> store_;
  std::deque<Event> events_;
  uint64_t next_event_id_ = 1;
  std::map<std::string, MetricSample> latest_;
  std::map<std::string, std::deque<std::pair<uint64_t, double>>> history_;
  std::vector<CollectorStatus> collector_status_;
  nlohmann::json simulation_state_;
  double last_processing_ms_ = 0.0;
  double last_cycle_ms_ = 0.0;
  double max_processing_ms_ = 0.0;
  uint64_t last_prune_ms_ = 0;

  std::atomic<uint64_t> cycles_{0};
  uint64_t start_ms_ = 0;
  std::string binary_hash_;
  bool initialized_ = false;
};

} // namespace edgeml

#endif
