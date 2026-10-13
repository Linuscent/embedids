/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <gtest/gtest.h>

#include "agent/config_loader.h"
#include "core/integrity/hash_util.h"
#include "support/test_support.h"

using namespace edgeml;

TEST(ConfigTest, DefaultsAreValid) {
  Config config;
  EXPECT_EQ(validate_config(config), "");
  EXPECT_EQ(config.device.platform, "simulation");
  EXPECT_EQ(config.ai.expected_fps, 30.0);
  EXPECT_EQ(config.heartbeat.timeout_seconds, 15u);
  EXPECT_GE(config.correlations.size(), 2u);
}

TEST(ConfigTest, ParsesSpecExample) {
  const char *yaml = R"(
device:
  id: demo-edge-001
  platform: simulation
sampling:
  interval_ms: 1000
gpu:
  high_utilization: 85
ai:
  expected_fps: 30
  minimum_fps: 20
network:
  allowed_destinations:
    - "10.0.0.20:1883"
    - "10.0.0.30:443"
process:
  allowed:
    - embedids-agent
    - inference-service
    - camera-service
    - mqtt-client
integrity:
  model:
    path: "/models/model.engine"
    sha256: "EXPECTED_HASH"
alerts:
  console: true
  json_file: true
  syslog: true
correlations:
  - name: unauthorized_gpu_workload
    severity: high
    window_seconds: 20
    requires:
      - security.unknown_processes
      - gpu.utilization
      - ai.inference.rate
)";
  Config config;
  std::string error;
  ASSERT_TRUE(apply_config_yaml(yaml, config, &error)) << error;
  EXPECT_EQ(config.device.id, "demo-edge-001");
  EXPECT_EQ(config.gpu.high_utilization, 85.0);
  EXPECT_EQ(config.ai.minimum_fps, 20.0);
  ASSERT_EQ(config.network.allowed_destinations.size(), 2u);
  EXPECT_EQ(config.network.allowed_destinations[1], "10.0.0.30:443");
  ASSERT_EQ(config.process.allowed.size(), 4u);
  EXPECT_EQ(config.integrity.model.sha256, "EXPECTED_HASH");
  EXPECT_TRUE(config.alerts.syslog);
  ASSERT_EQ(config.correlations.size(), 1u);
  EXPECT_EQ(config.correlations[0].severity, Severity::High);
  EXPECT_EQ(config.correlations[0].requires_signals.size(), 3u);
}

TEST(ConfigTest, EmptyDocumentKeepsDefaults) {
  Config config;
  std::string error;
  EXPECT_TRUE(apply_config_yaml("", config, &error)) << error;
  EXPECT_EQ(config.device.id, "demo-edge-001");
}

TEST(ConfigTest, RejectsInvalidValues) {
  std::string error;
  {
    Config config;
    EXPECT_FALSE(
        apply_config_yaml("device: {platform: windows}", config, &error));
    EXPECT_NE(error.find("platform"), std::string::npos);
  }
  {
    Config config;
    EXPECT_FALSE(apply_config_yaml("ai: {expected_fps: 30, minimum_fps: 40}",
                                   config, &error));
  }
  {
    Config config;
    EXPECT_FALSE(apply_config_yaml(
        "heartbeat: {interval_seconds: 5, timeout_seconds: 5}", config,
        &error));
  }
  {
    Config config;
    EXPECT_FALSE(apply_config_yaml(
        "correlations: [{name: x, severity: extreme, requires: [a]}]", config,
        &error));
  }
  {
    Config config;
    EXPECT_FALSE(apply_config_yaml("gpu: [1, 2", config, &error));
    EXPECT_NE(error.find("invalid configuration"), std::string::npos);
  }
  {
    Config config;
    EXPECT_FALSE(
        apply_config_yaml("sampling: {interval_ms: fast}", config, &error));
  }
}

TEST(ConfigTest, LoadFileRecordsPathAndHash) {
  test::TempDir dir;
  const std::string path = dir.file("config.yaml");
  const std::string text = "device:\n  id: file-device\n";
  {
    std::ofstream out(path);
    out << text;
  }
  Config config;
  std::string error;
  ASSERT_TRUE(load_config_file(path, config, &error)) << error;
  EXPECT_EQ(config.device.id, "file-device");
  EXPECT_EQ(config.source_path, path);
  EXPECT_EQ(config.source_sha256, sha256_string(text));

  Config missing;
  EXPECT_FALSE(load_config_file(dir.file("nope.yaml"), missing, &error));
}

TEST(ConfigTest, ShippedConfigsLoad) {
  for (const char *name : {"default.yaml", "demo.yaml", "jetson-example.yaml",
                           "linux-example.yaml"}) {
    Config config;
    std::string error;
    const std::string path =
        std::string(EDGEML_SOURCE_DIR) + "/configs/" + name;
    EXPECT_TRUE(load_config_file(path, config, &error))
        << name << ": " << error;
  }
}
