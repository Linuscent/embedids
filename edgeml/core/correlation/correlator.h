/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Rule-based multi-signal correlation (spec section 11).
 */

#ifndef EDGEML_CORE_CORRELATION_CORRELATOR_H
#define EDGEML_CORE_CORRELATION_CORRELATOR_H

#include <vector>

#include "core/alert/alert.h"
#include "core/config/config.h"

namespace edgeml {

class Correlator {
public:
  explicit Correlator(std::vector<CorrelationRule> rules)
      : rules_(std::move(rules)) {}

  /**
   * Evaluate correlations against known alerts. A requirement matches an
   * alert by rule name or by one of its signal metrics; the alert must be
   * active or have resolved within the correlation window.
   */
  std::vector<Finding> evaluate(const std::vector<Alert> &alerts,
                                uint64_t now_ms) const;

  const std::vector<CorrelationRule> &rules() const { return rules_; }

private:
  std::vector<CorrelationRule> rules_;
};

} // namespace edgeml

#endif
