/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware abstraction layer (spec section 18). Every telemetry backend -
 * simulation, generic Linux or NVIDIA Jetson - implements MetricCollector.
 * The detection engine only sees the normalized TelemetryBatch.
 */

#ifndef EDGEML_COLLECTORS_COMMON_COLLECTOR_H
#define EDGEML_COLLECTORS_COMMON_COLLECTOR_H

#include "core/schema/telemetry.h"

namespace edgeml {

class MetricCollector {
public:
  virtual ~MetricCollector() = default;

  /** Probe and prepare the backend. Returning false disables the collector. */
  virtual bool initialize() = 0;

  /** Append this cycle's observations. Returning false counts as a miss. */
  virtual bool collect(MetricBatch &metrics) = 0;

  virtual const char *name() const = 0;

  /**
   * Whether a missing report counts against the telemetry heartbeat.
   * Local self-checks (integrity, agent self-monitoring) return false.
   */
  virtual bool heartbeat_required() const { return true; }
};

} // namespace edgeml

#endif
