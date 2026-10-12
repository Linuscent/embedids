/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_WORKLOAD_SNAPSHOT_H
#define EDGEML_SIMULATOR_WORKLOAD_SNAPSHOT_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/schema/telemetry.h"

namespace edgeml::sim {

/** Raw simulated device state, exchanged between simulator and agent. */
struct SimSnapshot {
  uint64_t timestamp_ms = 0;
  std::string device_id;
  std::vector<std::string> active_attacks;
  std::map<std::string, double> metrics;
  std::vector<ProcessInfo> processes;
  std::vector<ConnectionInfo> connections;
  std::vector<FileAccessInfo> file_accesses;
};

void to_json(nlohmann::json &j, const SimSnapshot &snapshot);
void from_json(const nlohmann::json &j, SimSnapshot &snapshot);

} // namespace edgeml::sim

#endif
