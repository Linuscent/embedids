/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Allow-list policies that turn inventories (processes, connections, file
 * accesses) into numeric security metrics for EmbedIDS.
 */

#ifndef EDGEML_CORE_DETECTION_POLICY_H
#define EDGEML_CORE_DETECTION_POLICY_H

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "core/config/config.h"
#include "core/schema/telemetry.h"

namespace edgeml {

/** Exact match, or prefix match when the pattern ends with '*'. */
bool matches_pattern(const std::string &value, const std::string &pattern);

struct ProcessPolicyResult {
  std::vector<ProcessInfo> unknown;    /**< outside allow list, runtime met */
  std::vector<ProcessInfo> privileged; /**< subset of unknown with UID 0 */
};

class ProcessPolicy {
public:
  explicit ProcessPolicy(const ProcessConfig &config) : config_(config) {}
  bool allowed(const std::string &name) const;
  ProcessPolicyResult evaluate(const std::vector<ProcessInfo> &processes,
                               uint64_t now_ms) const;

private:
  ProcessConfig config_;
};

struct DestinationFinding {
  std::string destination;
  std::string process;
  uint64_t first_seen_ms = 0;
  uint64_t bytes_out_per_s = 0;
};

class NetworkPolicy {
public:
  explicit NetworkPolicy(const NetworkConfig &config) : config_(config) {}
  bool allowed(const std::string &destination) const;
  /** Unknown outbound destinations currently connected. */
  std::vector<DestinationFinding>
  evaluate(const std::vector<ConnectionInfo> &connections, uint64_t now_ms);
  void reset() { first_seen_.clear(); }

private:
  NetworkConfig config_;
  std::map<std::string, uint64_t> first_seen_;
};

class ModelAccessPolicy {
public:
  explicit ModelAccessPolicy(const IntegrityConfig &config) : config_(config) {}
  void observe(const std::vector<FileAccessInfo> &accesses, uint64_t now_ms);
  /** Unexpected accesses inside the configured window. */
  std::vector<FileAccessInfo> recent_unexpected(uint64_t now_ms) const;
  void reset() { recent_.clear(); }

private:
  IntegrityConfig config_;
  std::deque<FileAccessInfo> recent_;
};

} // namespace edgeml

#endif
