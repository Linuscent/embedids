/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/correlation/correlator.h"

#include <algorithm>

namespace edgeml {

namespace {

bool matches(const Alert &alert, const std::string &requirement) {
  if (alert.rule == requirement) {
    return true;
  }
  return std::find(alert.signals.begin(), alert.signals.end(), requirement) !=
         alert.signals.end();
}

bool in_window(const Alert &alert, uint64_t now_ms, uint64_t window_ms) {
  if (alert.active) {
    return true;
  }
  return alert.resolved_ms != 0 && alert.resolved_ms + window_ms >= now_ms;
}

} // namespace

std::vector<Finding> Correlator::evaluate(const std::vector<Alert> &alerts,
                                          uint64_t now_ms) const {
  std::vector<Finding> findings;
  for (const auto &rule : rules_) {
    const uint64_t window_ms = rule.window_seconds * 1000ULL;
    std::vector<const Alert *> contributors;
    bool satisfied = true;

    // Every in-window alert matching a requirement contributes evidence.
    for (const auto &requirement : rule.requires_signals) {
      bool matched = false;
      for (const auto &alert : alerts) {
        if (alert.correlated() || !in_window(alert, now_ms, window_ms) ||
            !matches(alert, requirement)) {
          continue;
        }
        matched = true;
        if (std::find(contributors.begin(), contributors.end(), &alert) ==
            contributors.end()) {
          contributors.push_back(&alert);
        }
      }
      if (!matched) {
        satisfied = false;
        break;
      }
    }
    if (!satisfied) {
      continue;
    }

    Finding f;
    f.rule = rule.name;
    f.severity = rule.severity;
    f.category = category::kCorrelated;
    f.title = rule.title.empty() ? rule.name : rule.title;
    f.description = rule.description;

    nlohmann::json evidence = nlohmann::json::object();
    nlohmann::json contributing = nlohmann::json::array();
    for (const Alert *alert : contributors) {
      for (auto it = alert->evidence.begin(); it != alert->evidence.end();
           ++it) {
        if (!evidence.contains(it.key())) {
          evidence[it.key()] = it.value();
        }
      }
      contributing.push_back({{"alert_id", alert->alert_id},
                              {"rule", alert->rule},
                              {"title", alert->title},
                              {"severity", to_string(alert->severity)}});
      f.related_alerts.push_back(alert->alert_id);
      for (const auto &signal : alert->signals) {
        if (std::find(f.signals.begin(), f.signals.end(), signal) ==
            f.signals.end()) {
          f.signals.push_back(signal);
        }
      }
    }
    evidence["correlated_alerts"] = contributing;
    evidence["window_seconds"] = rule.window_seconds;
    f.evidence = std::move(evidence);
    findings.push_back(std::move(f));
  }
  return findings;
}

} // namespace edgeml
