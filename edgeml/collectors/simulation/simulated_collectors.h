/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_COLLECTORS_SIMULATION_SIMULATED_COLLECTORS_H
#define EDGEML_COLLECTORS_SIMULATION_SIMULATED_COLLECTORS_H

#include <memory>
#include <string>
#include <vector>

#include "collectors/common/collector.h"
#include "collectors/simulation/simulation_feed.h"

namespace edgeml {

/** Copies metrics with the given prefixes from the simulator snapshot. */
class SimulatedCollector : public MetricCollector {
public:
  SimulatedCollector(std::string name, std::shared_ptr<SimulationFeed> feed,
                     std::string source, std::vector<std::string> prefixes)
      : name_(std::move(name)), feed_(std::move(feed)),
        source_(std::move(source)), prefixes_(std::move(prefixes)) {}

  bool initialize() override { return feed_ != nullptr; }
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return name_.c_str(); }

protected:
  /** Hook for inventory data; called after metrics are copied. */
  virtual void collect_inventory(const sim::SimSnapshot &, MetricBatch &) {}

private:
  std::string name_;
  std::shared_ptr<SimulationFeed> feed_;
  std::string source_;
  std::vector<std::string> prefixes_;
};

class SimulatedSystemCollector : public SimulatedCollector {
public:
  explicit SimulatedSystemCollector(std::shared_ptr<SimulationFeed> feed)
      : SimulatedCollector("sim-system", std::move(feed), "system",
                           {"system."}) {}
};

class SimulatedGpuCollector : public SimulatedCollector {
public:
  explicit SimulatedGpuCollector(std::shared_ptr<SimulationFeed> feed)
      : SimulatedCollector("sim-gpu", std::move(feed), "gpu", {"gpu."}) {}
};

class SimulatedInferenceCollector : public SimulatedCollector {
public:
  explicit SimulatedInferenceCollector(std::shared_ptr<SimulationFeed> feed)
      : SimulatedCollector("sim-inference", std::move(feed), "ai",
                           {"ai.inference.", "ai.pipeline."}) {}
};

class SimulatedNetworkCollector : public SimulatedCollector {
public:
  explicit SimulatedNetworkCollector(std::shared_ptr<SimulationFeed> feed)
      : SimulatedCollector("sim-network", std::move(feed), "network",
                           {"network."}) {}

protected:
  void collect_inventory(const sim::SimSnapshot &snapshot,
                         MetricBatch &batch) override;
};

class SimulatedProcessCollector : public SimulatedCollector {
public:
  explicit SimulatedProcessCollector(std::shared_ptr<SimulationFeed> feed)
      : SimulatedCollector("sim-process", std::move(feed), "process", {}) {}

protected:
  void collect_inventory(const sim::SimSnapshot &snapshot,
                         MetricBatch &batch) override;
};

} // namespace edgeml

#endif
