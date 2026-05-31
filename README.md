# EmbedIDS

> **Modern Intrusion Detection System for Embedded Devices & IoT**

EmbedIDS is a lightweight, extensible intrusion detection library designed for embedded systems and IoT devices. It features user-managed memory, custom metrics, and pluggable detection algorithms with zero runtime overhead when disabled.

## Quick Start

```c
#include <embedids.h>

// Allocate history buffer (user-managed memory)
static embedids_metric_datapoint_t cpu_history[50];

// Configure CPU monitoring with 80% threshold
embedids_metric_config_t cpu_config = {
    .metric = {
        .name = "cpu_usage",
        .type = EMBEDIDS_METRIC_TYPE_PERCENTAGE,
        .history = cpu_history,
        .max_history_size = 50,
        .enabled = true
    },
    .algorithms = {{
        .type = EMBEDIDS_ALGORITHM_THRESHOLD,
        .enabled = true,
        .config.threshold = {
            .max_threshold.f32 = 80.0f,
            .check_max = true
        }
    }},
    .num_algorithms = 1
};

// Initialize context and system
embedids_context_t context;
memset(&context, 0, sizeof(context));

embedids_system_config_t system = {
    .metrics = &cpu_config,
    .max_metrics = 1,
    .num_active_metrics = 1
};

embedids_init(&context, &system);

// Monitor in real-time
embedids_metric_value_t value = {.f32 = get_cpu_usage()};
embedids_add_datapoint(&context, "cpu_usage", value, timestamp_ms);

if (embedids_analyze_metric(&context, "cpu_usage") != EMBEDIDS_OK) {
    handle_intrusion_detected();
}
```

## Architecture & Features

### **Extensible Design**
- **User-Managed Memory**: No malloc/free - perfect for embedded systems
- **Custom Metrics**: Support for float, int, percentage, boolean, enum types
- **Pluggable Algorithms**: Threshold, trend analysis, statistical, and custom detection
- **Multiple Algorithms per Metric**: Run several detection methods simultaneously
- **Real-time Analysis**: Low-latency threat detection with configurable history

### **Detection Algorithms**
| Algorithm | Description | Use Case |
|-----------|-------------|----------|
| **Threshold** | Min/max boundary checking | CPU usage, memory limits |
| **Trend** | Slope-based anomaly detection | Memory leaks, performance degradation |
| **Statistical** | Advanced statistical analysis | Complex pattern detection |
| **Custom** | User-defined detection functions | Domain-specific threats |

### **Metric Types**
- `EMBEDIDS_METRIC_TYPE_PERCENTAGE` - CPU usage, memory utilization (0-100%)
- `EMBEDIDS_METRIC_TYPE_FLOAT` - Sensor readings, network traffic
- `EMBEDIDS_METRIC_TYPE_UINT32/64` - Packet counts, process counts
- `EMBEDIDS_METRIC_TYPE_BOOL` - System states, security flags
- `EMBEDIDS_METRIC_TYPE_ENUM` - Custom enumerated values

## Installation

### **CMake (Recommended)**
```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
make -j$(nproc)
sudo make install
```

### **Integration Options**
```cmake
# Option 1: Installed package
find_package(EmbedIDS REQUIRED)
target_link_libraries(your_app EmbedIDS::embedids)

# Option 2: FetchContent (Git repository)
include(FetchContent)
FetchContent_Declare(
    EmbedIDS
    GIT_REPOSITORY https://github.com/Linuscent/EmbedIDS.git
    GIT_BRANCH main # Fetch the main branch
)
FetchContent_MakeAvailable(EmbedIDS)
target_link_libraries(your_app embedids)
```

### Build Options

- `BUILD_TESTS=ON/OFF` - Unit tests with GoogleTest (default: ON)
- `BUILD_EXAMPLES=ON/OFF` - Example applications (default: ON) 
- `ENABLE_COVERAGE=ON/OFF` - Code coverage reporting (default: OFF)

## Testing & Coverage

### Running Unit Tests

There are multiple ways to run the test suites

#### Method 1: Using CTest (Recommended)
```bash
# Build the project first
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)

# Run all tests
ctest

# Run tests with detailed output
ctest --verbose

# List available tests
ctest --list-tests
```

#### Method 2: Direct Test Execution  
```bash
# After building, run tests directly
./tests/embedids_tests

# Run specific test patterns (GoogleTest)
./tests/embedids_tests --gtest_filter="*Threshold*"
```

#### Method 3: Using make (if available)
```bash
make test  # May not be available in all configurations
```

### Code Coverage Analysis

Generate detailed coverage reports to see test effectiveness:

```bash
# Configure with coverage enabled
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON
make -j$(nproc)

# Generate coverage report
make coverage
```

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) file for details.
