/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Docker topology in-process: edgeml-simulator HTTP routes on one port,
 * an agent using HttpSimulationFeed / HttpSimulationControl against it.
 */

#include <thread>

#include <httplib.h>

#include <gtest/gtest.h>

#include "collectors/simulation/simulation_feed.h"
#include "core/detection/detection_engine.h"
#include "simulator/workload/control_server.h"
#include "support/test_support.h"

using namespace edgeml;
using namespace edgeml::test;

namespace {

class HttpSimulationTest : public ::testing::Test {
protected:
  void SetUp() override {
    sim::SimulatorOptions options;
    options.model_path = dir_.file("models/model.engine");
    simulator_ = std::make_unique<sim::EdgeDeviceSimulator>(options);
    ASSERT_TRUE(simulator_->initialize());
    simulator_->step(clock_.now_ms());
    sim::register_simulator_routes(server_, *simulator_, false);
    port_ = server_.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port_, 0);
    thread_ = std::thread([this] { server_.listen_after_bind(); });
    server_.wait_until_ready();
    endpoint_ = "http://127.0.0.1:" + std::to_string(port_);
  }

  void TearDown() override {
    server_.stop();
    thread_.join();
  }

  /** Advance simulator time (the real binary does this on a timer). */
  void advance(int seconds) {
    for (int i = 0; i < seconds; i++) {
      clock_.advance_ms(1000);
      simulator_->step(clock_.now_ms());
    }
  }

  TempDir dir_;
  ManualClock clock_;
  std::unique_ptr<sim::EdgeDeviceSimulator> simulator_;
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::string endpoint_;
};

} // namespace

TEST_F(HttpSimulationTest, FeedFetchesSnapshotsAndHonorsTelemetryLoss) {
  advance(1);
  HttpSimulationFeed feed(endpoint_, 1000);
  const sim::SimSnapshot *snapshot = feed.get(1);
  ASSERT_NE(snapshot, nullptr);
  EXPECT_TRUE(snapshot->metrics.count(metrics::kGpuUtilization));
  EXPECT_EQ(snapshot->processes.size(), 6u);
  // Cached within a cycle.
  EXPECT_EQ(feed.get(1), snapshot);

  HttpSimulationControl control(endpoint_, 1000);
  nlohmann::json result;
  std::string error;
  ASSERT_TRUE(control.activate("telemetry-loss", &result, &error)) << error;
  EXPECT_EQ(result["telemetry_available"], false);
  EXPECT_EQ(feed.get(2), nullptr);

  EXPECT_FALSE(control.activate("bogus", &result, &error));
  EXPECT_NE(error.find("unknown scenario"), std::string::npos);

  ASSERT_TRUE(control.reset(&result, &error));
  EXPECT_NE(feed.get(3), nullptr);
  EXPECT_TRUE(control.state()["reachable"].get<bool>());
}

TEST_F(HttpSimulationTest, UnreachableSimulatorReportsFailure) {
  HttpSimulationFeed feed("http://127.0.0.1:1", 200);
  EXPECT_EQ(feed.get(1), nullptr);
  HttpSimulationControl control("http://127.0.0.1:1", 200);
  std::string error;
  EXPECT_FALSE(control.activate("cryptominer", nullptr, &error));
  EXPECT_NE(error.find("unreachable"), std::string::npos);
  EXPECT_FALSE(control.state()["reachable"].get<bool>());
}

TEST_F(HttpSimulationTest, AgentDetectsAttackOverHttp) {
  Config config = make_test_config(dir_);
  config.simulation.mode = "http";
  config.simulation.endpoint = endpoint_;
  AgentHarness harness(config);
  ASSERT_TRUE(harness.initialize());

  auto step_both = [&](int seconds) {
    for (int i = 0; i < seconds; i++) {
      advance(1);
      harness.step();
    }
  };
  step_both(5);
  EXPECT_TRUE(harness.active().empty());

  ASSERT_TRUE(harness.activate("model-tamper"));
  step_both(2);
  EXPECT_TRUE(harness.find_active(rules::kModelIntegrity));

  ASSERT_TRUE(harness.activate("telemetry-loss"));
  step_both(25);
  EXPECT_TRUE(harness.find_active(rules::kHeartbeatLost));

  ASSERT_TRUE(harness.reset());
  step_both(5);
  EXPECT_TRUE(harness.active().empty());
}
