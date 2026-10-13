/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent/event_store.h"

#include <algorithm>

#include <sys/stat.h>

#include <sqlite3.h>

#include "core/common/clock.h"

namespace edgeml {

namespace {

constexpr const char *kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  type TEXT NOT NULL,
  severity TEXT NOT NULL,
  source TEXT,
  message TEXT,
  data TEXT
);
CREATE INDEX IF NOT EXISTS events_ts ON events(ts);
CREATE TABLE IF NOT EXISTS alerts (
  alert_id TEXT PRIMARY KEY,
  ts INTEGER NOT NULL,
  updated INTEGER,
  resolved INTEGER,
  severity TEXT NOT NULL,
  category TEXT,
  rule TEXT,
  subject TEXT,
  title TEXT,
  status TEXT,
  body TEXT
);
CREATE INDEX IF NOT EXISTS alerts_ts ON alerts(ts);
CREATE TABLE IF NOT EXISTS metrics_summary (
  minute INTEGER NOT NULL,
  metric TEXT NOT NULL,
  min REAL, max REAL, avg REAL, count INTEGER,
  PRIMARY KEY (minute, metric)
);
CREATE TABLE IF NOT EXISTS agent_state (
  key TEXT PRIMARY KEY,
  value TEXT,
  updated INTEGER
);
)SQL";

struct Statement {
  sqlite3_stmt *stmt = nullptr;
  Statement(sqlite3 *db, const char *sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      stmt = nullptr;
    }
  }
  ~Statement() { sqlite3_finalize(stmt); }
  explicit operator bool() const { return stmt != nullptr; }
  void text(int index, const std::string &value) {
    sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
  }
  void integer(int index, uint64_t value) {
    sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value));
  }
  void real(int index, double value) {
    sqlite3_bind_double(stmt, index, value);
  }
  int step() { return sqlite3_step(stmt); }
  std::string column_text(int index) {
    const unsigned char *value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char *>(value) : "";
  }
};

void make_parent_dirs(const std::string &path) {
  for (size_t pos = path.find('/', 1); pos != std::string::npos;
       pos = path.find('/', pos + 1)) {
    mkdir(path.substr(0, pos).c_str(), 0750);
  }
}

} // namespace

void to_json(nlohmann::json &j, const Event &event) {
  j = nlohmann::json{{"id", event.id},
                     {"timestamp", format_iso8601(event.timestamp_ms)},
                     {"timestamp_ms", event.timestamp_ms},
                     {"type", event.type},
                     {"severity", to_string(event.severity)},
                     {"source", event.source},
                     {"message", event.message},
                     {"data", event.data}};
}

EventStore::~EventStore() {
  if (db_) {
    sqlite3_close(db_);
  }
}

bool EventStore::exec(const char *sql, std::string *error) {
  char *message = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
    if (error) {
      *error = message ? message : "sqlite error";
    }
    sqlite3_free(message);
    return false;
  }
  return true;
}

bool EventStore::open(const std::string &path, std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (path != ":memory:") {
    make_parent_dirs(path);
  }
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    if (error) {
      *error = "cannot open " + path + ": " + sqlite3_errmsg(db_);
    }
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  sqlite3_busy_timeout(db_, 2000);
  exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
  if (!exec(kSchema, error)) {
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  return true;
}

void EventStore::record_event(const Event &event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) {
    return;
  }
  Statement s(db_, "INSERT INTO events(ts, type, severity, source, message, "
                   "data) VALUES (?, ?, ?, ?, ?, ?)");
  if (!s) {
    return;
  }
  s.integer(1, event.timestamp_ms);
  s.text(2, event.type);
  s.text(3, to_string(event.severity));
  s.text(4, event.source);
  s.text(5, event.message);
  s.text(6, event.data.dump());
  s.step();
}

void EventStore::upsert_alert(const Alert &alert) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) {
    return;
  }
  Statement s(db_,
              "INSERT INTO alerts(alert_id, ts, updated, resolved, severity, "
              "category, rule, subject, title, status, body) VALUES "
              "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(alert_id) DO "
              "UPDATE SET updated=excluded.updated, "
              "resolved=excluded.resolved, severity=excluded.severity, "
              "title=excluded.title, status=excluded.status, "
              "body=excluded.body");
  if (!s) {
    return;
  }
  s.text(1, alert.alert_id);
  s.integer(2, alert.timestamp_ms);
  s.integer(3, alert.updated_ms);
  s.integer(4, alert.resolved_ms);
  s.text(5, to_string(alert.severity));
  s.text(6, alert.category);
  s.text(7, alert.rule);
  s.text(8, alert.subject);
  s.text(9, alert.title);
  s.text(10, alert.active ? "active" : "resolved");
  s.text(11, nlohmann::json(alert).dump());
  s.step();
}

void EventStore::record_metric_summary(uint64_t minute_ms,
                                       const std::string &metric, double min,
                                       double max, double avg, uint64_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) {
    return;
  }
  Statement s(db_, "INSERT OR REPLACE INTO metrics_summary(minute, metric, "
                   "min, max, avg, count) VALUES (?, ?, ?, ?, ?, ?)");
  if (!s) {
    return;
  }
  s.integer(1, minute_ms);
  s.text(2, metric);
  s.real(3, min);
  s.real(4, max);
  s.real(5, avg);
  s.integer(6, count);
  s.step();
}

void EventStore::set_agent_state(const std::string &key,
                                 const std::string &value, uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) {
    return;
  }
  Statement s(db_, "INSERT OR REPLACE INTO agent_state(key, value, updated) "
                   "VALUES (?, ?, ?)");
  if (!s) {
    return;
  }
  s.text(1, key);
  s.text(2, value);
  s.integer(3, now_ms);
  s.step();
}

std::vector<Event> EventStore::recent_events(size_t limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Event> events;
  if (!db_) {
    return events;
  }
  Statement s(db_, "SELECT id, ts, type, severity, source, message, data FROM "
                   "events ORDER BY id DESC LIMIT ?");
  if (!s) {
    return events;
  }
  s.integer(1, limit);
  while (s.step() == SQLITE_ROW) {
    Event event;
    event.id = static_cast<uint64_t>(sqlite3_column_int64(s.stmt, 0));
    event.timestamp_ms = static_cast<uint64_t>(sqlite3_column_int64(s.stmt, 1));
    event.type = s.column_text(2);
    event.severity = parse_severity(s.column_text(3)).value_or(Severity::Info);
    event.source = s.column_text(4);
    event.message = s.column_text(5);
    event.data = nlohmann::json::parse(s.column_text(6), nullptr, false);
    events.push_back(std::move(event));
  }
  return events;
}

std::vector<nlohmann::json> EventStore::recent_alerts(size_t limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<nlohmann::json> alerts;
  if (!db_) {
    return alerts;
  }
  Statement s(db_, "SELECT body FROM alerts ORDER BY ts DESC LIMIT ?");
  if (!s) {
    return alerts;
  }
  s.integer(1, limit);
  while (s.step() == SQLITE_ROW) {
    alerts.push_back(nlohmann::json::parse(s.column_text(0), nullptr, false));
  }
  return alerts;
}

std::vector<nlohmann::json>
EventStore::metric_summary(const std::string &metric, size_t limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<nlohmann::json> rows;
  if (!db_) {
    return rows;
  }
  Statement s(db_, "SELECT minute, min, max, avg, count FROM metrics_summary "
                   "WHERE metric = ? ORDER BY minute DESC LIMIT ?");
  if (!s) {
    return rows;
  }
  s.text(1, metric);
  s.integer(2, limit);
  while (s.step() == SQLITE_ROW) {
    const auto minute = static_cast<uint64_t>(sqlite3_column_int64(s.stmt, 0));
    rows.push_back({{"minute", format_iso8601(minute)},
                    {"minute_ms", minute},
                    {"min", sqlite3_column_double(s.stmt, 1)},
                    {"max", sqlite3_column_double(s.stmt, 2)},
                    {"avg", sqlite3_column_double(s.stmt, 3)},
                    {"count", sqlite3_column_int64(s.stmt, 4)}});
  }
  return rows;
}

uint64_t EventStore::max_alert_sequence(const std::string &prefix) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) {
    return 0;
  }
  Statement s(db_, "SELECT MAX(CAST(substr(alert_id, ?) AS INTEGER)) FROM "
                   "alerts WHERE substr(alert_id, 1, ?) = ?");
  if (!s) {
    return 0;
  }
  s.integer(1, prefix.size() + 1);
  s.integer(2, prefix.size());
  s.text(3, prefix);
  if (s.step() == SQLITE_ROW) {
    return static_cast<uint64_t>(sqlite3_column_int64(s.stmt, 0));
  }
  return 0;
}

void EventStore::prune(uint64_t now_ms, const StorageConfig &retention) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) {
    return;
  }
  const uint64_t hour = 3600ULL * 1000ULL;
  struct {
    const char *sql;
    uint64_t age_ms;
  } rules[] = {
      {"DELETE FROM metrics_summary WHERE minute < ?",
       retention.metrics_retention_hours * hour},
      {"DELETE FROM events WHERE ts < ?",
       retention.events_retention_days * 24 * hour},
      {"DELETE FROM alerts WHERE ts < ? AND status = 'resolved'",
       retention.alerts_retention_days * 24 * hour},
  };
  for (const auto &rule : rules) {
    if (now_ms <= rule.age_ms) {
      continue;
    }
    Statement s(db_, rule.sql);
    if (s) {
      s.integer(1, now_ms - rule.age_ms);
      s.step();
    }
  }
}

void MetricSummaryAggregator::add(const TelemetryBatch &batch,
                                  EventStore &store) {
  const uint64_t minute = batch.timestamp_ms / 60000 * 60000;
  if (minute_ms_ != 0 && minute != minute_ms_) {
    flush(store);
  }
  minute_ms_ = minute;
  for (const auto &sample : batch.metrics) {
    Accumulator &acc = accumulators_[sample.metric];
    if (acc.count == 0) {
      acc.min = acc.max = sample.value;
    } else {
      acc.min = std::min(acc.min, sample.value);
      acc.max = std::max(acc.max, sample.value);
    }
    acc.sum += sample.value;
    acc.count++;
  }
}

void MetricSummaryAggregator::flush(EventStore &store) {
  for (const auto &entry : accumulators_) {
    const Accumulator &acc = entry.second;
    if (acc.count > 0) {
      store.record_metric_summary(minute_ms_, entry.first, acc.min, acc.max,
                                  acc.sum / static_cast<double>(acc.count),
                                  acc.count);
    }
  }
  accumulators_.clear();
}

} // namespace edgeml
