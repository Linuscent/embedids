/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_COMMON_MODEL_CONTEXT_H
#define EDGEML_SIMULATOR_COMMON_MODEL_CONTEXT_H

#include <cstdint>

#include "simulator/attacks/scenarios.h"
#include "simulator/common/sim_signal.h"

namespace edgeml::sim {

/** Inputs shared by every sub-model on each simulation step. */
struct ModelContext {
  const AttackState &attacks;
  double dt_seconds;
  double elapsed_seconds;
  uint64_t now_ms;
  SimRandom &rng;
};

/** Unified memory of a Jetson Orin Nano 8GB developer kit. */
inline constexpr double kDeviceMemoryMb = 7620.0;

} // namespace edgeml::sim

#endif
