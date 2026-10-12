/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic stand-in for a deployed TensorRT engine. The approved
 * content is identical on every run so its SHA-256 can be pinned in config.
 */

#ifndef EDGEML_SIMULATOR_WORKLOAD_MODEL_FILE_H
#define EDGEML_SIMULATOR_WORKLOAD_MODEL_FILE_H

#include <string>

namespace edgeml::sim {

std::string approved_model_bytes();
std::string tampered_model_bytes();
std::string approved_model_sha256();

/** Atomically (tmp + rename) write the approved or tampered model. */
bool write_model_file(const std::string &path, bool tampered,
                      std::string *error = nullptr);

} // namespace edgeml::sim

#endif
