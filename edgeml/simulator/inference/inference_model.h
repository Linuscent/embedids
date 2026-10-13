/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_INFERENCE_INFERENCE_MODEL_H
#define EDGEML_SIMULATOR_INFERENCE_INFERENCE_MODEL_H

#include <map>
#include <string>

#include "simulator/common/model_context.h"

namespace edgeml::sim {

/** Computer-vision inference service (FPS, latency, errors, health). */
class InferenceModel {
public:
  void update(const ModelContext &ctx);
  void write(std::map<std::string, double> &out) const;
  void reset();

private:
  SmoothedValue fps_{30.0, 2.0};
  double fps_out_ = 30.0;
  double latency_out_ = 30.0;
  double errors_out_ = 0.0;
  double health_out_ = 1.0;
};

} // namespace edgeml::sim

#endif
