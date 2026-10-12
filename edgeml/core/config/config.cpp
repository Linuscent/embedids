/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/config/config.h"

#include <sstream>

namespace edgeml {

std::vector<CorrelationRule> Config::default_correlations() {
  CorrelationRule gpu;
  gpu.name = "unauthorized_gpu_workload";
  gpu.title = "Possible unauthorized GPU workload";
  gpu.description = "GPU utilization increased while inference throughput "
                    "decreased and an unknown process was detected.";
  gpu.severity = Severity::High;
  gpu.window_seconds = 20;
  gpu.requires_signals = {"rogue_process", "unexpected_gpu_workload",
                          "inference_degradation"};

  CorrelationRule exfil;
  exfil.name = "model_data_exfiltration";
  exfil.title = "Possible model/data exfiltration";
  exfil.description = "An unexpected process read the deployed model while "
                      "high-volume traffic flowed to an unknown destination.";
  exfil.severity = Severity::Critical;
  exfil.window_seconds = 30;
  exfil.requires_signals = {"unknown_destination", "outbound_traffic_anomaly",
                            "model_file_access"};

  CorrelationRule hijack;
  hijack.name = "privileged_resource_hijack";
  hijack.title = "Privileged process exhausting device resources";
  hijack.description = "A process running as root outside the allow list "
                       "appeared while CPU and memory were exhausted.";
  hijack.severity = Severity::Critical;
  hijack.window_seconds = 30;
  hijack.requires_signals = {"security.privileged_processes",
                             "resource_exhaustion"};

  return {gpu, exfil, hijack};
}

std::string validate_config(const Config &config) {
  std::ostringstream errors;
  const auto &platform = config.device.platform;
  if (platform != "simulation" && platform != "linux" && platform != "jetson") {
    errors << "device.platform must be simulation, linux or jetson; ";
  }
  if (config.device.id.empty()) {
    errors << "device.id must not be empty; ";
  }
  if (config.sampling.interval_ms < 100 ||
      config.sampling.interval_ms > 60000) {
    errors << "sampling.interval_ms must be between 100 and 60000; ";
  }
  if (config.simulation.mode != "embedded" &&
      config.simulation.mode != "http") {
    errors << "simulation.mode must be embedded or http; ";
  }
  if (config.gpu.high_utilization <= 0 || config.gpu.high_utilization > 100) {
    errors << "gpu.high_utilization must be in (0, 100]; ";
  }
  if (config.ai.minimum_fps <= 0 ||
      config.ai.minimum_fps > config.ai.expected_fps) {
    errors << "ai.minimum_fps must be positive and <= ai.expected_fps; ";
  }
  if (config.ai.baseline_ratio <= 0 || config.ai.baseline_ratio > 1) {
    errors << "ai.baseline_ratio must be in (0, 1]; ";
  }
  if (config.network.baseline_bytes_out <= 0 ||
      config.network.anomaly_multiplier < 1) {
    errors << "network baseline must be positive and multiplier >= 1; ";
  }
  if (config.heartbeat.interval_seconds == 0 ||
      config.heartbeat.timeout_seconds <= config.heartbeat.interval_seconds) {
    errors << "heartbeat.timeout_seconds must exceed interval_seconds; ";
  }
  for (const auto &correlation : config.correlations) {
    if (correlation.name.empty() || correlation.requires_signals.empty()) {
      errors << "each correlation needs a name and requires list; ";
    }
  }
  if (config.api.port <= 0 || config.api.port > 65535) {
    errors << "api.port out of range; ";
  }
  return errors.str();
}

} // namespace edgeml
