/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>

#include <gtest/gtest.h>

#include "agent/telemetry_manager.h"

using namespace edgeml;

namespace {

class FakeCollector : public MetricCollector {
public:
  FakeCollector(const char *name, bool *healthy, bool required = true,
                bool init_ok = true)
      : name_(name), healthy_(healthy), required_(required), init_ok_(init_ok) {
  }
  bool initialize() override { return init_ok_; }
  bool collect(MetricBatch &batch) override {
    if (!*healthy_) {
      return false;
    }
    batch.add("fake", metrics::kGpuUtilization, 50.0, "percent");
    return true;
  }
  const char *name() const override { return name_; }
  bool heartbeat_required() const override { return required_; }

private:
  const char *name_;
  bool *healthy_;
  bool required_;
  bool init_ok_;
};

class ThrowingCollector : public MetricCollector {
public:
  bool initialize() override { return true; }
  bool collect(MetricBatch &) override { throw std::runtime_error("boom"); }
  const char *name() const override { return "throwing"; }
  bool heartbeat_required() const override { return false; }
};

} // namespace

TEST(TelemetryManagerTest, NormalizesSamples) {
  Config config;
  config.device.id = "dev-9";
  ManualClock clock(1000000);
  TelemetryManager manager(config, clock);
  bool healthy = true;
  manager.add_collector(std::make_unique<FakeCollector>("gpu", &healthy));
  manager.initialize();

  clock.advance_ms(1000);
  TelemetryBatch batch = manager.collect();
  EXPECT_EQ(batch.cycle, 1u);
  EXPECT_EQ(batch.device_id, "dev-9");
  const MetricSample *sample = batch.find(metrics::kGpuUtilization);
  ASSERT_NE(sample, nullptr);
  EXPECT_EQ(sample->device_id, "dev-9");
  EXPECT_EQ(sample->timestamp_ms, 1001000u);
  EXPECT_EQ(batch.value(metrics::kTelemetryHealth).value(), 100.0);
}

TEST(TelemetryManagerTest, HeartbeatTimeoutAndRecovery) {
  Config config; // interval 5 s, timeout 15 s
  ManualClock clock(1000000);
  TelemetryManager manager(config, clock);
  bool sim_ok = true;
  bool local_ok = true;
  manager.add_collector(std::make_unique<FakeCollector>("sim", &sim_ok));
  manager.add_collector(
      std::make_unique<FakeCollector>("local", &local_ok, false));
  manager.initialize();

  double max_age = 0;
  for (int i = 0; i < 30; i++) {
    clock.advance_ms(1000);
    auto batch = manager.collect();
    max_age = std::max(max_age, batch.value(metrics::kAgentHeartbeat).value());
  }
  // Heartbeats are emitted every 5 s, so the age stays within one interval.
  EXPECT_LE(max_age, 5.0);

  sim_ok = false;
  std::vector<CollectorTransition> transitions;
  TelemetryBatch batch;
  for (int i = 0; i < 20; i++) {
    clock.advance_ms(1000);
    transitions.clear();
    batch = manager.collect(&transitions);
    if (i == 0) {
      ASSERT_EQ(transitions.size(), 1u);
      EXPECT_EQ(transitions[0].name, "sim");
      EXPECT_FALSE(transitions[0].healthy);
    }
  }
  EXPECT_GT(batch.value(metrics::kAgentHeartbeat).value(), 15.0);
  EXPECT_EQ(batch.find(metrics::kAgentHeartbeat)->labels.at("stale"), "sim");
  EXPECT_EQ(batch.value(metrics::kTelemetryHealth).value(), 50.0);

  sim_ok = true;
  clock.advance_ms(1000);
  transitions.clear();
  batch = manager.collect(&transitions);
  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_TRUE(transitions[0].healthy);
  EXPECT_LT(batch.value(metrics::kAgentHeartbeat).value(), 1.0);
}

TEST(TelemetryManagerTest, OptionalCollectorsDoNotAffectHeartbeat) {
  Config config;
  ManualClock clock(1000000);
  TelemetryManager manager(config, clock);
  bool ok = false;
  manager.add_collector(std::make_unique<FakeCollector>("local", &ok, false));
  manager.add_collector(std::make_unique<ThrowingCollector>());
  manager.initialize();
  TelemetryBatch batch;
  for (int i = 0; i < 30; i++) {
    clock.advance_ms(1000);
    batch = manager.collect();
  }
  EXPECT_EQ(batch.value(metrics::kAgentHeartbeat).value(), 0.0);
  EXPECT_EQ(manager.status()[1].total_failures, 30u);
}

TEST(TelemetryManagerTest, FailedInitializationDisablesCollector) {
  Config config;
  ManualClock clock(1000000);
  TelemetryManager manager(config, clock);
  bool ok = true;
  manager.add_collector(
      std::make_unique<FakeCollector>("absent", &ok, true, false));
  std::vector<std::string> warnings;
  manager.initialize(&warnings);
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_NE(warnings[0].find("absent"), std::string::npos);
  clock.advance_ms(60000);
  auto batch = manager.collect();
  EXPECT_EQ(batch.find(metrics::kGpuUtilization), nullptr);
  EXPECT_EQ(batch.value(metrics::kAgentHeartbeat).value(), 0.0);
}
