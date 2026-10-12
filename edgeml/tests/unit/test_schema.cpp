/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "core/alert/alert.h"
#include "core/schema/telemetry.h"

using namespace edgeml;

TEST(SchemaTest, MetricSampleMatchesSpecShape) {
  MetricSample sample;
  sample.timestamp_ms = 1791324000000ULL; // 2026-10-06T22:00:00Z
  sample.device_id = "demo-edge-001";
  sample.source = "gpu";
  sample.metric = "gpu.utilization";
  sample.value = 87.5;
  sample.unit = "percent";
  sample.labels = {{"workload", "vision-inference"}};

  const nlohmann::json j = sample;
  for (const char *field : {"timestamp", "device_id", "source", "metric",
                            "value", "unit", "labels"}) {
    EXPECT_TRUE(j.contains(field)) << field;
  }
  EXPECT_EQ(j["timestamp"], "2026-10-06T22:00:00Z");
  EXPECT_EQ(j["labels"]["workload"], "vision-inference");

  const MetricSample parsed = j.get<MetricSample>();
  EXPECT_EQ(parsed.metric, sample.metric);
  EXPECT_DOUBLE_EQ(parsed.value, sample.value);
  EXPECT_EQ(parsed.timestamp_ms, sample.timestamp_ms);
  EXPECT_EQ(parsed.labels, sample.labels);
}

TEST(SchemaTest, ParsesIsoTimestampWhenMillisecondsMissing) {
  const auto j = nlohmann::json::parse(
      R"({"timestamp":"2026-10-06T22:00:00Z","metric":"gpu.utilization",
          "value":1})");
  EXPECT_EQ(j.get<MetricSample>().timestamp_ms, 1791324000000ULL);
}

TEST(SchemaTest, BatchAddFindValue) {
  TelemetryBatch batch;
  batch.timestamp_ms = 42;
  batch.device_id = "dev";
  batch.add("gpu", metrics::kGpuUtilization, 61.0, "percent");
  ASSERT_NE(batch.find(metrics::kGpuUtilization), nullptr);
  EXPECT_EQ(batch.find(metrics::kGpuUtilization)->timestamp_ms, 42u);
  EXPECT_EQ(batch.value(metrics::kGpuUtilization).value(), 61.0);
  EXPECT_FALSE(batch.value(metrics::kInferenceRate).has_value());
}

TEST(SchemaTest, CatalogCoversSpecMetrics) {
  for (const char *name : {"system.cpu.utilization",
                           "system.memory.used",
                           "system.memory.percent",
                           "system.process.count",
                           "system.load.average",
                           "system.temperature",
                           "gpu.utilization",
                           "gpu.memory.used",
                           "gpu.memory.percent",
                           "gpu.power",
                           "gpu.temperature",
                           "gpu.process.count",
                           "ai.inference.rate",
                           "ai.inference.latency",
                           "ai.inference.errors",
                           "ai.model.loaded",
                           "ai.model.hash",
                           "ai.pipeline.health",
                           "network.connections.total",
                           "network.connections.outbound",
                           "network.bytes.out",
                           "network.bytes.in",
                           "network.new.destinations",
                           "network.failed.connections",
                           "security.unknown_processes",
                           "security.privileged_processes",
                           "security.model_integrity",
                           "security.config_integrity",
                           "security.agent_heartbeat",
                           "security.telemetry_health"}) {
    EXPECT_NE(find_metric_descriptor(name), nullptr) << name;
  }
}

TEST(SchemaTest, AlertJsonMatchesSpecShape) {
  Alert alert;
  alert.alert_id = "ALT-000142";
  alert.timestamp_ms = 1791324064000ULL;
  alert.device_id = "demo-edge-001";
  alert.severity = Severity::High;
  alert.category = "runtime_anomaly";
  alert.rule = "unauthorized_gpu_workload";
  alert.title = "Possible unauthorized GPU workload";
  alert.description = "desc";
  alert.evidence = {{"gpu_utilization", 97}, {"inference_rate", 11}};

  const nlohmann::json j = alert;
  EXPECT_EQ(j["alert_id"], "ALT-000142");
  EXPECT_EQ(j["timestamp"], "2026-10-06T22:01:04Z");
  EXPECT_EQ(j["severity"], "high");
  EXPECT_EQ(j["category"], "runtime_anomaly");
  EXPECT_EQ(j["rule"], "unauthorized_gpu_workload");
  EXPECT_EQ(j["evidence"]["gpu_utilization"], 97);
  EXPECT_EQ(j["status"], "active");
}

TEST(SchemaTest, SeverityParsingAndSecurityState) {
  EXPECT_EQ(parse_severity("CRITICAL"), Severity::Critical);
  EXPECT_EQ(parse_severity("medium"), Severity::Medium);
  EXPECT_FALSE(parse_severity("severe").has_value());
  EXPECT_EQ(security_state_for(std::nullopt), SecurityState::Healthy);
  EXPECT_EQ(security_state_for(Severity::Medium), SecurityState::Warning);
  EXPECT_EQ(security_state_for(Severity::High), SecurityState::Alert);
  EXPECT_EQ(security_state_for(Severity::Critical), SecurityState::Critical);
  EXPECT_STREQ(to_string(SecurityState::Healthy), "HEALTHY");
}
