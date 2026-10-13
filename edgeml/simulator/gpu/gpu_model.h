/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_GPU_GPU_MODEL_H
#define EDGEML_SIMULATOR_GPU_GPU_MODEL_H

#include <map>
#include <string>

#include "simulator/common/model_context.h"

namespace edgeml::sim {

/** Jetson-class integrated GPU (utilization, memory, power, temperature). */
class GpuModel {
public:
  void update(const ModelContext &ctx);
  void write(std::map<std::string, double> &out) const;
  void reset();

private:
  SmoothedValue utilization_{62.0, 1.5};
  SmoothedValue memory_percent_{40.0, 3.0};
  SmoothedValue temperature_{53.0, 20.0};

  double utilization_out_ = 62.0;
  double memory_percent_out_ = 40.0;
  double power_out_ = 8.0;
  double temperature_out_ = 53.0;
  int process_count_ = 1;
};

} // namespace edgeml::sim

#endif
