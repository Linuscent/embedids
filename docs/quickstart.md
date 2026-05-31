# Quick Start

Get up and running with EmbedIDS in 5 minutes!

## Installation

```bash
git clone https://github.com/Linuscent/EmbedIDS.git
cd EmbedIDS
mkdir build && cd build
cmake ..
make
```

## Hello World Example

Create `my_monitor.c`:

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "embedids.h"

int main() {
    // 1. Allocate memory for metric history
    static embedids_metric_datapoint_t cpu_history[50];
    
    // 2. Configure CPU metric
    embedids_metric_t cpu_metric;
    memset(&cpu_metric, 0, sizeof(cpu_metric));
    strcpy(cpu_metric.name, "cpu_usage");
    cpu_metric.type = EMBEDIDS_METRIC_TYPE_PERCENTAGE;
    cpu_metric.history = cpu_history;
    cpu_metric.max_history_size = 50;
    cpu_metric.enabled = true;
    
    // 3. Configure threshold algorithm (alert if CPU > 80%)
    embedids_algorithm_t threshold_algo;
    memset(&threshold_algo, 0, sizeof(threshold_algo));
    threshold_algo.type = EMBEDIDS_ALGORITHM_THRESHOLD;
    threshold_algo.enabled = true;
    threshold_algo.config.threshold.max_threshold.f32 = 80.0f;
    threshold_algo.config.threshold.check_max = true;
    
    // 4. Create metric configuration
    embedids_metric_config_t metric_config;
    memset(&metric_config, 0, sizeof(metric_config));
    metric_config.metric = cpu_metric;
    metric_config.algorithms[0] = threshold_algo;
    metric_config.num_algorithms = 1;
    
    // 5. Create system configuration
    embedids_system_config_t system_config;
    memset(&system_config, 0, sizeof(system_config));
    system_config.metrics = &metric_config;
    system_config.max_metrics = 1;
    system_config.num_active_metrics = 1;
    
    // 6. Initialize EmbedIDS context and system
    embedids_context_t context;
    memset(&context, 0, sizeof(context));
    
    if (embedids_init(&context, &system_config) != EMBEDIDS_OK) {
        printf("Failed to initialize EmbedIDS\n");
        return 1;
    }
    
    printf("🔒 CPU Monitor Started (threshold: 80%%)\n\n");
    
    // 7. Monitoring loop
    for (int i = 0; i < 10; i++) {
        // Simulate CPU usage (gradually increasing)
        float cpu = 30.0f + (i * 8.0f);
        
        // Add data point
        embedids_metric_value_t value = {.f32 = cpu};
        embedids_add_datapoint(&context, "cpu_usage", value, time(NULL) * 1000);
        
        // Check for threats
        if (embedids_analyze_metric(&context, "cpu_usage") == EMBEDIDS_OK) {
            printf("✅ CPU: %.1f%% - Normal\n", cpu);
        } else {
            printf("🚨 CPU: %.1f%% - ALERT!\n", cpu);
        }
        
        sleep(1);
    }
    
    embedids_cleanup(&context);
    return 0;
}
```

## Compile and Run

```bash
gcc -o my_monitor my_monitor.c -L. -lembedids -I../include
./my_monitor
```

## Expected Output

```
🔒 CPU Monitor Started (threshold: 80%)

✅ CPU: 30.0% - Normal
✅ CPU: 38.0% - Normal
✅ CPU: 46.0% - Normal
✅ CPU: 54.0% - Normal
✅ CPU: 62.0% - Normal
✅ CPU: 70.0% - Normal
✅ CPU: 78.0% - Normal
🚨 CPU: 86.0% - ALERT!
🚨 CPU: 94.0% - ALERT!
🚨 CPU: 102.0% - ALERT!
```

## What Just Happened?

1. **Created a metric**: CPU usage with 50-point history
2. **Added detection**: Threshold algorithm at 80%
3. **Monitored in real-time**: Added data points and analyzed
4. **Got alerts**: When CPU exceeded threshold

## Next Steps

- **Multiple metrics**: Monitor CPU, memory, network together
- **Custom algorithms**: Implement your own detection logic
- **Advanced features**: Trend analysis, pattern detection
- **Real sensors**: Replace simulated data with actual sensor readings

See the [full tutorial](tutorial.md) for comprehensive examples and advanced usage!

## Quick Reference

### Core Functions
```c
embedids_init(&context, &config)             // Initialize system
embedids_add_datapoint(&context, name, val, time) // Add sensor data
embedids_analyze_metric(&context, name)      // Check one metric
embedids_analyze_all(&context)               // Check all metrics
embedids_cleanup(&context)                   // Shutdown system
```

### Algorithm Types
- `EMBEDIDS_ALGORITHM_THRESHOLD` - Simple min/max limits
- `EMBEDIDS_ALGORITHM_TREND` - Slope analysis over time
- `EMBEDIDS_ALGORITHM_CUSTOM` - Your own detection logic

### Return Codes
- `EMBEDIDS_OK` - All normal
- `EMBEDIDS_ERROR_THRESHOLD_EXCEEDED` - Threat detected
- `EMBEDIDS_ERROR_NOT_INITIALIZED` - Call init first
- `EMBEDIDS_ERROR_METRIC_NOT_FOUND` - Check metric name
