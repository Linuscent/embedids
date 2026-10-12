/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Parser for NVIDIA tegrastats output. Supports the JetPack 4.x format
 * (GR3D_FREQ 0%@114, POM_5V_GPU 120/120) and JetPack 5/6 format
 * (GR3D_FREQ 61%, VDD_CPU_GPU_CV 1011mW/1011mW).
 */

#ifndef EDGEML_COLLECTORS_JETSON_TEGRASTATS_PARSER_H
#define EDGEML_COLLECTORS_JETSON_TEGRASTATS_PARSER_H

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace edgeml::jetson {

struct TegrastatsSample {
  std::optional<double> ram_used_mb;
  std::optional<double> ram_total_mb;
  std::vector<double> cpu_loads; /**< online cores only */
  std::optional<double> gpu_load_percent;
  std::map<std::string, double> temperatures_c; /**< lowercase zone names */
  std::map<std::string, double> power_mw;       /**< rail -> instantaneous */

  std::optional<double> cpu_average() const;
  std::optional<double> gpu_power_watts() const;
  std::optional<double> gpu_temperature() const;
  std::optional<double> soc_temperature() const;
};

std::optional<TegrastatsSample> parse_tegrastats_line(const std::string &line);

} // namespace edgeml::jetson

#endif
