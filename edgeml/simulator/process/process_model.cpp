/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/process/process_model.h"

namespace edgeml::sim {

void ProcessModel::update(const ModelContext &ctx) {
  const uint64_t boot = boot_ms_;
  processes_ = {
      {1, "systemd", 0, boot, false},
      {412, "containerd", 0, boot + 4000, false},
      {kMqttClientPid, "mqtt-client", 1000, boot + 9000, false},
      {1032, "camera-service", 1000, boot + 11000, false},
      {kInferencePid, "inference-service", 1000, boot + 12000, true},
      {1310, "embedids-agent", 999, boot + 13000, false},
  };

  const auto &attacks = ctx.attacks;
  if (attacks.has(Attack::Cryptominer)) {
    processes_.push_back({4242, "unknown_worker", 1000,
                          attacks.started_ms(Attack::Cryptominer), true});
  }
  if (attacks.has(Attack::RogueProcess)) {
    processes_.push_back({4317, "rogue_daemon", 1000,
                          attacks.started_ms(Attack::RogueProcess), false});
  }
  if (attacks.has(Attack::PrivilegeEscalation)) {
    processes_.push_back({4401, "rogue-shell", 0,
                          attacks.started_ms(Attack::PrivilegeEscalation),
                          false});
  }

  // Kernel threads and short-lived helpers wobble slightly.
  if (ctx.rng.chance(0.2)) {
    background_processes_ =
        182 + static_cast<int>(std::round(ctx.rng.uniform(-2.0, 2.0)));
  }
}

void ProcessModel::write(std::map<std::string, double> &out) const {
  out[metrics::kProcessCount] =
      static_cast<double>(background_processes_ + processes_.size());
}

} // namespace edgeml::sim
