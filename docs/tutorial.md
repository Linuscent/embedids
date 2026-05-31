# Tutorial

## Introduction

EmbedIDS is a lightweight, extensible intrusion detection system designed for embedded devices and IoT systems. It provides real-time monitoring of system metrics with configurable detection algorithms.

### Key Features
- **User-managed memory**: No dynamic allocation, perfect for embedded systems
- **Stateless design**: Context-based API for thread safety and multiple instances
- **Extensible architecture**: Custom metrics and algorithms
- **Multiple detection methods**: Thresholds, trends, statistical analysis, and custom algorithms
- **Minimal footprint**: Optimized for resource-constrained environments
- **Real-time analysis**: Low-latency threat detection

## API Design

### Stateless Architecture

EmbedIDS uses a stateless design where all library state is managed through a user-provided context object (`embedids_context_t`). This design provides several benefits:

- **Thread Safety**: Multiple contexts can be used independently across threads
- **Multiple Instances**: Create separate EmbedIDS instances for different subsystems
- **Clear Ownership**: Users control the lifecycle of the context object
- **Embedded-Friendly**: No global state that could cause issues in embedded systems

```c
// Create and initialize a context
embedids_context_t context;
memset(&context, 0, sizeof(context));

// Pass context to all API calls
embedids_init(&context, &config);
embedids_add_datapoint(&context, "metric_name", value, timestamp);
embedids_analyze_metric(&context, "metric_name");
embedids_cleanup(&context);
```

## Getting Started

### Installation

1. **Clone the repository:**
```bash
git clone https://github.com/Linuscent/EmbedIDS.git
cd EmbedIDS
```

2. **Build the library:**
```bash
mkdir build && cd build
cmake ..
make
```

3. **Include in your project:**
```c
#include "embedids.h"
```

### Basic Project Structure
```
your_project/
├── src/
│   └── main.c
├── include/
│   └── embedids.h
├── lib/
│   └── libembedids.a
└── CMakeLists.txt
```

## Basic Usage

### Step 1: Initialize EmbedIDS

The simplest way to get started is with an empty configuration:

```c
#include <stdio.h>
#include <string.h>
#include "embedids.h"

int main() {
    // Initialize context and empty configuration
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_system_config_t config;
    memset(&config, 0, sizeof(config));
    
    embedids_result_t result = embedids_init(&context, &config);
    if (result != EMBEDIDS_OK) {
        printf("Failed to initialize EmbedIDS: %d\n", result);
        return 1;
    }
    
    printf("EmbedIDS v%s initialized successfully!\n", embedids_get_version());
    
    // Clean up
    embedids_cleanup(&context);
    return 0;
}
```

### Step 2: Basic Metric Monitoring

Let's create a simple CPU usage monitor:

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "embedids.h"

int main() {
    // Step 1: Allocate memory for metric history
    static embedids_metric_datapoint_t cpu_history[100];
    
    // Step 2: Configure the metric
    embedids_metric_t cpu_metric;
    memset(&cpu_metric, 0, sizeof(cpu_metric));
    strncpy(cpu_metric.name, "cpu_usage", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    cpu_metric.type = EMBEDIDS_METRIC_TYPE_PERCENTAGE;
    cpu_metric.history = cpu_history;
    cpu_metric.max_history_size = 100;
    cpu_metric.enabled = true;
    
    // Step 3: Configure threshold algorithm
    embedids_algorithm_t threshold_algo;
    memset(&threshold_algo, 0, sizeof(threshold_algo));
    threshold_algo.type = EMBEDIDS_ALGORITHM_THRESHOLD;
    threshold_algo.enabled = true;
    threshold_algo.config.threshold.max_threshold.f32 = 80.0f;  // 80% threshold
    threshold_algo.config.threshold.check_max = true;
    threshold_algo.config.threshold.check_min = false;
    
    // Step 4: Create metric configuration
    embedids_metric_config_t metric_config;
    memset(&metric_config, 0, sizeof(metric_config));
    metric_config.metric = cpu_metric;
    metric_config.algorithms[0] = threshold_algo;
    metric_config.num_algorithms = 1;
    
    // Step 5: Create system configuration
    embedids_system_config_t system_config;
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = &metric_config;
    system_config.max_metrics = 1;
    system_config.num_active_metrics = 1;
    
    // Step 6: Initialize EmbedIDS context and system
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_result_t result = embedids_init(&context, &system_config);
    if (result != EMBEDIDS_OK) {
        printf("Failed to initialize EmbedIDS: %d\n", result);
        return 1;
    }
    
    printf("CPU monitoring started (threshold: 80%%)\n");
    
    // Step 7: Monitoring loop
    for (int i = 0; i < 10; i++) {
        // Simulate CPU usage data
        float cpu_usage = 30.0f + (i * 7.0f);  // Gradually increasing
        
        // Add data point
        embedids_metric_value_t value = {.f32 = cpu_usage};
        uint64_t timestamp = (uint64_t)time(NULL) * 1000;
        
        result = embedids_add_datapoint(&context, &context, "cpu_usage", value, timestamp);
        if (result != EMBEDIDS_OK) {
            printf("Failed to add data point: %d\n", result);
            continue;
        }
        
        // Analyze the metric
        result = embedids_analyze_metric(&context, &context, "cpu_usage");
        if (result == EMBEDIDS_OK) {
            printf("Iteration %d: CPU %.1f%% - NORMAL\n", i+1, cpu_usage);
        } else {
            printf("Iteration %d: CPU %.1f%% - ALERT! Threshold exceeded\n", i+1, cpu_usage);
        }
        
        sleep(1);
    }
    
    embedids_cleanup(&context);
    return 0;
}
```

## Advanced Configuration

### Multiple Metrics with Different Algorithms

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "embedids.h"

// Function to simulate realistic sensor data
float get_cpu_usage() {
    return 20.0f + (rand() % 60);  // 20-80%
}

float get_memory_usage() {
    return 30.0f + (rand() % 50);  // 30-80%
}

float get_network_packets() {
    return 100.0f + (rand() % 500);  // 100-600 packets/s
}

int main() {
    srand((unsigned int)time(NULL));
    
    // Allocate memory for each metric
    static embedids_metric_datapoint_t cpu_history[50];
    static embedids_metric_datapoint_t memory_history[50];
    static embedids_metric_datapoint_t network_history[30];
    
    // ===== CPU Metric =====
    embedids_metric_t cpu_metric;
    memset(&cpu_metric, 0, sizeof(cpu_metric));
    strncpy(cpu_metric.name, "cpu_usage", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    cpu_metric.type = EMBEDIDS_METRIC_TYPE_PERCENTAGE;
    cpu_metric.history = cpu_history;
    cpu_metric.max_history_size = 50;
    cpu_metric.enabled = true;
    
    // CPU: Threshold algorithm
    embedids_algorithm_t cpu_threshold;
    memset(&cpu_threshold, 0, sizeof(cpu_threshold));
    cpu_threshold.type = EMBEDIDS_ALGORITHM_THRESHOLD;
    cpu_threshold.enabled = true;
    cpu_threshold.config.threshold.max_threshold.f32 = 75.0f;
    cpu_threshold.config.threshold.check_max = true;
    cpu_threshold.config.threshold.check_min = false;
    
    embedids_metric_config_t cpu_config;
    memset(&cpu_config, 0, sizeof(cpu_config));
    cpu_config.metric = cpu_metric;
    cpu_config.algorithms[0] = cpu_threshold;
    cpu_config.num_algorithms = 1;
    
    // ===== Memory Metric =====
    embedids_metric_t memory_metric;
    memset(&memory_metric, 0, sizeof(memory_metric));
    strncpy(memory_metric.name, "memory_usage", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    memory_metric.type = EMBEDIDS_METRIC_TYPE_PERCENTAGE;
    memory_metric.history = memory_history;
    memory_metric.max_history_size = 50;
    memory_metric.enabled = true;
    
    // Memory: Trend algorithm
    embedids_algorithm_t memory_trend;
    memset(&memory_trend, 0, sizeof(memory_trend));
    memory_trend.type = EMBEDIDS_ALGORITHM_TREND;
    memory_trend.enabled = true;
    memory_trend.config.trend.window_size = 5;
    memory_trend.config.trend.max_slope = 10.0f;  // Max 10% increase per window
    
    embedids_metric_config_t memory_config;
    memset(&memory_config, 0, sizeof(memory_config));
    memory_config.metric = memory_metric;
    memory_config.algorithms[0] = memory_trend;
    memory_config.num_algorithms = 1;
    
    // ===== Network Metric =====
    embedids_metric_t network_metric;
    memset(&network_metric, 0, sizeof(network_metric));
    strncpy(network_metric.name, "network_packets", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    network_metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
    network_metric.history = network_history;
    network_metric.max_history_size = 30;
    network_metric.enabled = true;
    
    // Network: Threshold algorithm
    embedids_algorithm_t network_threshold;
    memset(&network_threshold, 0, sizeof(network_threshold));
    network_threshold.type = EMBEDIDS_ALGORITHM_THRESHOLD;
    network_threshold.enabled = true;
    network_threshold.config.threshold.max_threshold.f32 = 500.0f;
    network_threshold.config.threshold.check_max = true;
    network_threshold.config.threshold.check_min = false;
    
    embedids_metric_config_t network_config;
    memset(&network_config, 0, sizeof(network_config));
    network_config.metric = network_metric;
    network_config.algorithms[0] = network_threshold;
    network_config.num_algorithms = 1;
    
    // ===== System Configuration =====
    embedids_metric_config_t metrics[3] = {cpu_config, memory_config, network_config};
    
    embedids_system_config_t system_config;
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = metrics;
    system_config.max_metrics = 3;
    system_config.num_active_metrics = 3;
    
    // Initialize EmbedIDS context and system
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_result_t result = embedids_init(&context, &system_config);
    if (result != EMBEDIDS_OK) {
        printf("Failed to initialize EmbedIDS: %d\n", result);
        return 1;
    }
    
    printf("Multi-metric monitoring started:\n");
    printf("- CPU: Threshold (75%%)\n");
    printf("- Memory: Trend analysis (5 window, 10%% max slope)\n");
    printf("- Network: Threshold (500 pkt/s)\n\n");
    
    // Monitoring loop
    for (int i = 0; i < 15; i++) {
        // Get sensor data
        float cpu = get_cpu_usage();
        float memory = get_memory_usage();
        float network = get_network_packets();
        
        // Add data points
        uint64_t timestamp = (uint64_t)time(NULL) * 1000 + i * 100;
        
        embedids_metric_value_t cpu_val = {.f32 = cpu};
        embedids_metric_value_t memory_val = {.f32 = memory};
        embedids_metric_value_t network_val = {.f32 = network};
        
        embedids_add_datapoint(&context, "cpu_usage", cpu_val, timestamp);
        embedids_add_datapoint(&context, "memory_usage", memory_val, timestamp);
        embedids_add_datapoint(&context, "network_packets", network_val, timestamp);
        
        // Analyze all metrics
        embedids_result_t overall_result = embedids_analyze_all(&context);
        
        printf("Iteration %2d: CPU=%4.1f%%, Memory=%4.1f%%, Network=%5.0f pkt/s ", 
               i+1, cpu, memory, network);
        
        if (overall_result == EMBEDIDS_OK) {
            printf("✅ SECURE\n");
        } else {
            printf("🚨 THREAT DETECTED\n");
        }
        
        usleep(200000);  // 200ms delay
    }
    
    embedids_cleanup(&context);
    return 0;
}
```

## Custom Algorithms

### Creating a Custom Detection Algorithm

Custom algorithms allow you to implement specialized detection logic:

```c
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "embedids.h"

// Custom algorithm context
typedef struct {
    float baseline;
    uint32_t violation_count;
    uint32_t max_violations;
} anomaly_detector_context_t;

// Custom algorithm implementation
embedids_result_t anomaly_detector(const embedids_metric_t* metric,
                                 const void* config,
                                 void* context) {
    (void)config;  // Unused in this example
    
    anomaly_detector_context_t* ctx = (anomaly_detector_context_t*)context;
    
    if (metric->current_size < 3) {
        return EMBEDIDS_OK;  // Need at least 3 data points
    }
    
    // Get recent values
    uint32_t idx1 = (metric->write_index - 1) % metric->max_history_size;
    uint32_t idx2 = (metric->write_index - 2) % metric->max_history_size;
    uint32_t idx3 = (metric->write_index - 3) % metric->max_history_size;
    
    float val1 = metric->history[idx1].value.f32;
    float val2 = metric->history[idx2].value.f32;
    float val3 = metric->history[idx3].value.f32;
    
    // Calculate moving average
    float avg = (val1 + val2 + val3) / 3.0f;
    
    // Check deviation from baseline
    float deviation = fabs(avg - ctx->baseline);
    float threshold = ctx->baseline * 0.5f;  // 50% deviation threshold
    
    if (deviation > threshold) {
        ctx->violation_count++;
        printf("  🔍 Anomaly detector: deviation %.2f from baseline %.2f (count: %d)\n",
               deviation, ctx->baseline, ctx->violation_count);
        
        if (ctx->violation_count >= ctx->max_violations) {
            printf("  🚨 ANOMALY ALERT: Sustained deviation detected!\n");
            ctx->violation_count = 0;  // Reset counter
            return EMBEDIDS_ERROR_THRESHOLD_EXCEEDED;
        }
    } else {
        ctx->violation_count = 0;  // Reset on normal reading
    }
    
    return EMBEDIDS_OK;
}

int main() {
    // User-managed memory
    static embedids_metric_datapoint_t sensor_history[40];
    
    // Custom algorithm context
    anomaly_detector_context_t detector_ctx = {
        .baseline = 50.0f,
        .violation_count = 0,
        .max_violations = 3
    };
    
    // Configure metric
    embedids_metric_t sensor_metric;
    memset(&sensor_metric, 0, sizeof(sensor_metric));
    strncpy(sensor_metric.name, "sensor_data", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
    sensor_metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
    sensor_metric.history = sensor_history;
    sensor_metric.max_history_size = 40;
    sensor_metric.enabled = true;
    
    // Configure custom algorithm
    embedids_algorithm_t custom_algo;
    memset(&custom_algo, 0, sizeof(custom_algo));
    custom_algo.type = EMBEDIDS_ALGORITHM_CUSTOM;
    custom_algo.enabled = true;
    custom_algo.config.custom.function = anomaly_detector;
    custom_algo.config.custom.config = NULL;
    custom_algo.config.custom.context = &detector_ctx;
    
    // Configure threshold algorithm as backup
    embedids_algorithm_t threshold_algo;
    memset(&threshold_algo, 0, sizeof(threshold_algo));
    threshold_algo.type = EMBEDIDS_ALGORITHM_THRESHOLD;
    threshold_algo.enabled = true;
    threshold_algo.config.threshold.max_threshold.f32 = 100.0f;
    threshold_algo.config.threshold.min_threshold.f32 = 0.0f;
    threshold_algo.config.threshold.check_max = true;
    threshold_algo.config.threshold.check_min = true;
    
    // Metric configuration with multiple algorithms
    embedids_metric_config_t metric_config;
    memset(&metric_config, 0, sizeof(metric_config));
    metric_config.metric = sensor_metric;
    metric_config.algorithms[0] = custom_algo;      // Custom algorithm first
    metric_config.algorithms[1] = threshold_algo;   // Threshold as backup
    metric_config.num_algorithms = 2;
    
    // System configuration
    embedids_system_config_t system_config;
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = &metric_config;
    system_config.max_metrics = 1;
    system_config.num_active_metrics = 1;
    
    // Initialize context and system
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    embedids_result_t result = embedids_init(&context, &system_config);
    if (result != EMBEDIDS_OK) {
        printf("Failed to initialize EmbedIDS: %d\n", result);
        return 1;
    }
    
    printf("Custom algorithm demo started (baseline: %.1f)\n", detector_ctx.baseline);
    printf("Algorithms: Custom Anomaly Detector + Threshold (0-100)\n\n");
    
    // Simulate sensor data with anomalies
    float sensor_values[] = {
        48.0, 52.0, 49.0, 51.0, 47.0,  // Normal data around baseline
        85.0, 88.0, 90.0,              // Anomaly: high values
        45.0, 50.0, 48.0,              // Back to normal
        15.0, 12.0, 18.0,              // Anomaly: low values
        52.0, 49.0, 51.0               // Normal again
    };
    
    int num_values = sizeof(sensor_values) / sizeof(sensor_values[0]);
    
    for (int i = 0; i < num_values; i++) {
        float value = sensor_values[i];
        
        // Add data point
        embedids_metric_value_t val = {.f32 = value};
        uint64_t timestamp = (uint64_t)time(NULL) * 1000 + i * 100;
        
        embedids_add_datapoint(&context, "sensor_data", val, timestamp);
        
        // Analyze
        result = embedids_analyze_metric(&context, "sensor_data");
        
        printf("Sample %2d: Value=%.1f ", i+1, value);
        if (result == EMBEDIDS_OK) {
            printf("✅ NORMAL\n");
        } else {
            printf("🚨 ANOMALY DETECTED\n");
        }
        
        usleep(300000);  // 300ms delay
    }
    
    embedids_cleanup(&context);
    return 0;
}
```

## Best Practices

### 1. Memory Management

```c
// ✅ Good: Static allocation for embedded systems
static embedids_metric_datapoint_t history[100];

// ❌ Avoid: Dynamic allocation in embedded systems
// embedids_metric_datapoint_t* history = malloc(sizeof(embedids_metric_datapoint_t) * 100);
```

### 2. Error Handling

```c
// ✅ Always check return codes
embedids_result_t result = embedids_add_datapoint(&context, "metric", value, timestamp);
if (result != EMBEDIDS_OK) {
    handle_error(result);
}

// ✅ Handle specific error types
switch (result) {
    case EMBEDIDS_ERROR_NOT_INITIALIZED:
        reinitialize_system();
        break;
    case EMBEDIDS_ERROR_METRIC_NOT_FOUND:
        log_error("Unknown metric");
        break;
    case EMBEDIDS_ERROR_BUFFER_FULL:
        // This is expected behavior with circular buffers
        break;
    default:
        handle_generic_error(result);
}
```

### 3. Metric Configuration

```c
// ✅ Use appropriate history sizes
embedids_metric_t fast_metric = {
    .max_history_size = 20,  // For high-frequency data
    // ...
};

embedids_metric_t slow_metric = {
    .max_history_size = 100, // For trend analysis
    // ...
};

// ✅ Choose appropriate thresholds
threshold_config.max_threshold.f32 = 85.0f;  // 85% CPU threshold
threshold_config.min_threshold.f32 = 5.0f;   // Minimum activity level
```

### 4. Performance Optimization

```c
// ✅ Batch operations when possible
for (int i = 0; i < num_sensors; i++) {
    embedids_add_datapoint(&context, sensor_names[i], values[i], timestamp);
}
// Analyze all at once
embedids_analyze_all(&context);

// ✅ Use appropriate data types
embedids_metric_value_t percentage_val = {.f32 = 75.5f};  // For percentages
embedids_metric_value_t count_val = {.u32 = 1024};        // For counts
```

## Troubleshooting

### Common Issues and Solutions

#### 1. Initialization Fails
```c
embedids_result_t result = embedids_init(&context, &config);
if (result == EMBEDIDS_ERROR_INVALID_PARAM) {
    // Check: config pointer is not NULL
    // Check: metric configurations are valid
    // Check: algorithm configurations are correct
}
```

#### 2. Metric Not Found
```c
result = embedids_add_datapoint(&context, "cpu_usage", value, timestamp);
if (result == EMBEDIDS_ERROR_METRIC_NOT_FOUND) {
    // Verify metric name matches exactly (case-sensitive)
    // Ensure metric was added to system configuration
    // Check metric is enabled
}
```

#### 3. Memory Issues
```c
// Ensure history arrays are large enough
static embedids_metric_datapoint_t history[SIZE];
if (SIZE < expected_data_points) {
    // Increase SIZE or reduce data retention period
}
```

#### 4. Algorithm Not Triggering
```c
// Check algorithm configuration
threshold_algo.enabled = true;  // Must be enabled
threshold_algo.config.threshold.check_max = true;  // Must enable checking

// Verify threshold values
printf("Threshold: %.2f, Current: %.2f\n", 
       threshold_config.max_threshold.f32, current_value);
```

### Debug Information

```c
// Check system status
if (embedids_is_initialized()) {
    printf("EmbedIDS is running\n");
} else {
    printf("EmbedIDS not initialized\n");
}

// Get version info
printf("EmbedIDS version: %s\n", embedids_get_version());

// Monitor metric states
for (int i = 0; i < num_metrics; i++) {
    printf("Metric %s: %d/%d data points\n", 
           metric_names[i], 
           metrics[i].current_size, 
           metrics[i].max_history_size);
}
```

### Performance Monitoring

```c
#include <time.h>

// Measure analysis performance
clock_t start = clock();
embedids_result_t result = embedids_analyze_all(&context);
clock_t end = clock();

double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
printf("Analysis took %.3f ms\n", cpu_time * 1000);
```

## Conclusion

EmbedIDS provides a flexible, efficient intrusion detection framework for embedded systems. Key takeaways:

1. **Start simple** with basic threshold monitoring
2. **Use user-managed memory** for predictable resource usage
3. **Combine multiple algorithms** for comprehensive detection
4. **Implement custom algorithms** for specialized requirements
5. **Follow embedded best practices** for reliable operation

For more examples, see the `examples/` directory in the EmbedIDS repository.

---

**Next Steps:**
- Try the provided examples
- Implement custom algorithms for your use case
- Integrate with your existing monitoring infrastructure
- Explore advanced configuration options

