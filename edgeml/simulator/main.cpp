/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * edgeml-simulator: Jetson-like EdgeML workload simulator with an HTTP control
 * and telemetry API.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "core/common/clock.h"
#include "simulator/attacks/scenarios.h"
#include "simulator/workload/control_server.h"
#include "simulator/workload/edge_device_simulator.h"
#include "simulator/workload/model_file.h"

namespace {

std::atomic<bool> g_stop{false};
std::atomic<httplib::Server *> g_server{nullptr};

void handle_signal(int) { g_stop = true; }

void usage() {
  std::cout << R"(Usage: edgeml-simulator [options]

Options:
  --scenario NAME[,NAME]  Scenario(s) to start: normal, cryptominer,
                          model-tamper, network-exfil, inference-degradation,
                          rogue-process, privilege-escalation, dos,
                          telemetry-loss (default normal)
  --attack-delay SECONDS  Run normally for SECONDS before the scenario starts
  --bind ADDR             HTTP bind address (default 0.0.0.0)
  --port PORT             HTTP port (default 9100)
  --interval-ms MS        Simulation step interval (default 1000)
  --seed N                Random seed for reproducible runs (default 42)
  --device-id ID          Device identifier (default demo-edge-001)
  --model-path PATH       Model file to manage (default /models/model.engine)
  --no-model-file         Do not create/modify a model file
  --stdout                Print telemetry snapshots as JSON lines, no HTTP
  --duration SECONDS      Exit after SECONDS
  --print-model-hash      Print the approved model SHA-256 and exit
  --write-model PATH      Write the approved model to PATH and exit
  --list-scenarios        List scenarios and expected alerts
  -h, --help              Show this help
)";
}

std::vector<std::string> split(const std::string &text) {
  std::vector<std::string> parts;
  std::stringstream in(text);
  std::string part;
  while (std::getline(in, part, ',')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  return parts;
}

} // namespace

int main(int argc, char **argv) {
  using namespace edgeml;
  using namespace edgeml::sim;

  SimulatorOptions options;
  std::string bind = "0.0.0.0";
  int port = 9100;
  uint32_t interval_ms = 1000;
  std::vector<std::string> scenarios;
  double attack_delay = 0;
  double duration = 0;
  bool to_stdout = false;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--scenario") {
      scenarios = split(value());
    } else if (arg == "--attack-delay") {
      attack_delay = std::stod(value());
    } else if (arg == "--bind") {
      bind = value();
    } else if (arg == "--port") {
      port = std::stoi(value());
    } else if (arg == "--interval-ms") {
      interval_ms = static_cast<uint32_t>(std::stoul(value()));
    } else if (arg == "--seed") {
      options.seed = static_cast<uint32_t>(std::stoul(value()));
    } else if (arg == "--device-id") {
      options.device_id = value();
    } else if (arg == "--model-path") {
      options.model_path = value();
    } else if (arg == "--no-model-file") {
      options.manage_model_file = false;
    } else if (arg == "--stdout") {
      to_stdout = true;
    } else if (arg == "--duration") {
      duration = std::stod(value());
    } else if (arg == "--print-model-hash") {
      std::cout << approved_model_sha256() << "\n";
      return 0;
    } else if (arg == "--write-model") {
      std::string error;
      if (!write_model_file(value(), false, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
      return 0;
    } else if (arg == "--list-scenarios") {
      for (const auto &info : scenario_catalog()) {
        std::cout << info.name << "\n    " << info.description
                  << "\n    expected:";
        for (const char *alert : info.expected_alerts) {
          std::cout << " " << alert;
        }
        std::cout << "\n";
      }
      return 0;
    } else if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else {
      std::cerr << "unknown option: " << arg << "\n";
      usage();
      return 2;
    }
  }

  for (const auto &name : scenarios) {
    if (name != "normal" && !parse_attack(name)) {
      std::cerr << "error: unknown scenario '" << name << "'\n";
      return 2;
    }
  }
  if (interval_ms < 50) {
    interval_ms = 50;
  }

  EdgeDeviceSimulator simulator(options);
  std::string error;
  if (!simulator.initialize(&error)) {
    std::cerr << "error: " << error << "\n";
    return 1;
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  std::signal(SIGPIPE, SIG_IGN);

  SystemClock clock;
  const uint64_t start_ms = clock.now_ms();
  simulator.step(start_ms);
  bool scenarios_started = false;
  auto start_scenarios = [&]() {
    for (const auto &name : scenarios) {
      std::string scenario_error;
      if (!simulator.activate(name, &scenario_error)) {
        std::cerr << "error: " << scenario_error << "\n";
      } else if (!to_stdout) {
        std::cout << "scenario '" << name << "' active" << std::endl;
      }
    }
    scenarios_started = true;
  };
  if (attack_delay <= 0) {
    start_scenarios();
  }

  // Simulation loop.
  std::thread loop([&] {
    auto next = std::chrono::steady_clock::now();
    while (!g_stop) {
      const uint64_t now = clock.now_ms();
      simulator.step(now);
      if (!scenarios_started &&
          static_cast<double>(now - start_ms) >= attack_delay * 1000.0) {
        start_scenarios();
      }
      if (to_stdout && simulator.telemetry_available()) {
        std::cout << nlohmann::json(simulator.snapshot()).dump() << std::endl;
      }
      if (duration > 0 &&
          static_cast<double>(now - start_ms) >= duration * 1000.0) {
        g_stop = true;
        break;
      }
      next += std::chrono::milliseconds(interval_ms);
      while (!g_stop && std::chrono::steady_clock::now() < next) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
    // Signal handlers only set the flag; stop the server from this thread.
    while (httplib::Server *server = g_server.load()) {
      server->stop();
      if (!server->is_running()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });

  if (to_stdout) {
    loop.join();
    return 0;
  }

  httplib::Server server;

  register_simulator_routes(server, simulator);

  std::cout << "edgeml-simulator listening on " << bind << ":" << port
            << " (device " << options.device_id << ", seed " << options.seed
            << ", model " << options.model_path << ")" << std::endl;
  if (!server.bind_to_port(bind, port)) {
    std::cerr << "error: cannot listen on " << bind << ":" << port << "\n";
    g_stop = true;
    loop.join();
    return 1;
  }
  g_server = &server;
  if (!g_stop && !server.listen_after_bind()) {
    if (!g_stop) {
      std::cerr << "error: cannot listen on " << bind << ":" << port << "\n";
      g_stop = true;
      loop.join();
      return 1;
    }
  }
  g_stop = true;
  loop.join();
  return 0;
}
