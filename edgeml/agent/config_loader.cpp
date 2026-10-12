/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent/config_loader.h"

#include <yaml-cpp/yaml.h>

#include "collectors/common/file_util.h"
#include "core/integrity/hash_util.h"

namespace edgeml {

namespace {

template <typename T>
void read(const YAML::Node &node, const char *key, T &out) {
  if (node && node[key]) {
    out = node[key].as<T>();
  }
}

void read_correlations(const YAML::Node &node, Config &config) {
  if (!node || !node.IsSequence()) {
    return;
  }
  std::vector<CorrelationRule> rules;
  for (const auto &item : node) {
    CorrelationRule rule;
    read(item, "name", rule.name);
    read(item, "title", rule.title);
    read(item, "description", rule.description);
    read(item, "window_seconds", rule.window_seconds);
    read(item, "requires", rule.requires_signals);
    std::string severity = "high";
    read(item, "severity", severity);
    auto parsed = parse_severity(severity);
    if (!parsed) {
      throw YAML::Exception(item.Mark(), "invalid severity '" + severity + "'");
    }
    rule.severity = *parsed;
    rules.push_back(std::move(rule));
  }
  config.correlations = std::move(rules);
}

} // namespace

bool apply_config_yaml(const std::string &yaml_text, Config &config,
                       std::string *error) {
  try {
    const YAML::Node root = YAML::Load(yaml_text);
    if (!root || root.IsNull()) {
      return true;
    }
    if (!root.IsMap()) {
      if (error) {
        *error = "configuration root must be a mapping";
      }
      return false;
    }

    const auto device = root["device"];
    read(device, "id", config.device.id);
    read(device, "platform", config.device.platform);
    read(device, "workload", config.device.workload);

    const auto sampling = root["sampling"];
    read(sampling, "interval_ms", config.sampling.interval_ms);
    read(sampling, "history_seconds", config.sampling.history_seconds);

    const auto simulation = root["simulation"];
    read(simulation, "mode", config.simulation.mode);
    read(simulation, "endpoint", config.simulation.endpoint);
    read(simulation, "seed", config.simulation.seed);
    read(simulation, "request_timeout_ms",
         config.simulation.request_timeout_ms);

    const auto system = root["system"];
    read(system, "cpu_exhaustion", config.system.cpu_exhaustion);
    read(system, "memory_exhaustion", config.system.memory_exhaustion);
    read(system, "exhaustion_seconds", config.system.exhaustion_seconds);

    const auto gpu = root["gpu"];
    read(gpu, "high_utilization", config.gpu.high_utilization);
    read(gpu, "sustain_seconds", config.gpu.sustain_seconds);

    const auto ai = root["ai"];
    read(ai, "expected_fps", config.ai.expected_fps);
    read(ai, "minimum_fps", config.ai.minimum_fps);
    read(ai, "baseline_ratio", config.ai.baseline_ratio);
    read(ai, "degradation_seconds", config.ai.degradation_seconds);
    read(ai, "stats_file", config.ai.stats_file);

    const auto network = root["network"];
    read(network, "allowed_destinations", config.network.allowed_destinations);
    read(network, "baseline_bytes_out", config.network.baseline_bytes_out);
    read(network, "anomaly_multiplier", config.network.anomaly_multiplier);
    read(network, "anomaly_seconds", config.network.anomaly_seconds);
    read(network, "escalation_bytes_out", config.network.escalation_bytes_out);
    read(network, "interface", config.network.interface);

    const auto process = root["process"];
    read(process, "allowed", config.process.allowed);
    read(process, "min_runtime_seconds", config.process.min_runtime_seconds);
    read(process, "ignore_kernel_threads",
         config.process.ignore_kernel_threads);

    const auto integrity = root["integrity"];
    if (integrity) {
      const auto model = integrity["model"];
      read(model, "path", config.integrity.model.path);
      read(model, "sha256", config.integrity.model.sha256);
      read(model, "allowed_readers", config.integrity.model.allowed_readers);
      read(integrity, "verify_config", config.integrity.verify_config);
      read(integrity, "rehash_interval_seconds",
           config.integrity.rehash_interval_seconds);
      read(integrity, "startup_grace_seconds",
           config.integrity.startup_grace_seconds);
      read(integrity, "access_window_seconds",
           config.integrity.access_window_seconds);
    }

    const auto heartbeat = root["heartbeat"];
    read(heartbeat, "interval_seconds", config.heartbeat.interval_seconds);
    read(heartbeat, "timeout_seconds", config.heartbeat.timeout_seconds);

    read_correlations(root["correlations"], config);

    const auto alerts = root["alerts"];
    read(alerts, "console", config.alerts.console);
    read(alerts, "json_file", config.alerts.json_file);
    read(alerts, "json_file_path", config.alerts.json_file_path);
    read(alerts, "syslog", config.alerts.syslog);
    read(alerts, "syslog_server", config.alerts.syslog_server);
    read(alerts, "webhook_url", config.alerts.webhook_url);
    read(alerts, "id_prefix", config.alerts.id_prefix);

    const auto storage = root["storage"];
    read(storage, "enabled", config.storage.enabled);
    read(storage, "path", config.storage.path);
    read(storage, "metrics_retention_hours",
         config.storage.metrics_retention_hours);
    read(storage, "events_retention_days",
         config.storage.events_retention_days);
    read(storage, "alerts_retention_days",
         config.storage.alerts_retention_days);

    const auto api = root["api"];
    read(api, "enabled", config.api.enabled);
    read(api, "bind", config.api.bind);
    read(api, "port", config.api.port);
    read(api, "static_dir", config.api.static_dir);
    read(api, "demo_controls", config.api.demo_controls);

    const auto linux_node = root["linux"];
    read(linux_node, "proc_root", config.linux_host.proc_root);
    read(linux_node, "sys_root", config.linux_host.sys_root);

    const auto jetson = root["jetson"];
    read(jetson, "tegrastats_path", config.jetson.tegrastats_path);
    read(jetson, "gpu_load_paths", config.jetson.gpu_load_paths);
  } catch (const YAML::Exception &e) {
    if (error) {
      *error = std::string("invalid configuration: ") + e.what();
    }
    return false;
  }

  const std::string problems = validate_config(config);
  if (!problems.empty()) {
    if (error) {
      *error = "invalid configuration: " + problems;
    }
    return false;
  }
  return true;
}

bool load_config_file(const std::string &path, Config &config,
                      std::string *error) {
  auto text = read_text_file(path);
  if (!text) {
    if (error) {
      *error = "cannot read configuration file " + path;
    }
    return false;
  }
  if (!apply_config_yaml(*text, config, error)) {
    return false;
  }
  config.source_path = path;
  config.source_sha256 = sha256_string(*text);
  return true;
}

} // namespace edgeml
