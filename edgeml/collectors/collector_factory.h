/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Builds the collector set for a platform. This is the only place that knows
 * which backend is in use; swapping simulation for Jetson is a config change.
 */

#ifndef EDGEML_COLLECTORS_COLLECTOR_FACTORY_H
#define EDGEML_COLLECTORS_COLLECTOR_FACTORY_H

#include <memory>
#include <string>
#include <vector>

#include "collectors/common/collector.h"
#include "collectors/simulation/simulation_feed.h"
#include "core/common/clock.h"
#include "core/config/config.h"

namespace edgeml {

struct CollectorSet {
  std::vector<std::unique_ptr<MetricCollector>> collectors;
  /** Demo control; null on hardware platforms. */
  std::shared_ptr<SimulationControl> simulation_control;
  /** Set when the simulator runs inside the agent process. */
  std::shared_ptr<sim::EdgeDeviceSimulator> embedded_simulator;
};

bool build_collectors(const Config &config, const Clock &clock,
                      CollectorSet &out, std::string *error = nullptr);

} // namespace edgeml

#endif
