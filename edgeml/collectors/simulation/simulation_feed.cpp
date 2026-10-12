/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/simulation/simulation_feed.h"

#include <httplib.h>

namespace edgeml {

namespace {

httplib::Client make_client(const std::string &endpoint, uint32_t timeout_ms) {
  httplib::Client client(endpoint);
  const time_t sec = timeout_ms / 1000;
  const time_t usec = (timeout_ms % 1000) * 1000;
  client.set_connection_timeout(sec, usec);
  client.set_read_timeout(sec, usec);
  client.set_write_timeout(sec, usec);
  return client;
}

} // namespace

const sim::SimSnapshot *SimulationFeed::get(uint64_t cycle) {
  if (!have_cycle_ || cycle != cycle_) {
    have_cycle_ = true;
    cycle_ = cycle;
    ok_ = fetch(snapshot_);
  }
  return ok_ ? &snapshot_ : nullptr;
}

bool InProcessSimulationFeed::fetch(sim::SimSnapshot &snapshot) {
  simulator_->step(clock_.now_ms());
  if (!simulator_->telemetry_available()) {
    return false;
  }
  snapshot = simulator_->snapshot();
  return true;
}

bool HttpSimulationFeed::fetch(sim::SimSnapshot &snapshot) {
  auto client = make_client(endpoint_, timeout_ms_);
  auto response = client.Get("/telemetry");
  if (!response || response->status != 200) {
    return false;
  }
  auto doc = nlohmann::json::parse(response->body, nullptr, false);
  if (doc.is_discarded()) {
    return false;
  }
  try {
    snapshot = doc.get<sim::SimSnapshot>();
  } catch (const nlohmann::json::exception &) {
    return false;
  }
  return true;
}

bool InProcessSimulationControl::activate(const std::string &scenario,
                                          nlohmann::json *result,
                                          std::string *error) {
  if (!simulator_->activate(scenario, error)) {
    return false;
  }
  if (result) {
    *result = simulator_->state();
  }
  return true;
}

bool InProcessSimulationControl::reset(nlohmann::json *result,
                                       std::string * /*error*/) {
  simulator_->reset();
  if (result) {
    *result = simulator_->state();
  }
  return true;
}

nlohmann::json InProcessSimulationControl::state() {
  return simulator_->state();
}

bool HttpSimulationControl::post(const std::string &path,
                                 nlohmann::json *result, std::string *error) {
  auto client = make_client(endpoint_, timeout_ms_ * 3);
  auto response = client.Post(path, "", "application/json");
  if (!response) {
    if (error) {
      *error = "simulator unreachable at " + endpoint_;
    }
    return false;
  }
  auto doc = nlohmann::json::parse(response->body, nullptr, false);
  if (response->status != 200) {
    if (error) {
      *error =
          !doc.is_discarded() && doc.contains("error")
              ? doc["error"].get<std::string>()
              : "simulator returned HTTP " + std::to_string(response->status);
    }
    return false;
  }
  if (result && !doc.is_discarded()) {
    *result = doc;
  }
  return true;
}

bool HttpSimulationControl::activate(const std::string &scenario,
                                     nlohmann::json *result,
                                     std::string *error) {
  return post("/scenario/" + scenario, result, error);
}

bool HttpSimulationControl::reset(nlohmann::json *result, std::string *error) {
  return post("/reset", result, error);
}

nlohmann::json HttpSimulationControl::state() {
  auto client = make_client(endpoint_, timeout_ms_);
  auto response = client.Get("/state");
  if (!response || response->status != 200) {
    return {{"reachable", false}, {"endpoint", endpoint_}};
  }
  auto doc = nlohmann::json::parse(response->body, nullptr, false);
  if (doc.is_discarded()) {
    return {{"reachable", false}, {"endpoint", endpoint_}};
  }
  doc["reachable"] = true;
  return doc;
}

} // namespace edgeml
