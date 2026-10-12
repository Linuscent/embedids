/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/workload/edge_device_simulator.h"

#include "simulator/workload/model_file.h"

namespace edgeml::sim {

namespace {
constexpr double kMaxStepSeconds = 5.0;
}

EdgeDeviceSimulator::EdgeDeviceSimulator(SimulatorOptions options)
    : options_(std::move(options)), rng_(options_.seed),
      network_(options_.model_path) {}

bool EdgeDeviceSimulator::initialize(std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!options_.manage_model_file) {
    return true;
  }
  return restore_model_locked(error);
}

bool EdgeDeviceSimulator::restore_model_locked(std::string *error) {
  if (!options_.manage_model_file) {
    model_tampered_ = false;
    return true;
  }
  if (!write_model_file(options_.model_path, false, error)) {
    return false;
  }
  model_tampered_ = false;
  return true;
}

void EdgeDeviceSimulator::step(uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_) {
    started_ = true;
    start_ms_ = now_ms;
    last_step_ms_ = now_ms;
    // Pretend the device booted an hour ago.
    processes_.set_boot_ms(now_ms > 3600000 ? now_ms - 3600000 : 0);
  }
  if (now_ms < last_step_ms_) {
    return;
  }
  double dt = static_cast<double>(now_ms - last_step_ms_) / 1000.0;
  if (dt <= 0.0) {
    dt = 0.001;
  }
  dt = std::min(dt, kMaxStepSeconds);
  last_step_ms_ = now_ms;
  steps_++;

  ModelContext ctx{attacks_, dt,
                   static_cast<double>(now_ms - start_ms_) / 1000.0, now_ms,
                   rng_};
  system_.update(ctx);
  gpu_.update(ctx);
  inference_.update(ctx);
  processes_.update(ctx);
  network_.update(ctx);
}

void EdgeDeviceSimulator::refresh_inventory_locked() {
  if (!started_) {
    return;
  }
  // Inventories are discrete state: they must not lag a scenario change
  // until the next timed step, or a reset would briefly report old attacks.
  ModelContext ctx{attacks_, 0.0,
                   static_cast<double>(last_step_ms_ - start_ms_) / 1000.0,
                   last_step_ms_, rng_};
  processes_.update(ctx);
  network_.update(ctx);
}

SimSnapshot EdgeDeviceSimulator::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  SimSnapshot snapshot;
  snapshot.timestamp_ms = last_step_ms_;
  snapshot.device_id = options_.device_id;
  snapshot.active_attacks = attacks_.names();
  system_.write(snapshot.metrics);
  gpu_.write(snapshot.metrics);
  inference_.write(snapshot.metrics);
  processes_.write(snapshot.metrics);
  network_.write(snapshot.metrics);
  snapshot.processes = processes_.processes();
  snapshot.connections = network_.connections();
  snapshot.file_accesses = network_.file_accesses();
  return snapshot;
}

bool EdgeDeviceSimulator::telemetry_available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !attacks_.has(Attack::TelemetryLoss);
}

bool EdgeDeviceSimulator::activate(const std::string &scenario,
                                   std::string *error) {
  if (scenario == "normal") {
    stop_attacks();
    return true;
  }
  if (scenario == "reset") {
    reset();
    return true;
  }

  auto attack = parse_attack(scenario);
  if (!attack) {
    if (error) {
      *error = "unknown scenario: " + scenario;
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  attacks_.activate(*attack, last_step_ms_);
  refresh_inventory_locked();
  if (*attack == Attack::ModelTamper && options_.manage_model_file) {
    if (!write_model_file(options_.model_path, true, error)) {
      return false;
    }
    model_tampered_ = true;
  }
  return true;
}

void EdgeDeviceSimulator::stop_attacks() {
  std::lock_guard<std::mutex> lock(mutex_);
  attacks_.clear();
  refresh_inventory_locked();
  std::string error;
  if (model_tampered_ && !restore_model_locked(&error)) {
    last_error_ = error;
  }
}

void EdgeDeviceSimulator::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  attacks_.clear();
  system_.reset();
  gpu_.reset();
  inference_.reset();
  network_.reset();
  refresh_inventory_locked();
  std::string error;
  if (!restore_model_locked(&error)) {
    last_error_ = error;
  }
}

nlohmann::json EdgeDeviceSimulator::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json scenarios = nlohmann::json::array();
  for (const auto &info : scenario_catalog()) {
    scenarios.push_back({{"name", info.name},
                         {"label", info.label},
                         {"description", info.description},
                         {"expected_alerts", info.expected_alerts},
                         {"active", attacks_.has(info.attack)}});
  }
  return {{"device_id", options_.device_id},
          {"active_attacks", attacks_.names()},
          {"telemetry_available", !attacks_.has(Attack::TelemetryLoss)},
          {"model_path", options_.model_path},
          {"model_tampered", model_tampered_},
          {"approved_model_sha256", approved_model_sha256()},
          {"steps", steps_},
          {"seed", options_.seed},
          {"last_error", last_error_},
          {"scenarios", scenarios}};
}

} // namespace edgeml::sim
