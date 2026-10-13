/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/embedids/signal_engine.h"

#include <algorithm>
#include <cstring>

namespace edgeml {

namespace {

bool violates(Comparison comparison, float value, float threshold) {
  switch (comparison) {
  case Comparison::Greater:
    return value > threshold;
  case Comparison::GreaterEqual:
    return value >= threshold;
  case Comparison::Less:
    return value < threshold;
  case Comparison::LessEqual:
    return value <= threshold;
  }
  return false;
}

/*
 * EmbedIDS custom algorithm: the condition must hold for every sample in a
 * streak that spans at least sustain_ms, ending at the newest sample.
 */
embedids_result_t sustained_condition_algorithm(const embedids_metric_t *metric,
                                                const void *config,
                                                void *context) {
  (void)config;
  auto *condition = static_cast<SignalEngine::SustainedCondition *>(context);
  condition->streak_valid = false;
  if (metric->current_size == 0) {
    return EMBEDIDS_OK;
  }

  const uint32_t capacity = metric->max_history_size;
  const uint32_t newest = (metric->write_index + capacity - 1) % capacity;
  const uint64_t newest_ts = metric->history[newest].timestamp_ms;

  uint64_t streak_start = 0;
  bool streak = false;
  for (uint32_t i = 0; i < metric->current_size; i++) {
    const uint32_t index = (newest + capacity - i) % capacity;
    const embedids_metric_datapoint_t &point = metric->history[index];
    if (!violates(condition->comparison, point.value.f32,
                  condition->threshold)) {
      break;
    }
    streak = true;
    streak_start = point.timestamp_ms;
  }

  if (!streak) {
    return EMBEDIDS_OK;
  }
  condition->streak_valid = true;
  condition->streak_start_ms = streak_start;
  if (newest_ts - streak_start >= condition->sustain_ms) {
    return EMBEDIDS_ERROR_CUSTOM_DETECTION;
  }
  return EMBEDIDS_OK;
}

} // namespace

const char *to_string(Comparison comparison) {
  switch (comparison) {
  case Comparison::Greater:
    return ">";
  case Comparison::GreaterEqual:
    return ">=";
  case Comparison::Less:
    return "<";
  case Comparison::LessEqual:
    return "<=";
  }
  return "?";
}

SignalEngine::SignalEngine(uint32_t history_size)
    : history_size_(history_size < 4 ? 4 : history_size) {}

SignalEngine::~SignalEngine() { embedids_cleanup(&context_); }

bool SignalEngine::add_signal(const SignalSpec &spec, std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error) {
      *error = message;
    }
    return false;
  };
  if (finalized_) {
    return fail("signals must be added before finalize()");
  }
  if (spec.id.empty() || spec.metric.empty()) {
    return fail("signal id and metric are required");
  }
  if (spec.metric.size() >= EMBEDIDS_MAX_METRIC_NAME_LEN) {
    return fail("metric name too long for EmbedIDS: " + spec.metric);
  }
  if (spec_index_.count(spec.id)) {
    return fail("duplicate signal id: " + spec.id);
  }

  auto metric_it = metric_index_.find(spec.metric);
  size_t metric_index;
  if (metric_it == metric_index_.end()) {
    if (metric_names_.size() >= EMBEDIDS_MAX_METRICS) {
      return fail("too many metrics for EmbedIDS");
    }
    metric_index = metric_names_.size();
    metric_names_.push_back(spec.metric);
    metric_index_[spec.metric] = metric_index;
  } else {
    metric_index = metric_it->second;
  }

  size_t algorithms_on_metric = 0;
  for (const auto &binding : bindings_) {
    if (binding.metric_index == metric_index) {
      algorithms_on_metric++;
    }
  }
  if (algorithms_on_metric >= EMBEDIDS_MAX_ALGORITHMS_PER_METRIC) {
    return fail("too many signals on metric " + spec.metric);
  }

  spec_index_[spec.id] = specs_.size();
  specs_.push_back(spec);
  states_.emplace_back();
  bindings_.push_back({metric_index, algorithms_on_metric, nullptr});
  return true;
}

bool SignalEngine::finalize(std::string *error) {
  if (finalized_) {
    return true;
  }

  metric_configs_.assign(metric_names_.size(), embedids_metric_config_t{});
  histories_.assign(metric_names_.size(),
                    std::vector<embedids_metric_datapoint_t>(history_size_));

  for (size_t i = 0; i < metric_names_.size(); i++) {
    embedids_metric_config_t &config = metric_configs_[i];
    std::memset(&config, 0, sizeof(config));
    std::strncpy(config.metric.name, metric_names_[i].c_str(),
                 EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    config.metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
    config.metric.history = histories_[i].data();
    config.metric.max_history_size = history_size_;
    config.metric.enabled = true;
  }

  for (size_t s = 0; s < specs_.size(); s++) {
    const SignalSpec &spec = specs_[s];
    SignalBinding &binding = bindings_[s];
    embedids_metric_config_t &config = metric_configs_[binding.metric_index];
    embedids_algorithm_t &algorithm =
        config.algorithms[binding.algorithm_index];
    algorithm.enabled = true;

    const bool plain_threshold =
        spec.sustain_ms == 0 && (spec.comparison == Comparison::Greater ||
                                 spec.comparison == Comparison::Less);
    if (plain_threshold) {
      algorithm.type = EMBEDIDS_ALGORITHM_THRESHOLD;
      if (spec.comparison == Comparison::Greater) {
        algorithm.config.threshold.check_max = true;
        algorithm.config.threshold.max_threshold.f32 =
            static_cast<float>(spec.threshold);
      } else {
        algorithm.config.threshold.check_min = true;
        algorithm.config.threshold.min_threshold.f32 =
            static_cast<float>(spec.threshold);
      }
    } else {
      conditions_.push_back({spec.comparison,
                             static_cast<float>(spec.threshold),
                             spec.sustain_ms, 0, false});
      binding.condition = &conditions_.back();
      algorithm.type = EMBEDIDS_ALGORITHM_CUSTOM;
      algorithm.config.custom.function = sustained_condition_algorithm;
      algorithm.config.custom.config = nullptr;
      algorithm.config.custom.context = binding.condition;
    }
    config.num_algorithms =
        std::max<uint32_t>(config.num_algorithms,
                           static_cast<uint32_t>(binding.algorithm_index + 1));
  }

  system_config_.metrics = metric_configs_.data();
  system_config_.max_metrics = static_cast<uint32_t>(metric_configs_.size());
  system_config_.num_active_metrics = system_config_.max_metrics;
  system_config_.user_context = this;

  if (metric_configs_.empty()) {
    finalized_ = true;
    return true;
  }

  embedids_result_t result = embedids_validate_config(&system_config_);
  if (result == EMBEDIDS_OK) {
    result = embedids_init(&context_, &system_config_);
  }
  if (result != EMBEDIDS_OK) {
    if (error) {
      *error = std::string("EmbedIDS init failed: ") +
               embedids_get_error_string(result);
    }
    return false;
  }
  finalized_ = true;
  return true;
}

void SignalEngine::ingest(const TelemetryBatch &batch) {
  if (!finalized_ || metric_configs_.empty()) {
    return;
  }
  for (size_t i = 0; i < metric_names_.size(); i++) {
    const MetricSample *sample = batch.find(metric_names_[i]);
    if (sample == nullptr) {
      continue;
    }
    embedids_metric_value_t value;
    value.f32 = static_cast<float>(sample->value);
    const uint64_t ts =
        sample->timestamp_ms != 0 ? sample->timestamp_ms : batch.timestamp_ms;
    embedids_add_datapoint(&context_, metric_names_[i].c_str(), value, ts);
  }
}

void SignalEngine::evaluate(uint64_t now_ms) {
  if (!finalized_ || metric_configs_.empty()) {
    return;
  }

  embedids_result_t results[EMBEDIDS_MAX_ALGORITHMS_PER_METRIC];
  for (size_t m = 0; m < metric_names_.size(); m++) {
    const embedids_metric_t &metric = metric_configs_[m].metric;
    for (auto &result : results) {
      result = EMBEDIDS_OK;
    }
    uint32_t count = 0;
    if (metric.current_size > 0) {
      embedids_analyze_metric_detailed(
          &context_, metric_names_[m].c_str(), results,
          EMBEDIDS_MAX_ALGORITHMS_PER_METRIC, &count);
    }

    for (size_t s = 0; s < specs_.size(); s++) {
      const SignalBinding &binding = bindings_[s];
      if (binding.metric_index != m) {
        continue;
      }
      SignalState &state = states_[s];
      state.has_data = metric.current_size > 0;
      if (!state.has_data) {
        state.active = false;
        continue;
      }
      const uint32_t newest =
          (metric.write_index + metric.max_history_size - 1) %
          metric.max_history_size;
      state.last_value = metric.history[newest].value.f32;

      const bool now_active = results[binding.algorithm_index] != EMBEDIDS_OK;
      if (binding.condition != nullptr) {
        state.condition_since_ms = binding.condition->streak_valid
                                       ? binding.condition->streak_start_ms
                                       : 0;
      } else {
        state.condition_since_ms =
            now_active ? (state.active ? state.condition_since_ms : now_ms) : 0;
      }
      if (now_active && !state.active) {
        state.active_since_ms = now_ms;
      }
      if (!now_active) {
        state.active_since_ms = 0;
      }
      state.active = now_active;
    }
  }
}

bool SignalEngine::active(const std::string &signal_id) const {
  const SignalState *s = state(signal_id);
  return s != nullptr && s->active;
}

const SignalState *SignalEngine::state(const std::string &signal_id) const {
  auto it = spec_index_.find(signal_id);
  return it == spec_index_.end() ? nullptr : &states_[it->second];
}

const SignalSpec *SignalEngine::spec(const std::string &signal_id) const {
  auto it = spec_index_.find(signal_id);
  return it == spec_index_.end() ? nullptr : &specs_[it->second];
}

void SignalEngine::reset() {
  if (finalized_ && !metric_configs_.empty()) {
    embedids_reset_all_metrics(&context_);
  }
  for (auto &state : states_) {
    state = SignalState{};
  }
  for (auto &condition : conditions_) {
    condition.streak_valid = false;
    condition.streak_start_ms = 0;
  }
}

} // namespace edgeml
