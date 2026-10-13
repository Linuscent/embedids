/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/jetson/tegrastats_parser.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace edgeml::jetson {

std::optional<double> TegrastatsSample::cpu_average() const {
  if (cpu_loads.empty()) {
    return std::nullopt;
  }
  double sum = 0;
  for (double load : cpu_loads) {
    sum += load;
  }
  return sum / static_cast<double>(cpu_loads.size());
}

std::optional<double> TegrastatsSample::gpu_power_watts() const {
  // Rail names differ per module; first match wins.
  for (const char *rail :
       {"VDD_GPU_SOC", "VDD_GPU", "POM_5V_GPU", "VDD_CPU_GPU_CV", "GPU"}) {
    auto it = power_mw.find(rail);
    if (it != power_mw.end()) {
      return it->second / 1000.0;
    }
  }
  return std::nullopt;
}

std::optional<double> TegrastatsSample::gpu_temperature() const {
  auto it = temperatures_c.find("gpu");
  if (it == temperatures_c.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<double> TegrastatsSample::soc_temperature() const {
  auto it = temperatures_c.find("tj");
  if (it != temperatures_c.end()) {
    return it->second;
  }
  std::optional<double> hottest;
  for (const auto &entry : temperatures_c) {
    // Disconnected sensors report -256C.
    if (entry.second > -100 && (!hottest || entry.second > *hottest)) {
      hottest = entry.second;
    }
  }
  return hottest;
}

std::optional<TegrastatsSample> parse_tegrastats_line(const std::string &line) {
  static const std::regex ram_re(R"(RAM (\d+)/(\d+)MB)");
  static const std::regex cpu_re(R"(CPU \[([^\]]*)\])");
  static const std::regex gr3d_re(R"(GR3D_FREQ (\d+)%)");
  static const std::regex temp_re(
      R"(([A-Za-z][A-Za-z0-9_]*)@(-?\d+(?:\.\d+)?)C\b)");
  static const std::regex power_re(
      R"(\b((?:VDD|POM|VIN)_[A-Z0-9_]+|GPU|CPU|SOC|CV|VDDRQ|SYS5V) (\d+)(?:mW)?/(\d+)(?:mW)?)");

  TegrastatsSample sample;
  bool any = false;
  std::smatch match;

  if (std::regex_search(line, match, ram_re)) {
    sample.ram_used_mb = std::stod(match[1]);
    sample.ram_total_mb = std::stod(match[2]);
    any = true;
  }
  if (std::regex_search(line, match, cpu_re)) {
    std::stringstream cores(match[1].str());
    std::string core;
    while (std::getline(cores, core, ',')) {
      const size_t percent = core.find('%');
      if (percent != std::string::npos) {
        try {
          sample.cpu_loads.push_back(std::stod(core.substr(0, percent)));
        } catch (const std::exception &) {
        }
      }
    }
    any = true;
  }
  if (std::regex_search(line, match, gr3d_re)) {
    sample.gpu_load_percent = std::stod(match[1]);
    any = true;
  }
  for (auto it = std::sregex_iterator(line.begin(), line.end(), temp_re);
       it != std::sregex_iterator(); ++it) {
    std::string name = (*it)[1].str();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    sample.temperatures_c[name] = std::stod((*it)[2].str());
    any = true;
  }
  for (auto it = std::sregex_iterator(line.begin(), line.end(), power_re);
       it != std::sregex_iterator(); ++it) {
    sample.power_mw[(*it)[1].str()] = std::stod((*it)[2].str());
    any = true;
  }

  if (!any) {
    return std::nullopt;
  }
  return sample;
}

} // namespace edgeml::jetson
