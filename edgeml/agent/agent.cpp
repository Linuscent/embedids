/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent/agent.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include <unistd.h>

#include "agent/alert_sinks.h"
#include "core/integrity/hash_util.h"
#include "embedids.h"

namespace edgeml {

namespace {

constexpr size_t kMaxEvents = 500;
constexpr uint64_t kPruneIntervalMs = 3600ULL * 1000ULL;
constexpr uint64_t kCorrelationLookbackMs = 10ULL * 60ULL * 1000ULL;
constexpr const char *kResetResolution = "demo reset";

std::string hostname() {
  char buffer[256] = {};
  if (gethostname(buffer, sizeof(buffer) - 1) != 0) {
    return "edge-device";
  }
  return buffer;
}

std::string prometheus_name(const std::string &metric) {
  std::string name = "edgeml_" + metric;
  for (auto &c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c))) {
      c = '_';
    }
  }
  return name;
}

} // namespace

Agent::Agent(Config config, const Clock &clock)
    : config_(std::move(config)), clock_(clock) {}

Agent::~Agent() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (store_ && store_->is_open()) {
    summary_.flush(*store_);
  }
}

void Agent::add_alert_sink(std::unique_ptr<AlertSink> sink) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (alerts_) {
    alerts_->add_sink(std::move(sink));
  } else {
    pending_sinks_.push_back(std::move(sink));
  }
}

void Agent::set_collectors(CollectorSet collectors) {
  collector_set_ = std::move(collectors);
  collectors_injected_ = true;
}

bool Agent::initialize(std::string *error) {
  std::lock_guard<std::mutex> pipeline(pipeline_mutex_);
  if (initialized_) {
    return true;
  }
  start_ms_ = clock_.now_ms();
  std::vector<std::string> warnings;

  if (!collectors_injected_ &&
      !build_collectors(config_, clock_, collector_set_, error)) {
    return false;
  }

  telemetry_ = std::make_unique<TelemetryManager>(config_, clock_);
  for (auto &collector : collector_set_.collectors) {
    telemetry_->add_collector(std::move(collector));
  }
  collector_set_.collectors.clear();
  telemetry_->initialize(&warnings);

  detection_ = std::make_unique<DetectionEngine>(config_);
  if (!detection_->initialize(error)) {
    return false;
  }
  correlator_ = std::make_unique<Correlator>(config_.correlations);

  binary_hash_ = sha256_file("/proc/self/exe").value_or("unavailable");

  std::lock_guard<std::mutex> state(state_mutex_);
  alerts_ = std::make_unique<AlertManager>(config_);

  if (config_.storage.enabled) {
    store_ = std::make_unique<EventStore>();
    std::string store_error;
    if (store_->open(config_.storage.path, &store_error)) {
      alerts_->set_next_sequence(
          store_->max_alert_sequence(config_.alerts.id_prefix) + 1);
      alerts_->add_sink(std::make_unique<StoreAlertSink>(*store_));
    } else {
      warnings.push_back("event storage disabled: " + store_error);
      store_.reset();
    }
  }

  if (config_.alerts.console) {
    alerts_->add_sink(
        std::make_unique<ConsoleAlertSink>(isatty(STDOUT_FILENO) != 0));
  }
  if (config_.alerts.json_file) {
    auto sink =
        std::make_unique<JsonFileAlertSink>(config_.alerts.json_file_path);
    if (sink->ok()) {
      alerts_->add_sink(std::move(sink));
    } else {
      warnings.push_back("cannot open alert file " +
                         config_.alerts.json_file_path);
    }
  }
  if (config_.alerts.syslog || !config_.alerts.syslog_server.empty()) {
    alerts_->add_sink(std::make_unique<SyslogAlertSink>(
        config_.alerts.syslog, config_.alerts.syslog_server, hostname()));
  }
  if (!config_.alerts.webhook_url.empty()) {
    alerts_->add_sink(
        std::make_unique<WebhookAlertSink>(config_.alerts.webhook_url));
  }
  for (auto &sink : pending_sinks_) {
    alerts_->add_sink(std::move(sink));
  }
  pending_sinks_.clear();

  collector_status_ = telemetry_->status();

  Event started;
  started.timestamp_ms = start_ms_;
  started.type = "agent.started";
  started.source = "agent";
  started.message = std::string("EmbedIDS agent ") + kAgentVersion +
                    " started on " + config_.device.platform + " platform";
  started.data = {{"version", kAgentVersion},
                  {"embedids_core", embedids_get_version()},
                  {"platform", config_.device.platform},
                  {"binary_sha256", binary_hash_},
                  {"config_path", config_.source_path},
                  {"config_sha256", config_.source_sha256},
                  {"signals", detection_->signals().signals().size()},
                  {"embedids_metrics", detection_->signals().metric_count()}};
  record_event_locked(std::move(started));
  for (const auto &warning : warnings) {
    Event event;
    event.timestamp_ms = start_ms_;
    event.type = "agent.warning";
    event.severity = Severity::Low;
    event.source = "agent";
    event.message = warning;
    record_event_locked(std::move(event));
    std::cerr << "warning: " << warning << "\n";
  }
  if (store_) {
    store_->set_agent_state("version", kAgentVersion, start_ms_);
    store_->set_agent_state("binary_sha256", binary_hash_, start_ms_);
    store_->set_agent_state("config_sha256", config_.source_sha256, start_ms_);
    store_->set_agent_state("started", format_iso8601(start_ms_), start_ms_);
  }

  initialized_ = true;
  return true;
}

void Agent::record_event_locked(Event event) {
  event.id = next_event_id_++;
  if (event.timestamp_ms == 0) {
    event.timestamp_ms = clock_.now_ms();
  }
  if (store_) {
    store_->record_event(event);
  }
  events_.push_back(std::move(event));
  while (events_.size() > kMaxEvents) {
    events_.pop_front();
  }
}

void Agent::handle_alert_updates_locked(
    const std::vector<AlertUpdate> &updates) {
  for (const auto &update : updates) {
    Event event;
    event.timestamp_ms = update.change == AlertChange::Raised
                             ? update.alert.timestamp_ms
                             : update.alert.updated_ms;
    event.type = std::string("alert.") + to_string(update.change);
    event.severity = update.change == AlertChange::Resolved
                         ? Severity::Info
                         : update.alert.severity;
    event.source = update.alert.rule;
    event.message = update.alert.title;
    if (!update.alert.subject.empty()) {
      event.message += " (" + update.alert.subject + ")";
    }
    event.data = {{"alert_id", update.alert.alert_id},
                  {"rule", update.alert.rule},
                  {"severity", to_string(update.alert.severity)},
                  {"signals", update.alert.signals}};
    record_event_locked(std::move(event));
  }
}

void Agent::update_history_locked(const TelemetryBatch &batch) {
  const uint64_t keep_ms = config_.sampling.history_seconds * 1000ULL;
  for (const auto &sample : batch.metrics) {
    latest_[sample.metric] = sample;
    auto &series = history_[sample.metric];
    series.emplace_back(sample.timestamp_ms, sample.value);
    while (!series.empty() &&
           series.front().first + keep_ms < batch.timestamp_ms) {
      series.pop_front();
    }
  }
}

void Agent::run_cycle() {
  std::lock_guard<std::mutex> pipeline(pipeline_mutex_);
  if (!initialized_) {
    return;
  }
  const uint64_t cycle_start = monotonic_ms();

  std::vector<CollectorTransition> transitions;
  TelemetryBatch batch = telemetry_->collect(&transitions);
  const uint64_t processing_start = monotonic_ms();

  std::vector<Finding> findings = detection_->process(batch);

  nlohmann::json sim_state;
  if (collector_set_.simulation_control && cycles_ % 2 == 0) {
    sim_state = collector_set_.simulation_control->state();
  }

  std::lock_guard<std::mutex> state(state_mutex_);
  const uint64_t now = batch.timestamp_ms;

  auto updates = alerts_->reconcile(findings, false, now);
  std::vector<Alert> recent;
  for (const auto &alert : alerts_->alerts()) {
    // Alerts closed by a demo reset must not feed new correlations.
    const bool reset = !alert.active && alert.resolution == kResetResolution;
    if (!reset &&
        (alert.active || alert.resolved_ms + kCorrelationLookbackMs >= now)) {
      recent.push_back(alert);
    }
  }
  auto correlated = correlator_->evaluate(recent, now);
  auto correlated_updates = alerts_->reconcile(correlated, true, now);
  updates.insert(updates.end(), correlated_updates.begin(),
                 correlated_updates.end());

  last_processing_ms_ = static_cast<double>(monotonic_ms() - processing_start);
  max_processing_ms_ = std::max(max_processing_ms_, last_processing_ms_);
  batch.add("agent", metrics::kAgentLatency, last_processing_ms_, "ms");

  for (const auto &transition : transitions) {
    Event event;
    event.timestamp_ms = now;
    event.type =
        transition.healthy ? "collector.recovered" : "collector.failed";
    event.severity = transition.healthy ? Severity::Info : Severity::Medium;
    event.source = transition.name;
    event.message = "Collector " + transition.name +
                    (transition.healthy ? " recovered" : " stopped reporting");
    record_event_locked(std::move(event));
  }
  handle_alert_updates_locked(updates);
  update_history_locked(batch);
  collector_status_ = telemetry_->status();
  if (!sim_state.is_null()) {
    simulation_state_ = std::move(sim_state);
  }

  if (store_) {
    summary_.add(batch, *store_);
    if (now - last_prune_ms_ >= kPruneIntervalMs) {
      store_->prune(now, config_.storage);
      last_prune_ms_ = now;
    }
  }

  last_cycle_ms_ = static_cast<double>(monotonic_ms() - cycle_start);
  cycles_++;
}

void Agent::run(const std::atomic<bool> &stop) {
  using namespace std::chrono;
  const auto interval = milliseconds(config_.sampling.interval_ms);
  auto next = steady_clock::now();
  while (!stop) {
    run_cycle();
    next += interval;
    const auto now = steady_clock::now();
    if (next < now) {
      next = now;
    }
    while (!stop && steady_clock::now() < next) {
      std::this_thread::sleep_for(std::min<steady_clock::duration>(
          milliseconds(100), next - steady_clock::now()));
    }
  }
}

nlohmann::json Agent::health() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  bool telemetry_ok = true;
  for (const auto &status : collector_status_) {
    if (status.initialized && status.heartbeat_required && !status.healthy) {
      telemetry_ok = false;
    }
  }
  return {{"status", initialized_ ? "ok" : "starting"},
          {"telemetry", telemetry_ok ? "ok" : "degraded"},
          {"cycles", cycles_.load()},
          {"timestamp", format_iso8601(clock_.now_ms())}};
}

SecurityState Agent::security_state() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return alerts_ ? security_state_for(alerts_->worst_active())
                 : SecurityState::Healthy;
}

nlohmann::json Agent::status() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  const uint64_t now = clock_.now_ms();

  nlohmann::json by_severity = {
      {"critical", 0}, {"high", 0}, {"medium", 0}, {"low", 0}, {"info", 0}};
  size_t active = 0;
  if (alerts_) {
    for (const auto &alert : alerts_->alerts()) {
      if (alert.active) {
        active++;
        by_severity[to_string(alert.severity)] =
            by_severity[to_string(alert.severity)].get<int>() + 1;
      }
    }
  }

  auto latest_value = [&](const char *metric) -> nlohmann::json {
    auto it = latest_.find(metric);
    return it == latest_.end() ? nlohmann::json(nullptr)
                               : nlohmann::json(it->second.value);
  };

  nlohmann::json model = {{"status", "unknown"}};
  auto model_it = latest_.find(metrics::kModelIntegrity);
  if (model_it != latest_.end()) {
    const auto &labels = model_it->second.labels;
    model = {
        {"status", labels.count("status") ? labels.at("status") : ""},
        {"path", labels.count("path") ? labels.at("path") : ""},
        {"expected", labels.count("expected") ? labels.at("expected") : ""},
        {"current", labels.count("current") ? labels.at("current") : ""},
        {"baseline", labels.count("baseline") ? labels.at("baseline") : ""}};
  }
  nlohmann::json config_integrity = {{"status", "unknown"}};
  auto config_it = latest_.find(metrics::kConfigIntegrity);
  if (config_it != latest_.end()) {
    const auto &labels = config_it->second.labels;
    config_integrity = {
        {"status", labels.count("status") ? labels.at("status") : ""},
        {"path", labels.count("path") ? labels.at("path") : ""}};
  }

  const auto state = alerts_ ? security_state_for(alerts_->worst_active())
                             : SecurityState::Healthy;

  return {
      {"timestamp", format_iso8601(now)},
      {"device_id", config_.device.id},
      {"platform", config_.device.platform},
      {"workload", config_.device.workload},
      {"security_state", to_string(state)},
      {"active_alerts", active},
      {"active_by_severity", by_severity},
      {"detections_total", alerts_ ? alerts_->total_raised() : 0},
      {"heartbeat_age_seconds", latest_value(metrics::kAgentHeartbeat)},
      {"telemetry_health_percent", latest_value(metrics::kTelemetryHealth)},
      {"model_integrity", model},
      {"config_integrity", config_integrity},
      {"agent",
       {{"version", kAgentVersion},
        {"embedids_core", embedids_get_version()},
        {"binary_sha256", binary_hash_},
        {"config_sha256", config_.source_sha256},
        {"uptime_seconds", (now - start_ms_) / 1000},
        {"cycles", cycles_.load()},
        {"sampling_interval_ms", config_.sampling.interval_ms},
        {"cpu_percent", latest_value(metrics::kAgentCpu)},
        {"memory_rss_mb", latest_value(metrics::kAgentMemory)},
        {"processing_latency_ms", last_processing_ms_},
        {"max_processing_latency_ms", max_processing_ms_},
        {"cycle_latency_ms", last_cycle_ms_},
        {"storage", store_ ? config_.storage.path : "disabled"}}},
      {"collectors", collector_status_},
      {"thresholds",
       {{"gpu_high_utilization", config_.gpu.high_utilization},
        {"expected_fps", config_.ai.expected_fps},
        {"minimum_fps", config_.ai.minimum_fps},
        {"baseline_fps", config_.ai.expected_fps * config_.ai.baseline_ratio},
        {"cpu_exhaustion", config_.system.cpu_exhaustion},
        {"memory_exhaustion", config_.system.memory_exhaustion},
        {"bytes_out_anomaly", config_.network.baseline_bytes_out *
                                  config_.network.anomaly_multiplier},
        {"bytes_out_baseline", config_.network.baseline_bytes_out},
        {"heartbeat_timeout_seconds", config_.heartbeat.timeout_seconds}}},
      {"simulation", simulation_state_.is_null() ? nlohmann::json(nullptr)
                                                 : simulation_state_},
      {"demo_controls", config_.api.demo_controls &&
                            collector_set_.simulation_control != nullptr},
  };
}

nlohmann::json Agent::metrics(uint32_t history_seconds) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  const uint64_t now = clock_.now_ms();
  nlohmann::json latest = nlohmann::json::object();
  for (const auto &entry : latest_) {
    latest[entry.first] = entry.second;
  }
  nlohmann::json history = nlohmann::json::object();
  if (history_seconds > 0) {
    const uint64_t since =
        now > history_seconds * 1000ULL ? now - history_seconds * 1000ULL : 0;
    for (const auto &entry : history_) {
      nlohmann::json points = nlohmann::json::array();
      for (const auto &point : entry.second) {
        if (point.first >= since) {
          points.push_back({point.first, point.second});
        }
      }
      history[entry.first] = std::move(points);
    }
  }
  return {{"timestamp", format_iso8601(now)},
          {"device_id", config_.device.id},
          {"latest", latest},
          {"history", history}};
}

std::string Agent::prometheus() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::ostringstream out;
  const std::string device = "device_id=\"" + config_.device.id + "\"";
  for (const auto &entry : latest_) {
    const std::string name = prometheus_name(entry.first);
    out << "# TYPE " << name << " gauge\n";
    out << name << "{" << device << "} " << entry.second.value << "\n";
  }
  out << "# TYPE embedids_active_alerts gauge\n";
  std::map<std::string, int> counts;
  if (alerts_) {
    for (const auto &alert : alerts_->alerts()) {
      if (alert.active) {
        counts[to_string(alert.severity)]++;
      }
    }
  }
  for (const char *severity : {"critical", "high", "medium", "low", "info"}) {
    out << "embedids_active_alerts{" << device << ",severity=\"" << severity
        << "\"} " << counts[severity] << "\n";
  }
  out << "# TYPE embedids_detections_total counter\n";
  out << "embedids_detections_total{" << device << "} "
      << (alerts_ ? alerts_->total_raised() : 0) << "\n";
  return out.str();
}

nlohmann::json Agent::alerts(bool active_only, size_t limit) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  nlohmann::json result = nlohmann::json::array();
  if (!alerts_) {
    return result;
  }
  const auto &all = alerts_->alerts();
  for (auto it = all.rbegin(); it != all.rend() && result.size() < limit;
       ++it) {
    if (!active_only || it->active) {
      result.push_back(*it);
    }
  }
  return result;
}

std::optional<nlohmann::json> Agent::alert(const std::string &alert_id) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (alerts_) {
    if (auto found = alerts_->find(alert_id)) {
      return nlohmann::json(*found);
    }
  }
  if (store_) {
    for (const auto &body : store_->recent_alerts(5000)) {
      if (body.value("alert_id", "") == alert_id) {
        return body;
      }
    }
  }
  return std::nullopt;
}

nlohmann::json Agent::events(size_t limit) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  nlohmann::json result = nlohmann::json::array();
  for (auto it = events_.rbegin();
       it != events_.rend() && result.size() < limit; ++it) {
    result.push_back(*it);
  }
  return result;
}

nlohmann::json Agent::rules() const {
  nlohmann::json result = {{"rules", nlohmann::json::array()},
                           {"correlations", nlohmann::json::array()}};
  for (const auto &rule : rule_catalog()) {
    result["rules"].push_back({{"id", rule.id},
                               {"title", rule.title},
                               {"severity", to_string(rule.severity)},
                               {"condition", rule.condition}});
  }
  for (const auto &rule : config_.correlations) {
    result["correlations"].push_back({{"name", rule.name},
                                      {"title", rule.title},
                                      {"severity", to_string(rule.severity)},
                                      {"window_seconds", rule.window_seconds},
                                      {"requires", rule.requires_signals}});
  }
  return result;
}

std::vector<Alert> Agent::alert_snapshot() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!alerts_) {
    return {};
  }
  return {alerts_->alerts().begin(), alerts_->alerts().end()};
}

bool Agent::demo_enabled() const {
  return config_.api.demo_controls &&
         collector_set_.simulation_control != nullptr;
}

bool Agent::demo_scenario(const std::string &name, nlohmann::json *result,
                          std::string *error) {
  if (!demo_enabled()) {
    if (error) {
      *error = "demo controls are only available on the simulation platform";
    }
    return false;
  }
  if (name == "reset") {
    return demo_reset(result, error);
  }
  if (!collector_set_.simulation_control->activate(name, result, error)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (result) {
    simulation_state_ = *result;
  }
  Event event;
  event.type = "demo.scenario";
  event.source = "demo";
  event.message = name == "normal"
                      ? "Normal workload restored (attacks stopped)"
                      : "Scenario '" + name + "' activated";
  event.data = {{"scenario", name}};
  record_event_locked(std::move(event));
  return true;
}

bool Agent::demo_reset(nlohmann::json *result, std::string *error) {
  if (!demo_enabled()) {
    if (error) {
      *error = "demo controls are only available on the simulation platform";
    }
    return false;
  }
  if (!collector_set_.simulation_control->reset(result, error)) {
    return false;
  }
  std::lock_guard<std::mutex> pipeline(pipeline_mutex_);
  detection_->reset();
  telemetry_->reset_heartbeats();

  std::lock_guard<std::mutex> lock(state_mutex_);
  const uint64_t now = clock_.now_ms();
  handle_alert_updates_locked(alerts_->resolve_all(now, kResetResolution));
  if (result) {
    simulation_state_ = *result;
  }
  Event event;
  event.timestamp_ms = now;
  event.type = "demo.reset";
  event.source = "demo";
  event.message = "Environment reset to baseline";
  record_event_locked(std::move(event));
  return true;
}

} // namespace edgeml
