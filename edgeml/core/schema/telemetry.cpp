/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/schema/telemetry.h"

#include "core/common/clock.h"

#include <cstring>
#include <ctime>

namespace edgeml {

MetricSample &TelemetryBatch::add(const std::string &source,
                                  const std::string &metric, double value,
                                  const std::string &unit, Labels labels) {
  MetricSample sample;
  sample.timestamp_ms = timestamp_ms;
  sample.device_id = device_id;
  sample.source = source;
  sample.metric = metric;
  sample.value = value;
  sample.unit = unit;
  sample.labels = std::move(labels);
  metrics.push_back(std::move(sample));
  return metrics.back();
}

const MetricSample *TelemetryBatch::find(const std::string &metric) const {
  for (const auto &sample : metrics) {
    if (sample.metric == metric) {
      return &sample;
    }
  }
  return nullptr;
}

std::optional<double> TelemetryBatch::value(const std::string &metric) const {
  const MetricSample *sample = find(metric);
  if (sample == nullptr) {
    return std::nullopt;
  }
  return sample->value;
}

const std::vector<MetricDescriptor> &metric_catalog() {
  static const std::vector<MetricDescriptor> catalog = {
      {metrics::kCpuUtilization, "system", "percent", "CPU utilization"},
      {metrics::kMemoryUsed, "system", "MB", "Memory in use"},
      {metrics::kMemoryPercent, "system", "percent", "Memory in use"},
      {metrics::kProcessCount, "system", "count", "Running processes"},
      {metrics::kLoadAverage, "system", "load", "1 minute load average"},
      {metrics::kTemperature, "system", "celsius", "SoC temperature"},
      {metrics::kGpuUtilization, "gpu", "percent", "GPU utilization"},
      {metrics::kGpuMemoryUsed, "gpu", "MB", "GPU memory in use"},
      {metrics::kGpuMemoryPercent, "gpu", "percent", "GPU memory in use"},
      {metrics::kGpuPower, "gpu", "watts", "GPU power draw"},
      {metrics::kGpuTemperature, "gpu", "celsius", "GPU temperature"},
      {metrics::kGpuProcessCount, "gpu", "count", "Processes using the GPU"},
      {metrics::kInferenceRate, "ai", "fps", "Inference throughput"},
      {metrics::kInferenceLatency, "ai", "ms", "Inference latency"},
      {metrics::kInferenceErrors, "ai", "errors/s", "Inference errors"},
      {metrics::kModelLoaded, "ai", "bool", "Model present and loaded"},
      {metrics::kModelHash, "ai", "info", "Model hash (label sha256)"},
      {metrics::kPipelineHealth, "ai", "bool", "Inference pipeline health"},
      {metrics::kConnectionsTotal, "network", "count", "Open connections"},
      {metrics::kConnectionsOutbound, "network", "count",
       "Outbound connections"},
      {metrics::kBytesOut, "network", "bytes/s", "Outbound traffic"},
      {metrics::kBytesIn, "network", "bytes/s", "Inbound traffic"},
      {metrics::kNewDestinations, "policy", "count",
       "Connected destinations outside the allow list"},
      {metrics::kFailedConnections, "network", "count",
       "Failed / pending connections"},
      {metrics::kUnknownProcesses, "policy", "count",
       "Processes outside the allow list"},
      {metrics::kPrivilegedProcesses, "policy", "count",
       "Unknown processes running as UID 0"},
      {metrics::kModelIntegrity, "integrity", "bool",
       "1 = model hash matches the approved value"},
      {metrics::kConfigIntegrity, "integrity", "bool",
       "1 = configuration unchanged since startup"},
      {metrics::kAgentHeartbeat, "agent", "seconds",
       "Age of the oldest collector heartbeat"},
      {metrics::kTelemetryHealth, "agent", "percent",
       "Collectors that reported this cycle"},
      {metrics::kModelFileAccess, "policy", "count",
       "Recent model reads by unexpected processes"},
      {metrics::kAgentCpu, "agent", "percent", "Agent CPU usage"},
      {metrics::kAgentMemory, "agent", "MB", "Agent resident memory"},
      {metrics::kAgentLatency, "agent", "ms", "Detection cycle latency"},
  };
  return catalog;
}

const MetricDescriptor *find_metric_descriptor(const std::string &name) {
  for (const auto &descriptor : metric_catalog()) {
    if (name == descriptor.name) {
      return &descriptor;
    }
  }
  return nullptr;
}

namespace {

uint64_t parse_iso8601(const std::string &text) {
  std::tm tm_utc{};
  if (strptime(text.c_str(), "%Y-%m-%dT%H:%M:%S", &tm_utc) == nullptr) {
    return 0;
  }
  return static_cast<uint64_t>(timegm(&tm_utc)) * 1000ULL;
}

} // namespace

void to_json(nlohmann::json &j, const MetricSample &sample) {
  j = nlohmann::json{{"timestamp", format_iso8601(sample.timestamp_ms)},
                     {"timestamp_ms", sample.timestamp_ms},
                     {"device_id", sample.device_id},
                     {"source", sample.source},
                     {"metric", sample.metric},
                     {"value", sample.value},
                     {"unit", sample.unit},
                     {"labels", sample.labels}};
}

void from_json(const nlohmann::json &j, MetricSample &sample) {
  sample = MetricSample{};
  if (j.contains("timestamp_ms")) {
    sample.timestamp_ms = j.at("timestamp_ms").get<uint64_t>();
  } else if (j.contains("timestamp")) {
    sample.timestamp_ms = parse_iso8601(j.at("timestamp").get<std::string>());
  }
  sample.device_id = j.value("device_id", "");
  sample.source = j.value("source", "");
  sample.metric = j.at("metric").get<std::string>();
  sample.value = j.at("value").get<double>();
  sample.unit = j.value("unit", "");
  if (j.contains("labels")) {
    sample.labels = j.at("labels").get<Labels>();
  }
}

void to_json(nlohmann::json &j, const ProcessInfo &process) {
  j = nlohmann::json{{"pid", process.pid},
                     {"name", process.name},
                     {"uid", process.uid},
                     {"start_time_ms", process.start_time_ms},
                     {"uses_gpu", process.uses_gpu}};
}

void from_json(const nlohmann::json &j, ProcessInfo &process) {
  process.pid = j.value("pid", 0);
  process.name = j.at("name").get<std::string>();
  process.uid = j.value("uid", 0u);
  process.start_time_ms = j.value("start_time_ms", uint64_t{0});
  process.uses_gpu = j.value("uses_gpu", false);
}

void to_json(nlohmann::json &j, const ConnectionInfo &connection) {
  j = nlohmann::json{{"destination", connection.destination},
                     {"process", connection.process},
                     {"outbound", connection.outbound},
                     {"bytes_out_per_s", connection.bytes_out_per_s}};
}

void from_json(const nlohmann::json &j, ConnectionInfo &connection) {
  connection.destination = j.at("destination").get<std::string>();
  connection.process = j.value("process", "");
  connection.outbound = j.value("outbound", true);
  connection.bytes_out_per_s = j.value("bytes_out_per_s", uint64_t{0});
}

void to_json(nlohmann::json &j, const FileAccessInfo &access) {
  j = nlohmann::json{{"path", access.path},
                     {"process", access.process},
                     {"pid", access.pid},
                     {"timestamp_ms", access.timestamp_ms}};
}

void from_json(const nlohmann::json &j, FileAccessInfo &access) {
  access.path = j.at("path").get<std::string>();
  access.process = j.value("process", "");
  access.pid = j.value("pid", 0);
  access.timestamp_ms = j.value("timestamp_ms", uint64_t{0});
}

} // namespace edgeml
