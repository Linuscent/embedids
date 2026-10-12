/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "api/rest/api_server.h"

#include <httplib.h>

namespace edgeml {

namespace {

constexpr size_t kApiThreads = 4;

void send_json(httplib::Response &res, const nlohmann::json &body,
               int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

void send_error(httplib::Response &res, int status,
                const std::string &message) {
  send_json(res, {{"error", message}}, status);
}

size_t size_param(const httplib::Request &req, const char *name,
                  size_t fallback, size_t maximum) {
  if (!req.has_param(name)) {
    return fallback;
  }
  try {
    const long value = std::stol(req.get_param_value(name));
    if (value < 0) {
      return fallback;
    }
    return std::min(static_cast<size_t>(value), maximum);
  } catch (const std::exception &) {
    return fallback;
  }
}

} // namespace

ApiServer::ApiServer(Agent &agent, ApiConfig config)
    : agent_(agent), config_(std::move(config)),
      server_(std::make_unique<httplib::Server>()) {
  register_routes();
}

ApiServer::~ApiServer() { stop(); }

void ApiServer::register_routes() {
  auto &server = *server_;

  // A handful of workers is plenty for a dashboard and a few API clients;
  // httplib's default scales with core count, which wastes memory.
  server.new_task_queue = [] { return new httplib::ThreadPool(kApiThreads); };

  server.set_default_headers(
      {{"Access-Control-Allow-Origin", "*"}, {"Cache-Control", "no-store"}});
  server.Options(R"(.*)", [](const httplib::Request &, httplib::Response &res) {
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.status = 204;
  });

  // Register each route at /x and /api/x.
  auto get = [&](const std::string &pattern, httplib::Server::Handler handler) {
    server.Get(pattern, handler);
    server.Get("/api" + pattern, handler);
  };
  auto post = [&](const std::string &pattern,
                  httplib::Server::Handler handler) {
    server.Post(pattern, handler);
    server.Post("/api" + pattern, handler);
  };

  get("/health", [this](const httplib::Request &, httplib::Response &res) {
    send_json(res, agent_.health());
  });

  get("/status", [this](const httplib::Request &, httplib::Response &res) {
    send_json(res, agent_.status());
  });

  get("/metrics", [this](const httplib::Request &req, httplib::Response &res) {
    if (req.get_param_value("format") == "prometheus") {
      res.set_content(agent_.prometheus(), "text/plain; version=0.0.4");
      return;
    }
    const auto seconds = static_cast<uint32_t>(size_param(
        req, "history", 0, agent_.config().sampling.history_seconds));
    send_json(res, agent_.metrics(seconds));
  });

  get("/alerts", [this](const httplib::Request &req, httplib::Response &res) {
    const bool active_only = req.get_param_value("status") == "active" ||
                             req.get_param_value("active") == "true";
    send_json(res,
              agent_.alerts(active_only, size_param(req, "limit", 100, 2000)));
  });

  get(R"(/alerts/([A-Za-z0-9_\-]+))",
      [this](const httplib::Request &req, httplib::Response &res) {
        auto alert = agent_.alert(req.matches[1]);
        if (!alert) {
          send_error(res, 404, "alert not found");
          return;
        }
        send_json(res, *alert);
      });

  get("/events", [this](const httplib::Request &req, httplib::Response &res) {
    send_json(res, agent_.events(size_param(req, "limit", 100, 500)));
  });

  get("/rules", [this](const httplib::Request &, httplib::Response &res) {
    send_json(res, agent_.rules());
  });

  post(R"(/demo/scenario/([a-z\-]+))",
       [this](const httplib::Request &req, httplib::Response &res) {
         if (!config_.demo_controls) {
           send_error(res, 403, "demo controls disabled");
           return;
         }
         nlohmann::json result;
         std::string error;
         if (!agent_.demo_scenario(req.matches[1], &result, &error)) {
           send_error(res, error.rfind("unknown scenario", 0) == 0 ? 404 : 409,
                      error);
           return;
         }
         send_json(res, {{"ok", true},
                         {"scenario", req.matches[1].str()},
                         {"simulation", result}});
       });

  post("/demo/reset", [this](const httplib::Request &, httplib::Response &res) {
    if (!config_.demo_controls) {
      send_error(res, 403, "demo controls disabled");
      return;
    }
    nlohmann::json result;
    std::string error;
    if (!agent_.demo_reset(&result, &error)) {
      send_error(res, 409, error);
      return;
    }
    send_json(res, {{"ok", true}, {"simulation", result}});
  });

  if (!config_.static_dir.empty()) {
    server.set_mount_point("/", config_.static_dir);
  }
}

bool ApiServer::start(std::string *error) {
  if (config_.port == 0) {
    bound_port_ = server_->bind_to_any_port(config_.bind);
  } else {
    bound_port_ =
        server_->bind_to_port(config_.bind, config_.port) ? config_.port : -1;
  }
  if (bound_port_ <= 0) {
    if (error) {
      *error = "cannot bind API server to " + config_.bind + ":" +
               std::to_string(config_.port);
    }
    return false;
  }
  thread_ = std::thread([this] { server_->listen_after_bind(); });
  return true;
}

void ApiServer::stop() {
  if (server_) {
    server_->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

} // namespace edgeml
