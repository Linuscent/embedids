/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bridge between normalized telemetry and the EmbedIDS C core.
 *
 * Each "signal" is a condition on one metric (e.g. gpu.utilization > 85 for
 * 10 s). Signals are registered as EmbedIDS algorithms: instantaneous
 * conditions use the built-in threshold algorithm, sustained conditions use a
 * custom algorithm that walks the EmbedIDS history ring buffer. All buffers
 * are allocated once in finalize(); the per-cycle path does not allocate
 * inside the core.
 */

#ifndef EDGEML_CORE_EMBEDIDS_SIGNAL_ENGINE_H
#define EDGEML_CORE_EMBEDIDS_SIGNAL_ENGINE_H

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/schema/telemetry.h"
#include "embedids.h"

namespace edgeml {

enum class Comparison { Greater, GreaterEqual, Less, LessEqual };

const char *to_string(Comparison comparison);

struct SignalSpec {
  std::string id;
  std::string metric;
  Comparison comparison = Comparison::Greater;
  double threshold = 0.0;
  uint32_t sustain_ms = 0;
};

struct SignalState {
  bool active = false;
  bool has_data = false;
  double last_value = 0.0;
  uint64_t condition_since_ms = 0; /**< start of the current violating streak */
  uint64_t active_since_ms = 0;
};

class SignalEngine {
public:
  explicit SignalEngine(uint32_t history_size = 64);
  ~SignalEngine();

  SignalEngine(const SignalEngine &) = delete;
  SignalEngine &operator=(const SignalEngine &) = delete;

  /** Register a signal. Must be called before finalize(). */
  bool add_signal(const SignalSpec &spec, std::string *error = nullptr);

  /** Allocate EmbedIDS buffers and initialize the core context. */
  bool finalize(std::string *error = nullptr);

  /** Push the latest value of every registered metric present in the batch. */
  void ingest(const TelemetryBatch &batch);

  /** Run EmbedIDS analysis and update every signal state. */
  void evaluate(uint64_t now_ms);

  bool active(const std::string &signal_id) const;
  const SignalState *state(const std::string &signal_id) const;
  const SignalSpec *spec(const std::string &signal_id) const;
  const std::vector<SignalSpec> &signals() const { return specs_; }

  /** Clear EmbedIDS history and signal state. */
  void reset();

  size_t metric_count() const { return metric_names_.size(); }
  uint32_t history_size() const { return history_size_; }

  /** Context passed to the sustained-condition EmbedIDS custom algorithm. */
  struct SustainedCondition {
    Comparison comparison;
    float threshold;
    uint32_t sustain_ms;
    uint64_t streak_start_ms;
    bool streak_valid;
  };

private:
  struct SignalBinding {
    size_t metric_index;
    size_t algorithm_index;
    SustainedCondition *condition; // null for plain thresholds
  };

  uint32_t history_size_;
  bool finalized_ = false;

  std::vector<SignalSpec> specs_;
  std::map<std::string, size_t> spec_index_;
  std::vector<SignalState> states_;
  std::vector<SignalBinding> bindings_;

  std::vector<std::string> metric_names_;
  std::map<std::string, size_t> metric_index_;

  // EmbedIDS-owned memory (user-managed, fixed after finalize()).
  std::vector<embedids_metric_config_t> metric_configs_;
  std::vector<std::vector<embedids_metric_datapoint_t>> histories_;
  std::deque<SustainedCondition> conditions_;
  embedids_system_config_t system_config_{};
  embedids_context_t context_{};
};

} // namespace edgeml

#endif
