/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_PROCESS_PROCESS_MODEL_H
#define EDGEML_SIMULATOR_PROCESS_PROCESS_MODEL_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/schema/telemetry.h"
#include "simulator/common/model_context.h"

namespace edgeml::sim {

inline constexpr int kMqttClientPid = 911;
inline constexpr int kInferencePid = 1204;

/** Process table of the simulated device. */
class ProcessModel {
public:
  explicit ProcessModel(uint64_t boot_ms = 0) : boot_ms_(boot_ms) {}
  void set_boot_ms(uint64_t boot_ms) { boot_ms_ = boot_ms; }
  void update(const ModelContext &ctx);
  void write(std::map<std::string, double> &out) const;
  const std::vector<ProcessInfo> &processes() const { return processes_; }

private:
  uint64_t boot_ms_;
  int background_processes_ = 182;
  std::vector<ProcessInfo> processes_;
};

} // namespace edgeml::sim

#endif
