/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_COMMON_SIM_SIGNAL_H
#define EDGEML_SIMULATOR_COMMON_SIM_SIGNAL_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace edgeml::sim {

/** Seeded random source so every scenario is reproducible. */
class SimRandom {
public:
  explicit SimRandom(uint32_t seed) : engine_(seed) {}
  void reseed(uint32_t seed) { engine_.seed(seed); }
  double gaussian(double stddev) {
    if (stddev <= 0) {
      return 0.0;
    }
    std::normal_distribution<double> dist(0.0, stddev);
    return dist(engine_);
  }
  double uniform(double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(engine_);
  }
  bool chance(double probability) { return uniform(0.0, 1.0) < probability; }

private:
  std::mt19937 engine_;
};

/**
 * First-order lag towards a target with measurement noise, so attack effects
 * ramp up realistically instead of jumping.
 */
class SmoothedValue {
public:
  SmoothedValue(double initial, double tau_seconds)
      : value_(initial), initial_(initial), tau_(tau_seconds) {}

  double update(double target, double dt_seconds) {
    const double alpha = 1.0 - std::exp(-dt_seconds / std::max(tau_, 1e-3));
    value_ += (target - value_) * alpha;
    return value_;
  }
  double value() const { return value_; }
  void reset() { value_ = initial_; }

private:
  double value_;
  double initial_;
  double tau_;
};

inline double clamp(double value, double lo, double hi) {
  return std::min(hi, std::max(lo, value));
}

} // namespace edgeml::sim

#endif
