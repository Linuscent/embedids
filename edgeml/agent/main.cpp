/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * embedids-agent: EmbedIDS EdgeML runtime security agent.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <malloc.h>
#include <sys/stat.h>

#include "agent/agent.h"
#include "agent/config_loader.h"
#include "api/rest/api_server.h"

namespace {

std::atomic<bool> g_stop{false};

void handle_signal(int) { g_stop = true; }

void usage() {
  std::cout <<
      R"(Usage: embedids-agent [options]

Options:
  -c, --config PATH       YAML configuration (default /etc/embedids/config.yaml
                          when present, otherwise built-in defaults)
  --platform NAME         simulation | linux | jetson
  --device-id ID          Override device.id
  --sim-mode MODE         embedded | http (simulation platform)
  --sim-endpoint URL      edgeml-simulator URL for http mode
  --model-path PATH       Model file to protect
  --data-dir DIR          Directory for embedids.db and alerts.jsonl
  --api-bind ADDR         API bind address
  --api-port PORT         API port (0 = disable API)
  --static-dir DIR        Serve the dashboard from DIR
  --duration SECONDS      Exit after this many seconds
  --once                  Run a single cycle and print status JSON
  --print-config-hash     Print SHA-256 of the configuration and exit
  -v, --version           Print version
  -h, --help              Show this help
)";
}

bool file_exists(const std::string &path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

} // namespace

int main(int argc, char **argv) {
  using namespace edgeml;

  // Bound glibc per-thread arenas: keeps RSS small on memory-constrained
  // edge devices.
  mallopt(M_ARENA_MAX, 2);

  std::string config_path;
  std::string platform, device_id, sim_mode, sim_endpoint, model_path, data_dir,
      api_bind, static_dir;
  int api_port = -1;
  double duration = 0;
  bool once = false;
  bool print_config_hash = false;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    auto value = [&](std::string &out) {
      if (i + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        std::exit(2);
      }
      out = argv[++i];
    };
    std::string tmp;
    if (arg == "-c" || arg == "--config") {
      value(config_path);
    } else if (arg == "--platform") {
      value(platform);
    } else if (arg == "--device-id") {
      value(device_id);
    } else if (arg == "--sim-mode") {
      value(sim_mode);
    } else if (arg == "--sim-endpoint") {
      value(sim_endpoint);
    } else if (arg == "--model-path") {
      value(model_path);
    } else if (arg == "--data-dir") {
      value(data_dir);
    } else if (arg == "--api-bind") {
      value(api_bind);
    } else if (arg == "--api-port") {
      value(tmp);
      api_port = std::stoi(tmp);
    } else if (arg == "--static-dir") {
      value(static_dir);
    } else if (arg == "--duration") {
      value(tmp);
      duration = std::stod(tmp);
    } else if (arg == "--once") {
      once = true;
    } else if (arg == "--print-config-hash") {
      print_config_hash = true;
    } else if (arg == "-v" || arg == "--version") {
      std::cout << "embedids-agent " << kAgentVersion << " (EmbedIDS core "
                << embedids_get_version() << ")\n";
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

  Config config;
  std::string error;
  if (config_path.empty() && file_exists("/etc/embedids/config.yaml")) {
    config_path = "/etc/embedids/config.yaml";
  }
  if (!config_path.empty() && !load_config_file(config_path, config, &error)) {
    std::cerr << "error: " << error << "\n";
    return 1;
  }
  if (print_config_hash) {
    std::cout << config.source_sha256 << "\n";
    return 0;
  }

  if (!platform.empty())
    config.device.platform = platform;
  if (!device_id.empty())
    config.device.id = device_id;
  if (!sim_mode.empty())
    config.simulation.mode = sim_mode;
  if (!sim_endpoint.empty()) {
    config.simulation.endpoint = sim_endpoint;
    if (sim_mode.empty())
      config.simulation.mode = "http";
  }
  if (!model_path.empty())
    config.integrity.model.path = model_path;
  if (!data_dir.empty()) {
    config.storage.path = data_dir + "/embedids.db";
    config.alerts.json_file_path = data_dir + "/alerts.jsonl";
  }
  if (!api_bind.empty())
    config.api.bind = api_bind;
  if (api_port == 0)
    config.api.enabled = false;
  if (api_port > 0)
    config.api.port = api_port;
  if (!static_dir.empty())
    config.api.static_dir = static_dir;

  const std::string problems = validate_config(config);
  if (!problems.empty()) {
    std::cerr << "error: invalid configuration: " << problems << "\n";
    return 1;
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  std::signal(SIGPIPE, SIG_IGN);

  SystemClock clock;
  Agent agent(config, clock);
  if (!agent.initialize(&error)) {
    std::cerr << "error: " << error << "\n";
    return 1;
  }

  if (once) {
    agent.run_cycle();
    std::cout << agent.status().dump(2) << "\n";
    return 0;
  }

  std::unique_ptr<ApiServer> api;
  if (config.api.enabled) {
    api = std::make_unique<ApiServer>(agent, config.api);
    if (!api->start(&error)) {
      std::cerr << "error: " << error << "\n";
      return 1;
    }
  }

  std::cout << "embedids-agent " << kAgentVersion << " monitoring device '"
            << config.device.id << "' (platform " << config.device.platform;
  if (config.device.platform == "simulation") {
    std::cout << ", simulator " << config.simulation.mode;
  }
  std::cout << ")";
  if (api) {
    std::cout << ", API on " << config.api.bind << ":" << api->port();
  }
  std::cout << std::endl;

  std::thread stopper;
  if (duration > 0) {
    stopper = std::thread([duration] {
      const auto deadline =
          std::chrono::steady_clock::now() +
          std::chrono::milliseconds(static_cast<long>(duration * 1000));
      while (!g_stop && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      g_stop = true;
    });
  }

  agent.run(g_stop);

  if (api) {
    api->stop();
  }
  if (stopper.joinable()) {
    stopper.join();
  }
  std::cout << "embedids-agent stopped" << std::endl;
  return 0;
}
