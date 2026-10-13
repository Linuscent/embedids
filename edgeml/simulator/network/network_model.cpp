/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/network/network_model.h"

#include "simulator/process/process_model.h"

namespace edgeml::sim {

void NetworkModel::update(const ModelContext &ctx) {
  const auto &attacks = ctx.attacks;
  const bool exfil = attacks.has(Attack::NetworkExfil);

  const double mqtt = clamp(8000.0 + ctx.rng.gaussian(1500.0), 2000, 20000);
  // Periodic HTTPS batch uploads of detections.
  const double upload =
      clamp(16000.0 + 6000.0 * std::sin(ctx.elapsed_seconds / 20.0 * 6.283) +
                ctx.rng.gaussian(2500.0),
            1000, 40000);
  exfil_rate_.update(exfil ? 2.4e6 : 0.0, ctx.dt_seconds);
  const double exfil_bytes =
      exfil ? std::max(0.0, exfil_rate_.value() + ctx.rng.gaussian(60000.0))
            : 0.0;
  if (!exfil) {
    exfil_rate_.reset();
  }

  connections_ = {
      {"10.0.0.20:1883", "mqtt-client", true, static_cast<uint64_t>(mqtt)},
      {"10.0.0.30:443", "inference-service", true,
       static_cast<uint64_t>(upload)},
  };
  file_accesses_.clear();
  if (exfil) {
    connections_.push_back({kExfilDestination, "mqtt-client", true,
                            static_cast<uint64_t>(exfil_bytes)});
    file_accesses_.push_back(
        {model_path_, "mqtt-client", kMqttClientPid, ctx.now_ms});
  }

  bytes_out_ = mqtt + upload + exfil_bytes;
  bytes_in_ = clamp(12000.0 + ctx.rng.gaussian(2000.0), 1000, 50000) +
              exfil_bytes * 0.02;
  // The miner keeps trying to reach its (blocked) pool.
  failed_ = attacks.has(Attack::Cryptominer)
                ? std::round(ctx.rng.uniform(2.0, 4.0))
                : 0.0;
}

void NetworkModel::write(std::map<std::string, double> &out) const {
  out[metrics::kConnectionsOutbound] = static_cast<double>(connections_.size());
  out[metrics::kConnectionsTotal] =
      static_cast<double>(connections_.size() + 1); // + inbound API client
  out[metrics::kBytesOut] = bytes_out_;
  out[metrics::kBytesIn] = bytes_in_;
  out[metrics::kFailedConnections] = failed_;
}

void NetworkModel::reset() { exfil_rate_.reset(); }

} // namespace edgeml::sim
