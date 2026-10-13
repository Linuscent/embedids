/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic detection rules (spec section 10). The engine only consumes
 * normalized TelemetryBatch data and never depends on a specific collector
 * backend, so the same rules run in simulation and on Jetson hardware.
 */

#ifndef EDGEML_CORE_DETECTION_DETECTION_ENGINE_H
#define EDGEML_CORE_DETECTION_DETECTION_ENGINE_H

#include <string>
#include <vector>

#include "core/alert/alert.h"
#include "core/config/config.h"
#include "core/detection/policy.h"
#include "core/embedids/signal_engine.h"
#include "core/schema/telemetry.h"

namespace edgeml {

namespace rules {
inline constexpr const char *kUnexpectedGpuWorkload = "unexpected_gpu_workload";
inline constexpr const char *kRogueProcess = "rogue_process";
inline constexpr const char *kUnknownDestination = "unknown_destination";
inline constexpr const char *kOutboundTrafficAnomaly =
    "outbound_traffic_anomaly";
inline constexpr const char *kDataExfiltration = "data_exfiltration";
inline constexpr const char *kModelIntegrity = "model_integrity_violation";
inline constexpr const char *kConfigIntegrity = "config_integrity_violation";
inline constexpr const char *kModelFileAccess = "model_file_access";
inline constexpr const char *kInferenceDegradation = "inference_degradation";
inline constexpr const char *kResourceExhaustion = "resource_exhaustion";
inline constexpr const char *kHeartbeatLost = "telemetry_heartbeat_lost";
} // namespace rules

struct RuleDescriptor {
  const char *id;
  const char *title;
  Severity severity;
  const char *condition;
};

/** Static description of the built-in rules (served by the API). */
const std::vector<RuleDescriptor> &rule_catalog();

class DetectionEngine {
public:
  explicit DetectionEngine(const Config &config);

  bool initialize(std::string *error = nullptr);

  /**
   * Derive policy metrics into the batch, run EmbedIDS and evaluate rules.
   * Returns every finding that is currently true.
   */
  std::vector<Finding> process(TelemetryBatch &batch);

  /** Forget all history (used by the demo reset). */
  void reset();

  const SignalEngine &signals() const { return signals_; }

  static std::vector<SignalSpec> build_signal_specs(const Config &config);

private:
  double value_of(const TelemetryBatch &batch, const char *metric) const;

  Config config_;
  SignalEngine signals_;
  ProcessPolicy process_policy_;
  NetworkPolicy network_policy_;
  ModelAccessPolicy model_access_policy_;

  ProcessPolicyResult last_processes_;
  std::vector<ProcessInfo> last_inventory_;
  std::vector<DestinationFinding> last_destinations_;
  Labels last_model_labels_;
  Labels last_config_labels_;
};

} // namespace edgeml

#endif
