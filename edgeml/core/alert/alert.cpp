/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/alert/alert.h"

#include "core/common/clock.h"

#include <algorithm>
#include <cctype>

namespace edgeml {

const char *to_string(Severity severity) {
  switch (severity) {
  case Severity::Info:
    return "info";
  case Severity::Low:
    return "low";
  case Severity::Medium:
    return "medium";
  case Severity::High:
    return "high";
  case Severity::Critical:
    return "critical";
  }
  return "unknown";
}

std::optional<Severity> parse_severity(const std::string &text) {
  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (Severity severity : {Severity::Info, Severity::Low, Severity::Medium,
                            Severity::High, Severity::Critical}) {
    if (lower == to_string(severity)) {
      return severity;
    }
  }
  return std::nullopt;
}

const char *to_string(SecurityState state) {
  switch (state) {
  case SecurityState::Healthy:
    return "HEALTHY";
  case SecurityState::Warning:
    return "WARNING";
  case SecurityState::Alert:
    return "ALERT";
  case SecurityState::Critical:
    return "CRITICAL";
  }
  return "UNKNOWN";
}

SecurityState security_state_for(std::optional<Severity> worst_active) {
  if (!worst_active || *worst_active <= Severity::Low) {
    return SecurityState::Healthy;
  }
  switch (*worst_active) {
  case Severity::Medium:
    return SecurityState::Warning;
  case Severity::High:
    return SecurityState::Alert;
  default:
    return SecurityState::Critical;
  }
}

void to_json(nlohmann::json &j, const Alert &alert) {
  j = nlohmann::json{
      {"alert_id", alert.alert_id},
      {"timestamp", format_iso8601(alert.timestamp_ms)},
      {"timestamp_ms", alert.timestamp_ms},
      {"device_id", alert.device_id},
      {"severity", to_string(alert.severity)},
      {"category", alert.category},
      {"rule", alert.rule},
      {"subject", alert.subject},
      {"title", alert.title},
      {"description", alert.description},
      {"evidence", alert.evidence},
      {"signals", alert.signals},
      {"related_alerts", alert.related_alerts},
      {"status", alert.active ? "active" : "resolved"},
      {"escalated", alert.escalated},
  };
  if (alert.updated_ms != 0) {
    j["updated"] = format_iso8601(alert.updated_ms);
  }
  if (alert.resolved_ms != 0) {
    j["resolved"] = format_iso8601(alert.resolved_ms);
    j["resolved_ms"] = alert.resolved_ms;
    j["resolution"] = alert.resolution;
  }
}

} // namespace edgeml
