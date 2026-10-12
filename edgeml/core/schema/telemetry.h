/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Common telemetry schema shared by every collector backend (simulation,
 * Linux, Jetson). The detection layer only ever sees these types.
 */

#ifndef EDGEML_CORE_SCHEMA_TELEMETRY_H
#define EDGEML_CORE_SCHEMA_TELEMETRY_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace edgeml {

using Labels = std::map<std::string, std::string>;

/** One normalized observation (spec section 8). */
struct MetricSample {
  uint64_t timestamp_ms = 0;
  std::string device_id;
  std::string source;
  std::string metric;
  double value = 0.0;
  std::string unit;
  Labels labels;
};

/** Process inventory entry. */
struct ProcessInfo {
  int pid = 0;
  std::string name;
  uint32_t uid = 0;
  uint64_t start_time_ms = 0;
  bool uses_gpu = false;
};

/** Established network connection. */
struct ConnectionInfo {
  std::string destination; /**< host:port */
  std::string process;     /**< owning process if known */
  bool outbound = true;
  uint64_t bytes_out_per_s = 0; /**< 0 when the backend cannot attribute */
};

/** File access observation (e.g. a read of the deployed model). */
struct FileAccessInfo {
  std::string path;
  std::string process;
  int pid = 0;
  uint64_t timestamp_ms = 0;
};

/**
 * Everything collected in a single sampling cycle. Collectors append to it;
 * the telemetry manager stamps device id and timestamps.
 */
struct TelemetryBatch {
  uint64_t cycle = 0;
  uint64_t timestamp_ms = 0;
  std::string device_id;

  std::vector<MetricSample> metrics;

  bool has_process_inventory = false;
  std::vector<ProcessInfo> processes;

  bool has_connection_inventory = false;
  std::vector<ConnectionInfo> connections;

  std::vector<FileAccessInfo> file_accesses;

  MetricSample &add(const std::string &source, const std::string &metric,
                    double value, const std::string &unit, Labels labels = {});
  const MetricSample *find(const std::string &metric) const;
  std::optional<double> value(const std::string &metric) const;
};

/** The spec names the collector output a MetricBatch. */
using MetricBatch = TelemetryBatch;

/** Static description of a known metric. */
struct MetricDescriptor {
  const char *name;
  const char *source;
  const char *unit;
  const char *description;
};

/** Catalogue of every MVP metric (spec section 9). */
const std::vector<MetricDescriptor> &metric_catalog();
const MetricDescriptor *find_metric_descriptor(const std::string &name);

namespace metrics {
// System
inline constexpr const char *kCpuUtilization = "system.cpu.utilization";
inline constexpr const char *kMemoryUsed = "system.memory.used";
inline constexpr const char *kMemoryPercent = "system.memory.percent";
inline constexpr const char *kProcessCount = "system.process.count";
inline constexpr const char *kLoadAverage = "system.load.average";
inline constexpr const char *kTemperature = "system.temperature";
// GPU
inline constexpr const char *kGpuUtilization = "gpu.utilization";
inline constexpr const char *kGpuMemoryUsed = "gpu.memory.used";
inline constexpr const char *kGpuMemoryPercent = "gpu.memory.percent";
inline constexpr const char *kGpuPower = "gpu.power";
inline constexpr const char *kGpuTemperature = "gpu.temperature";
inline constexpr const char *kGpuProcessCount = "gpu.process.count";
// AI workload
inline constexpr const char *kInferenceRate = "ai.inference.rate";
inline constexpr const char *kInferenceLatency = "ai.inference.latency";
inline constexpr const char *kInferenceErrors = "ai.inference.errors";
inline constexpr const char *kModelLoaded = "ai.model.loaded";
inline constexpr const char *kModelHash = "ai.model.hash";
inline constexpr const char *kPipelineHealth = "ai.pipeline.health";
// Network
inline constexpr const char *kConnectionsTotal = "network.connections.total";
inline constexpr const char *kConnectionsOutbound =
    "network.connections.outbound";
inline constexpr const char *kBytesOut = "network.bytes.out";
inline constexpr const char *kBytesIn = "network.bytes.in";
inline constexpr const char *kNewDestinations = "network.new.destinations";
inline constexpr const char *kFailedConnections = "network.failed.connections";
// Security
inline constexpr const char *kUnknownProcesses = "security.unknown_processes";
inline constexpr const char *kPrivilegedProcesses =
    "security.privileged_processes";
inline constexpr const char *kModelIntegrity = "security.model_integrity";
inline constexpr const char *kConfigIntegrity = "security.config_integrity";
inline constexpr const char *kAgentHeartbeat = "security.agent_heartbeat";
inline constexpr const char *kTelemetryHealth = "security.telemetry_health";
inline constexpr const char *kModelFileAccess = "security.model_file_access";
// Agent self-monitoring
inline constexpr const char *kAgentCpu = "agent.cpu.percent";
inline constexpr const char *kAgentMemory = "agent.memory.rss";
inline constexpr const char *kAgentLatency = "agent.processing.latency";
} // namespace metrics

void to_json(nlohmann::json &j, const MetricSample &sample);
void from_json(const nlohmann::json &j, MetricSample &sample);
void to_json(nlohmann::json &j, const ProcessInfo &process);
void from_json(const nlohmann::json &j, ProcessInfo &process);
void to_json(nlohmann::json &j, const ConnectionInfo &connection);
void from_json(const nlohmann::json &j, ConnectionInfo &connection);
void to_json(nlohmann::json &j, const FileAccessInfo &access);
void from_json(const nlohmann::json &j, FileAccessInfo &access);

} // namespace edgeml

#endif
