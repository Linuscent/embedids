/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared helpers for EdgeML unit and integration tests.
 */

#ifndef EDGEML_TESTS_SUPPORT_TEST_SUPPORT_H
#define EDGEML_TESTS_SUPPORT_TEST_SUPPORT_H

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include "agent/agent.h"
#include "core/common/clock.h"
#include "core/config/config.h"
#include "simulator/workload/model_file.h"

namespace edgeml::test {

/** Self-deleting temporary directory. */
class TempDir {
public:
  TempDir() {
    char pattern[] = "/tmp/edgeml-test-XXXXXX";
    const char *dir = mkdtemp(pattern);
    path_ = dir ? dir : "/tmp";
  }
  ~TempDir() {
    if (path_ != "/tmp") {
      const std::string command = "rm -rf '" + path_ + "'";
      [[maybe_unused]] int rc = std::system(command.c_str());
    }
  }
  const std::string &path() const { return path_; }
  std::string file(const std::string &name) const { return path_ + "/" + name; }

private:
  std::string path_;
};

/** Embedded-simulation configuration with all output sinks disabled. */
inline Config make_test_config(const TempDir &dir) {
  Config config;
  config.device.id = "test-edge-001";
  config.device.platform = "simulation";
  config.simulation.mode = "embedded";
  config.simulation.seed = 7;
  config.integrity.model.path = dir.file("models/model.engine");
  config.integrity.model.sha256 = sim::approved_model_sha256();
  config.alerts.console = false;
  config.alerts.json_file = false;
  config.storage.enabled = false;
  config.api.port = 0;
  return config;
}

/**
 * Drives an Agent with a ManualClock: each step advances one sampling
 * interval and runs one detection cycle, so minutes of device time run in
 * milliseconds and every scenario is deterministic.
 */
class AgentHarness {
public:
  explicit AgentHarness(Config config)
      : config_(std::move(config)), clock_(1790000000000ULL),
        agent_(std::make_unique<Agent>(config_, clock_)) {}

  bool initialize(std::string *error = nullptr) {
    return agent_->initialize(error);
  }

  void step(int cycles = 1) {
    for (int i = 0; i < cycles; i++) {
      clock_.advance_ms(config_.sampling.interval_ms);
      agent_->run_cycle();
    }
  }

  /** Step until an alert for rule is raised; returns cycles taken. */
  std::optional<int> step_until(const std::string &rule, int max_cycles) {
    for (int i = 1; i <= max_cycles; i++) {
      step();
      if (find(rule)) {
        return i;
      }
    }
    return std::nullopt;
  }

  bool activate(const std::string &scenario) {
    std::string error;
    return agent_->demo_scenario(scenario, nullptr, &error);
  }

  bool reset() {
    std::string error;
    return agent_->demo_reset(nullptr, &error);
  }

  std::vector<Alert> alerts() const { return agent_->alert_snapshot(); }

  std::vector<Alert> active() const {
    std::vector<Alert> result;
    for (const auto &alert : alerts()) {
      if (alert.active) {
        result.push_back(alert);
      }
    }
    return result;
  }

  /** Most recent alert (active or not) for a rule. */
  std::optional<Alert> find(const std::string &rule) const {
    const auto all = alerts();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
      if (it->rule == rule) {
        return *it;
      }
    }
    return std::nullopt;
  }

  std::optional<Alert> find_active(const std::string &rule) const {
    for (const auto &alert : active()) {
      if (alert.rule == rule) {
        return alert;
      }
    }
    return std::nullopt;
  }

  Agent &agent() { return *agent_; }
  ManualClock &clock() { return clock_; }
  const Config &config() const { return config_; }

private:
  Config config_;
  ManualClock clock_;
  std::unique_ptr<Agent> agent_;
};

} // namespace edgeml::test

#endif
