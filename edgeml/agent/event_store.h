/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Local SQLite event storage with rolling retention (spec section 22).
 */

#ifndef EDGEML_AGENT_EVENT_STORE_H
#define EDGEML_AGENT_EVENT_STORE_H

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/alert_manager.h"
#include "core/alert/alert.h"
#include "core/config/config.h"
#include "core/schema/telemetry.h"

struct sqlite3;

namespace edgeml {

/** Timeline entry: agent lifecycle, collector health, alerts, demo actions. */
struct Event {
  uint64_t id = 0;
  uint64_t timestamp_ms = 0;
  std::string type;
  Severity severity = Severity::Info;
  std::string source;
  std::string message;
  nlohmann::json data = nlohmann::json::object();
};

void to_json(nlohmann::json &j, const Event &event);

class EventStore {
public:
  EventStore() = default;
  ~EventStore();
  EventStore(const EventStore &) = delete;
  EventStore &operator=(const EventStore &) = delete;

  bool open(const std::string &path, std::string *error = nullptr);
  bool is_open() const { return db_ != nullptr; }

  void record_event(const Event &event);
  void upsert_alert(const Alert &alert);
  void record_metric_summary(uint64_t minute_ms, const std::string &metric,
                             double min, double max, double avg,
                             uint64_t count);
  void set_agent_state(const std::string &key, const std::string &value,
                       uint64_t now_ms);

  std::vector<Event> recent_events(size_t limit);
  std::vector<nlohmann::json> recent_alerts(size_t limit);
  /** Newest-first per-minute rollups for one metric. */
  std::vector<nlohmann::json> metric_summary(const std::string &metric,
                                             size_t limit);
  /** Highest numeric alert suffix for the prefix (0 when none). */
  uint64_t max_alert_sequence(const std::string &prefix);

  void prune(uint64_t now_ms, const StorageConfig &retention);

private:
  bool exec(const char *sql, std::string *error = nullptr);

  std::mutex mutex_;
  sqlite3 *db_ = nullptr;
};

/** Persists alert lifecycle changes. */
class StoreAlertSink : public AlertSink {
public:
  explicit StoreAlertSink(EventStore &store) : store_(store) {}
  void publish(const Alert &alert, AlertChange) override {
    store_.upsert_alert(alert);
  }
  const char *name() const override { return "sqlite"; }

private:
  EventStore &store_;
};

/** Per-minute min/max/avg rollup so raw telemetry is never stored. */
class MetricSummaryAggregator {
public:
  void add(const TelemetryBatch &batch, EventStore &store);
  void flush(EventStore &store);

private:
  struct Accumulator {
    double min = 0, max = 0, sum = 0;
    uint64_t count = 0;
  };
  uint64_t minute_ms_ = 0;
  std::map<std::string, Accumulator> accumulators_;
};

} // namespace edgeml

#endif
