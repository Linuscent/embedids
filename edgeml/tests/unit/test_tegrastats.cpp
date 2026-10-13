/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "collectors/jetson/jetson_collectors.h"
#include "collectors/jetson/tegrastats_parser.h"

using namespace edgeml;
using namespace edgeml::jetson;

TEST(TegrastatsTest, JetPack6OrinLine) {
  const std::string line =
      "10-08-2026 12:00:01 RAM 2448/7620MB (lfb 1x4MB) SWAP 0/3810MB (cached "
      "0MB) CPU [12%@1510,8%@1510,off,off,10%@1510,5%@1510] EMC_FREQ 0% "
      "GR3D_FREQ 61% cpu@48.5C soc2@47.031C soc0@47.968C gpu@48.25C "
      "tj@49.312C soc1@48.125C VDD_IN 5136mW/5136mW VDD_CPU_GPU_CV "
      "1011mW/1011mW VDD_SOC 1452mW/1452mW";
  auto sample = parse_tegrastats_line(line);
  ASSERT_TRUE(sample);
  EXPECT_EQ(sample->ram_used_mb.value(), 2448);
  EXPECT_EQ(sample->ram_total_mb.value(), 7620);
  ASSERT_EQ(sample->cpu_loads.size(), 4u);
  EXPECT_DOUBLE_EQ(sample->cpu_average().value(), 8.75);
  EXPECT_EQ(sample->gpu_load_percent.value(), 61);
  EXPECT_DOUBLE_EQ(sample->gpu_temperature().value(), 48.25);
  EXPECT_DOUBLE_EQ(sample->soc_temperature().value(), 49.312);
  EXPECT_EQ(sample->power_mw.at("VDD_IN"), 5136);
  EXPECT_DOUBLE_EQ(sample->gpu_power_watts().value(), 1.011);
}

TEST(TegrastatsTest, JetPack4NanoLine) {
  const std::string line =
      "RAM 1904/3956MB (lfb 74x4MB) SWAP 0/1978MB (cached 0MB) CPU "
      "[20%@1479,15%@1479,9%@1479,11%@1479] EMC_FREQ 3%@1600 GR3D_FREQ 37%@921 "
      "APE 25 PLL@32C CPU@35.5C PMIC@100C GPU@33.5C AO@41C thermal@34.25C "
      "POM_5V_IN 3140/3140 POM_5V_GPU 397/397 POM_5V_CPU 794/794";
  auto sample = parse_tegrastats_line(line);
  ASSERT_TRUE(sample);
  EXPECT_EQ(sample->gpu_load_percent.value(), 37);
  EXPECT_EQ(sample->cpu_loads.size(), 4u);
  EXPECT_DOUBLE_EQ(sample->gpu_temperature().value(), 33.5);
  EXPECT_DOUBLE_EQ(sample->gpu_power_watts().value(), 0.397);
  // No tj sensor: hottest valid zone.
  EXPECT_DOUBLE_EQ(sample->soc_temperature().value(), 100.0);
}

TEST(TegrastatsTest, IgnoresUnrelatedText) {
  EXPECT_FALSE(parse_tegrastats_line("hello world").has_value());
  EXPECT_FALSE(parse_tegrastats_line("").has_value());
}

TEST(TegrastatsTest, SourceFeedsJetsonCollectors) {
  auto source =
      std::make_shared<TegrastatsSource>("/nonexistent/tegrastats", 1000);
  EXPECT_FALSE(source->start());
  source->ingest_line(
      "RAM 3810/7620MB CPU [50%@1510] GR3D_FREQ 88% gpu@55.5C tj@60C "
      "VDD_GPU_SOC 4200mW/4000mW");

  JetsonGpuCollector gpu(source, {"/nonexistent/load"});
  // No GPU sysfs node or tegrastats binary on the build host.
  EXPECT_FALSE(gpu.initialize());
  TelemetryBatch batch;
  ASSERT_TRUE(gpu.collect(batch));
  EXPECT_EQ(batch.value(metrics::kGpuUtilization).value(), 88);
  EXPECT_EQ(batch.find(metrics::kGpuUtilization)->labels.at("backend"),
            "tegrastats");
  EXPECT_EQ(batch.value(metrics::kGpuMemoryPercent).value(), 50);
  EXPECT_EQ(batch.value(metrics::kGpuTemperature).value(), 55.5);

  JetsonPowerCollector power(source);
  ASSERT_TRUE(power.collect(batch));
  EXPECT_DOUBLE_EQ(batch.value(metrics::kGpuPower).value(), 4.2);

  JetsonThermalCollector thermal(source);
  ASSERT_TRUE(thermal.collect(batch));
  EXPECT_EQ(batch.value(metrics::kTemperature).value(), 60);
}
