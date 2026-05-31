#include "embedids.h"
#include <cstring>
#include <gtest/gtest.h>

/**
 * @brief Test fixture for analysis functionality
 * 
 * Tests analysis operations, trend analysis, multiple metric analysis,
 * and analysis result interpretation.
 */
class EmbedIDSAnalysisTest : public ::testing::Test {
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
   * @brief Helper function to setup a basic metric configuration
   */
  void setupBasicMetric(embedids_metric_config_t& metric_config, 
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
// Basic Analysis Operations Tests
// ============================================================================

TEST_F(EmbedIDSAnalysisTest, AnalyzeValidMetric) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "memory_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add some data points
  embedids_metric_value_t value;
  value.f32 = 45.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "memory_usage", value, 1000), EMBEDIDS_OK);

  // Test analyze specific metric
  embedids_result_t result = embedids_analyze_metric(&context, "memory_usage");
  EXPECT_EQ(result, EMBEDIDS_OK);
}

TEST_F(EmbedIDSAnalysisTest, AnalyzeNonexistentMetric) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "memory_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Test analyze non-existent metric
  embedids_result_t result = embedids_analyze_metric(&context, "nonexistent_metric");
  EXPECT_EQ(result, EMBEDIDS_ERROR_METRIC_NOT_FOUND);
}

TEST_F(EmbedIDSAnalysisTest, AnalyzeAllMetrics) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "memory_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add some data points
  embedids_metric_value_t value;
  value.f32 = 45.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "memory_usage", value, 1000), EMBEDIDS_OK);

  // Test analyze all metrics
  embedids_result_t result = embedids_analyze_all(&context);
  EXPECT_EQ(result, EMBEDIDS_OK);
}

// ============================================================================
// Trend Analysis Tests
// ============================================================================

TEST_F(EmbedIDSAnalysisTest, TrendAnalysisBasic) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "network_traffic", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add some data points for trend analysis
  embedids_metric_value_t value;
  value.f32 = 50.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "network_traffic", value, 1000), EMBEDIDS_OK);

  value.f32 = 52.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "network_traffic", value, 2000), EMBEDIDS_OK);

  value.f32 = 51.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "network_traffic", value, 3000), EMBEDIDS_OK);

  // Test trend analysis
  embedids_trend_t trend;
  embedids_result_t result = embedids_get_trend(&context, "network_traffic", &trend);
  EXPECT_EQ(result, EMBEDIDS_OK);
  EXPECT_EQ(trend, EMBEDIDS_TREND_STABLE);
}

TEST_F(EmbedIDSAnalysisTest, TrendAnalysisIncreasing) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "cpu_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add increasing data points
  embedids_metric_value_t value;
  for (int i = 0; i < 5; i++) {
    value.f32 = 10.0f + i * 10.0f;
    EXPECT_EQ(embedids_add_datapoint(&context, "cpu_usage", value, 1000 + i * 1000), EMBEDIDS_OK);
  }

  // Test trend analysis
  embedids_trend_t trend;
  embedids_result_t result = embedids_get_trend(&context, "cpu_usage", &trend);
  EXPECT_EQ(result, EMBEDIDS_OK);
  EXPECT_EQ(trend, EMBEDIDS_TREND_INCREASING);
}

TEST_F(EmbedIDSAnalysisTest, TrendAnalysisDecreasing) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "battery_level", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add decreasing data points
  embedids_metric_value_t value;
  for (int i = 0; i < 5; i++) {
    value.f32 = 100.0f - i * 10.0f;
    EXPECT_EQ(embedids_add_datapoint(&context, "battery_level", value, 1000 + i * 1000), EMBEDIDS_OK);
  }

  // Test trend analysis
  embedids_trend_t trend;
  embedids_result_t result = embedids_get_trend(&context, "battery_level", &trend);
  EXPECT_EQ(result, EMBEDIDS_OK);
  EXPECT_EQ(trend, EMBEDIDS_TREND_DECREASING);
}

TEST_F(EmbedIDSAnalysisTest, TrendAnalysisWithInsufficientData) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "sparse_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add only one data point
  embedids_metric_value_t value;
  value.f32 = 50.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "sparse_metric", value, 1000), EMBEDIDS_OK);

  // Test trend analysis with insufficient data
  embedids_trend_t trend;
  embedids_result_t result = embedids_get_trend(&context, "sparse_metric", &trend);
  EXPECT_EQ(result, EMBEDIDS_OK);
  // With insufficient data, trend should be stable
  EXPECT_EQ(trend, EMBEDIDS_TREND_STABLE);
}

// ============================================================================
// Multiple Metrics Analysis Tests
// ============================================================================

TEST_F(EmbedIDSAnalysisTest, MultipleMetricsAnalysisAllNormal) {
  // Setup multiple metrics
  embedids_metric_datapoint_t history_buffer1[5];
  embedids_metric_datapoint_t history_buffer2[5];

  embedids_metric_config_t metric_configs[2];
  
  // Configure first metric (normal)
  setupBasicMetric(metric_configs[0], history_buffer1, "metric1", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 5);

  // Configure second metric (normal)
  setupBasicMetric(metric_configs[1], history_buffer2, "metric2", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 5);

  // Setup system configuration
  embedids_system_config_t system_config;
  memset(&system_config, 0, sizeof(system_config));
  system_config.metrics = metric_configs;
  system_config.max_metrics = 2;
  system_config.num_active_metrics = 2;

  ASSERT_EQ(embedids_init(&context, &system_config), EMBEDIDS_OK);

  // Add normal data to both metrics
  embedids_metric_value_t value;
  value.f32 = 25.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "metric1", value, 1000), EMBEDIDS_OK);

  value.f32 = 30.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "metric2", value, 1000), EMBEDIDS_OK);

  // Analyze all should be OK
  embedids_result_t result = embedids_analyze_all(&context);
  EXPECT_EQ(result, EMBEDIDS_OK);
}

TEST_F(EmbedIDSAnalysisTest, MultipleMetricsAnalysisWithThresholdViolation) {
  // Setup multiple metrics
  embedids_metric_datapoint_t history_buffer1[5];
  embedids_metric_datapoint_t history_buffer2[5];

  embedids_metric_config_t metric_configs[2];
  
  // Configure first metric (normal)
  setupBasicMetric(metric_configs[0], history_buffer1, "metric1", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 5);

  // Configure second metric with threshold algorithm
  setupBasicMetric(metric_configs[1], history_buffer2, "metric2", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 5);
  
  // Add threshold algorithm to second metric
  metric_configs[1].algorithms[0].type = EMBEDIDS_ALGORITHM_THRESHOLD;
  metric_configs[1].algorithms[0].enabled = true;
  metric_configs[1].algorithms[0].config.threshold.max_threshold.f32 = 50.0f;
  metric_configs[1].algorithms[0].config.threshold.check_max = true;
  metric_configs[1].algorithms[0].config.threshold.check_min = false;
  metric_configs[1].num_algorithms = 1;

  // Setup system configuration
  embedids_system_config_t system_config;
  memset(&system_config, 0, sizeof(system_config));
  system_config.metrics = metric_configs;
  system_config.max_metrics = 2;
  system_config.num_active_metrics = 2;

  ASSERT_EQ(embedids_init(&context, &system_config), EMBEDIDS_OK);

  // Add normal data to first metric
  embedids_metric_value_t value;
  value.f32 = 25.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "metric1", value, 1000), EMBEDIDS_OK);

  // Add normal data to second metric
  value.f32 = 30.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "metric2", value, 1000), EMBEDIDS_OK);

  // Analyze all should be OK initially
  embedids_result_t result = embedids_analyze_all(&context);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Add threshold-violating data to second metric
  value.f32 = 75.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "metric2", value, 2000), EMBEDIDS_OK);

  // Analyze all should detect the violation
  result = embedids_analyze_all(&context);
  EXPECT_EQ(result, EMBEDIDS_ERROR_THRESHOLD_EXCEEDED);
}

// ============================================================================
// Analysis Error Handling Tests
// ============================================================================

TEST_F(EmbedIDSAnalysisTest, AnalysisParameterValidation) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "test_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Test null metric name for analysis
  embedids_result_t result = embedids_analyze_metric(&context, nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);

  // Test null parameters for trend analysis
  embedids_trend_t trend;
  result = embedids_get_trend(&context, nullptr, &trend);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);

  result = embedids_get_trend(&context, "test_metric", nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);
}

// ============================================================================
// Analysis with Different Metric States Tests
// ============================================================================

TEST_F(EmbedIDSAnalysisTest, AnalysisOfDisabledMetric) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "disabled_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);
  
  // Disable the metric
  metric_config.metric.enabled = false;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Analysis of disabled metric might behave differently
  embedids_result_t result = embedids_analyze_metric(&context, "disabled_metric");
  // Result depends on implementation - should either skip or return error
  EXPECT_TRUE(result == EMBEDIDS_OK || result == EMBEDIDS_ERROR_METRIC_DISABLED);
}

TEST_F(EmbedIDSAnalysisTest, AnalysisOfEmptyMetric) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "empty_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Don't add any data points - analyze empty metric
  embedids_result_t result = embedids_analyze_metric(&context, "empty_metric");
  EXPECT_EQ(result, EMBEDIDS_OK); // Should handle empty metrics gracefully

  // Test trend analysis on empty metric
  embedids_trend_t trend;
  result = embedids_get_trend(&context, "empty_metric", &trend);
  EXPECT_EQ(result, EMBEDIDS_OK);
  EXPECT_EQ(trend, EMBEDIDS_TREND_STABLE);
}

// ============================================================================
// Complex Analysis Scenarios Tests
// ============================================================================

TEST_F(EmbedIDSAnalysisTest, AnalysisWithMixedMetricTypes) {
  // Setup mixed metric types
  embedids_metric_datapoint_t history_buffer1[5];
  embedids_metric_datapoint_t history_buffer2[5];
  embedids_metric_datapoint_t history_buffer3[5];

  embedids_metric_config_t metric_configs[3];
  
  // Float metric
  setupBasicMetric(metric_configs[0], history_buffer1, "temperature", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 5);

  // Integer metric
  setupBasicMetric(metric_configs[1], history_buffer2, "count", 
                   EMBEDIDS_METRIC_TYPE_UINT32, 5);

  // Boolean metric
  setupBasicMetric(metric_configs[2], history_buffer3, "status", 
                   EMBEDIDS_METRIC_TYPE_BOOL, 5);

  // Setup system configuration
  embedids_system_config_t system_config;
  memset(&system_config, 0, sizeof(system_config));
  system_config.metrics = metric_configs;
  system_config.max_metrics = 3;
  system_config.num_active_metrics = 3;

  ASSERT_EQ(embedids_init(&context, &system_config), EMBEDIDS_OK);

  // Add data to all metrics
  embedids_metric_value_t value;
  
  value.f32 = 25.5f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 1000), EMBEDIDS_OK);

  value.u32 = 42;
  EXPECT_EQ(embedids_add_datapoint(&context, "count", value, 1000), EMBEDIDS_OK);

  value.boolean = true;
  EXPECT_EQ(embedids_add_datapoint(&context, "status", value, 1000), EMBEDIDS_OK);

  // Analyze all mixed metrics
  embedids_result_t result = embedids_analyze_all(&context);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Individual analysis should also work
  EXPECT_EQ(embedids_analyze_metric(&context, "temperature"), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "count"), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "status"), EMBEDIDS_OK);
}

TEST_F(EmbedIDSAnalysisTest, SequentialAnalysisCalls) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "test_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add initial data
  embedids_metric_value_t value;
  value.f32 = 50.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "test_metric", value, 1000), EMBEDIDS_OK);

  // Multiple sequential analysis calls should work
  EXPECT_EQ(embedids_analyze_metric(&context, "test_metric"), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "test_metric"), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_all(&context), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_all(&context), EMBEDIDS_OK);

  // Add more data and analyze again
  value.f32 = 55.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "test_metric", value, 2000), EMBEDIDS_OK);
  EXPECT_EQ(embedids_analyze_metric(&context, "test_metric"), EMBEDIDS_OK);
}
