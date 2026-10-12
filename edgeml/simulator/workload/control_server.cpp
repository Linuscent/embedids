/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/workload/control_server.h"

#include <iostream>

#include <httplib.h>

namespace edgeml::sim {

namespace {

void send_json(httplib::Response &res, const nlohmann::json &body,
               int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

} // namespace

void register_simulator_routes(httplib::Server &server,
                               EdgeDeviceSimulator &simulator,
                               bool log_actions) {
  server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
    send_json(res, {{"status", "ok"}});
  });
  server.Get("/telemetry",
             [&simulator](const httplib::Request &, httplib::Response &res) {
               if (!simulator.telemetry_available()) {
                 send_json(res, {{"error", "telemetry stopped"}}, 503);
                 return;
               }
               send_json(res, simulator.snapshot());
             });
  server.Get("/state",
             [&simulator](const httplib::Request &, httplib::Response &res) {
               send_json(res, simulator.state());
             });
  server.Get("/scenarios",
             [&simulator](const httplib::Request &, httplib::Response &res) {
               send_json(res, simulator.state()["scenarios"]);
             });
  server.Post(R"(/scenario/([a-z\-]+))",
              [&simulator, log_actions](const httplib::Request &req,
                                        httplib::Response &res) {
                std::string error;
                const std::string name = req.matches[1];
                if (!simulator.activate(name, &error)) {
                  send_json(res, {{"error", error}}, 404);
                  return;
                }
                if (log_actions) {
                  std::cout << "scenario '" << name << "' activated"
                            << std::endl;
                }
                send_json(res, simulator.state());
              });
  server.Post("/reset", [&simulator, log_actions](const httplib::Request &,
                                                  httplib::Response &res) {
    simulator.reset();
    if (log_actions) {
      std::cout << "simulation reset" << std::endl;
    }
    send_json(res, simulator.state());
  });
}

} // namespace edgeml::sim
