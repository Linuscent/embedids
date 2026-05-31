#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <math.h>
#define EMBEDIDS_ENABLED 1
#include <embedids.h>

// Custom algorithm context for pattern detection
typedef struct {
    float baseline;
    float threshold_multiplier;
    uint32_t consecutive_violations;
    uint32_t max_violations;
} pattern_detector_context_t;

// Custom algorithm: Advanced pattern detection
embedids_result_t advanced_pattern_detector(const embedids_metric_t* metric,
                                           const void* config,
                                           void* context) {
    (void)config; // Unused parameter
    pattern_detector_context_t* ctx = (pattern_detector_context_t*)context;
    
    if (metric->current_size < 3) {
        return EMBEDIDS_OK; // Need at least 3 data points
    }
    
    // Get the last few data points
    uint32_t idx1 = (metric->write_index - 1) % metric->max_history_size;
    uint32_t idx2 = (metric->write_index - 2) % metric->max_history_size;
    uint32_t idx3 = (metric->write_index - 3) % metric->max_history_size;
    
    float val1 = metric->history[idx1].value.f32;
    float val2 = metric->history[idx2].value.f32;
    float val3 = metric->history[idx3].value.f32;
    
    // Calculate variance from baseline
    float avg_recent = (val1 + val2 + val3) / 3.0f;
    float deviation = fabs(avg_recent - ctx->baseline);
    
    if (deviation > ctx->baseline * ctx->threshold_multiplier) {
        ctx->consecutive_violations++;
        printf("    [PATTERN] High deviation %.2f from baseline %.2f (violation %d/%d)\n", 
               deviation, ctx->baseline, ctx->consecutive_violations, ctx->max_violations);
        
        if (ctx->consecutive_violations >= ctx->max_violations) {
            printf("    [ALERT] PATTERN ALERT: Sustained anomalous behavior detected!\n");
            ctx->consecutive_violations = 0; // Reset counter
            return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
        }
    } else {
        ctx->consecutive_violations = 0; // Reset on normal reading
    }
    
    return EMBEDIDS_OK;
}

// Custom algorithm: Rate-of-change detector
embedids_result_t rate_change_detector(const embedids_metric_t* metric,
                                     const void* config,
                                     void* context) {
    (void)context; // Unused parameter
    
    if (metric->current_size < 2) {
        return EMBEDIDS_OK;
    }
    
    // Get last two data points
    uint32_t idx1 = (metric->write_index - 1) % metric->max_history_size;
    uint32_t idx2 = (metric->write_index - 2) % metric->max_history_size;
    
    float val1 = metric->history[idx1].value.f32;
    float val2 = metric->history[idx2].value.f32;
    uint64_t time1 = metric->history[idx1].timestamp_ms;
    uint64_t time2 = metric->history[idx2].timestamp_ms;
    
    if (time1 == time2) return EMBEDIDS_OK; // Avoid division by zero
    
    float rate = fabs(val1 - val2) / ((float)(time1 - time2) / 1000.0f); // per second
    float max_rate = *((float*)config); // Config holds max allowed rate
    
    if (rate > max_rate) {
        printf("    [RATE] Rapid change %.2f/s (max: %.2f/s)\n", rate, max_rate);
        return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
    }
    
    return EMBEDIDS_OK;
}

// Simulate realistic sensor data with occasional anomalies
float simulate_cpu_usage(int iteration) {
    float base = 20.0f + 15.0f * sin(iteration * 0.3f); // Normal oscillation
    float noise = ((float)rand() / RAND_MAX - 0.5f) * 10.0f;
    
    // Inject anomalies
    if (iteration == 8) return 95.0f; // Sudden spike
    if (iteration >= 12 && iteration <= 14) return 5.0f; // Sustained low
    if (iteration == 18) return 98.0f; // Another spike
    
    return fmax(0.0f, fmin(100.0f, base + noise));
}

float simulate_memory_pressure(int iteration) {
    float base = 40.0f + 20.0f * cos(iteration * 0.2f);
    float noise = ((float)rand() / RAND_MAX - 0.5f) * 8.0f;
    
    // Memory leak simulation
    if (iteration >= 15) base += (iteration - 15) * 2.0f;
    
    return fmax(0.0f, fmin(100.0f, base + noise));
}

float simulate_network_traffic(int iteration) {
    float base = 1000.0f + 500.0f * sin(iteration * 0.4f);
    float noise = ((float)rand() / RAND_MAX - 0.5f) * 200.0f;
    
    // DDoS simulation
    if (iteration >= 10 && iteration <= 12) return base * 5.0f;
    
    return fmax(0.0f, base + noise);
}

int main(void) {
    printf("EmbedIDS Extensible Architecture Example\n");
    printf("========================================\n");
    printf("Version: %s\n", embedids_get_version());
    printf("Demonstrating: Custom algorithms, user-managed memory, multiple metrics\n\n");
    
    srand((unsigned int)time(NULL));
    
    // User-managed memory for metric histories
    static embedids_metric_datapoint_t cpu_history[50];
    static embedids_metric_datapoint_t memory_history[50];
    static embedids_metric_datapoint_t network_history[30];
    
    // Custom algorithm contexts
    pattern_detector_context_t cpu_pattern_ctx = {
        .baseline = 35.0f,
        .threshold_multiplier = 0.8f,
        .consecutive_violations = 0,
        .max_violations = 3
    };
    
    pattern_detector_context_t memory_pattern_ctx = {
        .baseline = 50.0f,
        .threshold_multiplier = 0.6f,
        .consecutive_violations = 0,
        .max_violations = 2
    };
    
    float cpu_rate_limit = 20.0f; // Max 20% change per second
    float network_rate_limit = 1000.0f; // Max 1000 packets/s change rate
    
    // ===== CPU Metric Configuration =====
    embedids_metric_t cpu_metric;
    memset(&cpu_metric, 0, sizeof(cpu_metric));
    strncpy(cpu_metric.name, "cpu_usage", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    cpu_metric.type = EMBEDIDS_METRIC_TYPE_PERCENTAGE;
    cpu_metric.history = cpu_history;
    cpu_metric.max_history_size = 50;
    cpu_metric.enabled = true;
    
    // CPU algorithms: threshold + pattern detection + rate limiting
    embedids_algorithm_t cpu_algorithms[3];
    memset(cpu_algorithms, 0, sizeof(cpu_algorithms));
    
    // Threshold algorithm
    cpu_algorithms[0].type = EMBEDIDS_ALGORITHM_THRESHOLD;
    cpu_algorithms[0].enabled = true;
    cpu_algorithms[0].config.threshold.max_threshold.f32 = 85.0f;
    cpu_algorithms[0].config.threshold.check_max = true;
    cpu_algorithms[0].config.threshold.check_min = false;
    
    // Custom pattern detector
    cpu_algorithms[1].type = EMBEDIDS_ALGORITHM_CUSTOM;
    cpu_algorithms[1].enabled = true;
    cpu_algorithms[1].config.custom.function = advanced_pattern_detector;
    cpu_algorithms[1].config.custom.config = NULL;
    cpu_algorithms[1].config.custom.context = &cpu_pattern_ctx;
    
    // Custom rate detector
    cpu_algorithms[2].type = EMBEDIDS_ALGORITHM_CUSTOM;
    cpu_algorithms[2].enabled = true;
    cpu_algorithms[2].config.custom.function = rate_change_detector;
    cpu_algorithms[2].config.custom.config = &cpu_rate_limit;
    cpu_algorithms[2].config.custom.context = NULL;
    
    // ===== Memory Metric Configuration =====
    embedids_metric_t memory_metric;
    memset(&memory_metric, 0, sizeof(memory_metric));
    strncpy(memory_metric.name, "memory_pressure", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    memory_metric.type = EMBEDIDS_METRIC_TYPE_PERCENTAGE;
    memory_metric.history = memory_history;
    memory_metric.max_history_size = 50;
    memory_metric.enabled = true;
    
    // Memory algorithms: trend + pattern detection
    embedids_algorithm_t memory_algorithms[2];
    memset(memory_algorithms, 0, sizeof(memory_algorithms));
    
    // Trend algorithm
    memory_algorithms[0].type = EMBEDIDS_ALGORITHM_TREND;
    memory_algorithms[0].enabled = true;
    memory_algorithms[0].config.trend.window_size = 5;
    memory_algorithms[0].config.trend.max_slope = 15.0f;
    
    // Custom pattern detector
    memory_algorithms[1].type = EMBEDIDS_ALGORITHM_CUSTOM;
    memory_algorithms[1].enabled = true;
    memory_algorithms[1].config.custom.function = advanced_pattern_detector;
    memory_algorithms[1].config.custom.config = NULL;
    memory_algorithms[1].config.custom.context = &memory_pattern_ctx;
    
    // ===== Network Metric Configuration =====
    embedids_metric_t network_metric;
    memset(&network_metric, 0, sizeof(network_metric));
    strncpy(network_metric.name, "network_packets", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    network_metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
    network_metric.history = network_history;
    network_metric.max_history_size = 30;
    network_metric.enabled = true;
    
    // Network algorithms: threshold + rate detection
    embedids_algorithm_t network_algorithms[2];
    memset(network_algorithms, 0, sizeof(network_algorithms));
    
    // Threshold algorithm
    network_algorithms[0].type = EMBEDIDS_ALGORITHM_THRESHOLD;
    network_algorithms[0].enabled = true;
    network_algorithms[0].config.threshold.max_threshold.f32 = 3000.0f;
    network_algorithms[0].config.threshold.check_max = true;
    network_algorithms[0].config.threshold.check_min = false;
    
    // Rate detector
    network_algorithms[1].type = EMBEDIDS_ALGORITHM_CUSTOM;
    network_algorithms[1].enabled = true;
    network_algorithms[1].config.custom.function = rate_change_detector;
    network_algorithms[1].config.custom.config = &network_rate_limit;
    network_algorithms[1].config.custom.context = NULL;
    
    // ===== System Configuration =====
    embedids_metric_config_t metric_configs[3];
    
    // CPU metric config
    metric_configs[0].metric = cpu_metric;
    memcpy(metric_configs[0].algorithms, cpu_algorithms, sizeof(cpu_algorithms));
    metric_configs[0].num_algorithms = 3;
    
    // Memory metric config
    metric_configs[1].metric = memory_metric;
    memcpy(metric_configs[1].algorithms, memory_algorithms, sizeof(memory_algorithms));
    metric_configs[1].num_algorithms = 2;
    
    // Network metric config
    metric_configs[2].metric = network_metric;
    memcpy(metric_configs[2].algorithms, network_algorithms, sizeof(network_algorithms));
    metric_configs[2].num_algorithms = 2;
    
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_system_config_t system_config;
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = metric_configs;
    system_config.max_metrics = 3;
    system_config.num_active_metrics = 3;
    
    // Initialize EmbedIDS
    embedids_result_t result = embedids_init(&context, &system_config);
    if (result != EMBEDIDS_OK) {
        printf("ERROR: Failed to initialize EmbedIDS: %d\n", result);
        return 1;
    }
    
    printf("OK: EmbedIDS initialized with extensible architecture\n");
    printf("INFO: Monitoring 3 metrics with 7 total algorithms:\n");
    printf("   * CPU: Threshold + Pattern Detection + Rate Limiting\n");
    printf("   * Memory: Trend Analysis + Pattern Detection\n");
    printf("   * Network: Threshold + Rate Detection\n\n");
    
    printf("Starting advanced monitoring simulation...\n");
    printf("============================================================\n");
    printf("\n");
    
    // Monitoring simulation
    for (int i = 1; i <= 20; i++) {
        printf("--- Iteration %d ---\n", i);
        
        // Generate realistic sensor data
        float cpu = simulate_cpu_usage(i);
        float memory = simulate_memory_pressure(i);
        float network = simulate_network_traffic(i);
        
        printf("Data: CPU=%.1f%%, Memory=%.1f%%, Network=%.0f pkt/s\n", 
               cpu, memory, network);
        
        // Add data points
        embedids_metric_value_t cpu_val = {.f32 = cpu};
        embedids_metric_value_t memory_val = {.f32 = memory};
        embedids_metric_value_t network_val = {.f32 = network};
        
        uint64_t timestamp = (uint64_t)time(NULL) * 1000 + i * 100; // Simulate 100ms intervals
        
        embedids_add_datapoint(&context, "cpu_usage", cpu_val, timestamp);
        embedids_add_datapoint(&context, "memory_pressure", memory_val, timestamp);
        embedids_add_datapoint(&context, "network_packets", network_val, timestamp);
        
        // Analyze each metric individually to see which algorithms trigger
        printf("Analysis results:\n");
        
        // Analyze CPU
        embedids_result_t cpu_result = embedids_analyze_metric(&context, "cpu_usage");
        if (cpu_result == EMBEDIDS_OK) {
            printf("  OK: CPU: All algorithms passed\n");
        } else {
            printf("  ALERT: CPU: Anomaly detected by algorithm(s)\n");
        }
        
        // Analyze Memory
        embedids_result_t memory_result = embedids_analyze_metric(&context, "memory_pressure");
        if (memory_result == EMBEDIDS_OK) {
            printf("  OK: Memory: All algorithms passed\n");
        } else {
            printf("  ALERT: Memory: Anomaly detected by algorithm(s)\n");
        }
        
        // Analyze Network
        embedids_result_t network_result = embedids_analyze_metric(&context, "network_packets");
        if (network_result == EMBEDIDS_OK) {
            printf("  OK: Network: All algorithms passed\n");
        } else {
            printf("  ALERT: Network: Anomaly detected by algorithm(s)\n");
        }
        
        // Overall system status
        embedids_result_t overall_result = embedids_analyze_all(&context);
        if (overall_result == EMBEDIDS_OK) {
            printf("SYSTEM STATUS: SECURE\n");
        } else {
            printf("SYSTEM STATUS: THREAT DETECTED\n");
        }
        
        printf("\n");
        usleep(500000); // 500ms delay for dramatic effect
    }
    
    printf("============================================================\n");
    printf("\n");
    printf("Advanced monitoring simulation completed!\n");
    printf("Summary:\n");
    printf("   * Demonstrated 2 custom algorithm types\n");
    printf("   * Used user-managed memory (%.1fKB total)\n", 
           (sizeof(cpu_history) + sizeof(memory_history) + sizeof(network_history)) / 1024.0f);
    printf("   * Combined multiple detection strategies per metric\n");
    printf("   * Showed extensible architecture flexibility\n");
    
    // Cleanup
    embedids_cleanup(&context);
    printf("EmbedIDS cleaned up successfully.\n");
    
    return 0;
}
