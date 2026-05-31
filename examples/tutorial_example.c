#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <embedids.h>

int main() {
    // 1. Allocate memory for metric history
    static embedids_metric_datapoint_t cpu_history[50];
    
    // 2. Configure CPU metric
    embedids_metric_t cpu_metric;
    memset(&cpu_metric, 0, sizeof(cpu_metric));
    strncpy(cpu_metric.name, "cpu_usage", EMBEDIDS_MAX_METRIC_NAME_LEN - 1);
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
    
    printf("CPU Monitor Started (threshold: 80%%)\n\n");
    
    // 7. Monitoring loop
    for (int i = 0; i < 10; i++) {
        // Simulate CPU usage (gradually increasing)
        float cpu = 30.0f + (i * 8.0f);
        
        // Add data point
        embedids_metric_value_t value = {.f32 = cpu};
        embedids_add_datapoint(&context, "cpu_usage", value, (uint64_t)time(NULL) * 1000);
        
        // Analyze metric to detect anomalies
        if (embedids_analyze_metric(&context, "cpu_usage") == EMBEDIDS_OK) {
            printf("OK  CPU: %.1f%% - Normal\n", cpu);
        } else {
            printf("ALERT CPU: %.1f%% - THRESHOLD EXCEEDED!\n", cpu);
        }
        
        sleep(1);
    }
    
    embedids_cleanup(&context);
    return 0;
}
