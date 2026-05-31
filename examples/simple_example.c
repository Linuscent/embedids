#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <embedids.h>

/**
 * @brief Simple example demonstrating EmbedIDS usage with modern API
 */

#define MAX_HISTORY_SIZE 10

// Static buffers for metrics (user-managed memory)
static embedids_metric_datapoint_t cpu_history[MAX_HISTORY_SIZE];
static embedids_metric_datapoint_t memory_history[MAX_HISTORY_SIZE];
static embedids_metric_datapoint_t network_history[MAX_HISTORY_SIZE];

int main(void) {
    printf("EmbedIDS Example v%s\n", embedids_get_version());
    printf("====================\n\n");

    // Define metrics with thresholds using new extensible API
    embedids_metric_config_t metrics[3];
    
    // CPU Usage Metric
    metrics[0] = (embedids_metric_config_t){
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_PERCENTAGE,
            .history = cpu_history,
            .max_history_size = MAX_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 1
    };
    strncpy(metrics[0].metric.name, "cpu_usage", EMBEDIDS_MAX_METRIC_NAME_LEN);
    metrics[0].algorithms[0] = (embedids_algorithm_t){
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .max_threshold = { .f32 = 75.0f },  // Alert if CPU > 75%
            .check_max = true,
            .check_min = false
        }
    };
    
    // Memory Usage Metric  
    metrics[1] = (embedids_metric_config_t){
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_UINT32,
            .history = memory_history,
            .max_history_size = MAX_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 1
    };
    strncpy(metrics[1].metric.name, "memory_usage", EMBEDIDS_MAX_METRIC_NAME_LEN);
    metrics[1].algorithms[0] = (embedids_algorithm_t){
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .max_threshold = { .u32 = 512000 },  // Alert if memory > 500MB
            .check_max = true,
            .check_min = false
        }
    };
    
    // Network Packets Metric
    metrics[2] = (embedids_metric_config_t){
        .metric = {
            .type = EMBEDIDS_METRIC_TYPE_RATE,
            .history = network_history,
            .max_history_size = MAX_HISTORY_SIZE,
            .current_size = 0,
            .write_index = 0,
            .enabled = true
        },
        .num_algorithms = 1
    };
    strncpy(metrics[2].metric.name, "network_packets", EMBEDIDS_MAX_METRIC_NAME_LEN);
    metrics[2].algorithms[0] = (embedids_algorithm_t){
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .max_threshold = { .u32 = 800 },  // Alert if > 800 packets/sec
            .check_max = true,
            .check_min = false
        }
    };

    // Initialize the intrusion detection system
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_system_config_t system_config = {
        .metrics = metrics,
        .max_metrics = 3,
        .num_active_metrics = 3,
        .user_context = NULL
    };
    
    embedids_result_t result = embedids_init(&context, &system_config);
    if (result != EMBEDIDS_OK) {
        printf("Error: Failed to initialize EmbedIDS (code: %d)\n", result);
        return EXIT_FAILURE;
    }

    printf("EmbedIDS initialized successfully!\n");
    printf("Monitoring system with 3 metrics:\n");
    printf("  - CPU Usage: max 75%%\n");
    printf("  - Memory Usage: max 500 MB\n");
    printf("  - Network Packets: max 800/sec\n");
    printf("\nStarting monitoring simulation...\n\n");

    // Simulate monitoring for 10 iterations
    for (int i = 0; i < 10; i++) {
        uint64_t timestamp = (uint64_t)time(NULL) * 1000 + i * 1000;
        
        // Simulate CPU usage
        embedids_metric_value_t cpu_value = { .f32 = (float)(rand() % 100) };
        result = embedids_add_datapoint(&context, "cpu_usage", cpu_value, timestamp);
        if (result != EMBEDIDS_OK) {
            printf("Error adding CPU datapoint: %d\n", result);
        }
        
        // Simulate memory usage
        embedids_metric_value_t memory_value = { .u32 = 200000 + (rand() % 600000) };
        result = embedids_add_datapoint(&context, "memory_usage", memory_value, timestamp);
        if (result != EMBEDIDS_OK) {
            printf("Error adding memory datapoint: %d\n", result);
        }
        
        // Simulate network packets
        embedids_metric_value_t network_value = { .u32 = 100 + (rand() % 900) };
        result = embedids_add_datapoint(&context, "network_packets", network_value, timestamp);
        if (result != EMBEDIDS_OK) {
            printf("Error adding network datapoint: %d\n", result);
        }

        printf("Iteration %d:\n", i + 1);
        printf("  CPU: %.0f%%, Memory: %u KB, Network: %u pkt/s\n",
               cpu_value.f32, memory_value.u32, network_value.u32);

        // Analyze all metrics for potential intrusions
        result = embedids_analyze_all(&context);
        switch (result) {
            case EMBEDIDS_OK:
                printf("  Status: NORMAL - All metrics within acceptable ranges\n");
                break;
            case EMBEDIDS_ERROR_THRESHOLD_EXCEEDED:
                printf("  Status: ALERT - Potential intrusion detected! Threshold exceeded.\n");
                break;
            default:
                printf("  Status: ERROR - Analysis failed (code: %d)\n", result);
                break;
        }

        printf("\n");
        
        // Sleep for 1 second between iterations
        sleep(1);
    }

    printf("Monitoring simulation completed.\n");
    
    // Cleanup
    embedids_cleanup(&context);
    printf("EmbedIDS cleaned up successfully.\n");

    return EXIT_SUCCESS;
}
