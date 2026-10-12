/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * End-to-end scenario tests (spec sections 16 and 24): embedded simulator ->
 * simulated collectors -> EmbedIDS detection -> correlation -> alerts, run on
 * a virtual clock. Each test asserts the alerts a scenario must produce.
 */

#include <set>

#include <gtest/gtest.h>

#include "core/detection/detection_engine.h"
#include "support/test_support.h"

using namespace edgeml;
using namespace edgeml::test;

namespace {

class ScenarioTest : public ::testing::Test {
protected:
  void SetUp() override {
    harness_ = std::make_unique<AgentHarness>(make_test_config(dir_));
    std::string error;
    ASSERT_TRUE(harness_->initialize(&error)) << error;
    // Establish the healthy baseline first.
    harness_->step(30);
    ASSERT_TRUE(harness_->active().empty()) << describe_active();
  }

  std::string describe_active() const {
    std::string out;
    for (const auto &alert : harness_->active()) {
      out += alert.rule + "(" + to_string(alert.severity) + ") ";
    }
    return out;
  }

  std::set<std::string> raised_rules() const {
    std::set<std::string> rules;
    for (const auto &alert : harness_->alerts()) {
      rules.insert(alert.rule);
    }
    return rules;
  }

  TempDir dir_;
  std::unique_ptr<AgentHarness> harness_;
};

} // namespace

TEST_F(ScenarioTest, NormalOperationRaisesNoAlertsForTenMinutes) {
  harness_->step(600);
  EXPECT_TRUE(harness_->alerts().empty()) << describe_active();
  EXPECT_EQ(harness_->agent().security_state(), SecurityState::Healthy);
  const auto status = harness_->agent().status();
  EXPECT_EQ(status["model_integrity"]["status"], "verified");
  EXPECT_EQ(status["security_state"], "HEALTHY");
}

TEST_F(ScenarioTest, ScenarioA_CryptominerCorrelatesUnauthorizedGpuWorkload) {
  ASSERT_TRUE(harness_->activate("cryptominer"));
  auto cycles = harness_->step_until("unauthorized_gpu_workload", 30);
  ASSERT_TRUE(cycles.has_value()) << describe_active();
  EXPECT_LE(*cycles, 20);

  auto rogue = harness_->find_active(rules::kRogueProcess);
  ASSERT_TRUE(rogue);
  EXPECT_EQ(rogue->severity, Severity::Medium);
  EXPECT_EQ(rogue->subject, "unknown_worker");

  auto gpu = harness_->find_active(rules::kUnexpectedGpuWorkload);
  ASSERT_TRUE(gpu);
  EXPECT_EQ(gpu->severity, Severity::High);
  EXPECT_GT(gpu->evidence["gpu_utilization"].get<double>(), 85.0);
  EXPECT_LT(gpu->evidence["inference_rate"].get<double>(), 20.0);
  EXPECT_EQ(gpu->evidence["unexpected_gpu_processes"][0], "unknown_worker");

  auto degraded = harness_->find_active(rules::kInferenceDegradation);
  ASSERT_TRUE(degraded);
  EXPECT_EQ(degraded->severity, Severity::Medium);

  auto incident = harness_->find_active("unauthorized_gpu_workload");
  ASSERT_TRUE(incident);
  EXPECT_EQ(incident->severity, Severity::High);
  EXPECT_EQ(incident->category, category::kCorrelated);
  EXPECT_EQ(incident->related_alerts.size(), 3u);
  EXPECT_EQ(harness_->agent().security_state(), SecurityState::Alert);
}

TEST_F(ScenarioTest, ScenarioB_ModelTamperDetectedWithinTwoSeconds) {
  ASSERT_TRUE(harness_->activate("model-tamper"));
  auto cycles = harness_->step_until(rules::kModelIntegrity, 5);
  ASSERT_TRUE(cycles.has_value());
  EXPECT_LE(*cycles * harness_->config().sampling.interval_ms, 2000u);

  auto alert = harness_->find_active(rules::kModelIntegrity);
  ASSERT_TRUE(alert);
  EXPECT_EQ(alert->category, "integrity");
  EXPECT_EQ(alert->severity, Severity::Critical);
  EXPECT_EQ(alert->evidence["expected_sha256"], sim::approved_model_sha256());
  EXPECT_NE(alert->evidence["current_sha256"], sim::approved_model_sha256());
  EXPECT_EQ(harness_->agent().security_state(), SecurityState::Critical);

  // Restoring the approved model resolves the alert.
  ASSERT_TRUE(harness_->activate("normal"));
  harness_->step(2);
  EXPECT_FALSE(harness_->find_active(rules::kModelIntegrity));
}

TEST_F(ScenarioTest, ScenarioC_DataExfiltration) {
  ASSERT_TRUE(harness_->activate("network-exfil"));
  auto cycles = harness_->step_until("model_data_exfiltration", 20);
  ASSERT_TRUE(cycles.has_value()) << describe_active();

  auto destination = harness_->find_active(rules::kUnknownDestination);
  ASSERT_TRUE(destination);
  EXPECT_EQ(destination->subject, "203.0.113.45:8443");
  EXPECT_EQ(destination->severity, Severity::High); // escalated by volume
  EXPECT_EQ(destination->evidence["process"], "mqtt-client");

  auto anomaly = harness_->find_active(rules::kOutboundTrafficAnomaly);
  ASSERT_TRUE(anomaly);
  auto exfil = harness_->find_active(rules::kDataExfiltration);
  ASSERT_TRUE(exfil);
  EXPECT_EQ(exfil->severity, Severity::High);
  EXPECT_TRUE(harness_->find_active(rules::kModelFileAccess));

  auto incident = harness_->find_active("model_data_exfiltration");
  ASSERT_TRUE(incident);
  EXPECT_EQ(incident->severity, Severity::Critical);
  // The hijacked process is allow-listed, so no rogue process alert.
  EXPECT_FALSE(harness_->find(rules::kRogueProcess));
}

TEST_F(ScenarioTest, ScenarioD_PrivilegeEscalation) {
  ASSERT_TRUE(harness_->activate("privilege-escalation"));
  auto cycles = harness_->step_until(rules::kRogueProcess, 10);
  ASSERT_TRUE(cycles.has_value());
  auto alert = harness_->find_active(rules::kRogueProcess);
  ASSERT_TRUE(alert);
  EXPECT_EQ(alert->severity, Severity::High);
  EXPECT_EQ(alert->title, "Unauthorized privileged process");
  EXPECT_EQ(alert->subject, "rogue-shell");
  EXPECT_EQ(alert->evidence["uid"], 0);
}

TEST_F(ScenarioTest, ScenarioE_DenialOfService) {
  ASSERT_TRUE(harness_->activate("dos"));
  harness_->step(30);
  auto exhaustion = harness_->find_active(rules::kResourceExhaustion);
  ASSERT_TRUE(exhaustion) << describe_active();
  EXPECT_EQ(exhaustion->severity, Severity::High);
  EXPECT_TRUE(harness_->find_active(rules::kInferenceDegradation));
  EXPECT_FALSE(harness_->find(rules::kUnexpectedGpuWorkload));
}

TEST_F(ScenarioTest, ScenarioF_TelemetryHeartbeatLost) {
  ASSERT_TRUE(harness_->activate("telemetry-loss"));
  auto cycles = harness_->step_until(rules::kHeartbeatLost, 30);
  ASSERT_TRUE(cycles.has_value());
  // Timeout is 15 s; the last heartbeat may be up to one interval old.
  EXPECT_GE(*cycles, 11);
  EXPECT_LE(*cycles, 21);
  auto alert = harness_->find_active(rules::kHeartbeatLost);
  ASSERT_TRUE(alert);
  EXPECT_EQ(alert->severity, Severity::Critical);
  EXPECT_EQ(alert->description,
            "Security telemetry unavailable. Possible collector failure or "
            "tampering.");
  EXPECT_NE(
      alert->evidence["stale_collectors"].get<std::string>().find("sim-gpu"),
      std::string::npos);

  // Telemetry returns: heartbeat recovers and the alert resolves.
  ASSERT_TRUE(harness_->activate("normal"));
  harness_->step(2);
  EXPECT_FALSE(harness_->find_active(rules::kHeartbeatLost));
}

TEST_F(ScenarioTest, InferenceDegradationAlone) {
  ASSERT_TRUE(harness_->activate("inference-degradation"));
  harness_->step(25);
  auto alert = harness_->find_active(rules::kInferenceDegradation);
  ASSERT_TRUE(alert);
  EXPECT_EQ(alert->severity, Severity::Medium);
  EXPECT_EQ(raised_rules(),
            std::set<std::string>{rules::kInferenceDegradation});
}

TEST_F(ScenarioTest, RogueProcessAlone) {
  ASSERT_TRUE(harness_->activate("rogue-process"));
  harness_->step(4);
  EXPECT_FALSE(harness_->find(rules::kRogueProcess)) << "runtime < 5 s";
  harness_->step(3);
  auto alert = harness_->find_active(rules::kRogueProcess);
  ASSERT_TRUE(alert);
  EXPECT_EQ(alert->severity, Severity::Medium);
  EXPECT_EQ(alert->subject, "rogue_daemon");
  EXPECT_EQ(raised_rules(), std::set<std::string>{rules::kRogueProcess});
}

TEST_F(ScenarioTest, CombinedAttacksCorrelatePrivilegedHijack) {
  ASSERT_TRUE(harness_->activate("privilege-escalation"));
  ASSERT_TRUE(harness_->activate("dos"));
  harness_->step(30);
  auto incident = harness_->find_active("privileged_resource_hijack");
  ASSERT_TRUE(incident) << describe_active();
  EXPECT_EQ(incident->severity, Severity::Critical);
}

TEST_F(ScenarioTest, ResetReturnsToHealthy) {
  ASSERT_TRUE(harness_->activate("cryptominer"));
  ASSERT_TRUE(harness_->activate("model-tamper"));
  harness_->step(20);
  ASSERT_EQ(harness_->agent().security_state(), SecurityState::Critical);

  ASSERT_TRUE(harness_->reset());
  EXPECT_EQ(harness_->agent().security_state(), SecurityState::Healthy);
  const size_t before = harness_->alerts().size();
  harness_->step(60);
  EXPECT_TRUE(harness_->active().empty()) << describe_active();
  EXPECT_EQ(harness_->alerts().size(), before) << "no alerts after reset";
  EXPECT_EQ(harness_->agent().status()["model_integrity"]["status"],
            "verified");
}

TEST_F(ScenarioTest, StoppingAttacksResolvesAlertsNaturally) {
  ASSERT_TRUE(harness_->activate("cryptominer"));
  harness_->step(20);
  ASSERT_FALSE(harness_->active().empty());
  ASSERT_TRUE(harness_->activate("normal"));
  harness_->step(60);
  EXPECT_TRUE(harness_->active().empty()) << describe_active();
}

TEST_F(ScenarioTest, ProcessingLatencyWithinTarget) {
  ASSERT_TRUE(harness_->activate("cryptominer"));
  harness_->step(20);
  const auto status = harness_->agent().status();
  // Spec section 23: metric processing latency < 100 ms.
  EXPECT_LT(status["agent"]["max_processing_latency_ms"].get<double>(), 100.0);
}

TEST(ScenarioDeterminismTest, SameSeedSameAlertTimeline) {
  auto run = [] {
    TempDir dir;
    AgentHarness harness(make_test_config(dir));
    EXPECT_TRUE(harness.initialize());
    harness.step(10);
    harness.activate("cryptominer");
    harness.step(30);
    std::vector<std::pair<std::string, uint64_t>> timeline;
    for (const auto &alert : harness.alerts()) {
      timeline.emplace_back(alert.rule, alert.timestamp_ms);
    }
    return timeline;
  };
  EXPECT_EQ(run(), run());
}

TEST(ScenarioRobustnessTest, NoFalsePositivesAcrossSeeds) {
  for (uint32_t seed = 1; seed <= 25; seed++) {
    TempDir dir;
    Config config = make_test_config(dir);
    config.simulation.seed = seed;
    AgentHarness harness(config);
    ASSERT_TRUE(harness.initialize());
    harness.step(600);
    EXPECT_TRUE(harness.alerts().empty())
        << "seed " << seed << ": " << harness.alerts().front().rule;
  }
}

TEST(ScenarioRobustnessTest, AttacksDetectedAcrossSeeds) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"cryptominer", "unauthorized_gpu_workload"},
      {"network-exfil", "model_data_exfiltration"},
      {"dos", rules::kResourceExhaustion},
      {"inference-degradation", rules::kInferenceDegradation},
  };
  for (uint32_t seed = 1; seed <= 10; seed++) {
    for (const auto &[scenario, rule] : cases) {
      TempDir dir;
      Config config = make_test_config(dir);
      config.simulation.seed = seed;
      AgentHarness harness(config);
      ASSERT_TRUE(harness.initialize());
      harness.step(20);
      ASSERT_TRUE(harness.activate(scenario));
      EXPECT_TRUE(harness.step_until(rule, 40).has_value())
          << scenario << " seed " << seed;
    }
  }
}
