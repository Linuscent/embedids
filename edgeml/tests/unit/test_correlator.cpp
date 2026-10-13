/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "agent/alert_manager.h"
#include "core/correlation/correlator.h"

using namespace edgeml;

namespace {

Alert make_alert(const std::string &id, const std::string &rule,
                 std::vector<std::string> signals, uint64_t ts,
                 bool active = true, uint64_t resolved = 0) {
  Alert alert;
  alert.alert_id = id;
  alert.rule = rule;
  alert.signals = std::move(signals);
  alert.timestamp_ms = ts;
  alert.active = active;
  alert.resolved_ms = resolved;
  alert.severity = Severity::Medium;
  alert.category = "runtime_anomaly";
  alert.evidence = {{rule + "_evidence", 1}};
  return alert;
}

CorrelationRule gpu_rule() {
  CorrelationRule rule;
  rule.name = "unauthorized_gpu_workload";
  rule.title = "Possible unauthorized GPU workload";
  rule.severity = Severity::High;
  rule.window_seconds = 20;
  // Spec section 11 example: requirements expressed as metrics.
  rule.requires_signals = {"security.unknown_processes", "gpu.utilization",
                           "ai.inference.rate"};
  return rule;
}

} // namespace

TEST(CorrelatorTest, MatchesBySignalMetrics) {
  Correlator correlator({gpu_rule()});
  std::vector<Alert> alerts = {
      make_alert("ALT-1", "rogue_process", {"security.unknown_processes"},
                 1000),
      make_alert("ALT-2", "unexpected_gpu_workload",
                 {"gpu.utilization", "ai.inference.rate"}, 8000)};
  auto findings = correlator.evaluate(alerts, 9000);
  ASSERT_EQ(findings.size(), 1u);
  const Finding &f = findings[0];
  EXPECT_EQ(f.rule, "unauthorized_gpu_workload");
  EXPECT_EQ(f.category, category::kCorrelated);
  EXPECT_EQ(f.severity, Severity::High);
  EXPECT_EQ(f.related_alerts, (std::vector<std::string>{"ALT-1", "ALT-2"}));
  EXPECT_EQ(f.evidence["correlated_alerts"].size(), 2u);
  EXPECT_TRUE(f.evidence.contains("rogue_process_evidence"));
}

TEST(CorrelatorTest, MatchesByRuleName) {
  CorrelationRule rule;
  rule.name = "combo";
  rule.requires_signals = {"rogue_process", "inference_degradation"};
  Correlator correlator({rule});
  std::vector<Alert> alerts = {make_alert("A", "rogue_process", {}, 0)};
  EXPECT_TRUE(correlator.evaluate(alerts, 1000).empty());
  alerts.push_back(make_alert("B", "inference_degradation", {}, 500));
  EXPECT_EQ(correlator.evaluate(alerts, 1000).size(), 1u);
}

TEST(CorrelatorTest, ResolvedAlertsCountOnlyInsideWindow) {
  Correlator correlator({gpu_rule()});
  std::vector<Alert> alerts = {
      make_alert("ALT-1", "rogue_process", {"security.unknown_processes"}, 0,
                 false, 10000),
      make_alert("ALT-2", "unexpected_gpu_workload",
                 {"gpu.utilization", "ai.inference.rate"}, 12000)};
  EXPECT_EQ(correlator.evaluate(alerts, 25000).size(), 1u);
  EXPECT_TRUE(correlator.evaluate(alerts, 31000).empty());
}

TEST(CorrelatorTest, IgnoresCorrelatedAlertsAsInputs) {
  CorrelationRule rule;
  rule.name = "meta";
  rule.requires_signals = {"unauthorized_gpu_workload"};
  Correlator correlator({rule});
  Alert incident = make_alert("C", "unauthorized_gpu_workload", {}, 0);
  incident.category = category::kCorrelated;
  EXPECT_TRUE(correlator.evaluate({incident}, 1000).empty());
}

TEST(AlertManagerTest, LifecycleRaiseEscalateResolve) {
  Config config;
  AlertManager manager(config);

  struct Recorder : AlertSink {
    std::vector<std::pair<std::string, AlertChange>> *events;
    explicit Recorder(std::vector<std::pair<std::string, AlertChange>> *e)
        : events(e) {}
    void publish(const Alert &alert, AlertChange change) override {
      events->push_back({alert.alert_id, change});
    }
    const char *name() const override { return "recorder"; }
  };
  std::vector<std::pair<std::string, AlertChange>> events;
  manager.add_sink(std::make_unique<Recorder>(&events));

  Finding finding;
  finding.rule = "unknown_destination";
  finding.subject = "203.0.113.45:8443";
  finding.severity = Severity::Medium;
  finding.title = "Connection to unknown destination";

  auto updates = manager.reconcile({finding}, false, 1000);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].change, AlertChange::Raised);
  EXPECT_EQ(updates[0].alert.alert_id, "ALT-000001");
  EXPECT_EQ(manager.worst_active(), Severity::Medium);

  // Same finding again: no duplicate alert.
  EXPECT_TRUE(manager.reconcile({finding}, false, 2000).empty());

  finding.severity = Severity::High;
  finding.title = "High-volume traffic to unknown destination";
  updates = manager.reconcile({finding}, false, 3000);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].change, AlertChange::Escalated);
  EXPECT_TRUE(updates[0].alert.escalated);
  EXPECT_EQ(manager.active().size(), 1u);
  EXPECT_EQ(manager.active()[0].severity, Severity::High);

  // Correlation scope must not resolve base alerts.
  EXPECT_TRUE(manager.reconcile({}, true, 4000).empty());

  updates = manager.reconcile({}, false, 5000);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].change, AlertChange::Resolved);
  EXPECT_FALSE(manager.worst_active().has_value());
  EXPECT_EQ(manager.find("ALT-000001")->resolved_ms, 5000u);

  // Re-occurrence creates a new alert id.
  updates = manager.reconcile({finding}, false, 6000);
  EXPECT_EQ(updates[0].alert.alert_id, "ALT-000002");
  EXPECT_EQ(manager.total_raised(), 2u);
  EXPECT_EQ(events.size(), 4u);
}

TEST(AlertManagerTest, SubjectsAreTrackedIndependently) {
  Config config;
  AlertManager manager(config);
  Finding a;
  a.rule = "rogue_process";
  a.subject = "a";
  Finding b = a;
  b.subject = "b";
  manager.reconcile({a, b}, false, 1000);
  EXPECT_EQ(manager.active().size(), 2u);
  manager.reconcile({b}, false, 2000);
  ASSERT_EQ(manager.active().size(), 1u);
  EXPECT_EQ(manager.active()[0].subject, "b");
}

TEST(AlertManagerTest, ResolveAllAndSequence) {
  Config config;
  config.alerts.id_prefix = "X-";
  AlertManager manager(config);
  manager.set_next_sequence(42);
  Finding f;
  f.rule = "r";
  auto updates = manager.reconcile({f}, false, 1);
  EXPECT_EQ(updates[0].alert.alert_id, "X-000042");
  updates = manager.resolve_all(2, "demo reset");
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].alert.resolution, "demo reset");
  EXPECT_TRUE(manager.active().empty());
}

TEST(AlertManagerTest, HistoryIsBounded) {
  Config config;
  AlertManager manager(config, 10);
  for (int i = 0; i < 50; i++) {
    Finding f;
    f.rule = "r" + std::to_string(i);
    manager.reconcile({f}, false, i);
  }
  EXPECT_LE(manager.alerts().size(), 10u);
  EXPECT_EQ(manager.total_raised(), 50u);
}
