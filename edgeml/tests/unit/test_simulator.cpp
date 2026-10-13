/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "core/integrity/hash_util.h"
#include "simulator/workload/edge_device_simulator.h"
#include "simulator/workload/model_file.h"
#include "support/test_support.h"

using namespace edgeml;
using namespace edgeml::sim;

namespace {

SimulatorOptions options_for(const test::TempDir &dir, uint32_t seed = 42) {
  SimulatorOptions options;
  options.seed = seed;
  options.model_path = dir.file("models/model.engine");
  return options;
}

constexpr uint64_t kStart = 1790000000000ULL;

} // namespace

TEST(SimulatorTest, NormalStateMatchesSpecRanges) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  ASSERT_TRUE(sim.initialize());

  double gpu_sum = 0, fps_sum = 0, cpu_sum = 0;
  double fps_min = 100, fps_max = 0, latency_min = 1000, latency_max = 0;
  const int samples = 600;
  for (int i = 0; i <= samples; i++) {
    sim.step(kStart + i * 1000ULL);
    if (i == 0) {
      continue;
    }
    const auto snapshot = sim.snapshot();
    const auto &m = snapshot.metrics;
    gpu_sum += m.at(metrics::kGpuUtilization);
    cpu_sum += m.at(metrics::kCpuUtilization);
    const double fps = m.at(metrics::kInferenceRate);
    fps_sum += fps;
    fps_min = std::min(fps_min, fps);
    fps_max = std::max(fps_max, fps);
    latency_min = std::min(latency_min, m.at(metrics::kInferenceLatency));
    latency_max = std::max(latency_max, m.at(metrics::kInferenceLatency));
    EXPECT_GT(m.at(metrics::kGpuMemoryPercent), 33);
    EXPECT_LT(m.at(metrics::kGpuMemoryPercent), 47);
    EXPECT_LT(m.at(metrics::kBytesOut), 100000);
  }
  EXPECT_NEAR(gpu_sum / samples, 62.5, 3.5); // spec: 55-70 %
  EXPECT_NEAR(cpu_sum / samples, 42.5, 3.0); // spec: 35-50 %
  EXPECT_NEAR(fps_sum / samples, 30.0, 1.0); // spec: 28-32 FPS
  EXPECT_GT(fps_min, 27.0);
  EXPECT_LT(fps_max, 33.0);
  EXPECT_GT(latency_min, 22.0); // spec: 25-35 ms
  EXPECT_LT(latency_max, 38.0);
  // Not static: telemetry varies.
  EXPECT_GT(fps_max - fps_min, 1.0);
}

TEST(SimulatorTest, DeterministicForSameSeed) {
  test::TempDir dir_a, dir_b;
  EdgeDeviceSimulator a(options_for(dir_a, 9));
  EdgeDeviceSimulator b(options_for(dir_b, 9));
  ASSERT_TRUE(a.initialize());
  ASSERT_TRUE(b.initialize());
  for (int i = 0; i < 50; i++) {
    a.step(kStart + i * 1000ULL);
    b.step(kStart + i * 1000ULL);
    if (i == 10) {
      ASSERT_TRUE(a.activate("cryptominer"));
      ASSERT_TRUE(b.activate("cryptominer"));
    }
  }
  EXPECT_EQ(nlohmann::json(a.snapshot()).dump(),
            nlohmann::json(b.snapshot()).dump());
}

TEST(SimulatorTest, CryptominerProfile) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  ASSERT_TRUE(sim.initialize());
  sim.step(kStart);
  ASSERT_TRUE(sim.activate("cryptominer"));
  for (int i = 1; i <= 15; i++) {
    sim.step(kStart + i * 1000ULL);
  }
  const auto snapshot = sim.snapshot();
  EXPECT_GE(snapshot.metrics.at(metrics::kGpuUtilization), 94.0);
  EXPECT_LT(snapshot.metrics.at(metrics::kInferenceRate), 15.0);
  EXPECT_EQ(snapshot.metrics.at(metrics::kGpuProcessCount), 2.0);
  bool found = false;
  for (const auto &p : snapshot.processes) {
    if (p.name == "unknown_worker") {
      found = true;
      EXPECT_TRUE(p.uses_gpu);
      EXPECT_EQ(p.start_time_ms, kStart);
    }
  }
  EXPECT_TRUE(found);
  EXPECT_EQ(snapshot.active_attacks, std::vector<std::string>{"cryptominer"});
}

TEST(SimulatorTest, ModelTamperAndRestore) {
  test::TempDir dir;
  const auto options = options_for(dir);
  EdgeDeviceSimulator sim(options);
  ASSERT_TRUE(sim.initialize());
  EXPECT_EQ(sha256_file(options.model_path).value(), approved_model_sha256());

  ASSERT_TRUE(sim.activate("model-tamper"));
  const auto tampered = sha256_file(options.model_path).value();
  EXPECT_NE(tampered, approved_model_sha256());
  EXPECT_TRUE(sim.state()["model_tampered"].get<bool>());

  ASSERT_TRUE(sim.activate("normal"));
  EXPECT_EQ(sha256_file(options.model_path).value(), approved_model_sha256());
}

TEST(SimulatorTest, ApprovedModelHashIsStable) {
  // Pinned in configs/demo.yaml; changing the generator breaks the demo.
  EXPECT_EQ(approved_model_sha256(),
            "57869ad7a571163daf939d5e3ebe1dcfcb81437654d1636b0cdfdc233d92f56c");
}

TEST(SimulatorTest, ExfilScenarioCreatesConnectionAndModelReads) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  ASSERT_TRUE(sim.initialize());
  sim.step(kStart);
  ASSERT_TRUE(sim.activate("exfil")); // alias
  for (int i = 1; i <= 10; i++) {
    sim.step(kStart + i * 1000ULL);
  }
  const auto snapshot = sim.snapshot();
  EXPECT_GT(snapshot.metrics.at(metrics::kBytesOut), 1.5e6);
  ASSERT_EQ(snapshot.connections.size(), 3u);
  EXPECT_EQ(snapshot.connections[2].destination, kExfilDestination);
  ASSERT_EQ(snapshot.file_accesses.size(), 1u);
  EXPECT_EQ(snapshot.file_accesses[0].process, "mqtt-client");
}

TEST(SimulatorTest, TelemetryLossAndReset) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  ASSERT_TRUE(sim.initialize());
  EXPECT_TRUE(sim.telemetry_available());
  ASSERT_TRUE(sim.activate("telemetry-loss"));
  EXPECT_FALSE(sim.telemetry_available());
  sim.reset();
  EXPECT_TRUE(sim.telemetry_available());
  EXPECT_TRUE(sim.state()["active_attacks"].empty());
}

TEST(SimulatorTest, UnknownScenarioRejected) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  std::string error;
  EXPECT_FALSE(sim.activate("meteor-strike", &error));
  EXPECT_NE(error.find("unknown scenario"), std::string::npos);
}

TEST(SimulatorTest, SnapshotJsonRoundTrip) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  ASSERT_TRUE(sim.initialize());
  sim.step(kStart);
  sim.activate("network-exfil");
  sim.step(kStart + 1000);
  const auto original = sim.snapshot();
  const auto parsed = nlohmann::json(original).get<SimSnapshot>();
  EXPECT_EQ(parsed.metrics, original.metrics);
  EXPECT_EQ(parsed.processes.size(), original.processes.size());
  EXPECT_EQ(parsed.connections.size(), original.connections.size());
  EXPECT_EQ(parsed.file_accesses.size(), original.file_accesses.size());
}

TEST(SimulatorTest, ScenarioChangesApplyToInventoriesImmediately) {
  test::TempDir dir;
  EdgeDeviceSimulator sim(options_for(dir));
  ASSERT_TRUE(sim.initialize());
  sim.step(kStart);
  auto has_process = [&](const std::string &name) {
    for (const auto &p : sim.snapshot().processes) {
      if (p.name == name) {
        return true;
      }
    }
    return false;
  };
  // No step() between the scenario change and the snapshot.
  ASSERT_TRUE(sim.activate("cryptominer"));
  EXPECT_TRUE(has_process("unknown_worker"));
  sim.reset();
  EXPECT_FALSE(has_process("unknown_worker"));
  ASSERT_TRUE(sim.activate("network-exfil"));
  EXPECT_EQ(sim.snapshot().connections.size(), 3u);
  ASSERT_TRUE(sim.activate("normal"));
  EXPECT_EQ(sim.snapshot().connections.size(), 2u);
  EXPECT_TRUE(sim.snapshot().file_accesses.empty());
}
