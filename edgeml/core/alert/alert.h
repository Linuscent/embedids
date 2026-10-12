/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_CORE_ALERT_ALERT_H
#define EDGEML_CORE_ALERT_ALERT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace edgeml {

enum class Severity { Info = 0, Low = 1, Medium = 2, High = 3, Critical = 4 };

const char *to_string(Severity severity);
std::optional<Severity> parse_severity(const std::string &text);

/** Overall device security state derived from active alerts. */
enum class SecurityState { Healthy, Warning, Alert, Critical };
const char *to_string(SecurityState state);
SecurityState security_state_for(std::optional<Severity> worst_active);

namespace category {
inline constexpr const char *kRuntimeAnomaly = "runtime_anomaly";
inline constexpr const char *kProcess = "process";
inline constexpr const char *kNetwork = "network";
inline constexpr const char *kIntegrity = "integrity";
inline constexpr const char *kPerformance = "performance";
inline constexpr const char *kAvailability = "availability";
inline constexpr const char *kCorrelated = "correlated_incident";
} // namespace category

/**
 * A rule's current verdict for one subject. Findings are recomputed every
 * cycle; the alert manager turns transitions into alerts.
 */
struct Finding {
  std::string rule;
  std::string subject; /**< e.g. process name or destination; may be empty */
  Severity severity = Severity::Medium;
  std::string category;
  std::string title;
  std::string description;
  nlohmann::json evidence = nlohmann::json::object();
  std::vector<std::string> signals;        /**< metrics involved */
  std::vector<std::string> related_alerts; /**< used by correlations */

  std::string key() const {
    return subject.empty() ? rule : rule + ":" + subject;
  }
};

/** Alert schema (spec section 12) plus lifecycle fields. */
struct Alert {
  std::string alert_id;
  uint64_t timestamp_ms = 0;
  std::string device_id;
  Severity severity = Severity::Medium;
  std::string category;
  std::string rule;
  std::string subject;
  std::string title;
  std::string description;
  nlohmann::json evidence = nlohmann::json::object();
  std::vector<std::string> signals;
  std::vector<std::string> related_alerts;

  std::string key;
  bool active = true;
  bool escalated = false;
  uint64_t updated_ms = 0;
  uint64_t resolved_ms = 0;
  std::string resolution;

  bool correlated() const { return category == category::kCorrelated; }
};

void to_json(nlohmann::json &j, const Alert &alert);

} // namespace edgeml

#endif
