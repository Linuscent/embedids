/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdio>
#include <fstream>

#include <gtest/gtest.h>

#include "collectors/common/file_integrity_collector.h"
#include "core/integrity/hash_util.h"
#include "simulator/workload/model_file.h"
#include "support/test_support.h"

using namespace edgeml;

namespace {

const MetricSample &integrity(const TelemetryBatch &batch) {
  const MetricSample *sample = batch.find(metrics::kModelIntegrity);
  EXPECT_NE(sample, nullptr);
  static MetricSample empty;
  return sample ? *sample : empty;
}

TelemetryBatch collect(FileIntegrityCollector &collector, ManualClock &clock) {
  clock.advance_ms(1000);
  TelemetryBatch batch;
  batch.timestamp_ms = clock.now_ms();
  EXPECT_TRUE(collector.collect(batch));
  return batch;
}

} // namespace

TEST(FileIntegrityTest, DetectsModification) {
  test::TempDir dir;
  const std::string path = dir.file("models/model.engine");
  ASSERT_TRUE(sim::write_model_file(path, false));

  ManualClock clock;
  FileIntegrityOptions options;
  options.path = path;
  options.expected_sha256 = sim::approved_model_sha256();
  options.emit_model_metrics = true;
  FileIntegrityCollector collector(options, clock);
  ASSERT_TRUE(collector.initialize());

  auto batch = collect(collector, clock);
  EXPECT_EQ(integrity(batch).value, 1.0);
  EXPECT_EQ(integrity(batch).labels.at("status"), "verified");
  EXPECT_EQ(batch.value(metrics::kModelLoaded).value(), 1.0);
  EXPECT_EQ(batch.find(metrics::kModelHash)->labels.at("sha256"),
            sim::approved_model_sha256());

  ASSERT_TRUE(sim::write_model_file(path, true));
  batch = collect(collector, clock);
  EXPECT_EQ(integrity(batch).value, 0.0);
  EXPECT_EQ(integrity(batch).labels.at("status"), "violated");
  EXPECT_EQ(integrity(batch).labels.at("expected"),
            sim::approved_model_sha256());
  EXPECT_EQ(integrity(batch).labels.at("current"),
            sha256_string(sim::tampered_model_bytes()));

  ASSERT_TRUE(sim::write_model_file(path, false));
  batch = collect(collector, clock);
  EXPECT_EQ(integrity(batch).value, 1.0);
}

TEST(FileIntegrityTest, ExpectedHashIsCaseInsensitive) {
  test::TempDir dir;
  const std::string path = dir.file("m.bin");
  ASSERT_TRUE(sim::write_model_file(path, false));
  std::string upper = sim::approved_model_sha256();
  for (auto &c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  ManualClock clock;
  FileIntegrityOptions options;
  options.path = path;
  options.expected_sha256 = upper;
  FileIntegrityCollector collector(options, clock);
  ASSERT_TRUE(collector.initialize());
  EXPECT_EQ(integrity(collect(collector, clock)).value, 1.0);
}

TEST(FileIntegrityTest, TrustOnFirstUse) {
  test::TempDir dir;
  const std::string path = dir.file("config.yaml");
  {
    std::ofstream out(path);
    out << "device: {id: a}\n";
  }
  ManualClock clock;
  FileIntegrityOptions options;
  options.path = path;
  FileIntegrityCollector collector(options, clock);
  ASSERT_TRUE(collector.initialize());
  auto batch = collect(collector, clock);
  EXPECT_EQ(integrity(batch).value, 1.0);
  EXPECT_EQ(integrity(batch).labels.at("baseline"), "trust-on-first-use");

  {
    std::ofstream out(path);
    out << "device: {id: b, platform: simulation}\n";
  }
  EXPECT_EQ(integrity(collect(collector, clock)).value, 0.0);
}

TEST(FileIntegrityTest, MissingFileGraceThenViolation) {
  test::TempDir dir;
  ManualClock clock;
  FileIntegrityOptions options;
  options.path = dir.file("absent.engine");
  options.expected_sha256 = sim::approved_model_sha256();
  options.startup_grace_seconds = 3;
  FileIntegrityCollector collector(options, clock);
  ASSERT_TRUE(collector.initialize());

  TelemetryBatch batch = collect(collector, clock);
  EXPECT_EQ(batch.find(metrics::kModelIntegrity), nullptr);
  clock.advance_ms(5000);
  batch = collect(collector, clock);
  EXPECT_EQ(integrity(batch).value, 0.0);
  EXPECT_EQ(integrity(batch).labels.at("status"), "missing");
}

TEST(FileIntegrityTest, DeletedModelIsAViolation) {
  test::TempDir dir;
  const std::string path = dir.file("model.engine");
  ASSERT_TRUE(sim::write_model_file(path, false));
  ManualClock clock;
  FileIntegrityOptions options;
  options.path = path;
  options.expected_sha256 = sim::approved_model_sha256();
  options.emit_model_metrics = true;
  FileIntegrityCollector collector(options, clock);
  ASSERT_TRUE(collector.initialize());
  EXPECT_EQ(integrity(collect(collector, clock)).value, 1.0);
  std::remove(path.c_str());
  auto batch = collect(collector, clock);
  EXPECT_EQ(integrity(batch).value, 0.0);
  EXPECT_EQ(batch.value(metrics::kModelLoaded).value(), 0.0);
}
