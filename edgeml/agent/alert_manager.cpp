/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent/alert_manager.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace edgeml {

const char *to_string(AlertChange change) {
  switch (change) {
  case AlertChange::Raised:
    return "raised";
  case AlertChange::Escalated:
    return "escalated";
  case AlertChange::Resolved:
    return "resolved";
  }
  return "unknown";
}

AlertManager::AlertManager(const Config &config, size_t max_history)
    : config_(config), max_history_(max_history) {}

void AlertManager::add_sink(std::unique_ptr<AlertSink> sink) {
  sinks_.push_back(std::move(sink));
}

Alert *AlertManager::find_active(const std::string &key) {
  for (auto it = alerts_.rbegin(); it != alerts_.rend(); ++it) {
    if (it->active && it->key == key) {
      return &*it;
    }
  }
  return nullptr;
}

void AlertManager::publish(const Alert &alert, AlertChange change) {
  for (auto &sink : sinks_) {
    sink->publish(alert, change);
  }
}

std::vector<AlertUpdate>
AlertManager::reconcile(const std::vector<Finding> &findings,
                        bool scope_correlated, uint64_t now_ms) {
  std::vector<AlertUpdate> updates;
  std::set<std::string> present;

  for (const auto &finding : findings) {
    const std::string key = finding.key();
    present.insert(key);
    Alert *existing = find_active(key);
    if (existing == nullptr) {
      Alert alert;
      char id[32];
      std::snprintf(id, sizeof(id), "%06llu",
                    static_cast<unsigned long long>(next_sequence_++));
      alert.alert_id = config_.alerts.id_prefix + id;
      alert.timestamp_ms = now_ms;
      alert.device_id = config_.device.id;
      alert.severity = finding.severity;
      alert.category = finding.category;
      alert.rule = finding.rule;
      alert.subject = finding.subject;
      alert.title = finding.title;
      alert.description = finding.description;
      alert.evidence = finding.evidence;
      alert.signals = finding.signals;
      alert.related_alerts = finding.related_alerts;
      alert.key = key;
      alerts_.push_back(alert);
      total_raised_++;
      publish(alert, AlertChange::Raised);
      updates.push_back({alert, AlertChange::Raised});
      continue;
    }
    if (finding.severity > existing->severity) {
      existing->severity = finding.severity;
      existing->title = finding.title;
      existing->description = finding.description;
      existing->evidence = finding.evidence;
      existing->signals = finding.signals;
      existing->escalated = true;
      existing->updated_ms = now_ms;
      publish(*existing, AlertChange::Escalated);
      updates.push_back({*existing, AlertChange::Escalated});
    }
  }

  for (auto &alert : alerts_) {
    if (alert.active && alert.correlated() == scope_correlated &&
        present.count(alert.key) == 0) {
      alert.active = false;
      alert.resolved_ms = now_ms;
      alert.updated_ms = now_ms;
      alert.resolution = "condition cleared";
      publish(alert, AlertChange::Resolved);
      updates.push_back({alert, AlertChange::Resolved});
    }
  }

  trim();
  return updates;
}

std::vector<AlertUpdate> AlertManager::resolve_all(uint64_t now_ms,
                                                   const std::string &reason) {
  std::vector<AlertUpdate> updates;
  for (auto &alert : alerts_) {
    if (!alert.active) {
      continue;
    }
    alert.active = false;
    alert.resolved_ms = now_ms;
    alert.updated_ms = now_ms;
    alert.resolution = reason;
    publish(alert, AlertChange::Resolved);
    updates.push_back({alert, AlertChange::Resolved});
  }
  return updates;
}

std::vector<Alert> AlertManager::active() const {
  std::vector<Alert> result;
  for (const auto &alert : alerts_) {
    if (alert.active) {
      result.push_back(alert);
    }
  }
  return result;
}

std::optional<Alert> AlertManager::find(const std::string &alert_id) const {
  for (const auto &alert : alerts_) {
    if (alert.alert_id == alert_id) {
      return alert;
    }
  }
  return std::nullopt;
}

std::optional<Severity> AlertManager::worst_active() const {
  std::optional<Severity> worst;
  for (const auto &alert : alerts_) {
    if (alert.active && (!worst || alert.severity > *worst)) {
      worst = alert.severity;
    }
  }
  return worst;
}

void AlertManager::trim() {
  while (alerts_.size() > max_history_) {
    auto oldest_resolved =
        std::find_if(alerts_.begin(), alerts_.end(),
                     [](const Alert &alert) { return !alert.active; });
    if (oldest_resolved == alerts_.end()) {
      break;
    }
    alerts_.erase(oldest_resolved);
  }
}

} // namespace edgeml
