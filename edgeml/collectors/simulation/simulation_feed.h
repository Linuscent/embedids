/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Simulation backend: a SimulationFeed provides one simulator snapshot per
 * sampling cycle, either from an in-process EdgeDeviceSimulator or from a
 * remote edgeml-simulator over HTTP. SimulationControl drives demo scenarios.
 */

#ifndef EDGEML_COLLECTORS_SIMULATION_SIMULATION_FEED_H
#define EDGEML_COLLECTORS_SIMULATION_SIMULATION_FEED_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "core/common/clock.h"
#include "simulator/workload/edge_device_simulator.h"
#include "simulator/workload/snapshot.h"

namespace edgeml {

class SimulationFeed {
public:
  virtual ~SimulationFeed() = default;

  /** Snapshot for this cycle (fetched once and cached), or null on failure. */
  const sim::SimSnapshot *get(uint64_t cycle);

protected:
  virtual bool fetch(sim::SimSnapshot &snapshot) = 0;

private:
  bool have_cycle_ = false;
  uint64_t cycle_ = 0;
  bool ok_ = false;
  sim::SimSnapshot snapshot_;
};

class InProcessSimulationFeed : public SimulationFeed {
public:
  InProcessSimulationFeed(std::shared_ptr<sim::EdgeDeviceSimulator> simulator,
                          const Clock &clock)
      : simulator_(std::move(simulator)), clock_(clock) {}

protected:
  bool fetch(sim::SimSnapshot &snapshot) override;

private:
  std::shared_ptr<sim::EdgeDeviceSimulator> simulator_;
  const Clock &clock_;
};

class HttpSimulationFeed : public SimulationFeed {
public:
  HttpSimulationFeed(std::string endpoint, uint32_t timeout_ms)
      : endpoint_(std::move(endpoint)), timeout_ms_(timeout_ms) {}

protected:
  bool fetch(sim::SimSnapshot &snapshot) override;

private:
  std::string endpoint_;
  uint32_t timeout_ms_;
};

/** Demo control surface used by POST /demo/scenario and /demo/reset. */
class SimulationControl {
public:
  virtual ~SimulationControl() = default;
  virtual bool activate(const std::string &scenario, nlohmann::json *result,
                        std::string *error) = 0;
  virtual bool reset(nlohmann::json *result, std::string *error) = 0;
  virtual nlohmann::json state() = 0;
};

class InProcessSimulationControl : public SimulationControl {
public:
  explicit InProcessSimulationControl(
      std::shared_ptr<sim::EdgeDeviceSimulator> simulator)
      : simulator_(std::move(simulator)) {}
  bool activate(const std::string &scenario, nlohmann::json *result,
                std::string *error) override;
  bool reset(nlohmann::json *result, std::string *error) override;
  nlohmann::json state() override;

private:
  std::shared_ptr<sim::EdgeDeviceSimulator> simulator_;
};

class HttpSimulationControl : public SimulationControl {
public:
  HttpSimulationControl(std::string endpoint, uint32_t timeout_ms)
      : endpoint_(std::move(endpoint)), timeout_ms_(timeout_ms) {}
  bool activate(const std::string &scenario, nlohmann::json *result,
                std::string *error) override;
  bool reset(nlohmann::json *result, std::string *error) override;
  nlohmann::json state() override;

private:
  bool post(const std::string &path, nlohmann::json *result,
            std::string *error);

  std::string endpoint_;
  uint32_t timeout_ms_;
};

} // namespace edgeml

#endif
