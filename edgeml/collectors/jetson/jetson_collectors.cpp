/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/jetson/jetson_collectors.h"

#include <csignal>
#include <cstdio>

#include <sys/wait.h>
#include <unistd.h>

#include "collectors/common/file_util.h"
#include "core/common/clock.h"

namespace edgeml {

namespace {
constexpr uint64_t kMaxSampleAgeMs = 5000;
}

TegrastatsSource::TegrastatsSource(std::string binary, uint32_t interval_ms)
    : binary_(std::move(binary)), interval_ms_(interval_ms) {}

TegrastatsSource::~TegrastatsSource() {
  running_ = false;
  if (child_pid_ > 0) {
    ::kill(child_pid_, SIGTERM);
  }
  if (reader_.joinable()) {
    reader_.join();
  }
  if (child_pid_ > 0) {
    ::waitpid(child_pid_, nullptr, 0);
  }
}

bool TegrastatsSource::start() {
  if (running_) {
    return true;
  }
  if (::access(binary_.c_str(), X_OK) != 0) {
    return false;
  }
  int pipe_fds[2];
  if (::pipe(pipe_fds) != 0) {
    return false;
  }
  const std::string interval = std::to_string(interval_ms_);
  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    return false;
  }
  if (pid == 0) {
    ::dup2(pipe_fds[1], STDOUT_FILENO);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    ::execl(binary_.c_str(), binary_.c_str(), "--interval", interval.c_str(),
            static_cast<char *>(nullptr));
    ::_exit(127);
  }
  ::close(pipe_fds[1]);
  child_pid_ = pid;
  running_ = true;
  reader_ = std::thread(&TegrastatsSource::reader_loop, this, pipe_fds[0]);
  return true;
}

void TegrastatsSource::reader_loop(int fd) {
  FILE *stream = ::fdopen(fd, "r");
  if (stream == nullptr) {
    ::close(fd);
    return;
  }
  char buffer[2048];
  while (running_ && std::fgets(buffer, sizeof(buffer), stream) != nullptr) {
    ingest_line(buffer);
  }
  std::fclose(stream);
}

void TegrastatsSource::ingest_line(const std::string &line) {
  auto sample = jetson::parse_tegrastats_line(line);
  if (!sample) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  latest_ = std::move(sample);
  latest_ms_ = monotonic_ms();
}

std::optional<jetson::TegrastatsSample>
TegrastatsSource::latest(uint64_t max_age_ms) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!latest_ || monotonic_ms() - latest_ms_ > max_age_ms) {
    return std::nullopt;
  }
  return latest_;
}

bool JetsonGpuCollector::initialize() {
  for (const auto &path : load_paths_) {
    if (read_text_file(path)) {
      active_load_path_ = path;
      break;
    }
  }
  const bool tegrastats = source_ && source_->start();
  return !active_load_path_.empty() || tegrastats;
}

std::optional<double> JetsonGpuCollector::read_sysfs_load() const {
  if (active_load_path_.empty()) {
    return std::nullopt;
  }
  auto text = read_text_file(active_load_path_);
  if (!text) {
    return std::nullopt;
  }
  try {
    return std::stod(*text) / 10.0; // per-mille -> percent
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

bool JetsonGpuCollector::collect(MetricBatch &batch) {
  auto sample = source_ ? source_->latest(kMaxSampleAgeMs) : std::nullopt;
  std::optional<double> load = read_sysfs_load();
  if (!load && sample) {
    load = sample->gpu_load_percent;
  }
  if (!load) {
    return false;
  }
  batch.add("gpu", metrics::kGpuUtilization, *load, "percent",
            {{"backend", active_load_path_.empty() ? "tegrastats" : "sysfs"}});
  if (sample) {
    if (auto temp = sample->gpu_temperature()) {
      batch.add("gpu", metrics::kGpuTemperature, *temp, "celsius");
    }
    // Jetson GPUs share system RAM; report unified memory usage.
    if (sample->ram_used_mb && sample->ram_total_mb &&
        *sample->ram_total_mb > 0) {
      batch.add("gpu", metrics::kGpuMemoryUsed, *sample->ram_used_mb, "MB",
                {{"memory", "unified"}});
      batch.add("gpu", metrics::kGpuMemoryPercent,
                100.0 * *sample->ram_used_mb / *sample->ram_total_mb, "percent",
                {{"memory", "unified"}});
    }
  }
  return true;
}

bool JetsonPowerCollector::collect(MetricBatch &batch) {
  auto sample = source_->latest(kMaxSampleAgeMs);
  if (!sample) {
    return false;
  }
  if (auto watts = sample->gpu_power_watts()) {
    batch.add("gpu", metrics::kGpuPower, *watts, "watts");
  }
  return true;
}

bool JetsonThermalCollector::collect(MetricBatch &batch) {
  auto sample = source_->latest(kMaxSampleAgeMs);
  if (!sample) {
    return false;
  }
  if (auto temp = sample->soc_temperature()) {
    batch.add("system", metrics::kTemperature, *temp, "celsius");
  }
  return true;
}

} // namespace edgeml
