/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Jetson-like EdgeML device simulator (spec sections 7.2, 15, 16).
 * Thread-safe: the stepping loop and the control API may run concurrently.
 */

#ifndef EDGEML_SIMULATOR_WORKLOAD_EDGE_DEVICE_SIMULATOR_H
#define EDGEML_SIMULATOR_WORKLOAD_EDGE_DEVICE_SIMULATOR_H

#include <cstdint>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "simulator/attacks/scenarios.h"
#include "simulator/common/sim_signal.h"
#include "simulator/gpu/gpu_model.h"
#include "simulator/inference/inference_model.h"
#include "simulator/network/network_model.h"
#include "simulator/process/process_model.h"
#include "simulator/workload/snapshot.h"
#include "simulator/workload/system_model.h"

namespace edgeml::sim {

struct SimulatorOptions {
  std::string device_id = "demo-edge-001";
  uint32_t seed = 42;
  /** Model file the simulator owns (created, tampered and restored). */
  std::string model_path = "/models/model.engine";
  bool manage_model_file = true;
};

class EdgeDeviceSimulator {
public:
  explicit EdgeDeviceSimulator(SimulatorOptions options);

  /** Create the approved model file. */
  bool initialize(std::string *error = nullptr);

  /** Advance the simulation to now_ms (the first call only anchors time). */
  void step(uint64_t now_ms);

  SimSnapshot snapshot() const;
  bool telemetry_available() const;

  /**
   * Activate a scenario. "normal" stops all attacks (values ramp back),
   * "reset" additionally snaps every model back to baseline.
   */
  bool activate(const std::string &scenario, std::string *error = nullptr);
  void stop_attacks();
  void reset();

  nlohmann::json state() const;
  const SimulatorOptions &options() const { return options_; }

private:
  bool restore_model_locked(std::string *error);
  /** Apply attack changes to the process/connection tables immediately. */
  void refresh_inventory_locked();

  SimulatorOptions options_;
  mutable std::mutex mutex_;
  SimRandom rng_;
  AttackState attacks_;

  SystemModel system_;
  GpuModel gpu_;
  InferenceModel inference_;
  NetworkModel network_;
  ProcessModel processes_;

  bool started_ = false;
  uint64_t start_ms_ = 0;
  uint64_t last_step_ms_ = 0;
  uint64_t steps_ = 0;
  bool model_tampered_ = false;
  std::string last_error_;
};

} // namespace edgeml::sim

#endif
