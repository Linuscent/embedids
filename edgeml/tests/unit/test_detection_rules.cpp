/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Rule-level tests: synthetic telemetry in, findings out. No collectors or
 * simulator involved, which is exactly how hardware data will arrive.
 */

#include <map>

#include <gtest/gtest.h>

#include "core/detection/detection_engine.h"

using namespace edgeml;

namespace {

class DetectionTest : public ::testing::Test {
protected:
  void SetUp() override { build(); }

  void build() {
    engine_ = std::make_unique<DetectionEngine>(config_);
    std::string error;
    ASSERT_TRUE(engine_->initialize(&error)) << error;
  }

  std::map<std::string, double> normal() const {
    return {{metrics::kGpuUtilization, 62}, {metrics::kInferenceRate, 30},
            {metrics::kCpuUtilization, 42}, {metrics::kMemoryPercent, 40},
            {metrics::kBytesOut, 25000},    {metrics::kModelIntegrity, 1},
            {metrics::kConfigIntegrity, 1}, {metrics::kAgentHeartbeat, 2}};
  }

  std::vector<ProcessInfo> base_processes() const {
    return {{1, "systemd", 0, 0, false},
            {100, "inference-service", 1000, 0, true},
            {101, "mqtt-client", 1000, 0, false}};
  }

  std::vector<ConnectionInfo> base_connections() const {
    return {{"10.0.0.20:1883", "mqtt-client", true, 8000},
            {"10.0.0.30:443", "inference-service", true, 16000}};
  }

  std::vector<Finding> tick(std::map<std::string, double> values,
                            std::vector<ProcessInfo> processes = {},
                            std::vector<ConnectionInfo> connections = {},
                            std::vector<FileAccessInfo> accesses = {},
                            Labels model_labels = {}) {
    now_ += 1000;
    TelemetryBatch batch;
    batch.timestamp_ms = now_;
    for (const auto &entry : values) {
      Labels labels;
      if (entry.first == metrics::kModelIntegrity) {
        labels = model_labels;
      }
      batch.add("test", entry.first, entry.second, "", labels);
    }
    batch.has_process_inventory = true;
    batch.processes = processes.empty() ? base_processes() : processes;
    batch.has_connection_inventory = true;
    batch.connections = connections.empty() ? base_connections() : connections;
    batch.file_accesses = accesses;
    return engine_->process(batch);
  }

  static const Finding *find(const std::vector<Finding> &findings,
                             const std::string &rule) {
    for (const auto &f : findings) {
      if (f.rule == rule) {
        return &f;
      }
    }
    return nullptr;
  }

  Config config_;
  std::unique_ptr<DetectionEngine> engine_;
  uint64_t now_ = 1790000000000ULL;
};

} // namespace

TEST_F(DetectionTest, NormalTelemetryProducesNoFindings) {
  for (int i = 0; i < 60; i++) {
    EXPECT_TRUE(tick(normal()).empty());
  }
}

TEST_F(DetectionTest, Rule1UnexpectedGpuWorkloadNeedsSustainedCondition) {
  auto values = normal();
  values[metrics::kGpuUtilization] = 97;
  values[metrics::kInferenceRate] = 11;
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(find(tick(values), rules::kUnexpectedGpuWorkload), nullptr);
  }
  auto findings = tick(values);
  const Finding *f = find(findings, rules::kUnexpectedGpuWorkload);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::High);
  EXPECT_EQ(f->evidence["gpu_utilization"], 97);
  EXPECT_EQ(f->evidence["inference_rate"], 11);
}

TEST_F(DetectionTest, HighGpuWithHealthyInferenceIsNotAnAlert) {
  auto values = normal();
  values[metrics::kGpuUtilization] = 97; // e.g. a heavier but legitimate model
  for (int i = 0; i < 20; i++) {
    EXPECT_EQ(find(tick(values), rules::kUnexpectedGpuWorkload), nullptr);
  }
}

TEST_F(DetectionTest, Rule2UnknownProcessAfterMinimumRuntime) {
  auto processes = base_processes();
  processes.push_back({4242, "unknown_worker", 1000, now_ + 1000, true});
  // First seen at the next tick; runtime reaches 5 s on the sixth tick.
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(find(tick(normal(), processes), rules::kRogueProcess), nullptr);
  }
  auto findings = tick(normal(), processes);
  const Finding *f = find(findings, rules::kRogueProcess);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::Medium);
  EXPECT_EQ(f->subject, "unknown_worker");
  EXPECT_EQ(f->title, "Unknown process detected");
}

TEST_F(DetectionTest, Rule2EscalatesForRoot) {
  auto processes = base_processes();
  processes.push_back({4401, "rogue-shell", 0, now_ - 60000, false});
  auto findings = tick(normal(), processes);
  const Finding *f = find(findings, rules::kRogueProcess);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::High);
  EXPECT_EQ(f->title, "Unauthorized privileged process");
  EXPECT_EQ(f->evidence["uid"], 0);
}

TEST_F(DetectionTest, AllowListSupportsWildcards) {
  config_.process.allowed.push_back("kworker*");
  build();
  auto processes = base_processes();
  processes.push_back({55, "kworker/0:1", 0, 0, false});
  EXPECT_EQ(find(tick(normal(), processes), rules::kRogueProcess), nullptr);
}

TEST_F(DetectionTest, Rule3UnknownDestinationEscalatesWithTraffic) {
  auto connections = base_connections();
  connections.push_back({"203.0.113.45:8443", "mqtt-client", true, 30000});
  auto findings = tick(normal(), {}, connections);
  const Finding *f = find(findings, rules::kUnknownDestination);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::Medium);
  EXPECT_EQ(f->subject, "203.0.113.45:8443");

  auto values = normal();
  values[metrics::kBytesOut] = 2.4e6;
  findings = tick(values, {}, connections);
  f = find(findings, rules::kUnknownDestination);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::High);
}

TEST_F(DetectionTest, Rule6ExfiltrationAndOutboundAnomaly) {
  auto connections = base_connections();
  connections.push_back({"203.0.113.45:8443", "mqtt-client", true, 2400000});
  auto values = normal();
  values[metrics::kBytesOut] = 2.4e6;

  auto findings = tick(values, {}, connections);
  EXPECT_NE(find(findings, rules::kDataExfiltration), nullptr);
  // Outbound anomaly needs anomaly_seconds of sustained traffic.
  EXPECT_EQ(find(findings, rules::kOutboundTrafficAnomaly), nullptr);
  for (uint32_t i = 0; i < config_.network.anomaly_seconds; i++) {
    findings = tick(values, {}, connections);
  }
  EXPECT_NE(find(findings, rules::kOutboundTrafficAnomaly), nullptr);
  EXPECT_EQ(find(findings, rules::kDataExfiltration)->severity, Severity::High);
}

TEST_F(DetectionTest, HighTrafficToKnownDestinationIsNotExfiltration) {
  auto values = normal();
  values[metrics::kBytesOut] = 2.4e6;
  auto findings = tick(values);
  EXPECT_EQ(find(findings, rules::kDataExfiltration), nullptr);
  EXPECT_EQ(find(findings, rules::kUnknownDestination), nullptr);
}

TEST_F(DetectionTest, Rule4ModelIntegrityViolationShowsHashes) {
  auto values = normal();
  values[metrics::kModelIntegrity] = 0;
  auto findings = tick(values, {}, {}, {},
                       {{"path", "/models/model.engine"},
                        {"expected", "83ff00"},
                        {"current", "a7bc00"}});
  const Finding *f = find(findings, rules::kModelIntegrity);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::Critical);
  EXPECT_EQ(f->category, category::kIntegrity);
  EXPECT_EQ(f->evidence["expected_sha256"], "83ff00");
  EXPECT_EQ(f->evidence["current_sha256"], "a7bc00");
  EXPECT_NE(f->description.find("EXPECTED: 83ff00"), std::string::npos);
}

TEST_F(DetectionTest, Rule5InferenceDegradation) {
  auto values = normal();
  values[metrics::kInferenceRate] = 14;
  std::vector<Finding> findings;
  for (int i = 0; i < 10; i++) {
    findings = tick(values);
    EXPECT_EQ(find(findings, rules::kInferenceDegradation), nullptr);
  }
  findings = tick(values);
  const Finding *f = find(findings, rules::kInferenceDegradation);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::Medium);
  EXPECT_EQ(f->evidence["gpu_unusually_high"], false);
  // GPU is not above its limit, so Rule 1 must stay quiet.
  EXPECT_EQ(find(findings, rules::kUnexpectedGpuWorkload), nullptr);
}

TEST_F(DetectionTest, Rule7HeartbeatLost) {
  auto values = normal();
  values[metrics::kAgentHeartbeat] = 15;
  EXPECT_EQ(find(tick(values), rules::kHeartbeatLost), nullptr);
  values[metrics::kAgentHeartbeat] = 16;
  const auto findings = tick(values);
  const Finding *f = find(findings, rules::kHeartbeatLost);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::Critical);
  EXPECT_NE(f->description.find("Security telemetry unavailable"),
            std::string::npos);
}

TEST_F(DetectionTest, ResourceExhaustion) {
  auto values = normal();
  values[metrics::kCpuUtilization] = 99.5;
  values[metrics::kMemoryPercent] = 95;
  std::vector<Finding> findings;
  for (uint32_t i = 0; i <= config_.system.exhaustion_seconds; i++) {
    findings = tick(values);
  }
  const Finding *f = find(findings, rules::kResourceExhaustion);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::High);
}

TEST_F(DetectionTest, UnexpectedModelReader) {
  std::vector<FileAccessInfo> accesses = {
      {config_.integrity.model.path, "mqtt-client", 911, 0},
      {config_.integrity.model.path, "inference-service", 100, 0},
      {"/etc/hosts", "mqtt-client", 911, 0}};
  auto findings = tick(normal(), {}, {}, accesses);
  const Finding *f = find(findings, rules::kModelFileAccess);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->evidence["recent_accesses"], 1);

  // Access stays visible for access_window_seconds, then clears.
  for (uint32_t i = 0; i < config_.integrity.access_window_seconds; i++) {
    findings = tick(normal());
  }
  EXPECT_NE(find(findings, rules::kModelFileAccess), nullptr);
  findings = tick(normal());
  EXPECT_EQ(find(findings, rules::kModelFileAccess), nullptr);
}

TEST_F(DetectionTest, ConfigIntegrity) {
  auto values = normal();
  values[metrics::kConfigIntegrity] = 0;
  const auto findings = tick(values);
  const Finding *f = find(findings, rules::kConfigIntegrity);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->severity, Severity::High);
}

TEST_F(DetectionTest, PolicyMetricsAreAddedToTheBatch) {
  TelemetryBatch batch;
  batch.timestamp_ms = now_ + 1000;
  batch.has_process_inventory = true;
  batch.processes = base_processes();
  batch.processes.push_back({9, "rogue", 0, 0, false});
  batch.has_connection_inventory = true;
  batch.connections = base_connections();
  engine_->process(batch);
  EXPECT_EQ(batch.value(metrics::kUnknownProcesses).value(), 1.0);
  EXPECT_EQ(batch.value(metrics::kPrivilegedProcesses).value(), 1.0);
  EXPECT_EQ(batch.value(metrics::kNewDestinations).value(), 0.0);
  EXPECT_EQ(batch.value(metrics::kModelFileAccess).value(), 0.0);
}

TEST_F(DetectionTest, ResetForgetsHistory) {
  auto values = normal();
  values[metrics::kInferenceRate] = 14;
  for (int i = 0; i < 11; i++) {
    tick(values);
  }
  ASSERT_NE(find(tick(values), rules::kInferenceDegradation), nullptr);
  engine_->reset();
  EXPECT_EQ(find(tick(values), rules::kInferenceDegradation), nullptr);
}

TEST(DetectionCatalogTest, EveryRuleDescribed) {
  EXPECT_EQ(rule_catalog().size(), 11u);
  for (const auto &rule : rule_catalog()) {
    EXPECT_NE(std::string(rule.condition), "");
  }
}
