/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/simulation/simulated_collectors.h"

namespace edgeml {

bool SimulatedCollector::collect(MetricBatch &batch) {
  const sim::SimSnapshot *snapshot = feed_->get(batch.cycle);
  if (snapshot == nullptr) {
    return false;
  }
  for (const auto &entry : snapshot->metrics) {
    for (const auto &prefix : prefixes_) {
      if (entry.first.compare(0, prefix.size(), prefix) == 0) {
        const MetricDescriptor *descriptor =
            find_metric_descriptor(entry.first);
        batch.add(source_, entry.first, entry.second,
                  descriptor ? descriptor->unit : "");
        break;
      }
    }
  }
  collect_inventory(*snapshot, batch);
  return true;
}

void SimulatedNetworkCollector::collect_inventory(
    const sim::SimSnapshot &snapshot, MetricBatch &batch) {
  batch.has_connection_inventory = true;
  batch.connections = snapshot.connections;
}

void SimulatedProcessCollector::collect_inventory(
    const sim::SimSnapshot &snapshot, MetricBatch &batch) {
  batch.has_process_inventory = true;
  batch.processes = snapshot.processes;
  batch.file_accesses.insert(batch.file_accesses.end(),
                             snapshot.file_accesses.begin(),
                             snapshot.file_accesses.end());
}

} // namespace edgeml
