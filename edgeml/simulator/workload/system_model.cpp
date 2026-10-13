/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/workload/system_model.h"

#include "core/schema/telemetry.h"

namespace edgeml::sim {

void SystemModel::update(const ModelContext &ctx) {
  const auto &attacks = ctx.attacks;
  double cpu_target = 42.0;
  double memory_target = 40.0;
  if (attacks.has(Attack::Cryptominer)) {
    cpu_target += 18.0;
    memory_target += 4.0;
  }
  if (attacks.has(Attack::RogueProcess)) {
    cpu_target += 6.0;
  }
  if (attacks.has(Attack::PrivilegeEscalation)) {
    cpu_target += 4.0;
  }
  if (attacks.has(Attack::NetworkExfil)) {
    cpu_target += 9.0;
    memory_target += 2.0;
  }
  if (attacks.has(Attack::DenialOfService)) {
    cpu_target = 100.0;
    memory_target = 95.5;
  }

  cpu_.update(cpu_target, ctx.dt_seconds);
  memory_.update(memory_target, ctx.dt_seconds);
  const bool saturated = attacks.has(Attack::DenialOfService);
  cpu_out_ =
      clamp(cpu_.value() + ctx.rng.gaussian(saturated ? 0.4 : 3.0), 0, 100);
  memory_out_ = clamp(memory_.value() + ctx.rng.gaussian(0.4), 0, 100);
  load_.update(cpu_out_ / 100.0 * 6.0 * 0.9, ctx.dt_seconds);
  temperature_.update(47.0 + cpu_out_ * 0.12, ctx.dt_seconds);
  temperature_out_ = temperature_.value() + ctx.rng.gaussian(0.3);
}

void SystemModel::write(std::map<std::string, double> &out) const {
  out[metrics::kCpuUtilization] = cpu_out_;
  out[metrics::kMemoryPercent] = memory_out_;
  out[metrics::kMemoryUsed] = memory_out_ * kDeviceMemoryMb / 100.0;
  out[metrics::kLoadAverage] = load_.value();
  out[metrics::kTemperature] = temperature_out_;
}

void SystemModel::reset() {
  cpu_.reset();
  memory_.reset();
  load_.reset();
  temperature_.reset();
}

} // namespace edgeml::sim
