/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Turns per-cycle findings into an alert lifecycle (raised -> escalated ->
 * resolved) and fans changes out to sinks.
 */

#ifndef EDGEML_AGENT_ALERT_MANAGER_H
#define EDGEML_AGENT_ALERT_MANAGER_H

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/alert/alert.h"
#include "core/config/config.h"

namespace edgeml {

enum class AlertChange { Raised, Escalated, Resolved };
const char *to_string(AlertChange change);

class AlertSink {
public:
  virtual ~AlertSink() = default;
  virtual void publish(const Alert &alert, AlertChange change) = 0;
  virtual const char *name() const = 0;
};

struct AlertUpdate {
  Alert alert;
  AlertChange change;
};

class AlertManager {
public:
  explicit AlertManager(const Config &config, size_t max_history = 2000);

  void add_sink(std::unique_ptr<AlertSink> sink);

  /**
   * Reconcile current findings. scope_correlated selects which alerts the
   * findings describe: base rules (false) or correlations (true). Alerts in
   * that scope without a matching finding are resolved.
   */
  std::vector<AlertUpdate> reconcile(const std::vector<Finding> &findings,
                                     bool scope_correlated, uint64_t now_ms);

  /** Resolve every active alert (demo reset). */
  std::vector<AlertUpdate> resolve_all(uint64_t now_ms,
                                       const std::string &reason);

  const std::deque<Alert> &alerts() const { return alerts_; }
  std::vector<Alert> active() const;
  std::optional<Alert> find(const std::string &alert_id) const;
  std::optional<Severity> worst_active() const;
  uint64_t total_raised() const { return total_raised_; }

  /** Continue numbering after a restart. */
  void set_next_sequence(uint64_t next) { next_sequence_ = next; }
  uint64_t next_sequence() const { return next_sequence_; }

private:
  Alert *find_active(const std::string &key);
  void publish(const Alert &alert, AlertChange change);
  void trim();

  Config config_;
  size_t max_history_;
  std::deque<Alert> alerts_;
  std::vector<std::unique_ptr<AlertSink>> sinks_;
  uint64_t next_sequence_ = 1;
  uint64_t total_raised_ = 0;
};

} // namespace edgeml

#endif
