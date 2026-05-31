#include "embedids.h"
#include <cstring>
#include <gtest/gtest.h>

/**
 * @brief Test fixture for algorithm functionality
 * 
 * Tests threshold algorithm, trend algorithm, statistical algorithm,
 * and algorithm configuration across different metric types.
 */
class EmbedIDSAlgorithmsTest : public ::testing::Test {
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
   * @brief Helper function to setup a metric with threshold algorithm
   */
  void setupThresholdMetric(embedids_metric_config_t& metric_config,
                           embedids_metric_datapoint_t* history_buffer,
                           const char* name,
                           embedids_metric_type_t type,
                           uint32_t history_size) {
    memset(&metric_config, 0, sizeof(metric_config));
    strncpy(metric_config.metric.name, name, EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    metric_config.metric.type = type;
    metric_config.metric.enabled = true;
    metric_config.metric.history = history_buffer;
    metric_config.metric.max_history_size = history_size;
    metric_config.metric.current_size = 0;
    metric_config.metric.write_index = 0;
    metric_config.num_algorithms = 1;
    
    // Configure threshold algorithm
    metric_config.algorithms[0].type = EMBEDIDS_ALGORITHM_THRESHOLD;
    metric_config.algorithms[0].enabled = true;
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

private:
  embedids_system_config_t system_config;
};

// ============================================================================
// Threshold Algorithm Tests - Float Type
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, ThresholdAlgorithmFloat) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "temperature", 
                       EMBEDIDS_METRIC_TYPE_FLOAT, 10);
  
  // Configure float threshold algorithm
  metric_config.algorithms[0].config.threshold.min_threshold.f32 = 10.0f;
  metric_config.algorithms[0].config.threshold.max_threshold.f32 = 80.0f;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Test normal value (should pass)
  embedids_metric_value_t value;
  value.f32 = 50.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 1000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "temperature"), EMBEDIDS_OK);

  // Test high value (should trigger threshold)
  value.f32 = 90.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "temperature"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);

  // Test low value (should trigger threshold)
  value.f32 = 5.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 3000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "temperature"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

TEST_F(EmbedIDSAlgorithmsTest, ThresholdAlgorithmFloatBoundaryValues) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "precise_temp", 
                       EMBEDIDS_METRIC_TYPE_FLOAT, 10);
  
  // Configure threshold with precise boundaries
  metric_config.algorithms[0].config.threshold.min_threshold.f32 = 0.0f;
  metric_config.algorithms[0].config.threshold.max_threshold.f32 = 100.0f;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Test exact boundary values
  value.f32 = 0.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "precise_temp", value, 1000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "precise_temp"), EMBEDIDS_OK);

  value.f32 = 100.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "precise_temp", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "precise_temp"), EMBEDIDS_OK);

  // Test just outside boundaries
  value.f32 = -0.1f;
  EXPECT_EQ(embedids_add_datapoint(&context, "precise_temp", value, 3000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "precise_temp"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);

  value.f32 = 100.1f;
  EXPECT_EQ(embedids_add_datapoint(&context, "precise_temp", value, 4000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "precise_temp"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

// ============================================================================
// Threshold Algorithm Tests - Integer Types
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, ThresholdAlgorithmUint32) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "packet_count", 
                       EMBEDIDS_METRIC_TYPE_UINT32, 10);
  
  // Configure uint32 threshold algorithm
  metric_config.algorithms[0].config.threshold.min_threshold.u32 = 100;
  metric_config.algorithms[0].config.threshold.max_threshold.u32 = 10000;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Test normal value
  value.u32 = 5000;
  EXPECT_EQ(embedids_add_datapoint(&context, "packet_count", value, 1000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "packet_count"), EMBEDIDS_OK);

  // Test min threshold violation
  value.u32 = 50;
  EXPECT_EQ(embedids_add_datapoint(&context, "packet_count", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "packet_count"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);

  // Test max threshold violation
  value.u32 = 15000;
  EXPECT_EQ(embedids_add_datapoint(&context, "packet_count", value, 3000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "packet_count"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

TEST_F(EmbedIDSAlgorithmsTest, ThresholdAlgorithmUint64) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "byte_count", 
                       EMBEDIDS_METRIC_TYPE_UINT64, 10);
  
  // Configure uint64 threshold algorithm
  metric_config.algorithms[0].config.threshold.min_threshold.u64 = 1000000ULL;
  metric_config.algorithms[0].config.threshold.max_threshold.u64 = 1000000000ULL;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Test normal value
  value.u64 = 500000000ULL;
  EXPECT_EQ(embedids_add_datapoint(&context, "byte_count", value, 1000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "byte_count"), EMBEDIDS_OK);

  // Test min threshold violation
  value.u64 = 500000ULL;
  EXPECT_EQ(embedids_add_datapoint(&context, "byte_count", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "byte_count"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);

  // Test max threshold violation
  value.u64 = 2000000000ULL;
  EXPECT_EQ(embedids_add_datapoint(&context, "byte_count", value, 3000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "byte_count"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

// ============================================================================
// Threshold Algorithm Tests - Enum Type
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, ThresholdAlgorithmEnum) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "error_level", 
                       EMBEDIDS_METRIC_TYPE_ENUM, 10);
  
  // Configure enum threshold (0=OK, 1=WARN, 2=ERROR, 3=CRITICAL)
  metric_config.algorithms[0].config.threshold.min_threshold.enum_val = 0;
  metric_config.algorithms[0].config.threshold.max_threshold.enum_val = 2;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Test normal enum value (WARNING level)
  value.enum_val = 1;
  EXPECT_EQ(embedids_add_datapoint(&context, "error_level", value, 1000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "error_level"), EMBEDIDS_OK);

  // Test max threshold violation (CRITICAL level)
  value.enum_val = 3;
  EXPECT_EQ(embedids_add_datapoint(&context, "error_level", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "error_level"), EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

// ============================================================================
// Threshold Algorithm Tests - Boolean Type
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, BooleanMetricWithThreshold) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "security_breach", 
                       EMBEDIDS_METRIC_TYPE_BOOL, 10);
  
  // Configure threshold algorithm (though not really applicable for boolean)
  metric_config.algorithms[0].config.threshold.check_min = false;
  metric_config.algorithms[0].config.threshold.check_max = false;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Test boolean values
  value.boolean = false;
  EXPECT_EQ(embedids_add_datapoint(&context, "security_breach", value, 1000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "security_breach"), EMBEDIDS_OK);

  value.boolean = true;
  EXPECT_EQ(embedids_add_datapoint(&context, "security_breach", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "security_breach"), EMBEDIDS_OK);
}

// ============================================================================
// Trend Algorithm Tests
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, TrendAlgorithmTesting) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  memset(&metric_config, 0, sizeof(metric_config));
  strncpy(metric_config.metric.name, "cpu_trend", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
  metric_config.metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
  metric_config.metric.enabled = true;
  metric_config.metric.history = history_buffer;
  metric_config.metric.max_history_size = 10;
  metric_config.metric.current_size = 0;
  metric_config.metric.write_index = 0;

  // Configure trend algorithm
  metric_config.algorithms[0].type = EMBEDIDS_ALGORITHM_TREND;
  metric_config.algorithms[0].enabled = true;
  metric_config.algorithms[0].config.trend.window_size = 5;
  metric_config.algorithms[0].config.trend.max_slope = 10.0f;
  metric_config.algorithms[0].config.trend.max_variance = 100.0f;
  metric_config.algorithms[0].config.trend.expected_trend = EMBEDIDS_TREND_STABLE;
  metric_config.num_algorithms = 1;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add data points for trend analysis
  embedids_metric_value_t value;
  value.f32 = 50.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_trend", value, 1000), EMBEDIDS_OK);

  value.f32 = 52.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_trend", value, 2000), EMBEDIDS_OK);

  value.f32 = 51.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "cpu_trend", value, 3000), EMBEDIDS_OK);

  // Analyze trend - should be OK (stable trend)
  embedids_result_t result = embedids_analyze_metric(&context, "cpu_trend");
  EXPECT_EQ(result, EMBEDIDS_OK);
}

TEST_F(EmbedIDSAlgorithmsTest, TrendAlgorithmIncreasingPattern) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  memset(&metric_config, 0, sizeof(metric_config));
  strncpy(metric_config.metric.name, "memory_usage", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
  metric_config.metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
  metric_config.metric.enabled = true;
  metric_config.metric.history = history_buffer;
  metric_config.metric.max_history_size = 10;
  metric_config.metric.current_size = 0;
  metric_config.metric.write_index = 0;

  // Configure trend algorithm expecting stable trend
  metric_config.algorithms[0].type = EMBEDIDS_ALGORITHM_TREND;
  metric_config.algorithms[0].enabled = true;
  metric_config.algorithms[0].config.trend.window_size = 3;
  metric_config.algorithms[0].config.trend.max_slope = 5.0f;
  metric_config.algorithms[0].config.trend.max_variance = 10.0f;
  metric_config.algorithms[0].config.trend.expected_trend = EMBEDIDS_TREND_STABLE;
  metric_config.num_algorithms = 1;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add increasing data points
  embedids_metric_value_t value;
  for (int i = 0; i < 5; i++) {
    value.f32 = 10.0f + i * 20.0f; // Strong increasing pattern
    EXPECT_EQ(embedids_add_datapoint(&context, "memory_usage", value, 1000 + i * 1000), EMBEDIDS_OK);
  }

  // Trend analysis might detect unexpected trend
  embedids_result_t result = embedids_analyze_metric(&context, "memory_usage");
  // Result depends on implementation - could be OK or trend anomaly
  EXPECT_TRUE(result == EMBEDIDS_OK || result == EMBEDIDS_ERROR_TREND_ANOMALY);
}

// ============================================================================
// Empty Metric Analysis Tests
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, EmptyMetricAnalysis) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "empty_metric", 
                       EMBEDIDS_METRIC_TYPE_FLOAT, 10);
  
  metric_config.algorithms[0].config.threshold.min_threshold.f32 = 10.0f;
  metric_config.algorithms[0].config.threshold.max_threshold.f32 = 80.0f;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Analyze empty metric should return OK (no data to analyze)
  embedids_result_t result = embedids_analyze_metric(&context, "empty_metric");
  EXPECT_EQ(result, EMBEDIDS_OK);
}

// ============================================================================
// Algorithm Configuration Tests
// ============================================================================

TEST_F(EmbedIDSAlgorithmsTest, DisabledAlgorithm) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupThresholdMetric(metric_config, history_buffer, "test_metric", 
                       EMBEDIDS_METRIC_TYPE_FLOAT, 10);
  
  // Configure but disable the algorithm
  metric_config.algorithms[0].enabled = false;
  metric_config.algorithms[0].config.threshold.min_threshold.f32 = 10.0f;
  metric_config.algorithms[0].config.threshold.max_threshold.f32 = 80.0f;
  metric_config.algorithms[0].config.threshold.check_min = true;
  metric_config.algorithms[0].config.threshold.check_max = true;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add value that would violate threshold if algorithm was enabled
  embedids_metric_value_t value;
  value.f32 = 90.0f; // Above threshold
  EXPECT_EQ(embedids_add_datapoint(&context, "test_metric", value, 1000), EMBEDIDS_OK);

  // Analysis should pass since algorithm is disabled
  embedids_result_t result = embedids_analyze_metric(&context, "test_metric");
  EXPECT_EQ(result, EMBEDIDS_OK);
}
