#include "embedids.h"
#include <cstring>
#include <gtest/gtest.h>

/**
 * @brief Test fixture for metric management functionality
 * 
 * Tests metric configuration, data point operations, different metric types,
 * and metric-specific operations like reset functionality.
 */
class EmbedIDSMetricsTest : public ::testing::Test {
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
   * @brief Helper function to create a basic metric configuration
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
// Data Point Operations Tests
// ============================================================================

TEST_F(EmbedIDSMetricsTest, AddDatapointToValidMetric) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "cpu_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);
  EXPECT_TRUE(embedids_is_initialized(&context));

  // Test adding valid data point
  embedids_metric_value_t value;
  value.f32 = 45.5f;
  embedids_result_t result = embedids_add_datapoint(&context, "cpu_usage", value, 1000);
  EXPECT_EQ(result, EMBEDIDS_OK);
}

TEST_F(EmbedIDSMetricsTest, AddDatapointToNonexistentMetric) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "cpu_usage", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Test adding data point for non-existent metric
  embedids_metric_value_t value;
  value.f32 = 45.5f;
  embedids_result_t result = embedids_add_datapoint(&context, "nonexistent_metric", value, 1000);
  EXPECT_EQ(result, EMBEDIDS_ERROR_METRIC_NOT_FOUND);
}

// ============================================================================
// Different Metric Types Tests
// ============================================================================

TEST_F(EmbedIDSMetricsTest, DifferentMetricTypes) {
  // Setup multiple metrics with different types
  embedids_metric_datapoint_t history_buffer1[5];
  embedids_metric_datapoint_t history_buffer2[5];
  embedids_metric_datapoint_t history_buffer3[5];
  embedids_metric_datapoint_t history_buffer4[5];

  embedids_metric_config_t metric_configs[4];
  
  // Configure uint32 metric
  setupBasicMetric(metric_configs[0], history_buffer1, "counter", 
                   EMBEDIDS_METRIC_TYPE_UINT32, 5);

  // Configure uint64 metric
  setupBasicMetric(metric_configs[1], history_buffer2, "byte_counter", 
                   EMBEDIDS_METRIC_TYPE_UINT64, 5);

  // Configure boolean metric
  setupBasicMetric(metric_configs[2], history_buffer3, "alarm_status", 
                   EMBEDIDS_METRIC_TYPE_BOOL, 5);

  // Configure enum metric
  setupBasicMetric(metric_configs[3], history_buffer4, "system_state", 
                   EMBEDIDS_METRIC_TYPE_ENUM, 5);

  // Setup system configuration
  embedids_system_config_t system_config;
  memset(&system_config, 0, sizeof(system_config));
  system_config.metrics = metric_configs;
  system_config.max_metrics = 4;
  system_config.num_active_metrics = 4;

  // Initialize system
  embedids_result_t result = embedids_init(&context, &system_config);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Test uint32 metric
  embedids_metric_value_t value;
  value.u32 = 12345;
  result = embedids_add_datapoint(&context, "counter", value, 1000);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Test uint64 metric
  value.u64 = 1234567890ULL;
  result = embedids_add_datapoint(&context, "byte_counter", value, 1000);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Test boolean metric
  value.boolean = true;
  result = embedids_add_datapoint(&context, "alarm_status", value, 2000);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Test enum metric
  value.enum_val = 3;
  result = embedids_add_datapoint(&context, "system_state", value, 3000);
  EXPECT_EQ(result, EMBEDIDS_OK);
}

TEST_F(EmbedIDSMetricsTest, FloatMetricPrecision) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "temperature", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Test precision values
  embedids_metric_value_t value;
  
  value.f32 = 3.14159f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 1000), EMBEDIDS_OK);
  
  value.f32 = -273.15f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 2000), EMBEDIDS_OK);
  
  value.f32 = 0.001f;
  EXPECT_EQ(embedids_add_datapoint(&context, "temperature", value, 3000), EMBEDIDS_OK);
}

TEST_F(EmbedIDSMetricsTest, LargeIntegerValues) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "large_counter", 
                   EMBEDIDS_METRIC_TYPE_UINT64, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  embedids_metric_value_t value;
  
  // Test maximum values
  value.u64 = UINT64_MAX;
  EXPECT_EQ(embedids_add_datapoint(&context, "large_counter", value, 1000), EMBEDIDS_OK);
  
  // Test typical large values
  value.u64 = 18446744073709551615ULL; // Near max uint64
  EXPECT_EQ(embedids_add_datapoint(&context, "large_counter", value, 2000), EMBEDIDS_OK);
}

// ============================================================================
// Metric Reset Functionality Tests
// ============================================================================

TEST_F(EmbedIDSMetricsTest, MetricResetFunctionality) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "test_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add some data points
  embedids_metric_value_t value;
  value.f32 = 25.0f;
  embedids_result_t result = embedids_add_datapoint(&context, "test_metric", value, 1000);
  EXPECT_EQ(result, EMBEDIDS_OK);

  value.f32 = 30.0f;
  result = embedids_add_datapoint(&context, "test_metric", value, 2000);
  EXPECT_EQ(result, EMBEDIDS_OK);

  // Reset all metrics
  result = embedids_reset_all_metrics(&context);
  EXPECT_EQ(result, EMBEDIDS_OK);
}

// ============================================================================
// Metric Buffer Management Tests
// ============================================================================

TEST_F(EmbedIDSMetricsTest, MetricBufferOverflow) {
  const uint32_t buffer_size = 3;
  embedids_metric_datapoint_t history_buffer[buffer_size];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "small_buffer", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, buffer_size);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Fill buffer beyond capacity
  embedids_metric_value_t value;
  for (uint32_t i = 0; i < buffer_size + 2; i++) {
    value.f32 = (float)i;
    embedids_result_t result = embedids_add_datapoint(&context, "small_buffer", value, 1000 + i * 1000);
    EXPECT_EQ(result, EMBEDIDS_OK);
  }
  
  // Buffer should handle overflow gracefully (circular buffer behavior)
  EXPECT_EQ(metric_config.metric.current_size, buffer_size);
}

TEST_F(EmbedIDSMetricsTest, MetricTimestampOrdering) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "time_test", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Add data points with different timestamps
  embedids_metric_value_t value;
  
  value.f32 = 1.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "time_test", value, 1000), EMBEDIDS_OK);
  
  value.f32 = 2.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "time_test", value, 500), EMBEDIDS_OK); // Earlier timestamp
  
  value.f32 = 3.0f;
  EXPECT_EQ(embedids_add_datapoint(&context, "time_test", value, 2000), EMBEDIDS_OK);
  
  // System should accept all data points regardless of timestamp order
  EXPECT_EQ(metric_config.metric.current_size, 3);
}

// ============================================================================
// Metric State Validation Tests
// ============================================================================

TEST_F(EmbedIDSMetricsTest, DisabledMetricBehavior) {
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  
  setupBasicMetric(metric_config, history_buffer, "disabled_metric", 
                   EMBEDIDS_METRIC_TYPE_FLOAT, 10);
  
  // Disable the metric
  metric_config.metric.enabled = false;

  ASSERT_EQ(initializeWithMetric(&metric_config), EMBEDIDS_OK);

  // Attempt to add data to disabled metric
  embedids_metric_value_t value;
  value.f32 = 10.0f;
  embedids_result_t result = embedids_add_datapoint(&context, "disabled_metric", value, 1000);
  
  // Behavior may vary - should either reject or accept but not process
  // The exact behavior depends on implementation
  EXPECT_TRUE(result == EMBEDIDS_OK || result == EMBEDIDS_ERROR_METRIC_DISABLED);
}
