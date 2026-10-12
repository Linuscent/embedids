/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/common/file_integrity_collector.h"

#include <algorithm>
#include <cctype>

#include <sys/stat.h>

#include "core/integrity/hash_util.h"

namespace edgeml {

FileIntegrityCollector::FileIntegrityCollector(FileIntegrityOptions options,
                                               const Clock &clock)
    : options_(std::move(options)), clock_(clock) {
  expected_ = options_.expected_sha256;
  std::transform(expected_.begin(), expected_.end(), expected_.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  baseline_source_ = expected_.empty() ? "trust-on-first-use" : "config";
}

bool FileIntegrityCollector::initialize() {
  started_ms_ = clock_.now_ms();
  return !options_.path.empty();
}

bool FileIntegrityCollector::collect(MetricBatch &batch) {
  const uint64_t now = clock_.now_ms();
  struct stat st {};
  const bool exists = ::stat(options_.path.c_str(), &st) == 0;

  if (!exists) {
    const bool in_grace =
        now - started_ms_ < options_.startup_grace_seconds * 1000ULL;
    if (in_grace && current_.empty()) {
      return true; // not deployed yet
    }
    current_ = "missing";
    have_stat_ = false;
  } else {
    const int64_t mtime_ns =
        static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL +
        st.st_mtim.tv_nsec;
    const bool changed = !have_stat_ ||
                         static_cast<uint64_t>(st.st_size) != last_size_ ||
                         mtime_ns != last_mtime_ns_ ||
                         static_cast<uint64_t>(st.st_ino) != last_inode_;
    const bool due =
        now - last_hash_ms_ >= options_.rehash_interval_seconds * 1000ULL;
    if (changed || due || current_ == "missing" || current_.empty()) {
      auto hash = sha256_file(options_.path);
      current_ = hash ? *hash : "unreadable";
      last_hash_ms_ = now;
      have_stat_ = true;
      last_size_ = static_cast<uint64_t>(st.st_size);
      last_mtime_ns_ = mtime_ns;
      last_inode_ = static_cast<uint64_t>(st.st_ino);
    }
    if (expected_.empty() && hash_like(current_)) {
      expected_ = current_;
    }
  }

  const bool verified = !expected_.empty() && current_ == expected_;
  const std::string status =
      verified ? "verified" : (current_ == "missing" ? "missing" : "violated");
  batch.add("integrity", options_.integrity_metric, verified ? 1.0 : 0.0,
            "bool",
            {{"path", options_.path},
             {"expected", expected_},
             {"current", current_},
             {"status", status},
             {"baseline", baseline_source_}});

  if (options_.emit_model_metrics) {
    batch.add("ai", metrics::kModelLoaded, exists ? 1.0 : 0.0, "bool",
              {{"path", options_.path}});
    batch.add("ai", metrics::kModelHash, exists ? 1.0 : 0.0, "info",
              {{"sha256", current_}});
  }
  return true;
}

bool FileIntegrityCollector::hash_like(const std::string &value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(),
                     [](unsigned char c) { return std::isxdigit(c); });
}

} // namespace edgeml
