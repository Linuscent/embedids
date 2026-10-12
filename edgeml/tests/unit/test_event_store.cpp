/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "agent/event_store.h"
#include "support/test_support.h"

using namespace edgeml;

TEST(EventStoreTest, PersistsEventsAlertsAndSequence) {
  test::TempDir dir;
  const std::string path = dir.file("data/embedids.db");
  {
    EventStore store;
    std::string error;
    ASSERT_TRUE(store.open(path, &error)) << error;

    Event event;
    event.timestamp_ms = 1000;
    event.type = "agent.started";
    event.message = "hello";
    event.data = {{"k", 1}};
    store.record_event(event);

    Alert alert;
    alert.alert_id = "ALT-000041";
    alert.timestamp_ms = 2000;
    alert.severity = Severity::High;
    alert.rule = "rogue_process";
    alert.title = "Unknown process detected";
    store.upsert_alert(alert);
    alert.active = false;
    alert.resolved_ms = 3000;
    store.upsert_alert(alert); // update, not duplicate
    store.set_agent_state("version", "0.1.0", 1000);
  }

  EventStore reopened;
  ASSERT_TRUE(reopened.open(path));
  auto events = reopened.recent_events(10);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, "agent.started");
  EXPECT_EQ(events[0].data["k"], 1);

  auto alerts = reopened.recent_alerts(10);
  ASSERT_EQ(alerts.size(), 1u);
  EXPECT_EQ(alerts[0]["status"], "resolved");
  EXPECT_EQ(reopened.max_alert_sequence("ALT-"), 41u);
  EXPECT_EQ(reopened.max_alert_sequence("OTHER-"), 0u);
}

TEST(EventStoreTest, RetentionPrunesOldRows) {
  EventStore store;
  ASSERT_TRUE(store.open(":memory:"));
  const uint64_t day = 24ULL * 3600 * 1000;
  const uint64_t now = 100 * day;

  Event old_event;
  old_event.timestamp_ms = now - 8 * day;
  old_event.type = "old";
  store.record_event(old_event);
  Event new_event;
  new_event.timestamp_ms = now - day;
  new_event.type = "new";
  store.record_event(new_event);

  Alert old_alert;
  old_alert.alert_id = "ALT-000001";
  old_alert.timestamp_ms = now - 31 * day;
  old_alert.active = false;
  store.upsert_alert(old_alert);
  Alert old_active = old_alert;
  old_active.alert_id = "ALT-000002";
  old_active.active = true;
  store.upsert_alert(old_active);

  StorageConfig retention; // 24 h / 7 d / 30 d
  store.prune(now, retention);
  auto events = store.recent_events(10);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, "new");
  auto alerts = store.recent_alerts(10);
  ASSERT_EQ(alerts.size(), 1u); // active alerts are never pruned
  EXPECT_EQ(alerts[0]["alert_id"], "ALT-000002");
}

TEST(EventStoreTest, MetricSummaryRollsUpPerMinute) {
  test::TempDir dir;
  EventStore store;
  ASSERT_TRUE(store.open(dir.file("db.sqlite")));
  MetricSummaryAggregator aggregator;
  for (int i = 0; i < 61; i++) {
    TelemetryBatch batch;
    batch.timestamp_ms = 60000ULL * 10 + i * 1000ULL;
    batch.add("gpu", metrics::kGpuUtilization, 50.0 + i, "percent");
    aggregator.add(batch, store);
  }
  aggregator.flush(store);
  auto rows = store.metric_summary(metrics::kGpuUtilization, 10);
  ASSERT_EQ(rows.size(), 2u);
  // Newest first: minute 11 holds only the final sample.
  EXPECT_EQ(rows[0]["count"], 1);
  EXPECT_EQ(rows[0]["min"], 110.0);
  EXPECT_EQ(rows[1]["count"], 60);
  EXPECT_EQ(rows[1]["min"], 50.0);
  EXPECT_EQ(rows[1]["max"], 109.0);
  EXPECT_DOUBLE_EQ(rows[1]["avg"].get<double>(), 79.5);
}

TEST(EventStoreTest, OpenFailureIsReported) {
  EventStore store;
  std::string error;
  EXPECT_FALSE(store.open("/proc/forbidden/db.sqlite", &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(store.is_open());
  store.record_event(Event{}); // no-op, no crash
}
