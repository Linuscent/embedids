/**
 * @file user_application.c
 * @brief Example showing how a user integrates EmbedIDS into their IoT application
 * 
 * This example demonstrates real-world integration patterns:
 * 1. Custom metrics for specific IoT device monitoring
 * 2. Multiple detection algorithms per metric
 * 3. User-managed memory for embedded constraints
 * 4. Integration with existing monitoring systems
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <embedids.h>

#define DEVICE_HISTORY_SIZE 20
#define NUM_DEVICE_METRICS 4

// User-managed memory buffers for IoT device metrics
static embedids_metric_datapoint_t temperature_history[DEVICE_HISTORY_SIZE];
static embedids_metric_datapoint_t humidity_history[DEVICE_HISTORY_SIZE];
static embedids_metric_datapoint_t power_history[DEVICE_HISTORY_SIZE];
static embedids_metric_datapoint_t connection_history[DEVICE_HISTORY_SIZE];

// Application-specific data structure
typedef struct {
    float temperature_celsius;
    float humidity_percent;
    float power_consumption_watts;
    uint32_t active_connections;
    bool device_tampered;
    uint64_t uptime_seconds;
} iot_device_status_t;

// Custom algorithm: Detect device tampering based on rapid environmental changes
embedids_result_t tampering_detector(const embedids_metric_t* metric,
                                    const void* config,
                                    void* context) {
    (void)config;  // Unused
    (void)context; // Unused
    
    // Need at least 5 data points for tampering detection
    if (metric->current_size < 5) {
        return EMBEDIDS_OK;
    }
    
    // Check for rapid temperature or humidity changes that might indicate tampering
    uint32_t latest = (metric->write_index > 0) ? metric->write_index - 1 : metric->max_history_size - 1;
    uint32_t prev = (latest > 0) ? latest - 1 : metric->max_history_size - 1;
    
    float current_val = metric->history[latest].value.f32;
    float prev_val = metric->history[prev].value.f32;
    
    // Alert if temperature changes by more than 15C or humidity by 30% in one reading
    if (strncmp(metric->name, "temperature", 11) == 0) {
        if (fabs(current_val - prev_val) > 15.0f) {
            printf("TAMPERING ALERT: Rapid temperature change detected (%.1fC -> %.1fC)\n", 
                   prev_val, current_val);
            return EMBEDIDS_ERROR_CUSTOM_DETECTION;
        }
    } else if (strncmp(metric->name, "humidity", 8) == 0) {
        if (fabs(current_val - prev_val) > 30.0f) {
            printf("TAMPERING ALERT: Rapid humidity change detected (%.1f%% -> %.1f%%)\n", 
                   prev_val, current_val);
            return EMBEDIDS_ERROR_CUSTOM_DETECTION;
        }
    }
    
    return EMBEDIDS_OK;
}

// Simulate reading real IoT device metrics
iot_device_status_t read_device_status(int iteration) {
    iot_device_status_t status;
    
    // Simulate normal operation with occasional anomalies
    status.temperature_celsius = 22.0f + (rand() % 10) - 5 + 
                               (iteration > 5 && iteration < 8 ? 20.0f : 0.0f); // Simulate heating attack
    status.humidity_percent = 45.0f + (rand() % 20);
    status.power_consumption_watts = 5.0f + (rand() % 3);
    status.active_connections = 1 + (rand() % (iteration > 8 ? 10 : 3)); // Simulate connection flood
    status.device_tampered = false;
    status.uptime_seconds = iteration * 60; // Simulate 1 minute per iteration
    
    return status;
}

int main(void) {
    printf("IoT Device Security Monitor v%s\n", embedids_get_version());
    printf("=====================================\n\n");

    // Configure metrics for IoT device monitoring
    embedids_metric_config_t device_metrics[NUM_DEVICE_METRICS];
    
    // Temperature Monitoring (with tampering detection)
    device_metrics[0] = (embedids_metric_config_t) {
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_FLOAT,
            .history = temperature_history,
            .max_history_size = DEVICE_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 2
    };
    strncpy(device_metrics[0].metric.name, "temperature", EMBEDIDS_MAX_METRIC_NAME_LEN);
    
    // Threshold algorithm for temperature
    device_metrics[0].algorithms[0] = (embedids_algorithm_t) {
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .min_threshold = { .f32 = 0.0f },    // Alert if temp < 0C
            .max_threshold = { .f32 = 40.0f },   // Alert if temp > 40C
            .check_max = true,
            .check_min = true
        }
    };
    
    // Custom tampering detection algorithm
    device_metrics[0].algorithms[1] = (embedids_algorithm_t) {
        .type = EMBEDIDS_ALGORITHM_CUSTOM,
        .enabled = true,
        .config.custom = {
            .function = tampering_detector,
            .config = NULL,
            .context = NULL
        }
    };
    
    // Humidity Monitoring (with tampering detection)
    device_metrics[1] = (embedids_metric_config_t) {
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_FLOAT,
            .history = humidity_history,
            .max_history_size = DEVICE_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 2
    };
    strncpy(device_metrics[1].metric.name, "humidity", EMBEDIDS_MAX_METRIC_NAME_LEN);
    
    device_metrics[1].algorithms[0] = (embedids_algorithm_t) {
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .min_threshold = { .f32 = 10.0f },   // Alert if humidity < 10%
            .max_threshold = { .f32 = 90.0f },   // Alert if humidity > 90%
            .check_max = true,
            .check_min = true
        }
    };
    
    device_metrics[1].algorithms[1] = (embedids_algorithm_t) {
        .type = EMBEDIDS_ALGORITHM_CUSTOM,
        .enabled = true,
        .config.custom = {
            .function = tampering_detector,
            .config = NULL,
            .context = NULL
        }
    };
    
    // Power Consumption Monitoring
    device_metrics[2] = (embedids_metric_config_t) {
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_FLOAT,
            .history = power_history,
            .max_history_size = DEVICE_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 1
    };
    strncpy(device_metrics[2].metric.name, "power", EMBEDIDS_MAX_METRIC_NAME_LEN);
    
    device_metrics[2].algorithms[0] = (embedids_algorithm_t) {
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .max_threshold = { .f32 = 15.0f },   // Alert if power > 15W (possible crypto mining)
            .check_max = true,
            .check_min = false
        }
    };
    
    // Network Connection Monitoring
    device_metrics[3] = (embedids_metric_config_t) {
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_UINT32,
            .history = connection_history,
            .max_history_size = DEVICE_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 1
    };
    strncpy(device_metrics[3].metric.name, "connections", EMBEDIDS_MAX_METRIC_NAME_LEN);
    
    device_metrics[3].algorithms[0] = (embedids_algorithm_t) {
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .max_threshold = { .u32 = 5 },       // Alert if > 5 connections (DDoS/scanning)
            .check_max = true,
            .check_min = false
        }
    };

    // Initialize EmbedIDS
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_system_config_t system_config = {
        .metrics = device_metrics,
        .max_metrics = NUM_DEVICE_METRICS,
        .num_active_metrics = NUM_DEVICE_METRICS,
        .user_context = NULL
    };
    
    embedids_result_t result = embedids_init(&context, &system_config);
    if (result != EMBEDIDS_OK) {
        printf("Failed to initialize EmbedIDS security monitoring: %d\n", result);
        return EXIT_FAILURE;
    }
    
    printf("IoT Security monitoring initialized successfully!\n");
    printf("Monitoring %d metrics with user-managed memory\n", NUM_DEVICE_METRICS);
    printf("Memory footprint: %zu bytes\n\n", 
           sizeof(temperature_history) + sizeof(humidity_history) + 
           sizeof(power_history) + sizeof(connection_history));
    
    // Main monitoring loop
    for (int iteration = 1; iteration <= 12; iteration++) {
        printf("--- Device Status Check %d ---\n", iteration);
        
        // Read current device status
        iot_device_status_t status = read_device_status(iteration);
        uint64_t timestamp = (uint64_t)time(NULL) * 1000 + iteration * 60000; // 1 minute intervals
        
        // Add metrics to EmbedIDS
        embedids_metric_value_t temp_val = { .f32 = status.temperature_celsius };
        embedids_metric_value_t humidity_val = { .f32 = status.humidity_percent };
        embedids_metric_value_t power_val = { .f32 = status.power_consumption_watts };
        embedids_metric_value_t conn_val = { .u32 = status.active_connections };
        
        embedids_add_datapoint(&context, "temperature", temp_val, timestamp);
        embedids_add_datapoint(&context, "humidity", humidity_val, timestamp);
        embedids_add_datapoint(&context, "power", power_val, timestamp);
        embedids_add_datapoint(&context, "connections", conn_val, timestamp);
        
        printf("Temp: %.1fC, Humidity: %.1f%%, Power: %.1fW, Connections: %u\n",
               status.temperature_celsius, status.humidity_percent,
               status.power_consumption_watts, status.active_connections);
        
        // Analyze for security threats
        result = embedids_analyze_all(&context);
        switch (result) {
            case EMBEDIDS_OK:
                printf("OK: Device secure - all metrics normal\n");
                break;
            case EMBEDIDS_ERROR_THRESHOLD_EXCEEDED:
                printf("WARNING: Threshold exceeded - possible attack!\n");
                break;
            case EMBEDIDS_ERROR_CUSTOM_DETECTION:
                printf("BREACH: Custom algorithm detected tampering!\n");
                break;
            default:
                printf("ERROR: Security analysis error: %d\n", result);
                break;
        }
        
        printf("\n");
        sleep(1);
    }
    
    printf("Monitoring session completed.\n");
    printf("IoT device security monitoring has been stopped.\n");
    
    embedids_cleanup(&context);
    return EXIT_SUCCESS;
}
