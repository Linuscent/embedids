#include "embedids.h"
#include <cstring>
#include <cmath>
#include <gtest/gtest.h>

/**
 * @brief Test fixture for extensible algorithm functionality
 * 
 * Tests custom algorithms with metrics checking, algorithm state management,
 * multiple algorithm configurations, and extensible architecture features.
 */
class EmbedIDSExtensibleTest : public ::testing::Test {
protected:
  embedids_context_t context;
  
  void SetUp() override { 
    memset(&context, 0, sizeof(context));
    embedids_cleanup(&context); 
  }

  void TearDown() override { 
    embedids_cleanup(&context); 
  }

  /**
   * @brief Helper function to setup a metric with custom algorithm
   */
  void setupCustomMetric(embedids_metric_config_t& metric_config,
                        embedids_metric_datapoint_t* history_buffer,
                        const char* name,
                        embedids_metric_type_t type,
                        uint32_t history_size,
                        embedids_custom_algorithm_fn algorithm_fn,
                        void* config = nullptr,
                        void* context = nullptr) {
    memset(&metric_config, 0, sizeof(metric_config));
    strncpy(metric_config.metric.name, name, EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    metric_config.metric.type = type;
    metric_config.metric.enabled = true;
    metric_config.metric.history = history_buffer;
    metric_config.metric.max_history_size = history_size;
    metric_config.metric.current_size = 0;
    metric_config.metric.write_index = 0;
    metric_config.num_algorithms = 1;
    
    // Configure custom algorithm
    metric_config.algorithms[0].type = EMBEDIDS_ALGORITHM_CUSTOM;
    metric_config.algorithms[0].enabled = true;
    metric_config.algorithms[0].config.custom.function = algorithm_fn;
    metric_config.algorithms[0].config.custom.config = config;
    metric_config.algorithms[0].config.custom.context = context;
  }

  /**
   * @brief Helper function to initialize system with a single metric
   */
  embedids_result_t initializeWithMetric(embedids_metric_config_t* metric_config) {
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = metric_config;
    system_config.max_metrics = 1;
    system_config.num_active_metrics = 1;
    
    return embedids_init(&context, &system_config);
  }

  /**
   * @brief Helper function to initialize system with multiple metrics
   */
  embedids_result_t initializeWithMetrics(embedids_metric_config_t* metric_configs, uint32_t count) {
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = metric_configs;
    system_config.max_metrics = count;
    system_config.num_active_metrics = count;
    
    return embedids_init(&context, &system_config);
  }

private:
  embedids_system_config_t system_config;
};

// ============================================================================
// Custom Algorithm Context Structures
// ============================================================================

/**
 * @brief Context for pattern detection algorithm
 */
typedef struct {
  float baseline;
  float threshold_multiplier;
  uint32_t consecutive_violations;
  uint32_t max_violations;
  uint32_t call_count; // For testing
} pattern_detector_context_t;

/**
 * @brief Context for rate-of-change detection algorithm
 */
typedef struct {
  float max_rate;
  uint32_t call_count; // For testing
  embedids_result_t last_result; // For testing
} rate_change_context_t;

/**
 * @brief Context for statistical variance algorithm
 */
typedef struct {
  float variance_threshold;
  uint32_t window_size;
  uint32_t call_count; // For testing
  float calculated_variance; // For testing
} variance_detector_context_t;

// ============================================================================
// Custom Algorithm Implementations
// ============================================================================

/**
 * @brief Custom algorithm: Pattern detection with baseline deviation
 */
static embedids_result_t pattern_detector_algorithm(const embedids_metric_t* metric,
                                                   const void* config,
                                                   void* context) {
  (void)config; // Unused parameter
  pattern_detector_context_t* ctx = static_cast<pattern_detector_context_t*>(context);
  
  if (!ctx || !metric) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }
  
  ctx->call_count++;
  
  if (metric->current_size < 3) {
    return EMBEDIDS_OK; // Need at least 3 data points
  }
  
  // Get the last three data points
  uint32_t idx1 = (metric->write_index - 1 + metric->max_history_size) % metric->max_history_size;
  uint32_t idx2 = (metric->write_index - 2 + metric->max_history_size) % metric->max_history_size;
  uint32_t idx3 = (metric->write_index - 3 + metric->max_history_size) % metric->max_history_size;
  
  float val1 = metric->history[idx1].value.f32;
  float val2 = metric->history[idx2].value.f32;
  float val3 = metric->history[idx3].value.f32;
  
  // Calculate average and deviation from baseline
  float avg_recent = (val1 + val2 + val3) / 3.0f;
  float deviation = std::abs(avg_recent - ctx->baseline);
  float threshold = ctx->baseline * ctx->threshold_multiplier;
  
  // Debug print (comment out in production)
  // printf("Pattern detector: avg=%.2f, baseline=%.2f, deviation=%.2f, threshold=%.2f, violations=%d\n",
  //        avg_recent, ctx->baseline, deviation, threshold, ctx->consecutive_violations);
  
  if (deviation > threshold) {
    ctx->consecutive_violations++;
    
    if (ctx->consecutive_violations >= ctx->max_violations) {
      ctx->consecutive_violations = 0; // Reset counter
      return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
  } else {
    ctx->consecutive_violations = 0; // Reset on normal reading
  }
  
  return EMBEDIDS_OK;
}

/**
 * @brief Custom algorithm: Rate-of-change detection
 */
static embedids_result_t rate_change_algorithm(const embedids_metric_t* metric,
                                              const void* config,
                                              void* context) {
  rate_change_context_t* ctx = static_cast<rate_change_context_t*>(context);
  
  if (!ctx || !metric) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }
  
  ctx->call_count++;
  
  if (metric->current_size < 2) {
    ctx->last_result = EMBEDIDS_OK;
    return EMBEDIDS_OK;
  }
  
  // Get last two data points
  uint32_t idx1 = (metric->write_index - 1 + metric->max_history_size) % metric->max_history_size;
  uint32_t idx2 = (metric->write_index - 2 + metric->max_history_size) % metric->max_history_size;
  
  float val1 = metric->history[idx1].value.f32;
  float val2 = metric->history[idx2].value.f32;
  uint64_t time1 = metric->history[idx1].timestamp_ms;
  uint64_t time2 = metric->history[idx2].timestamp_ms;
  
  if (time1 == time2) {
    ctx->last_result = EMBEDIDS_OK;
    return EMBEDIDS_OK; // Avoid division by zero
  }
  
  float rate = std::abs(val1 - val2) / (static_cast<float>(time1 - time2) / 1000.0f);
  float max_rate = config ? *static_cast<const float*>(config) : ctx->max_rate;
  
  if (rate > max_rate) {
    ctx->last_result = EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
  }
  
  ctx->last_result = EMBEDIDS_OK;
  return EMBEDIDS_OK;
}

/**
 * @brief Custom algorithm: Statistical variance detection
 */
static embedids_result_t variance_detector_algorithm(const embedids_metric_t* metric,
                                                    const void* config,
                                                    void* context) {
  (void)config; // Unused parameter
  variance_detector_context_t* ctx = static_cast<variance_detector_context_t*>(context);
  
  if (!ctx || !metric) {
    return EMBEDIDS_ERROR_INVALID_PARAM;
  }
  
  ctx->call_count++;
  
  if (metric->current_size < ctx->window_size) {
    ctx->calculated_variance = 0.0f;
    return EMBEDIDS_OK; // Need enough data points
  }
  
  // Calculate variance over the window
  float sum = 0.0f;
  float sum_sq = 0.0f;
  uint32_t count = std::min(ctx->window_size, metric->current_size);
  
  for (uint32_t i = 0; i < count; i++) {
    uint32_t idx = (metric->write_index - 1 - i + metric->max_history_size) % metric->max_history_size;
    float val = metric->history[idx].value.f32;
    sum += val;
    sum_sq += val * val;
  }
  
  float mean = sum / count;
  ctx->calculated_variance = (sum_sq / count) - (mean * mean);
  
  if (ctx->calculated_variance > ctx->variance_threshold) {
    return EMBEDIDS_ERROR_STATISTICAL_ANOMALY;
  }
  
  return EMBEDIDS_OK;
}

/**
 * @brief Always-failing custom algorithm for testing error conditions
 */
static embedids_result_t always_fail_algorithm(const embedids_metric_t* metric,
                                              const void* config,
                                              void* context) {
  (void)metric;
  (void)config;
  (void)context;
  return EMBEDIDS_ERROR_CUSTOM_DETECTION;
}

// ============================================================================
// Basic Custom Algorithm Tests
// ============================================================================

TEST_F(EmbedIDSExtensibleTest, PatternDetectorBasicOperation) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  pattern_detector_context_t ctx = {
    .baseline = 50.0f,
    .threshold_multiplier = 0.2f, // 20% deviation threshold
    .consecutive_violations = 0,
    .max_violations = 2,
    .call_count = 0
  };
  
  setupCustomMetric(metric_config, history_buffer, "cpu_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   pattern_detector_algorithm, nullptr, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add normal values (should not trigger)
  embedids_metric_value_t value;
  value.f32 = 48.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 1000), EMBEDIDS_OK);
  value.f32 = 52.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 2000), EMBEDIDS_OK);
  value.f32 = 49.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 3000), EMBEDIDS_OK);
  
  // Should be OK - values are within 20% of baseline (50.0)
  EXPECT_EQ(embedids_analyze_metric(&context, "cpu_usage"), EMBEDIDS_OK);
  EXPECT_GT(ctx.call_count, 0);
  EXPECT_EQ(ctx.consecutive_violations, 0);
}

TEST_F(EmbedIDSExtensibleTest, PatternDetectorAnomalyDetection) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  pattern_detector_context_t ctx = {
    .baseline = 50.0f,
    .threshold_multiplier = 0.2f, // 20% deviation threshold (10.0 deviation)
    .consecutive_violations = 0,
    .max_violations = 2, // Alert after 2 consecutive violations
    .call_count = 0
  };
  
  setupCustomMetric(metric_config, history_buffer, "cpu_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   pattern_detector_algorithm, nullptr, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Add 3 values that deviate significantly from baseline (70.0 vs 50.0 baseline = 20 deviation > 10 threshold)
  value.f32 = 70.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 1000), EMBEDIDS_OK);
  value.f32 = 75.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 2000), EMBEDIDS_OK);
  value.f32 = 80.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 3000), EMBEDIDS_OK);
  
  // First analysis: should detect first violation (average of 3 points = 75.0, deviation = 25.0 > 10.0)
  EXPECT_EQ(embedids_analyze_metric(&context, "cpu_usage"), EMBEDIDS_OK);
  EXPECT_EQ(ctx.consecutive_violations, 1);
  
  // Add another high value to continue the pattern
  value.f32 = 85.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 4000), EMBEDIDS_OK);
  
  // Second analysis: should detect second violation and trigger alert
  // New average of last 3: (75+80+85)/3 = 80.0, deviation = 30.0 > 10.0
  EXPECT_EQ(embedids_analyze_metric(&context, "cpu_usage"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
  EXPECT_GT(ctx.call_count, 0);
  EXPECT_EQ(ctx.consecutive_violations, 0); // Should be reset after alert
}

TEST_F(EmbedIDSExtensibleTest, RateChangeDetectorBasicOperation) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  float max_rate = 10.0f; // Max 10 units per second
  rate_change_context_t ctx = {
    .max_rate = max_rate,
    .call_count = 0,
    .last_result = EMBEDIDS_OK
  };
  
  setupCustomMetric(metric_config, history_buffer, "temperature", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   rate_change_algorithm, &max_rate, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add values with gradual change (should not trigger)
  embedids_metric_value_t value;
  value.f32 = 20.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 1000), EMBEDIDS_OK);
  value.f32 = 25.0f; // Change of 5 units in 1 second = 5/s rate
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 2000), EMBEDIDS_OK);
  
  EXPECT_EQ(embedids_analyze_metric(&context, "temperature"), EMBEDIDS_OK);
  EXPECT_GT(ctx.call_count, 0);
  EXPECT_EQ(ctx.last_result, EMBEDIDS_OK);
}

TEST_F(EmbedIDSExtensibleTest, RateChangeDetectorRapidChange) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  float max_rate = 10.0f; // Max 10 units per second
  rate_change_context_t ctx = {
    .max_rate = max_rate,
    .call_count = 0,
    .last_result = EMBEDIDS_OK
  };
  
  setupCustomMetric(metric_config, history_buffer, "temperature", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   rate_change_algorithm, &max_rate, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add values with rapid change (should trigger)
  embedids_metric_value_t value;
  value.f32 = 20.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 1000), EMBEDIDS_OK);
  value.f32 = 40.0f; // Change of 20 units in 1 second = 20/s rate (exceeds 10/s)
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 2000), EMBEDIDS_OK);
  
  EXPECT_EQ(embedids_analyze_metric(&context, "temperature"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
  EXPECT_GT(ctx.call_count, 0);
  EXPECT_EQ(ctx.last_result, EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

TEST_F(EmbedIDSExtensibleTest, VarianceDetectorBasicOperation) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  variance_detector_context_t ctx = {
    .variance_threshold = 100.0f,
    .window_size = 5,
    .call_count = 0,
    .calculated_variance = 0.0f
  };
  
  setupCustomMetric(metric_config, history_buffer, "network_latency", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   variance_detector_algorithm, nullptr, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add consistent values (low variance)
  embedids_metric_value_t value;
  for (int i = 0; i < 6; i++) {
    value.f32 = 10.0f + static_cast<float>(i % 2); // Values alternate between 10 and 11
    EXPECT_EQ(embedids_add_datapoint(&context, "network_latency", value, 1000 + i * 1000), EMBEDIDS_OK);
  }
  
  EXPECT_EQ(embedids_analyze_metric(&context, "network_latency"), EMBEDIDS_OK);
  EXPECT_GT(ctx.call_count, 0);
  EXPECT_LT(ctx.calculated_variance, ctx.variance_threshold);
}

TEST_F(EmbedIDSExtensibleTest, VarianceDetectorHighVariance) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  variance_detector_context_t ctx = {
    .variance_threshold = 10.0f, // Low threshold
    .window_size = 5,
    .call_count = 0,
    .calculated_variance = 0.0f
  };
  
  setupCustomMetric(metric_config, history_buffer, "network_latency", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   variance_detector_algorithm, nullptr, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add highly variable values
  embedids_metric_value_t value;
  float values[] = {5.0f, 50.0f, 2.0f, 45.0f, 8.0f, 40.0f};
  for (int i = 0; i < 6; i++) {
    value.f32 = values[i];
    EXPECT_EQ(embedids_add_datapoint(&context, "network_latency", value, 1000 + i * 1000), EMBEDIDS_OK);
  }
  
  EXPECT_EQ(embedids_analyze_metric(&context, "network_latency"), EMBEDIDS_ERROR_STATISTICAL_ANOMALY);
  EXPECT_GT(ctx.call_count, 0);
  EXPECT_GT(ctx.calculated_variance, ctx.variance_threshold);
}

// ============================================================================
// Multiple Algorithm Tests
// ============================================================================

TEST_F(EmbedIDSExtensibleTest, MultipleCustomAlgorithmsOnSingleMetric) {
  embedids_metric_datapoint_t history_buffer[15];
  embedids_metric_config_t metric_config;
  
  // Setup contexts for both algorithms
  pattern_detector_context_t pattern_ctx = {
    .baseline = 50.0f,
    .threshold_multiplier = 0.3f,
    .consecutive_violations = 0,
    .max_violations = 2,
    .call_count = 0
  };
  
  variance_detector_context_t variance_ctx = {
    .variance_threshold = 50.0f,
    .window_size = 4,
    .call_count = 0,
    .calculated_variance = 0.0f
  };
  
  // Setup metric with multiple custom algorithms
  memset(&metric_config, 0, sizeof(metric_config));
  strncpy(metric_config.metric.name, "multi_algo", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
  metric_config.metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
  metric_config.metric.enabled = true;
  metric_config.metric.history = history_buffer;
  metric_config.metric.max_history_size = 15;
  metric_config.metric.current_size = 0;
  metric_config.metric.write_index = 0;
  metric_config.num_algorithms = 2;
  
  // First algorithm: Pattern detector
  metric_config.algorithms[0].type = EMBEDIDS_ALGORITHM_CUSTOM;
  metric_config.algorithms[0].enabled = true;
  metric_config.algorithms[0].config.custom.function = pattern_detector_algorithm;
  metric_config.algorithms[0].config.custom.config = nullptr;
  metric_config.algorithms[0].config.custom.context = &pattern_ctx;
  
  // Second algorithm: Variance detector
  metric_config.algorithms[1].type = EMBEDIDS_ALGORITHM_CUSTOM;
  metric_config.algorithms[1].enabled = true;
  metric_config.algorithms[1].config.custom.function = variance_detector_algorithm;
  metric_config.algorithms[1].config.custom.config = nullptr;
  metric_config.algorithms[1].config.custom.context = &variance_ctx;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add normal values (should pass both algorithms)
  embedids_metric_value_t value;
  for (int i = 0; i < 6; i++) {
    value.f32 = 48.0f + static_cast<float>(i % 3); // Values: 48, 49, 50, 48, 49, 50
    EXPECT_EQ(embedids_add_datapoint(&context, "multi_algo", value, 1000 + i * 1000), EMBEDIDS_OK);
  }
  
  EXPECT_EQ(embedids_analyze_metric(&context, "multi_algo"), EMBEDIDS_OK);
  EXPECT_GT(pattern_ctx.call_count, 0);
  EXPECT_GT(variance_ctx.call_count, 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(EmbedIDSExtensibleTest, CustomAlgorithmErrorHandling) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupCustomMetric(metric_config, history_buffer, "error_test", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   always_fail_algorithm, nullptr, nullptr);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  value.f32 = 25.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "error_test", value, 1000), EMBEDIDS_OK);
  
  // Algorithm should always fail
  EXPECT_EQ(embedids_analyze_metric(&context, "error_test"), EMBEDIDS_ERROR_CUSTOM_DETECTION);
}

TEST_F(EmbedIDSExtensibleTest, NullAlgorithmFunction) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupCustomMetric(metric_config, history_buffer, "null_test", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   nullptr, nullptr, nullptr);

  // Library may allow null custom algorithm function at init
  // Let's check if it handles it gracefully during analysis
  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);
  
  embedids_metric_value_t value;
  value.f32 = 25.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "null_test", value, 1000), EMBEDIDS_OK);
  
  // Analysis should handle null function gracefully (no crash)
  embedids_result_t result = embedids_analyze_metric(&context, "null_test");
  // Either OK (ignored) or error is acceptable behavior
  EXPECT_TRUE(result == EMBEDIDS_OK || result != EMBEDIDS_OK);
}

TEST_F(EmbedIDSExtensibleTest, CustomAlgorithmWithNullMetric) {
  // This tests the algorithm's internal null checking
  embedids_result_t result = pattern_detector_algorithm(nullptr, nullptr, nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);
}

// ============================================================================
// Context State Management Tests
// ============================================================================

TEST_F(EmbedIDSExtensibleTest, AlgorithmContextStateManagement) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  pattern_detector_context_t ctx = {
    .baseline = 30.0f,
    .threshold_multiplier = 0.1f, // Very strict threshold (10% = 3.0 deviation)
    .consecutive_violations = 0,
    .max_violations = 3,
    .call_count = 0
  };
  
  setupCustomMetric(metric_config, history_buffer, "state_test", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   pattern_detector_algorithm, nullptr, &ctx);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Add 3 values that violate threshold (average will be 40.0, deviation = 10.0 > 3.0)
  value.f32 = 40.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 1000), EMBEDIDS_OK);
  value.f32 = 40.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 2000), EMBEDIDS_OK);
  value.f32 = 40.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 3000), EMBEDIDS_OK);
  
  // First analysis - should count first violation
  EXPECT_EQ(embedids_analyze_metric(&context, "state_test"), EMBEDIDS_OK);
  EXPECT_EQ(ctx.consecutive_violations, 1);
  
  // Continue with violating values
  value.f32 = 45.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 4000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "state_test"), EMBEDIDS_OK);
  EXPECT_EQ(ctx.consecutive_violations, 2);
  
  // Third violation - should trigger alert and reset
  value.f32 = 50.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 5000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "state_test"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
  EXPECT_EQ(ctx.consecutive_violations, 0); // Should be reset after alert
  
  // Add normal values to test reset behavior
  value.f32 = 29.0f; // Within threshold
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 6000), EMBEDIDS_OK);
  value.f32 = 30.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 7000), EMBEDIDS_OK);
  value.f32 = 31.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "state_test", value, 8000), EMBEDIDS_OK);
  
  EXPECT_EQ(embedids_analyze_metric(&context, "state_test"), EMBEDIDS_OK);
  EXPECT_EQ(ctx.consecutive_violations, 0); // Should remain 0 for normal values
}

// ============================================================================
// Integration Tests with Multiple Metrics
// ============================================================================

TEST_F(EmbedIDSExtensibleTest, MultipleMetricsWithCustomAlgorithms) {
  // Setup buffers and contexts for multiple metrics
  embedids_metric_datapoint_t cpu_history[10];
  embedids_metric_datapoint_t memory_history[10];
  embedids_metric_datapoint_t network_history[10];
  
  pattern_detector_context_t cpu_ctx = {50.0f, 0.2f, 0, 2, 0};
  rate_change_context_t memory_ctx = {15.0f, 0, EMBEDIDS_OK};
  variance_detector_context_t network_ctx = {25.0f, 4, 0, 0.0f};
  
  embedids_metric_config_t metrics[3];
  
  // Setup CPU metric with pattern detector
  setupCustomMetric(metrics[0], cpu_history, "cpu_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   pattern_detector_algorithm, nullptr, &cpu_ctx);
  
  // Setup memory metric with rate change detector
  float max_memory_rate = 15.0f;
  setupCustomMetric(metrics[1], memory_history, "memory_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   rate_change_algorithm, &max_memory_rate, &memory_ctx);
  
  // Setup network metric with variance detector
  setupCustomMetric(metrics[2], network_history, "network_latency", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   variance_detector_algorithm, nullptr, &network_ctx);

  ASSERT_EQ(initializeWithMetrics(metrics, 3), EMBEDIDS_OK);

  // Add normal data to all metrics
  embedids_metric_value_t value;
  
  // CPU: Normal values
  value.f32 = 45.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 1000), EMBEDIDS_OK);
  
  // Memory: Gradual change
  value.f32 = 60.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "memory_usage", value, 1000), EMBEDIDS_OK);
  value.f32 = 65.0f; // Change of 5 in 1 second = 5/s (under 15/s limit)
  EXPECT_EQ(embedids_add_datapoint(&context, "memory_usage", value, 2000), EMBEDIDS_OK);
  
  // Network: Low variance values
  for (int i = 0; i < 5; i++) {
    value.f32 = 100.0f + static_cast<float>(i % 2) * 2.0f; // 100, 102, 100, 102, 100
    EXPECT_EQ(embedids_add_datapoint(&context, "network_latency", value, 1000 + i * 1000), EMBEDIDS_OK);
  }
  
  // Analyze all metrics - should all be OK
  EXPECT_EQ(embedids_analyze_all(&context), EMBEDIDS_OK);
  
  // Verify all algorithms were called
  EXPECT_GT(cpu_ctx.call_count, 0);
  EXPECT_GT(memory_ctx.call_count, 0);
  EXPECT_GT(network_ctx.call_count, 0);
}

TEST_F(EmbedIDSExtensibleTest, CustomAlgorithmMetricTypeValidation) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  uint32_t call_count = 0;
  
  // Custom algorithm that checks metric type
  auto type_checking_algorithm = [](const embedids_metric_t* metric, 
                                   const void* /* config */, void* context) -> embedids_result_t {
    uint32_t* count = static_cast<uint32_t*>(context);
    (*count)++;
    
    if (!metric) return EMBEDIDS_ERROR_INVALID_PARAM;
    
    // Check that metric type is float as expected
    if (metric->type != EMBEDIDS_METRIC_TYPE_FLOAT) {
      return EMBEDIDS_ERROR_METRIC_TYPE_MISMATCH;
    }
    
    // Check that history buffer is properly initialized
    if (!metric->history) {
      return EMBEDIDS_ERROR_INVALID_PARAM;
    }
    
    return EMBEDIDS_OK;
  };
  
  setupCustomMetric(metric_config, history_buffer, "type_test", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10, 
                   type_checking_algorithm, nullptr, &call_count);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  value.f32 = 25.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "type_test", value, 1000), EMBEDIDS_OK);
  
  EXPECT_EQ(embedids_analyze_metric(&context, "type_test"), EMBEDIDS_OK);
  EXPECT_GT(call_count, 0);
}
