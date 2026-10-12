/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_COLLECTORS_COMMON_FILE_INTEGRITY_COLLECTOR_H
#define EDGEML_COLLECTORS_COMMON_FILE_INTEGRITY_COLLECTOR_H

#include <cstdint>
#include <string>

#include "collectors/common/collector.h"
#include "core/common/clock.h"

namespace edgeml {

struct FileIntegrityOptions {
  std::string name = "file-integrity";
  std::string path;
  /** Approved SHA-256; empty means trust the first observed hash. */
  std::string expected_sha256;
  std::string integrity_metric = metrics::kModelIntegrity;
  /** Also emit ai.model.loaded / ai.model.hash. */
  bool emit_model_metrics = false;
  uint32_t rehash_interval_seconds = 30;
  /** Tolerate a missing file for this long after startup. */
  uint32_t startup_grace_seconds = 10;
};

/**
 * Platform-neutral SHA-256 integrity monitor. The file is re-hashed when its
 * size, mtime or inode changes and periodically as a safety net, so large
 * TensorRT engines are not hashed every cycle.
 */
class FileIntegrityCollector : public MetricCollector {
public:
  FileIntegrityCollector(FileIntegrityOptions options, const Clock &clock);

  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return options_.name.c_str(); }
  bool heartbeat_required() const override { return false; }

  const std::string &expected() const { return expected_; }
  const std::string &current() const { return current_; }

private:
  static bool hash_like(const std::string &value);

  FileIntegrityOptions options_;
  const Clock &clock_;
  uint64_t started_ms_ = 0;
  uint64_t last_hash_ms_ = 0;
  std::string expected_;
  std::string baseline_source_;
  std::string current_;
  bool have_stat_ = false;
  uint64_t last_size_ = 0;
  int64_t last_mtime_ns_ = 0;
  uint64_t last_inode_ = 0;
};

} // namespace edgeml

#endif
