/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "core/embedids/signal_engine.h"

using namespace edgeml;

namespace {

TelemetryBatch batch_with(uint64_t ts, const std::string &metric, double v) {
  TelemetryBatch batch;
  batch.timestamp_ms = ts;
  batch.add("test", metric, v, "");
  return batch;
}

void feed(SignalEngine &engine, uint64_t ts, const std::string &metric,
          double value) {
  engine.ingest(batch_with(ts, metric, value));
  engine.evaluate(ts);
}

} // namespace

TEST(SignalEngineTest, InstantThresholdUsesEmbedIDSCore) {
  SignalEngine engine;
  ASSERT_TRUE(engine.add_signal({"high", "m", Comparison::Greater, 85.0, 0}));
  ASSERT_TRUE(engine.add_signal({"low", "m", Comparison::Less, 20.0, 0}));
  ASSERT_TRUE(engine.finalize());
  EXPECT_EQ(engine.metric_count(), 1u);

  feed(engine, 1000, "m", 50);
  EXPECT_FALSE(engine.active("high"));
  EXPECT_FALSE(engine.active("low"));

  feed(engine, 2000, "m", 90);
  EXPECT_TRUE(engine.active("high"));
  EXPECT_FALSE(engine.active("low"));
  EXPECT_EQ(engine.state("high")->last_value, 90.0);

  feed(engine, 3000, "m", 10);
  EXPECT_FALSE(engine.active("high"));
  EXPECT_TRUE(engine.active("low"));
}

TEST(SignalEngineTest, SustainedConditionRequiresDuration) {
  SignalEngine engine;
  ASSERT_TRUE(engine.add_signal(
      {"gpu", "gpu.utilization", Comparison::Greater, 85.0, 10000}));
  ASSERT_TRUE(engine.finalize());

  feed(engine, 0, "gpu.utilization", 60);
  for (uint64_t t = 1000; t <= 10000; t += 1000) {
    feed(engine, t, "gpu.utilization", 97);
    EXPECT_FALSE(engine.active("gpu")) << "t=" << t;
  }
  feed(engine, 11000, "gpu.utilization", 97);
  EXPECT_TRUE(engine.active("gpu"));
  EXPECT_EQ(engine.state("gpu")->condition_since_ms, 1000u);

  // One normal sample breaks the streak.
  feed(engine, 12000, "gpu.utilization", 60);
  EXPECT_FALSE(engine.active("gpu"));
  feed(engine, 13000, "gpu.utilization", 97);
  EXPECT_FALSE(engine.active("gpu"));
}

TEST(SignalEngineTest, InclusiveComparisonsUseCustomAlgorithm) {
  SignalEngine engine;
  ASSERT_TRUE(engine.add_signal({"ge", "m", Comparison::GreaterEqual, 5, 0}));
  ASSERT_TRUE(engine.add_signal({"le", "m", Comparison::LessEqual, 5, 0}));
  ASSERT_TRUE(engine.finalize());
  feed(engine, 1000, "m", 5);
  EXPECT_TRUE(engine.active("ge"));
  EXPECT_TRUE(engine.active("le"));
  feed(engine, 2000, "m", 6);
  EXPECT_TRUE(engine.active("ge"));
  EXPECT_FALSE(engine.active("le"));
}

TEST(SignalEngineTest, MissingSamplesKeepPreviousState) {
  SignalEngine engine;
  ASSERT_TRUE(engine.add_signal({"x", "m", Comparison::Greater, 1, 0}));
  ASSERT_TRUE(engine.finalize());
  EXPECT_FALSE(engine.active("x"));
  EXPECT_FALSE(engine.state("x")->has_data);

  feed(engine, 1000, "m", 5);
  EXPECT_TRUE(engine.active("x"));
  TelemetryBatch empty;
  empty.timestamp_ms = 2000;
  engine.ingest(empty);
  engine.evaluate(2000);
  EXPECT_TRUE(engine.active("x"));
}

TEST(SignalEngineTest, ResetClearsHistory) {
  SignalEngine engine;
  ASSERT_TRUE(engine.add_signal({"x", "m", Comparison::Greater, 1, 0}));
  ASSERT_TRUE(engine.finalize());
  feed(engine, 1000, "m", 5);
  ASSERT_TRUE(engine.active("x"));
  engine.reset();
  EXPECT_FALSE(engine.active("x"));
  engine.evaluate(2000);
  EXPECT_FALSE(engine.active("x"));
}

TEST(SignalEngineTest, RejectsInvalidRegistrations) {
  SignalEngine engine;
  std::string error;
  EXPECT_FALSE(engine.add_signal({"", "m", Comparison::Greater, 1, 0}, &error));
  ASSERT_TRUE(engine.add_signal({"a", "m", Comparison::Greater, 1, 0}));
  EXPECT_FALSE(
      engine.add_signal({"a", "m", Comparison::Greater, 2, 0}, &error));
  EXPECT_NE(error.find("duplicate"), std::string::npos);
  EXPECT_FALSE(
      engine.add_signal({"long", std::string(EMBEDIDS_MAX_METRIC_NAME_LEN, 'x'),
                         Comparison::Greater, 1, 0},
                        &error));
  for (int i = 1; i < EMBEDIDS_MAX_ALGORITHMS_PER_METRIC; i++) {
    ASSERT_TRUE(engine.add_signal(
        {"s" + std::to_string(i), "m", Comparison::Greater, 1.0 * i, 0}));
  }
  EXPECT_FALSE(
      engine.add_signal({"overflow", "m", Comparison::Greater, 9, 0}, &error));
  ASSERT_TRUE(engine.finalize());
  EXPECT_FALSE(
      engine.add_signal({"late", "n", Comparison::Greater, 1, 0}, &error));
}

TEST(SignalEngineTest, HistoryWrapsWithoutLosingStreak) {
  SignalEngine engine(8);
  ASSERT_TRUE(engine.add_signal({"x", "m", Comparison::Greater, 1, 5000}));
  ASSERT_TRUE(engine.finalize());
  for (uint64_t t = 0; t < 40000; t += 1000) {
    feed(engine, t, "m", 5);
  }
  EXPECT_TRUE(engine.active("x"));
}
