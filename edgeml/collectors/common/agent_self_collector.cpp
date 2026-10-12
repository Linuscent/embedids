/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/common/agent_self_collector.h"

#include <ctime>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "core/common/clock.h"

namespace edgeml {

bool AgentSelfCollector::initialize() {
  ticks_per_second_ = sysconf(_SC_CLK_TCK);
  page_size_ = sysconf(_SC_PAGESIZE);
  if (ticks_per_second_ <= 0) {
    ticks_per_second_ = 100;
  }
  if (page_size_ <= 0) {
    page_size_ = 4096;
  }
  last_wall_ms_ = monotonic_ms();
  return read_cpu_ticks(last_ticks_);
}

bool AgentSelfCollector::read_cpu_ticks(uint64_t &ticks) const {
  std::ifstream in(proc_root_ + "/self/stat");
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  const size_t close = content.rfind(')');
  if (close == std::string::npos) {
    return false;
  }
  std::istringstream fields(content.substr(close + 2));
  std::string field;
  // After "pid (comm) " the state is field 3; utime/stime are fields 14/15.
  uint64_t utime = 0, stime = 0;
  for (int index = 3; fields >> field; index++) {
    if (index == 14) {
      utime = std::stoull(field);
    } else if (index == 15) {
      stime = std::stoull(field);
      break;
    }
  }
  ticks = utime + stime;
  return true;
}

bool AgentSelfCollector::collect(MetricBatch &batch) {
  uint64_t ticks = 0;
  if (!read_cpu_ticks(ticks)) {
    return false;
  }
  const uint64_t wall = monotonic_ms();
  const uint64_t wall_delta = wall - last_wall_ms_;
  if (wall_delta > 0) {
    const double cpu_ms = static_cast<double>(ticks - last_ticks_) * 1000.0 /
                          static_cast<double>(ticks_per_second_);
    batch.add("agent", metrics::kAgentCpu,
              100.0 * cpu_ms / static_cast<double>(wall_delta), "percent");
  }
  last_ticks_ = ticks;
  last_wall_ms_ = wall;

  std::ifstream statm(proc_root_ + "/self/statm");
  uint64_t size_pages = 0, resident_pages = 0;
  if (statm >> size_pages >> resident_pages) {
    batch.add("agent", metrics::kAgentMemory,
              static_cast<double>(resident_pages) *
                  static_cast<double>(page_size_) / (1024.0 * 1024.0),
              "MB");
  }
  return true;
}

bool InferenceStatsCollector::collect(MetricBatch &batch) {
  struct stat st {};
  if (::stat(path_.c_str(), &st) != 0) {
    return false;
  }
  if (std::time(nullptr) - st.st_mtime >
      static_cast<time_t>(max_age_seconds_)) {
    return false; // publisher stopped: counts against the heartbeat
  }
  std::ifstream in(path_);
  nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    return false;
  }
  try {
    if (doc.contains("fps")) {
      batch.add("ai", metrics::kInferenceRate, doc["fps"].get<double>(), "fps");
    }
    if (doc.contains("latency_ms")) {
      batch.add("ai", metrics::kInferenceLatency,
                doc["latency_ms"].get<double>(), "ms");
    }
    if (doc.contains("errors")) {
      batch.add("ai", metrics::kInferenceErrors, doc["errors"].get<double>(),
                "errors/s");
    }
    if (doc.contains("healthy")) {
      batch.add("ai", metrics::kPipelineHealth,
                doc["healthy"].get<bool>() ? 1.0 : 0.0, "bool");
    }
  } catch (const nlohmann::json::exception &) {
    return false;
  }
  return true;
}

} // namespace edgeml
