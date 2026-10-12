/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * REST API contract tests against a live ApiServer on an ephemeral port.
 */

#include <httplib.h>

#include <gtest/gtest.h>

#include "api/rest/api_server.h"
#include "support/test_support.h"

using namespace edgeml;
using namespace edgeml::test;

namespace {

class ApiTest : public ::testing::Test {
protected:
  void SetUp() override {
    Config config = make_test_config(dir_);
    config.api.bind = "127.0.0.1";
    config.api.port = 0;
    harness_ = std::make_unique<AgentHarness>(config);
    ASSERT_TRUE(harness_->initialize());
    harness_->step(5);
    server_ = std::make_unique<ApiServer>(harness_->agent(), config.api);
    std::string error;
    ASSERT_TRUE(server_->start(&error)) << error;
    client_ = std::make_unique<httplib::Client>("127.0.0.1", server_->port());
    client_->set_read_timeout(5, 0);
  }

  void TearDown() override { server_->stop(); }

  nlohmann::json get_json(const std::string &path, int expected = 200) {
    auto res = client_->Get(path);
    EXPECT_TRUE(res) << path;
    if (!res) {
      return {};
    }
    EXPECT_EQ(res->status, expected) << path << ": " << res->body;
    return nlohmann::json::parse(res->body, nullptr, false);
  }

  nlohmann::json post_json(const std::string &path, int expected = 200) {
    auto res = client_->Post(path, "", "application/json");
    EXPECT_TRUE(res) << path;
    if (!res) {
      return {};
    }
    EXPECT_EQ(res->status, expected) << path << ": " << res->body;
    return nlohmann::json::parse(res->body, nullptr, false);
  }

  TempDir dir_;
  std::unique_ptr<AgentHarness> harness_;
  std::unique_ptr<ApiServer> server_;
  std::unique_ptr<httplib::Client> client_;
};

} // namespace

TEST_F(ApiTest, HealthAndStatus) {
  auto health = get_json("/health");
  EXPECT_EQ(health["status"], "ok");

  auto status = get_json("/status");
  EXPECT_EQ(status["device_id"], "test-edge-001");
  EXPECT_EQ(status["security_state"], "HEALTHY");
  EXPECT_EQ(status["active_alerts"], 0);
  EXPECT_EQ(status["model_integrity"]["status"], "verified");
  EXPECT_TRUE(status["demo_controls"].get<bool>());
  EXPECT_FALSE(status["agent"]["binary_sha256"].get<std::string>().empty());
  EXPECT_TRUE(status["collectors"].is_array());

  // Same routes under the /api prefix used by the dashboard proxy.
  EXPECT_EQ(get_json("/api/status")["device_id"], "test-edge-001");
}

TEST_F(ApiTest, MetricsJsonAndPrometheus) {
  auto metrics = get_json("/metrics?history=60");
  EXPECT_TRUE(metrics["latest"].contains("gpu.utilization"));
  EXPECT_EQ(metrics["latest"]["gpu.utilization"]["unit"], "percent");
  EXPECT_EQ(metrics["history"]["ai.inference.rate"].size(), 5u);

  auto no_history = get_json("/metrics");
  EXPECT_TRUE(no_history["history"].empty());

  auto res = client_->Get("/metrics?format=prometheus");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(
      res->body.find("edgeml_gpu_utilization{device_id=\"test-edge-001\"}"),
      std::string::npos);
  EXPECT_NE(res->body.find("embedids_active_alerts"), std::string::npos);
}

TEST_F(ApiTest, DemoScenarioProducesQueryableAlert) {
  auto result = post_json("/demo/scenario/model-tamper");
  EXPECT_TRUE(result["ok"].get<bool>());
  harness_->step(2);

  auto alerts = get_json("/alerts?status=active");
  ASSERT_EQ(alerts.size(), 1u);
  EXPECT_EQ(alerts[0]["rule"], "model_integrity_violation");
  EXPECT_EQ(alerts[0]["severity"], "critical");
  const std::string id = alerts[0]["alert_id"];

  auto alert = get_json("/alerts/" + id);
  EXPECT_EQ(alert["alert_id"], id);
  EXPECT_TRUE(alert["evidence"].contains("expected_sha256"));
  get_json("/alerts/ALT-999999", 404);

  auto events = get_json("/events?limit=10");
  ASSERT_FALSE(events.empty());
  bool saw_scenario = false, saw_alert = false;
  for (const auto &event : events) {
    saw_scenario |= event["type"] == "demo.scenario";
    saw_alert |= event["type"] == "alert.raised";
  }
  EXPECT_TRUE(saw_scenario);
  EXPECT_TRUE(saw_alert);

  EXPECT_EQ(get_json("/status")["security_state"], "CRITICAL");
  auto reset = post_json("/demo/reset");
  EXPECT_TRUE(reset["ok"].get<bool>());
  EXPECT_EQ(get_json("/status")["security_state"], "HEALTHY");
  EXPECT_TRUE(get_json("/alerts?status=active").empty());
  EXPECT_EQ(get_json("/alerts").size(), 1u);
}

TEST_F(ApiTest, RejectsUnknownScenario) {
  auto body = post_json("/demo/scenario/meteor-strike", 404);
  EXPECT_NE(body["error"].get<std::string>().find("unknown scenario"),
            std::string::npos);
}

TEST_F(ApiTest, RulesCatalog) {
  auto rules = get_json("/rules");
  EXPECT_EQ(rules["rules"].size(), 11u);
  EXPECT_GE(rules["correlations"].size(), 2u);
}

TEST(ApiDisabledDemoTest, DemoControlsCanBeDisabled) {
  TempDir dir;
  Config config = make_test_config(dir);
  config.api.bind = "127.0.0.1";
  config.api.demo_controls = false;
  AgentHarness harness(config);
  ASSERT_TRUE(harness.initialize());
  ApiServer server(harness.agent(), config.api);
  ASSERT_TRUE(server.start());
  httplib::Client client("127.0.0.1", server.port());
  auto res = client.Post("/demo/scenario/cryptominer", "", "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 403);
  server.stop();
}
