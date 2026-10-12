/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/inference/inference_model.h"

#include "core/schema/telemetry.h"

namespace edgeml::sim {

void InferenceModel::update(const ModelContext &ctx) {
  const auto &attacks = ctx.attacks;
  double target = 30.0 + 0.8 * std::sin(ctx.elapsed_seconds / 45.0 * 6.283);
  if (attacks.has(Attack::InferenceDegradation)) {
    target = std::min(target, 14.0);
  }
  if (attacks.has(Attack::Cryptominer)) {
    target = std::min(target, 11.0);
  }
  if (attacks.has(Attack::DenialOfService)) {
    target = std::min(target, 5.0);
  }
  fps_.update(target, ctx.dt_seconds);

  const bool disturbed = !attacks.empty();
  fps_out_ =
      clamp(fps_.value() + ctx.rng.gaussian(disturbed ? 0.9 : 0.6), 0.5, 60.0);
  latency_out_ =
      clamp(1000.0 / fps_out_ * 0.95 + ctx.rng.gaussian(1.2), 5.0, 2000.0);

  if (attacks.has(Attack::ModelTamper)) {
    // A modified engine produces malformed outputs.
    errors_out_ = std::round(ctx.rng.uniform(3.0, 6.0));
  } else {
    errors_out_ = ctx.rng.chance(0.03) ? 1.0 : 0.0;
  }
  health_out_ = attacks.has(Attack::DenialOfService) ? 0.0 : 1.0;
}

void InferenceModel::write(std::map<std::string, double> &out) const {
  out[metrics::kInferenceRate] = fps_out_;
  out[metrics::kInferenceLatency] = latency_out_;
  out[metrics::kInferenceErrors] = errors_out_;
  out[metrics::kPipelineHealth] = health_out_;
}

void InferenceModel::reset() { fps_.reset(); }

} // namespace edgeml::sim
