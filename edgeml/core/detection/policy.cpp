/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/detection/policy.h"

#include <algorithm>

namespace edgeml {

bool matches_pattern(const std::string &value, const std::string &pattern) {
  if (!pattern.empty() && pattern.back() == '*') {
    return value.compare(0, pattern.size() - 1, pattern, 0,
                         pattern.size() - 1) == 0;
  }
  return value == pattern;
}

bool ProcessPolicy::allowed(const std::string &name) const {
  return std::any_of(config_.allowed.begin(), config_.allowed.end(),
                     [&](const std::string &pattern) {
                       return matches_pattern(name, pattern);
                     });
}

ProcessPolicyResult
ProcessPolicy::evaluate(const std::vector<ProcessInfo> &processes,
                        uint64_t now_ms) const {
  ProcessPolicyResult result;
  const uint64_t min_runtime_ms = config_.min_runtime_seconds * 1000ULL;
  for (const auto &process : processes) {
    if (allowed(process.name)) {
      continue;
    }
    const uint64_t runtime =
        now_ms > process.start_time_ms ? now_ms - process.start_time_ms : 0;
    if (runtime < min_runtime_ms) {
      continue;
    }
    result.unknown.push_back(process);
    if (process.uid == 0) {
      result.privileged.push_back(process);
    }
  }
  return result;
}

bool NetworkPolicy::allowed(const std::string &destination) const {
  const std::string host = destination.substr(0, destination.rfind(':'));
  for (const auto &entry : config_.allowed_destinations) {
    if (entry == destination) {
      return true;
    }
    if (entry.find(':') == std::string::npos && matches_pattern(host, entry)) {
      return true;
    }
  }
  return false;
}

std::vector<DestinationFinding>
NetworkPolicy::evaluate(const std::vector<ConnectionInfo> &connections,
                        uint64_t now_ms) {
  std::map<std::string, DestinationFinding> unknown;
  for (const auto &connection : connections) {
    if (!connection.outbound || allowed(connection.destination)) {
      continue;
    }
    auto seen = first_seen_.emplace(connection.destination, now_ms).first;
    DestinationFinding &finding = unknown[connection.destination];
    finding.destination = connection.destination;
    if (finding.process.empty()) {
      finding.process = connection.process;
    }
    finding.first_seen_ms = seen->second;
    finding.bytes_out_per_s += connection.bytes_out_per_s;
  }

  std::vector<DestinationFinding> result;
  result.reserve(unknown.size());
  for (auto &entry : unknown) {
    result.push_back(std::move(entry.second));
  }
  return result;
}

void ModelAccessPolicy::observe(const std::vector<FileAccessInfo> &accesses,
                                uint64_t now_ms) {
  for (const auto &access : accesses) {
    if (access.path != config_.model.path) {
      continue;
    }
    const bool allowed = std::any_of(
        config_.model.allowed_readers.begin(),
        config_.model.allowed_readers.end(), [&](const std::string &pattern) {
          return matches_pattern(access.process, pattern);
        });
    if (!allowed) {
      FileAccessInfo copy = access;
      if (copy.timestamp_ms == 0) {
        copy.timestamp_ms = now_ms;
      }
      recent_.push_back(std::move(copy));
    }
  }
  const uint64_t window = config_.access_window_seconds * 1000ULL;
  while (!recent_.empty() && recent_.front().timestamp_ms + window < now_ms) {
    recent_.pop_front();
  }
}

std::vector<FileAccessInfo>
ModelAccessPolicy::recent_unexpected(uint64_t now_ms) const {
  const uint64_t window = config_.access_window_seconds * 1000ULL;
  std::vector<FileAccessInfo> result;
  for (const auto &access : recent_) {
    if (access.timestamp_ms + window >= now_ms) {
      result.push_back(access);
    }
  }
  return result;
}

} // namespace edgeml
