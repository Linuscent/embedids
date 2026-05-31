#include "embedids.h"
#include <cstring>
#include <gtest/gtest.h>

/**
 * @brief Test fixture for core EmbedIDS functionality
 * 
 * Tests basic system initialization, configuration validation,
 * version information, and error handling.
 */
class EmbedIDSCoreTest : public ::testing::Test {
protected:
  embedids_context_t context;
  
  void SetUp() override { 
    memset(&context, 0, sizeof(context));
    embedids_cleanup(&context); 
  }

  void TearDown() override { 
    embedids_cleanup(&context); 
  }
};

// ============================================================================
// System Initialization Tests
// ============================================================================

TEST_F(EmbedIDSCoreTest, InitializeSystem) {
  embedids_system_config_t config;
  memset(&config, 0, sizeof(config));

  embedids_result_t result = embedids_init(&context, &config);
  EXPECT_EQ(result, EMBEDIDS_OK);

  EXPECT_TRUE(embedids_is_initialized(&context));
}

TEST_F(EmbedIDSCoreTest, InitializationWithNullConfig) {
  embedids_result_t result = embedids_init(nullptr, nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);
  EXPECT_FALSE(embedids_is_initialized(&context));
}

TEST_F(EmbedIDSCoreTest, CleanupFunction) {
  embedids_system_config_t config;
  memset(&config, 0, sizeof(config));
  ASSERT_EQ(embedids_init(&context, &config), EMBEDIDS_OK);
  EXPECT_TRUE(embedids_is_initialized(&context));

  embedids_cleanup(&context);
  EXPECT_FALSE(embedids_is_initialized(&context));

  embedids_cleanup(&context);
  EXPECT_FALSE(embedids_is_initialized(&context));
}

// ============================================================================
// Configuration Validation Tests
// ============================================================================

TEST_F(EmbedIDSCoreTest, ConfigValidationWithNullConfig) {
  embedids_result_t result = embedids_validate_config(nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);
}

TEST_F(EmbedIDSCoreTest, ConfigValidationWithNullMetrics) {
  embedids_system_config_t config;
  memset(&config, 0, sizeof(config));
  config.metrics = nullptr;
  
  embedids_result_t result = embedids_validate_config(&config);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);
}

// ============================================================================
// Version and Error Information Tests
// ============================================================================

TEST_F(EmbedIDSCoreTest, VersionInfo) {
  const char *version = embedids_get_version();
  EXPECT_NE(version, nullptr);
  EXPECT_GT(strlen(version), 0);
}

TEST_F(EmbedIDSCoreTest, ErrorStringFunction) {
  const char *error_str = embedids_get_error_string(EMBEDIDS_OK);
  EXPECT_NE(error_str, nullptr);

  error_str = embedids_get_error_string(EMBEDIDS_ERROR_INVALID_PARAM);
  EXPECT_NE(error_str, nullptr);

  error_str = embedids_get_error_string(EMBEDIDS_ERROR_NOT_INITIALIZED);
  EXPECT_NE(error_str, nullptr);
}

// ============================================================================
// Uninitialized System Tests
// ============================================================================

TEST_F(EmbedIDSCoreTest, UninitializedOperations) {
  EXPECT_FALSE(embedids_is_initialized(&context));

  embedids_metric_value_t value;
  value.f32 = 10.0f;
  
  // Test operations on uninitialized system
  embedids_result_t result = embedids_add_datapoint(&context, "test_metric", value, 1000);
  EXPECT_EQ(result, EMBEDIDS_ERROR_NOT_INITIALIZED);

  result = embedids_analyze_all(&context);
  EXPECT_EQ(result, EMBEDIDS_ERROR_NOT_INITIALIZED);

  result = embedids_analyze_metric(&context, "test_metric");
  EXPECT_EQ(result, EMBEDIDS_ERROR_NOT_INITIALIZED);

  result = embedids_reset_all_metrics(&context);
  EXPECT_EQ(result, EMBEDIDS_ERROR_NOT_INITIALIZED);
}

// ============================================================================
// Null Parameter Tests
// ============================================================================

TEST_F(EmbedIDSCoreTest, NullParameterHandling) {
  // Setup a basic initialized system
  embedids_metric_datapoint_t history_buffer[10];
  embedids_metric_config_t metric_config;
  memset(&metric_config, 0, sizeof(metric_config));

  strncpy(metric_config.metric.name, "test_metric", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
  metric_config.metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
  metric_config.metric.enabled = true;
  metric_config.metric.history = history_buffer;
  metric_config.metric.max_history_size = 10;
  metric_config.metric.current_size = 0;
  metric_config.metric.write_index = 0;

  embedids_system_config_t system_config;
  memset(&system_config, 0, sizeof(system_config));
  system_config.metrics = &metric_config;
  system_config.max_metrics = 1;
  system_config.num_active_metrics = 1;

  ASSERT_EQ(embedids_init(&context, &system_config), EMBEDIDS_OK);

  // Test null metric name
  embedids_metric_value_t value;
  value.f32 = 10.0f;
  embedids_result_t result = embedids_add_datapoint(&context, nullptr, value, 1000);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);

  result = embedids_analyze_metric(&context, nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);

  // Test trend analysis with null parameters
  embedids_trend_t trend;
  result = embedids_get_trend(&context, nullptr, &trend);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);

  result = embedids_get_trend(&context, "test_metric", nullptr);
  EXPECT_EQ(result, EMBEDIDS_ERROR_INVALID_PARAM);
}
