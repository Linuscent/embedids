/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/workload/snapshot.h"

namespace edgeml::sim {

void to_json(nlohmann::json &j, const SimSnapshot &snapshot) {
  j = nlohmann::json{{"timestamp_ms", snapshot.timestamp_ms},
                     {"device_id", snapshot.device_id},
                     {"active_attacks", snapshot.active_attacks},
                     {"metrics", snapshot.metrics},
                     {"processes", snapshot.processes},
                     {"connections", snapshot.connections},
                     {"file_accesses", snapshot.file_accesses}};
}

void from_json(const nlohmann::json &j, SimSnapshot &snapshot) {
  snapshot.timestamp_ms = j.value("timestamp_ms", uint64_t{0});
  snapshot.device_id = j.value("device_id", "");
  snapshot.active_attacks =
      j.value("active_attacks", std::vector<std::string>{});
  snapshot.metrics = j.value("metrics", std::map<std::string, double>{});
  snapshot.processes = j.value("processes", std::vector<ProcessInfo>{});
  snapshot.connections = j.value("connections", std::vector<ConnectionInfo>{});
  snapshot.file_accesses =
      j.value("file_accesses", std::vector<FileAccessInfo>{});
}

} // namespace edgeml::sim
