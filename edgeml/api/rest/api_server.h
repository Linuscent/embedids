/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * REST API (spec section 7.3). Every route is also served under /api so the
 * dashboard works both behind the nginx proxy and when served by the agent.
 */

#ifndef EDGEML_API_REST_API_SERVER_H
#define EDGEML_API_REST_API_SERVER_H

#include <memory>
#include <string>
#include <thread>

#include "agent/agent.h"
#include "core/config/config.h"

namespace httplib {
class Server;
}

namespace edgeml {

class ApiServer {
public:
  ApiServer(Agent &agent, ApiConfig config);
  ~ApiServer();

  ApiServer(const ApiServer &) = delete;
  ApiServer &operator=(const ApiServer &) = delete;

  /** Bind and start serving in a background thread. port 0 = ephemeral. */
  bool start(std::string *error = nullptr);
  void stop();
  int port() const { return bound_port_; }

private:
  void register_routes();

  Agent &agent_;
  ApiConfig config_;
  std::unique_ptr<httplib::Server> server_;
  std::thread thread_;
  int bound_port_ = 0;
};

} // namespace edgeml

#endif
