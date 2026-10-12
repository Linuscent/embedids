/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_WORKLOAD_SYSTEM_MODEL_H
#define EDGEML_SIMULATOR_WORKLOAD_SYSTEM_MODEL_H

#include <map>
#include <string>

#include "simulator/common/model_context.h"

namespace edgeml::sim {

/** CPU, memory, load and SoC temperature of the simulated device. */
class SystemModel {
public:
  void update(const ModelContext &ctx);
  void write(std::map<std::string, double> &out) const;
  void reset();

private:
  SmoothedValue cpu_{42.0, 2.0};
  SmoothedValue memory_{40.0, 4.0};
  SmoothedValue load_{2.3, 15.0};
  SmoothedValue temperature_{52.0, 25.0};
  double cpu_out_ = 42.0;
  double memory_out_ = 40.0;
  double temperature_out_ = 52.0;
};

} // namespace edgeml::sim

#endif
