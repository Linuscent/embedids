/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_COLLECTORS_COMMON_AGENT_SELF_COLLECTOR_H
#define EDGEML_COLLECTORS_COMMON_AGENT_SELF_COLLECTOR_H

#include <cstdint>
#include <string>

#include "collectors/common/collector.h"

namespace edgeml {

/** Measures the agent's own CPU and memory footprint (spec section 23). */
class AgentSelfCollector : public MetricCollector {
public:
  explicit AgentSelfCollector(std::string proc_root = "/proc")
      : proc_root_(std::move(proc_root)) {}

  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "agent-self"; }
  bool heartbeat_required() const override { return false; }

private:
  bool read_cpu_ticks(uint64_t &ticks) const;

  std::string proc_root_;
  uint64_t last_ticks_ = 0;
  uint64_t last_wall_ms_ = 0;
  long ticks_per_second_ = 100;
  long page_size_ = 4096;
};

/**
 * Reads inference statistics published by the AI application as a small JSON
 * document, e.g. {"fps": 29.8, "latency_ms": 31.2, "errors": 0,
 * "healthy": true}. This is the integration contract for real TensorRT
 * pipelines on Linux and Jetson.
 */
class InferenceStatsCollector : public MetricCollector {
public:
  InferenceStatsCollector(std::string path,
                          std::string name = "inference-stats",
                          uint32_t max_age_seconds = 5)
      : path_(std::move(path)), name_(std::move(name)),
        max_age_seconds_(max_age_seconds) {}

  bool initialize() override { return !path_.empty(); }
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return name_.c_str(); }

private:
  std::string path_;
  std::string name_;
  uint32_t max_age_seconds_;
};

} // namespace edgeml

#endif
