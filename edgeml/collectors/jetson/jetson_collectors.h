/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVIDIA Jetson collectors (hardware porting phase 2). These are written
 * against documented L4T interfaces (tegrastats, GPU load sysfs node) but have
 * not yet been validated on hardware; see docs/hardware-port.md.
 */

#ifndef EDGEML_COLLECTORS_JETSON_JETSON_COLLECTORS_H
#define EDGEML_COLLECTORS_JETSON_JETSON_COLLECTORS_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "collectors/common/agent_self_collector.h"
#include "collectors/common/collector.h"
#include "collectors/jetson/tegrastats_parser.h"

namespace edgeml {

/**
 * Runs `tegrastats --interval N` and keeps the latest parsed line.
 * Shared by the GPU, power and thermal collectors.
 */
class TegrastatsSource {
public:
  TegrastatsSource(std::string binary, uint32_t interval_ms);
  ~TegrastatsSource();

  TegrastatsSource(const TegrastatsSource &) = delete;
  TegrastatsSource &operator=(const TegrastatsSource &) = delete;

  bool start();
  /** Latest sample no older than max_age_ms. */
  std::optional<jetson::TegrastatsSample> latest(uint64_t max_age_ms) const;
  /** Feed a line directly (used by tests and file replay). */
  void ingest_line(const std::string &line);

private:
  void reader_loop(int fd);

  std::string binary_;
  uint32_t interval_ms_;
  int child_pid_ = -1;
  std::thread reader_;
  std::atomic<bool> running_{false};
  mutable std::mutex mutex_;
  std::optional<jetson::TegrastatsSample> latest_;
  uint64_t latest_ms_ = 0;
};

class JetsonGpuCollector : public MetricCollector {
public:
  JetsonGpuCollector(std::shared_ptr<TegrastatsSource> source,
                     std::vector<std::string> load_paths)
      : source_(std::move(source)), load_paths_(std::move(load_paths)) {}
  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "jetson-gpu"; }

private:
  std::optional<double> read_sysfs_load() const;

  std::shared_ptr<TegrastatsSource> source_;
  std::vector<std::string> load_paths_;
  std::string active_load_path_;
};

class JetsonPowerCollector : public MetricCollector {
public:
  explicit JetsonPowerCollector(std::shared_ptr<TegrastatsSource> source)
      : source_(std::move(source)) {}
  bool initialize() override { return source_ != nullptr; }
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "jetson-power"; }
  bool heartbeat_required() const override { return false; }

private:
  std::shared_ptr<TegrastatsSource> source_;
};

class JetsonThermalCollector : public MetricCollector {
public:
  explicit JetsonThermalCollector(std::shared_ptr<TegrastatsSource> source)
      : source_(std::move(source)) {}
  bool initialize() override { return source_ != nullptr; }
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "jetson-thermal"; }
  bool heartbeat_required() const override { return false; }

private:
  std::shared_ptr<TegrastatsSource> source_;
};

/** TensorRT pipeline statistics published by the inference application. */
class JetsonInferenceCollector : public InferenceStatsCollector {
public:
  explicit JetsonInferenceCollector(std::string stats_path)
      : InferenceStatsCollector(std::move(stats_path), "jetson-inference") {}
};

} // namespace edgeml

#endif
