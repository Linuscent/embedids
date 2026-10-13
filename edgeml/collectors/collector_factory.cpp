/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/collector_factory.h"

#include "collectors/common/agent_self_collector.h"
#include "collectors/common/file_integrity_collector.h"
#include "collectors/jetson/jetson_collectors.h"
#include "collectors/linux/linux_collectors.h"
#include "collectors/simulation/simulated_collectors.h"

namespace edgeml {

namespace {

bool add_simulation_collectors(const Config &config, const Clock &clock,
                               CollectorSet &out, std::string *error) {
  std::shared_ptr<SimulationFeed> feed;
  if (config.simulation.mode == "embedded") {
    sim::SimulatorOptions options;
    options.device_id = config.device.id;
    options.seed = config.simulation.seed;
    options.model_path = config.integrity.model.path;
    auto simulator = std::make_shared<sim::EdgeDeviceSimulator>(options);
    if (!simulator->initialize(error)) {
      return false;
    }
    out.embedded_simulator = simulator;
    feed = std::make_shared<InProcessSimulationFeed>(simulator, clock);
    out.simulation_control =
        std::make_shared<InProcessSimulationControl>(simulator);
  } else {
    feed = std::make_shared<HttpSimulationFeed>(
        config.simulation.endpoint, config.simulation.request_timeout_ms);
    out.simulation_control = std::make_shared<HttpSimulationControl>(
        config.simulation.endpoint, config.simulation.request_timeout_ms);
  }
  out.collectors.push_back(std::make_unique<SimulatedSystemCollector>(feed));
  out.collectors.push_back(std::make_unique<SimulatedGpuCollector>(feed));
  out.collectors.push_back(std::make_unique<SimulatedInferenceCollector>(feed));
  out.collectors.push_back(std::make_unique<SimulatedNetworkCollector>(feed));
  out.collectors.push_back(std::make_unique<SimulatedProcessCollector>(feed));
  return true;
}

void add_linux_collectors(const Config &config, CollectorSet &out,
                          bool include_thermal) {
  const auto &proc = config.linux_host.proc_root;
  out.collectors.push_back(std::make_unique<ProcCpuCollector>(proc));
  out.collectors.push_back(std::make_unique<ProcMemoryCollector>(proc));
  out.collectors.push_back(std::make_unique<ProcProcessCollector>(
      proc, config.process.ignore_kernel_threads));
  out.collectors.push_back(
      std::make_unique<LinuxNetworkCollector>(proc, config.network.interface));
  if (include_thermal) {
    out.collectors.push_back(
        std::make_unique<LinuxThermalCollector>(config.linux_host.sys_root));
  }
}

} // namespace

bool build_collectors(const Config &config, const Clock &clock,
                      CollectorSet &out, std::string *error) {
  const auto &platform = config.device.platform;
  if (platform == "simulation") {
    if (!add_simulation_collectors(config, clock, out, error)) {
      return false;
    }
  } else if (platform == "linux") {
    add_linux_collectors(config, out, true);
    if (!config.ai.stats_file.empty()) {
      out.collectors.push_back(
          std::make_unique<InferenceStatsCollector>(config.ai.stats_file));
    }
  } else if (platform == "jetson") {
    add_linux_collectors(config, out, false);
    auto tegrastats = std::make_shared<TegrastatsSource>(
        config.jetson.tegrastats_path, config.sampling.interval_ms);
    out.collectors.push_back(std::make_unique<JetsonGpuCollector>(
        tegrastats, config.jetson.gpu_load_paths));
    out.collectors.push_back(
        std::make_unique<JetsonPowerCollector>(tegrastats));
    out.collectors.push_back(
        std::make_unique<JetsonThermalCollector>(tegrastats));
    if (!config.ai.stats_file.empty()) {
      out.collectors.push_back(
          std::make_unique<JetsonInferenceCollector>(config.ai.stats_file));
    }
  } else {
    if (error) {
      *error = "unsupported platform: " + platform;
    }
    return false;
  }

  // Platform-neutral self-protection collectors.
  FileIntegrityOptions model;
  model.name = "model-integrity";
  model.path = config.integrity.model.path;
  model.expected_sha256 = config.integrity.model.sha256;
  model.integrity_metric = metrics::kModelIntegrity;
  model.emit_model_metrics = true;
  model.rehash_interval_seconds = config.integrity.rehash_interval_seconds;
  model.startup_grace_seconds = config.integrity.startup_grace_seconds;
  out.collectors.push_back(
      std::make_unique<FileIntegrityCollector>(model, clock));

  if (config.integrity.verify_config && !config.source_path.empty()) {
    FileIntegrityOptions cfg;
    cfg.name = "config-integrity";
    cfg.path = config.source_path;
    cfg.expected_sha256 = config.source_sha256;
    cfg.integrity_metric = metrics::kConfigIntegrity;
    cfg.rehash_interval_seconds = config.integrity.rehash_interval_seconds;
    cfg.startup_grace_seconds = 0;
    out.collectors.push_back(
        std::make_unique<FileIntegrityCollector>(cfg, clock));
  }

  out.collectors.push_back(
      std::make_unique<AgentSelfCollector>(config.linux_host.proc_root));
  return true;
}

} // namespace edgeml
