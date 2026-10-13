/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/attacks/scenarios.h"

namespace edgeml::sim {

const std::vector<ScenarioInfo> &scenario_catalog() {
  static const std::vector<ScenarioInfo> catalog = {
      {"cryptominer",
       Attack::Cryptominer,
       "Simulate Cryptominer",
       "Rogue GPU job 'unknown_worker' saturates the GPU and starves the "
       "inference pipeline.",
       {"rogue_process", "unexpected_gpu_workload", "inference_degradation",
        "unauthorized_gpu_workload"}},
      {"model-tamper",
       Attack::ModelTamper,
       "Modify AI Model",
       "Overwrites part of the deployed TensorRT engine file.",
       {"model_integrity_violation"}},
      {"network-exfil",
       Attack::NetworkExfil,
       "Simulate Data Exfiltration",
       "A hijacked mqtt-client reads the model and streams it to "
       "203.0.113.45:8443.",
       {"unknown_destination", "outbound_traffic_anomaly", "data_exfiltration",
        "model_file_access", "model_data_exfiltration"}},
      {"inference-degradation",
       Attack::InferenceDegradation,
       "Degrade Inference",
       "Inference throughput drops to ~14 FPS.",
       {"inference_degradation"}},
      {"rogue-process",
       Attack::RogueProcess,
       "Launch Rogue Process",
       "Starts 'rogue_daemon', a process outside the allow list.",
       {"rogue_process"}},
      {"privilege-escalation",
       Attack::PrivilegeEscalation,
       "Privilege Escalation",
       "Spawns 'rogue-shell' running as UID 0.",
       {"rogue_process"}},
      {"dos",
       Attack::DenialOfService,
       "Denial of Service",
       "CPU pinned at 100%, memory at 95%, inference collapses to 5 FPS.",
       {"resource_exhaustion", "inference_degradation"}},
      {"telemetry-loss",
       Attack::TelemetryLoss,
       "Stop Telemetry",
       "Collectors stop reporting, emulating agent tampering.",
       {"telemetry_heartbeat_lost"}},
  };
  return catalog;
}

const char *to_string(Attack attack) {
  for (const auto &info : scenario_catalog()) {
    if (info.attack == attack) {
      return info.name;
    }
  }
  return "unknown";
}

std::optional<Attack> parse_attack(const std::string &name) {
  static const std::map<std::string, Attack> aliases = {
      {"rogue-gpu", Attack::Cryptominer},
      {"exfil", Attack::NetworkExfil},
      {"data-exfil", Attack::NetworkExfil},
      {"degrade", Attack::InferenceDegradation},
      {"privesc", Attack::PrivilegeEscalation},
      {"denial-of-service", Attack::DenialOfService},
      {"agent-tamper", Attack::TelemetryLoss},
      {"stop-telemetry", Attack::TelemetryLoss},
  };
  for (const auto &info : scenario_catalog()) {
    if (name == info.name) {
      return info.attack;
    }
  }
  auto it = aliases.find(name);
  if (it != aliases.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<std::string> AttackState::names() const {
  std::vector<std::string> result;
  for (const auto &entry : started_) {
    result.emplace_back(to_string(entry.first));
  }
  return result;
}

} // namespace edgeml::sim
