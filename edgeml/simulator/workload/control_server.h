/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * HTTP telemetry/control API of edgeml-simulator:
 *   GET  /health, /telemetry (503 while telemetry is stopped), /state,
 *        /scenarios
 *   POST /scenario/{name}, /reset
 */

#ifndef EDGEML_SIMULATOR_WORKLOAD_CONTROL_SERVER_H
#define EDGEML_SIMULATOR_WORKLOAD_CONTROL_SERVER_H

#include "simulator/workload/edge_device_simulator.h"

namespace httplib {
class Server;
}

namespace edgeml::sim {

void register_simulator_routes(httplib::Server &server,
                               EdgeDeviceSimulator &simulator,
                               bool log_actions = true);

} // namespace edgeml::sim

#endif
