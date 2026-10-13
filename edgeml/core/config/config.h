/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runtime configuration (spec section 13). Defaults match the simulated
 * Jetson-class device so an empty YAML file yields a working demo.
 */

#ifndef EDGEML_CORE_CONFIG_CONFIG_H
#define EDGEML_CORE_CONFIG_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/alert/alert.h"

namespace edgeml {

struct DeviceConfig {
  std::string id = "demo-edge-001";
  /** simulation | linux | jetson */
  std::string platform = "simulation";
  std::string workload = "vision-inference";
};

struct SamplingConfig {
  uint32_t interval_ms = 1000;
  /** Seconds of per-metric history retained for /metrics and the dashboard. */
  uint32_t history_seconds = 600;
};

struct SimulationConfig {
  /** embedded: simulator runs inside the agent; http: remote edgeml-simulator
   */
  std::string mode = "embedded";
  std::string endpoint = "http://127.0.0.1:9100";
  uint32_t seed = 42;
  uint32_t request_timeout_ms = 800;
};

struct SystemDetectionConfig {
  double cpu_exhaustion = 95.0;
  double memory_exhaustion = 90.0;
  uint32_t exhaustion_seconds = 5;
};

struct GpuConfig {
  double high_utilization = 85.0;
  uint32_t sustain_seconds = 10;
};

struct AiConfig {
  double expected_fps = 30.0;
  double minimum_fps = 20.0;
  /** Rule 1 treats fps below expected_fps * ratio as "below baseline". */
  double baseline_ratio = 0.9;
  uint32_t degradation_seconds = 10;
  /** Optional JSON stats file written by the inference service (hardware). */
  std::string stats_file;
};

struct NetworkConfig {
  std::vector<std::string> allowed_destinations = {"10.0.0.20:1883",
                                                   "10.0.0.30:443"};
  /** Normal outbound traffic in bytes/s. */
  double baseline_bytes_out = 40000.0;
  double anomaly_multiplier = 5.0;
  uint32_t anomaly_seconds = 3;
  /** Unknown destination escalates to HIGH above this outbound rate. */
  double escalation_bytes_out = 500000.0;
  std::string interface; /**< Linux: restrict byte counters to one NIC */
};

struct ProcessConfig {
  std::vector<std::string> allowed = {"systemd",        "containerd",
                                      "embedids-agent", "inference-service",
                                      "camera-service", "mqtt-client"};
  uint32_t min_runtime_seconds = 5;
  bool ignore_kernel_threads = true;
};

struct ModelIntegrityConfig {
  std::string path = "/models/model.engine";
  /** Approved hash; empty = trust on first use. */
  std::string sha256;
  std::vector<std::string> allowed_readers = {"inference-service",
                                              "embedids-agent"};
};

struct IntegrityConfig {
  ModelIntegrityConfig model;
  bool verify_config = true;
  uint32_t rehash_interval_seconds = 30;
  uint32_t startup_grace_seconds = 10;
  uint32_t access_window_seconds = 15;
};

struct HeartbeatConfig {
  uint32_t interval_seconds = 5;
  uint32_t timeout_seconds = 15;
};

struct CorrelationRule {
  std::string name;
  std::string title;
  std::string description;
  Severity severity = Severity::High;
  uint32_t window_seconds = 20;
  /** Rule names or metric names that must all be observed in the window. */
  std::vector<std::string> requires_signals;
};

struct AlertsConfig {
  bool console = true;
  bool json_file = true;
  std::string json_file_path = "/var/lib/embedids/alerts.jsonl";
  bool syslog = false;
  /** Optional remote syslog collector (RFC 5424 over UDP), "host:port". */
  std::string syslog_server;
  std::string webhook_url;
  std::string id_prefix = "ALT-";
};

struct StorageConfig {
  bool enabled = true;
  std::string path = "/var/lib/embedids/embedids.db";
  uint32_t metrics_retention_hours = 24;
  uint32_t events_retention_days = 7;
  uint32_t alerts_retention_days = 30;
};

struct ApiConfig {
  bool enabled = true;
  std::string bind = "0.0.0.0";
  int port = 8081;
  /** Serve the dashboard from this directory when set. */
  std::string static_dir;
  bool demo_controls = true;
};

struct LinuxConfig {
  std::string proc_root = "/proc";
  std::string sys_root = "/sys";
};

struct JetsonConfig {
  std::string tegrastats_path = "/usr/bin/tegrastats";
  std::vector<std::string> gpu_load_paths = {
      "/sys/devices/platform/gpu.0/load", "/sys/devices/gpu.0/load",
      "/sys/devices/platform/17000000.ga10b/load",
      "/sys/devices/platform/17000000.gv11b/load"};
};

struct Config {
  DeviceConfig device;
  SamplingConfig sampling;
  SimulationConfig simulation;
  SystemDetectionConfig system;
  GpuConfig gpu;
  AiConfig ai;
  NetworkConfig network;
  ProcessConfig process;
  IntegrityConfig integrity;
  HeartbeatConfig heartbeat;
  std::vector<CorrelationRule> correlations = default_correlations();
  AlertsConfig alerts;
  StorageConfig storage;
  ApiConfig api;
  LinuxConfig linux_host;
  JetsonConfig jetson;

  /** Path the configuration was loaded from (used for config integrity). */
  std::string source_path;
  /** SHA-256 of the configuration file when it was loaded. */
  std::string source_sha256;

  static std::vector<CorrelationRule> default_correlations();
};

/** Validate semantic constraints. Returns an empty string when valid. */
std::string validate_config(const Config &config);

} // namespace edgeml

#endif
