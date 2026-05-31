/*
 * Copyright 2026 Linuscent Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "embedids.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Helper function to find a metric by name */
static embedids_metric_config_t *find_metric_config(const embedids_context_t *context, const char *metric_name) {
  if (!context || !context->system_config) {
    return NULL;
  }

  for (uint32_t i = 0; i < context->system_config->num_active_metrics;
       i++) {
    embedids_metric_config_t *config =
        &context->system_config->metrics[i];
    if (strncmp(config->metric.name, metric_name,
                EMBEDIDS_MAX_METRIC_NAME_LEN) == 0) {
      return config;
    }
  }
  return NULL;
}

/* Built-in threshold algorithm implementation */
static embedids_result_t
run_threshold_algorithm(const embedids_metric_t *metric,
                        const embedids_threshold_config_t *config) {
  if (metric->current_size == 0) {
    return EMBEDIDS_OK; // No data to analyze
  }

  // Get the most recent value
  uint32_t latest_index = (metric->write_index > 0)
                              ? metric->write_index - 1
                              : metric->max_history_size - 1;
  embedids_metric_value_t latest_value = metric->history[latest_index].value;

  // Check thresholds based on metric type
  switch (metric->type) {
  case EMBEDIDS_METRIC_TYPE_UINT32:
    if (config->check_min && latest_value.u32 < config->min_threshold.u32) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    if (config->check_max && latest_value.u32 > config->max_threshold.u32) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    break;
  case EMBEDIDS_METRIC_TYPE_UINT64:
    if (config->check_min && latest_value.u64 < config->min_threshold.u64) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    if (config->check_max && latest_value.u64 > config->max_threshold.u64) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    break;
#if EMBEDIDS_ENABLE_FLOATING_POINT
  case EMBEDIDS_METRIC_TYPE_FLOAT:
  case EMBEDIDS_METRIC_TYPE_PERCENTAGE:
  case EMBEDIDS_METRIC_TYPE_RATE:
    if (config->check_min && latest_value.f32 < config->min_threshold.f32) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    if (config->check_max && latest_value.f32 > config->max_threshold.f32) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    break;
#if EMBEDIDS_ENABLE_DOUBLE_PRECISION
  case EMBEDIDS_METRIC_TYPE_DOUBLE:
    if (config->check_min && latest_value.f64 < config->min_threshold.f64) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    if (config->check_max && latest_value.f64 > config->max_threshold.f64) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    break;
#endif
#endif
  case EMBEDIDS_METRIC_TYPE_BOOL:
    // Boolean metrics don't use threshold comparison
    break;
  case EMBEDIDS_METRIC_TYPE_ENUM:
    // Enum metrics could use discrete value checking
    if (config->check_min &&
        latest_value.enum_val < config->min_threshold.enum_val) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    if (config->check_max &&
        latest_value.enum_val > config->max_threshold.enum_val) {
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    break;
  }

  return EMBEDIDS_OK;
}

/* Built-in trend algorithm implementation */
static embedids_result_t
run_trend_algorithm(const embedids_metric_t *metric,
                    const embedids_trend_config_t *config) {
  if (metric->current_size < config->window_size || config->window_size < 2) {
    return EMBEDIDS_OK; // Not enough data for trend analysis
  }

  // TODO: Implement full trend analysis using linear regression
  // This is a placeholder that needs proper implementation
  (void)metric;  // Suppress unused parameter warning
  (void)config;  // Suppress unused parameter warning
  
  return EMBEDIDS_OK;
}

embedids_result_t embedids_init(embedids_context_t *context, const embedids_system_config_t *config) {
  if (context == NULL || config == NULL) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  context->system_config = (embedids_system_config_t *)config;
  context->initialized = true;

  return EMBEDIDS_OK;
}

embedids_result_t embedids_add_datapoint(embedids_context_t *context, const char *metric_name,
                                         embedids_metric_value_t value,
                                         uint64_t timestamp_ms) {
  if (!context || !context->initialized) {
    return EMBEDIDS_ERROR_NOT_INITIALIZED;
  }

  if (metric_name == NULL) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  embedids_metric_config_t *config = find_metric_config(context, metric_name);
  if (config == NULL) {
    return EMBEDIDS_ERROR_METRIC_NOT_FOUND;
  }

  embedids_metric_t *metric = &config->metric;
  if (!metric->enabled) {
    return EMBEDIDS_ERROR_METRIC_DISABLED;
  }

  if (metric->history == NULL) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  // Add data point to circular buffer
  metric->history[metric->write_index].value = value;
  metric->history[metric->write_index].timestamp_ms = timestamp_ms;

  // Update buffer state
  metric->write_index = (metric->write_index + 1) % metric->max_history_size;
  if (metric->current_size < metric->max_history_size) {
    metric->current_size++;
  }

  return EMBEDIDS_OK;
}

embedids_result_t embedids_analyze_all(embedids_context_t *context) {
  if (!context || !context->initialized) {
    return EMBEDIDS_ERROR_NOT_INITIALIZED;
  }

  // Analyze all active metrics
  for (uint32_t i = 0; i < context->system_config->num_active_metrics;
       i++) {
    embedids_metric_config_t *config =
        &context->system_config->metrics[i];
    if (config->metric.enabled) {
      embedids_result_t result = embedids_analyze_metric(context, config->metric.name);
      if (result != EMBEDIDS_OK) {
        return result; // Return first anomaly detected
      }
    }
  }

  return EMBEDIDS_OK;
}

embedids_result_t embedids_analyze_metric(embedids_context_t *context, const char *metric_name) {
  if (!context || !context->initialized) {
    return EMBEDIDS_ERROR_NOT_INITIALIZED;
  }

  if (metric_name == NULL) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  embedids_metric_config_t *config = find_metric_config(context, metric_name);
  if (config == NULL) {
    return EMBEDIDS_ERROR_METRIC_NOT_FOUND;
  }

  if (!config->metric.enabled) {
    return EMBEDIDS_ERROR_METRIC_DISABLED;
  }

  // Run all algorithms for this metric
  for (uint32_t i = 0; i < config->num_algorithms; i++) {
    embedids_algorithm_t *algorithm = &config->algorithms[i];
    if (!algorithm->enabled) {
      continue;
    }

    embedids_result_t result = EMBEDIDS_OK;

    switch (algorithm->type) {
    case EMBEDIDS_ALGORITHM_THRESHOLD:
      result = run_threshold_algorithm(&config->metric,
                                       &algorithm->config.threshold);
      break;
    case EMBEDIDS_ALGORITHM_TREND:
      result = run_trend_algorithm(&config->metric, &algorithm->config.trend);
      break;
    case EMBEDIDS_ALGORITHM_CUSTOM:
      if (algorithm->config.custom.function) {
        result = algorithm->config.custom.function(
            &config->metric, algorithm->config.custom.config,
            algorithm->config.custom.context);
      }
      break;
    }

    if (result != EMBEDIDS_OK) {
      return result; // Return first error detected
    }
  }

  return EMBEDIDS_OK;
}

void embedids_cleanup(embedids_context_t *context) {
  if (context) {
    memset(context, 0, sizeof(embedids_context_t));
  }
}

embedids_result_t embedids_get_trend(embedids_context_t *context, const char *metric_name,
                                     embedids_trend_t *trend) {
  if (!context || !context->initialized) {
    return EMBEDIDS_ERROR_NOT_INITIALIZED;
  }

  if (metric_name == NULL || trend == NULL) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  embedids_metric_config_t *config = find_metric_config(context, metric_name);
  if (config == NULL) {
    return EMBEDIDS_ERROR_METRIC_NOT_FOUND;
  }

  if (!config->metric.enabled) {
    return EMBEDIDS_ERROR_METRIC_DISABLED;
  }

  embedids_metric_t *metric = &config->metric;
  
  // Need at least 2 data points for trend analysis
  if (metric->current_size < 2) {
    *trend = EMBEDIDS_TREND_STABLE;
    return EMBEDIDS_OK;
  }

  // For better trend analysis, calculate slope using linear regression approach
  // but with a simplified version for embedded systems
  
  // Get the range of data to analyze
  uint32_t start_index = (metric->current_size == metric->max_history_size) 
                        ? metric->write_index 
                        : 0;
  
  uint32_t num_points = metric->current_size < 3 ? metric->current_size : 3; // Use last 3 points for trend
  
  float sum_change = 0.0f;
  uint32_t changes = 0;
  
  // Calculate average change between consecutive points
  for (uint32_t i = 1; i < num_points; i++) {
    uint32_t curr_idx = (start_index + i) % metric->max_history_size;
    uint32_t prev_idx = (start_index + i - 1) % metric->max_history_size;
    
    float curr_val, prev_val;
    
    // Extract values based on metric type
    switch (metric->type) {
    case EMBEDIDS_METRIC_TYPE_UINT32:
      curr_val = (float)metric->history[curr_idx].value.u32;
      prev_val = (float)metric->history[prev_idx].value.u32;
      break;
    case EMBEDIDS_METRIC_TYPE_UINT64:
      curr_val = (float)metric->history[curr_idx].value.u64;
      prev_val = (float)metric->history[prev_idx].value.u64;
      break;
#if EMBEDIDS_ENABLE_FLOATING_POINT
    case EMBEDIDS_METRIC_TYPE_FLOAT:
    case EMBEDIDS_METRIC_TYPE_PERCENTAGE:
    case EMBEDIDS_METRIC_TYPE_RATE:
      curr_val = metric->history[curr_idx].value.f32;
      prev_val = metric->history[prev_idx].value.f32;
      break;
#endif
    default:
      *trend = EMBEDIDS_TREND_STABLE;
      return EMBEDIDS_OK;
    }
    
    sum_change += curr_val - prev_val;
    changes++;
  }
  
  if (changes == 0) {
    *trend = EMBEDIDS_TREND_STABLE;
    return EMBEDIDS_OK;
  }
  
  float avg_change = sum_change / changes;
  
  // Define threshold for what constitutes a trend vs stable
  // For small changes (< 5% of first value), consider stable
  uint32_t first_idx = (metric->current_size == metric->max_history_size) 
                      ? metric->write_index 
                      : 0;
  
  float first_val;
  switch (metric->type) {
  case EMBEDIDS_METRIC_TYPE_UINT32:
    first_val = (float)metric->history[first_idx].value.u32;
    break;
  case EMBEDIDS_METRIC_TYPE_UINT64:
    first_val = (float)metric->history[first_idx].value.u64;
    break;
#if EMBEDIDS_ENABLE_FLOATING_POINT
  case EMBEDIDS_METRIC_TYPE_FLOAT:
  case EMBEDIDS_METRIC_TYPE_PERCENTAGE:
  case EMBEDIDS_METRIC_TYPE_RATE:
    first_val = metric->history[first_idx].value.f32;
    break;
#endif
  default:
    first_val = 1.0f; // fallback
    break;
  }
  
  float threshold = fabsf(first_val) * 0.05f; // 5% threshold
  if (threshold < 1.0f) threshold = 1.0f; // minimum threshold
  
  if (fabsf(avg_change) < threshold) {
    *trend = EMBEDIDS_TREND_STABLE;
  } else if (avg_change > 0) {
    *trend = EMBEDIDS_TREND_INCREASING;
  } else {
    *trend = EMBEDIDS_TREND_DECREASING;
  }

  return EMBEDIDS_OK;
}

const char *embedids_get_version(void) { return EMBEDIDS_VERSION_STRING; }

bool embedids_is_initialized(const embedids_context_t *context) { 
  return context ? context->initialized : false; 
}

embedids_result_t
embedids_validate_config(const embedids_system_config_t *config) {
  if (!config) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  if (!config->metrics) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }

  if (config->max_metrics == 0 || config->max_metrics > EMBEDIDS_MAX_METRICS) {
    return EMBEDIDS_ERROR_CONFIG_INVALID;
  }

  return EMBEDIDS_OK;
}

const char *embedids_get_error_string(embedids_result_t error_code) {
  switch (error_code) {
  case EMBEDIDS_OK:
    return "Success";
  case EMBEDIDS_ERROR_INVALID_PARAM:
    return "Invalid parameter";
  case EMBEDIDS_ERROR_NOT_INITIALIZED:
    return "Library not initialized";
  case EMBEDIDS_ERROR_ALREADY_INITIALIZED:
    return "Library already initialized";
  case EMBEDIDS_ERROR_CONFIG_INVALID:
    return "Invalid configuration";
  case EMBEDIDS_ERROR_OUT_OF_MEMORY:
    return "Out of memory";
  case EMBEDIDS_ERROR_BUFFER_FULL:
    return "Buffer full";
  case EMBEDIDS_ERROR_BUFFER_CORRUPT:
    return "Buffer corrupted";
  case EMBEDIDS_ERROR_ALIGNMENT_ERROR:
    return "Memory alignment error";
  case EMBEDIDS_ERROR_METRIC_NOT_FOUND:
    return "Metric not found";
  case EMBEDIDS_ERROR_METRIC_DISABLED:
    return "Metric disabled";
  case EMBEDIDS_ERROR_METRIC_TYPE_MISMATCH:
    return "Metric type mismatch";
  case EMBEDIDS_ERROR_METRIC_NAME_TOO_LONG:
    return "Metric name too long";
  case EMBEDIDS_ERROR_ALGORITHM_FAILED:
    return "Algorithm failed";
  case EMBEDIDS_ERROR_ALGORITHM_NOT_SUPPORTED:
    return "Algorithm not supported";
  case EMBEDIDS_ERROR_CUSTOM_ALGORITHM_NULL:
    return "Custom algorithm is null";
  case EMBEDIDS_ERROR_THRESHOLD_EXCEEDED:
    return "Threshold exceeded";
  case EMBEDIDS_ERROR_TREND_ANOMALY:
    return "Trend anomaly detected";
  case EMBEDIDS_ERROR_CUSTOM_DETECTION:
    return "Custom detection triggered";
  case EMBEDIDS_ERROR_STATISTICAL_ANOMALY:
    return "Statistical anomaly detected";
  case EMBEDIDS_ERROR_TIMEOUT:
    return "Operation timeout";
  case EMBEDIDS_ERROR_HARDWARE_FAULT:
    return "Hardware fault";
  case EMBEDIDS_ERROR_TIMESTAMP_INVALID:
    return "Invalid timestamp";
  case EMBEDIDS_ERROR_THREAD_UNSAFE:
    return "Thread safety violation";
  default:
    return "Unknown error";
  }
}

embedids_result_t embedids_reset_all_metrics(embedids_context_t *context) {
  if (!context || !context->initialized) {
    return EMBEDIDS_ERROR_NOT_INITIALIZED;
  }

  // Reset all metric histories
  for (uint32_t i = 0; i < context->system_config->num_active_metrics;
       i++) {
    embedids_metric_t *metric =
        &context->system_config->metrics[i].metric;
    metric->current_size = 0;
    metric->write_index = 0;

    // Clear the history buffer if it exists
    if (metric->history) {
      memset(metric->history, 0,
             metric->max_history_size * sizeof(embedids_metric_datapoint_t));
    }
  }

  return EMBEDIDS_OK;
}
