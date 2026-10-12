/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_SIMULATOR_NETWORK_NETWORK_MODEL_H
#define EDGEML_SIMULATOR_NETWORK_NETWORK_MODEL_H

#include <map>
#include <string>
#include <vector>

#include "core/schema/telemetry.h"
#include "simulator/common/model_context.h"

namespace edgeml::sim {

inline constexpr const char *kExfilDestination = "203.0.113.45:8443";

/** Network activity: MQTT telemetry, HTTPS uploads, attack traffic. */
class NetworkModel {
public:
  explicit NetworkModel(std::string model_path)
      : model_path_(std::move(model_path)) {}
  void update(const ModelContext &ctx);
  void write(std::map<std::string, double> &out) const;
  void reset();

  const std::vector<ConnectionInfo> &connections() const {
    return connections_;
  }
  const std::vector<FileAccessInfo> &file_accesses() const {
    return file_accesses_;
  }

private:
  std::string model_path_;
  SmoothedValue exfil_rate_{0.0, 2.5};
  double bytes_out_ = 24000.0;
  double bytes_in_ = 12000.0;
  double failed_ = 0.0;
  std::vector<ConnectionInfo> connections_;
  std::vector<FileAccessInfo> file_accesses_;
};

} // namespace edgeml::sim

#endif
