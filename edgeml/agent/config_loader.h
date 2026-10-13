/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEML_AGENT_CONFIG_LOADER_H
#define EDGEML_AGENT_CONFIG_LOADER_H

#include <string>

#include "core/config/config.h"

namespace edgeml {

/** Apply YAML text on top of the existing (default) configuration. */
bool apply_config_yaml(const std::string &yaml_text, Config &config,
                       std::string *error);

/**
 * Load a YAML file over defaults, record its path and SHA-256 for the
 * configuration integrity check, and validate the result.
 */
bool load_config_file(const std::string &path, Config &config,
                      std::string *error);

} // namespace edgeml

#endif
