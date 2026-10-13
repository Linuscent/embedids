/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Attack scenarios the simulator can inject (spec section 16).
 */

#ifndef EDGEML_SIMULATOR_ATTACKS_SCENARIOS_H
#define EDGEML_SIMULATOR_ATTACKS_SCENARIOS_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace edgeml::sim {

enum class Attack {
  Cryptominer,
  ModelTamper,
  NetworkExfil,
  InferenceDegradation,
  RogueProcess,
  PrivilegeEscalation,
  DenialOfService,
  TelemetryLoss,
};

struct ScenarioInfo {
  const char *name;
  Attack attack;
  const char *label;
  const char *description;
  std::vector<const char *> expected_alerts;
};

const std::vector<ScenarioInfo> &scenario_catalog();
const char *to_string(Attack attack);

/** Accepts canonical names and aliases (e.g. "rogue-gpu", "exfil"). */
std::optional<Attack> parse_attack(const std::string &name);

/** Currently active attacks and when each started. */
class AttackState {
public:
  bool has(Attack attack) const { return started_.count(attack) != 0; }
  uint64_t started_ms(Attack attack) const {
    auto it = started_.find(attack);
    return it == started_.end() ? 0 : it->second;
  }
  void activate(Attack attack, uint64_t now_ms) {
    started_.emplace(attack, now_ms);
  }
  void clear() { started_.clear(); }
  bool empty() const { return started_.empty(); }
  std::vector<std::string> names() const;

private:
  std::map<Attack, uint64_t> started_;
};

} // namespace edgeml::sim

#endif
