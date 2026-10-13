/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/gpu/gpu_model.h"

#include "core/schema/telemetry.h"

namespace edgeml::sim {

void GpuModel::update(const ModelContext &ctx) {
  const auto &attacks = ctx.attacks;
  // Normal vision workload: 55-70% with a slow scene-dependent swing.
  double util_target =
      62.0 + 4.0 * std::sin(ctx.elapsed_seconds / 90.0 * 6.283);
  double memory_target = 40.0;

  if (attacks.has(Attack::InferenceDegradation)) {
    util_target = 79.0;
  }
  if (attacks.has(Attack::DenialOfService)) {
    util_target = 45.0; // starved by the CPU
  }
  if (attacks.has(Attack::Cryptominer)) {
    util_target = 97.5;
    memory_target = 74.0;
  }

  utilization_.update(util_target, ctx.dt_seconds);
  memory_percent_.update(memory_target, ctx.dt_seconds);

  const double noise = attacks.has(Attack::Cryptominer) ? 1.0 : 2.5;
  utilization_out_ =
      clamp(utilization_.value() + ctx.rng.gaussian(noise), 0, 100);
  memory_percent_out_ =
      clamp(memory_percent_.value() + ctx.rng.gaussian(0.8), 0, 100);
  power_out_ =
      clamp(3.0 + utilization_out_ * 0.08 + ctx.rng.gaussian(0.2), 0.5, 25.0);
  temperature_.update(44.0 + utilization_out_ * 0.15, ctx.dt_seconds);
  temperature_out_ = temperature_.value() + ctx.rng.gaussian(0.3);
  process_count_ = 1 + (attacks.has(Attack::Cryptominer) ? 1 : 0);
}

void GpuModel::write(std::map<std::string, double> &out) const {
  out[metrics::kGpuUtilization] = utilization_out_;
  out[metrics::kGpuMemoryPercent] = memory_percent_out_;
  out[metrics::kGpuMemoryUsed] = memory_percent_out_ * kDeviceMemoryMb / 100;
  out[metrics::kGpuPower] = power_out_;
  out[metrics::kGpuTemperature] = temperature_out_;
  out[metrics::kGpuProcessCount] = process_count_;
}

void GpuModel::reset() {
  utilization_.reset();
  memory_percent_.reset();
  temperature_.reset();
}

} // namespace edgeml::sim
