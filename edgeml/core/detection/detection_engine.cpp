/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/detection/detection_engine.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

#include "core/common/clock.h"
#include "core/integrity/hash_util.h"

namespace edgeml {

namespace {

namespace sig {
constexpr const char *kGpuHigh = "gpu_high";
constexpr const char *kFpsBelowBaseline = "fps_below_baseline";
constexpr const char *kFpsDegraded = "fps_degraded";
constexpr const char *kCpuExhausted = "cpu_exhausted";
constexpr const char *kMemoryExhausted = "memory_exhausted";
constexpr const char *kUnknownProcesses = "unknown_processes";
constexpr const char *kPrivilegedProcesses = "privileged_processes";
constexpr const char *kNewDestination = "new_destination";
constexpr const char *kBytesOutAnomaly = "bytes_out_anomaly";
constexpr const char *kBytesOutExfil = "bytes_out_exfil";
constexpr const char *kBytesOutEscalation = "bytes_out_escalation";
constexpr const char *kModelAccess = "model_access";
constexpr const char *kModelIntegrityBad = "model_integrity_bad";
constexpr const char *kConfigIntegrityBad = "config_integrity_bad";
constexpr const char *kHeartbeatLost = "heartbeat_lost";
} // namespace sig

uint32_t history_size_for(const Config &config) {
  uint32_t longest_s = std::max(
      {config.gpu.sustain_seconds, config.ai.degradation_seconds,
       config.system.exhaustion_seconds, config.network.anomaly_seconds});
  const uint32_t interval = std::max<uint32_t>(config.sampling.interval_ms, 1);
  uint32_t needed = (longest_s * 1000) / interval + 8;
  return std::max<uint32_t>(needed, 64);
}

double round1(double value) { return std::round(value * 10.0) / 10.0; }

std::string format_rate(double bytes_per_s) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(1);
  if (bytes_per_s >= 1e6) {
    out << bytes_per_s / 1e6 << " MB/s";
  } else {
    out << bytes_per_s / 1e3 << " KB/s";
  }
  return out.str();
}

std::string join(const std::vector<std::string> &items) {
  std::string out;
  for (const auto &item : items) {
    if (!out.empty()) {
      out += ", ";
    }
    out += item;
  }
  return out;
}

} // namespace

const std::vector<RuleDescriptor> &rule_catalog() {
  static const std::vector<RuleDescriptor> catalog = {
      {rules::kUnexpectedGpuWorkload, "GPU workload outside expected profile",
       Severity::High,
       "gpu.utilization > gpu.high_utilization AND ai.inference.rate < "
       "expected_fps * baseline_ratio FOR gpu.sustain_seconds"},
      {rules::kRogueProcess, "Unknown / privileged process", Severity::Medium,
       "process not in allow list AND runtime > min_runtime_seconds "
       "(HIGH when UID == 0)"},
      {rules::kUnknownDestination, "Connection to unknown destination",
       Severity::Medium,
       "outbound destination not in allow list (HIGH when network.bytes.out "
       "> escalation_bytes_out)"},
      {rules::kOutboundTrafficAnomaly, "Outbound traffic anomaly",
       Severity::Medium,
       "network.bytes.out > baseline * multiplier FOR anomaly_seconds"},
      {rules::kDataExfiltration, "Possible data exfiltration", Severity::High,
       "unknown destination AND network.bytes.out > baseline * multiplier"},
      {rules::kModelIntegrity, "AI model integrity violation",
       Severity::Critical, "SHA256(model) != approved hash"},
      {rules::kConfigIntegrity, "Configuration integrity violation",
       Severity::High, "SHA256(config) changed since startup"},
      {rules::kModelFileAccess, "Unexpected model file access",
       Severity::Medium, "model read by a process outside allowed_readers"},
      {rules::kInferenceDegradation, "Inference performance degradation",
       Severity::Medium,
       "ai.inference.rate < minimum_fps FOR "
       "degradation_seconds"},
      {rules::kResourceExhaustion, "Resource exhaustion", Severity::High,
       "cpu > cpu_exhaustion AND memory > memory_exhaustion FOR "
       "exhaustion_seconds"},
      {rules::kHeartbeatLost, "Telemetry heartbeat lost", Severity::Critical,
       "collector heartbeat missing > heartbeat.timeout_seconds"},
  };
  return catalog;
}

std::vector<SignalSpec> DetectionEngine::build_signal_specs(const Config &c) {
  const uint32_t gpu_ms = c.gpu.sustain_seconds * 1000;
  const double exfil_bytes =
      c.network.baseline_bytes_out * c.network.anomaly_multiplier;
  return {
      {sig::kGpuHigh, metrics::kGpuUtilization, Comparison::Greater,
       c.gpu.high_utilization, gpu_ms},
      {sig::kFpsBelowBaseline, metrics::kInferenceRate, Comparison::Less,
       c.ai.expected_fps * c.ai.baseline_ratio, gpu_ms},
      {sig::kFpsDegraded, metrics::kInferenceRate, Comparison::Less,
       c.ai.minimum_fps, c.ai.degradation_seconds * 1000},
      {sig::kCpuExhausted, metrics::kCpuUtilization, Comparison::Greater,
       c.system.cpu_exhaustion, c.system.exhaustion_seconds * 1000},
      {sig::kMemoryExhausted, metrics::kMemoryPercent, Comparison::Greater,
       c.system.memory_exhaustion, c.system.exhaustion_seconds * 1000},
      {sig::kUnknownProcesses, metrics::kUnknownProcesses, Comparison::Greater,
       0.0, 0},
      {sig::kPrivilegedProcesses, metrics::kPrivilegedProcesses,
       Comparison::Greater, 0.0, 0},
      {sig::kNewDestination, metrics::kNewDestinations, Comparison::Greater,
       0.0, 0},
      {sig::kBytesOutAnomaly, metrics::kBytesOut, Comparison::Greater,
       exfil_bytes, c.network.anomaly_seconds * 1000},
      {sig::kBytesOutExfil, metrics::kBytesOut, Comparison::Greater,
       exfil_bytes, 0},
      {sig::kBytesOutEscalation, metrics::kBytesOut, Comparison::Greater,
       c.network.escalation_bytes_out, 0},
      {sig::kModelAccess, metrics::kModelFileAccess, Comparison::Greater, 0.0,
       0},
      {sig::kModelIntegrityBad, metrics::kModelIntegrity, Comparison::Less, 0.5,
       0},
      {sig::kConfigIntegrityBad, metrics::kConfigIntegrity, Comparison::Less,
       0.5, 0},
      {sig::kHeartbeatLost, metrics::kAgentHeartbeat, Comparison::Greater,
       static_cast<double>(c.heartbeat.timeout_seconds), 0},
  };
}

DetectionEngine::DetectionEngine(const Config &config)
    : config_(config), signals_(history_size_for(config)),
      process_policy_(config.process), network_policy_(config.network),
      model_access_policy_(config.integrity) {}

bool DetectionEngine::initialize(std::string *error) {
  for (const auto &spec : build_signal_specs(config_)) {
    if (!signals_.add_signal(spec, error)) {
      return false;
    }
  }
  return signals_.finalize(error);
}

double DetectionEngine::value_of(const TelemetryBatch &batch,
                                 const char *metric) const {
  if (auto value = batch.value(metric)) {
    return *value;
  }
  for (const auto &spec : signals_.signals()) {
    if (spec.metric == metric) {
      const SignalState *state = signals_.state(spec.id);
      if (state && state->has_data) {
        return state->last_value;
      }
    }
  }
  return 0.0;
}

void DetectionEngine::reset() {
  signals_.reset();
  network_policy_.reset();
  model_access_policy_.reset();
  last_processes_ = {};
  last_inventory_.clear();
  last_destinations_.clear();
  last_model_labels_.clear();
  last_config_labels_.clear();
}

std::vector<Finding> DetectionEngine::process(TelemetryBatch &batch) {
  const uint64_t now = batch.timestamp_ms;

  // 1. Policy enrichment: inventories -> numeric security metrics.
  if (batch.has_process_inventory) {
    last_inventory_ = batch.processes;
    last_processes_ = process_policy_.evaluate(batch.processes, now);
    batch.add("policy", metrics::kUnknownProcesses,
              static_cast<double>(last_processes_.unknown.size()), "count");
    batch.add("policy", metrics::kPrivilegedProcesses,
              static_cast<double>(last_processes_.privileged.size()), "count");
  }
  if (batch.has_connection_inventory) {
    last_destinations_ = network_policy_.evaluate(batch.connections, now);
    batch.add("policy", metrics::kNewDestinations,
              static_cast<double>(last_destinations_.size()), "count");
  }
  model_access_policy_.observe(batch.file_accesses, now);
  const auto unexpected_reads = model_access_policy_.recent_unexpected(now);
  batch.add("policy", metrics::kModelFileAccess,
            static_cast<double>(unexpected_reads.size()), "count");

  if (const MetricSample *s = batch.find(metrics::kModelIntegrity)) {
    last_model_labels_ = s->labels;
  }
  if (const MetricSample *s = batch.find(metrics::kConfigIntegrity)) {
    last_config_labels_ = s->labels;
  }

  // 2. EmbedIDS evaluation.
  signals_.ingest(batch);
  signals_.evaluate(now);

  auto active = [&](const char *id) { return signals_.active(id); };
  const double gpu = value_of(batch, metrics::kGpuUtilization);
  const double fps = value_of(batch, metrics::kInferenceRate);
  const double cpu = value_of(batch, metrics::kCpuUtilization);
  const double memory = value_of(batch, metrics::kMemoryPercent);
  const double bytes_out = value_of(batch, metrics::kBytesOut);
  const double exfil_threshold =
      config_.network.baseline_bytes_out * config_.network.anomaly_multiplier;

  std::vector<Finding> findings;

  // Rule 1 - unexpected GPU workload.
  if (active(sig::kGpuHigh) && active(sig::kFpsBelowBaseline)) {
    Finding f;
    f.rule = rules::kUnexpectedGpuWorkload;
    f.severity = Severity::High;
    f.category = category::kRuntimeAnomaly;
    f.title = "GPU workload outside expected profile";
    std::ostringstream d;
    d << "GPU utilization is " << round1(gpu) << "% (limit "
      << config_.gpu.high_utilization << "%) while inference fell to "
      << round1(fps) << " FPS (expected " << config_.ai.expected_fps
      << ") for more than " << config_.gpu.sustain_seconds << " s.";
    f.description = d.str();
    std::vector<std::string> gpu_processes;
    for (const auto &p : last_inventory_) {
      if (p.uses_gpu && !process_policy_.allowed(p.name)) {
        gpu_processes.push_back(p.name);
      }
    }
    f.evidence = {{"gpu_utilization", round1(gpu)},
                  {"inference_rate", round1(fps)},
                  {"gpu_threshold", config_.gpu.high_utilization},
                  {"expected_fps", config_.ai.expected_fps},
                  {"unexpected_gpu_processes", gpu_processes}};
    f.signals = {metrics::kGpuUtilization, metrics::kInferenceRate};
    findings.push_back(std::move(f));
  }

  // Rule 2 - rogue process (escalates when running as root).
  if (active(sig::kUnknownProcesses)) {
    for (const auto &p : last_processes_.unknown) {
      Finding f;
      f.rule = rules::kRogueProcess;
      f.subject = p.name;
      const bool privileged = p.uid == 0;
      f.severity = privileged ? Severity::High : Severity::Medium;
      f.category = category::kProcess;
      f.title = privileged ? "Unauthorized privileged process"
                           : "Unknown process detected";
      const uint64_t runtime_s =
          now > p.start_time_ms ? (now - p.start_time_ms) / 1000 : 0;
      std::ostringstream d;
      d << "Process '" << p.name << "' (pid " << p.pid << ", uid " << p.uid
        << ") is not in the allowed process list and has been running for "
        << runtime_s << " s.";
      f.description = d.str();
      f.evidence = {{"process", p.name},      {"pid", p.pid},
                    {"uid", p.uid},           {"runtime_seconds", runtime_s},
                    {"uses_gpu", p.uses_gpu}, {"privileged", privileged}};
      f.signals = {metrics::kUnknownProcesses};
      if (privileged) {
        f.signals.push_back(metrics::kPrivilegedProcesses);
      }
      findings.push_back(std::move(f));
    }
  }

  // Rule 3 - suspicious outbound connection.
  const bool escalated = active(sig::kBytesOutEscalation);
  if (active(sig::kNewDestination)) {
    for (const auto &dest : last_destinations_) {
      Finding f;
      f.rule = rules::kUnknownDestination;
      f.subject = dest.destination;
      f.severity = escalated ? Severity::High : Severity::Medium;
      f.category = category::kNetwork;
      f.title = escalated ? "High-volume traffic to unknown destination"
                          : "Connection to unknown destination";
      std::ostringstream d;
      d << "Outbound connection to " << dest.destination;
      if (!dest.process.empty()) {
        d << " by '" << dest.process << "'";
      }
      d << " is not in the allowed destination list";
      if (escalated) {
        d << " and outbound traffic is " << format_rate(bytes_out);
      }
      d << ".";
      f.description = d.str();
      f.evidence = {
          {"destination", dest.destination},
          {"process", dest.process},
          {"first_seen", format_iso8601(dest.first_seen_ms)},
          {"bytes_out_per_s", round1(bytes_out)},
          {"escalation_threshold", config_.network.escalation_bytes_out}};
      f.signals = {metrics::kNewDestinations};
      if (escalated) {
        f.signals.push_back(metrics::kBytesOut);
      }
      findings.push_back(std::move(f));
    }
  }

  // Outbound traffic anomaly (supports Rule 6 and correlation).
  if (active(sig::kBytesOutAnomaly)) {
    Finding f;
    f.rule = rules::kOutboundTrafficAnomaly;
    f.severity = Severity::Medium;
    f.category = category::kNetwork;
    f.title = "Outbound traffic anomaly";
    f.description =
        "Outbound traffic of " + format_rate(bytes_out) + " exceeds " +
        std::to_string(static_cast<int>(config_.network.anomaly_multiplier)) +
        "x the normal baseline of " +
        format_rate(config_.network.baseline_bytes_out) + ".";
    f.evidence = {{"bytes_out_per_s", round1(bytes_out)},
                  {"baseline_bytes_out", config_.network.baseline_bytes_out},
                  {"multiplier", config_.network.anomaly_multiplier}};
    f.signals = {metrics::kBytesOut};
    findings.push_back(std::move(f));
  }

  // Rule 6 - data exfiltration pattern.
  if (active(sig::kNewDestination) && active(sig::kBytesOutExfil)) {
    std::vector<std::string> destinations;
    for (const auto &dest : last_destinations_) {
      destinations.push_back(dest.destination);
    }
    Finding f;
    f.rule = rules::kDataExfiltration;
    f.severity = Severity::High;
    f.category = category::kNetwork;
    f.title = "Possible data exfiltration";
    f.description = "New external destination (" + join(destinations) +
                    ") combined with outbound traffic of " +
                    format_rate(bytes_out) + ".";
    f.evidence = {{"destinations", destinations},
                  {"bytes_out_per_s", round1(bytes_out)},
                  {"threshold_bytes_out", exfil_threshold}};
    f.signals = {metrics::kNewDestinations, metrics::kBytesOut};
    findings.push_back(std::move(f));
  }

  // Model read by an unexpected process.
  if (active(sig::kModelAccess)) {
    std::set<std::string> readers;
    for (const auto &access : unexpected_reads) {
      readers.insert(access.process + " (pid " + std::to_string(access.pid) +
                     ")");
    }
    std::vector<std::string> reader_list(readers.begin(), readers.end());
    Finding f;
    f.rule = rules::kModelFileAccess;
    f.severity = Severity::Medium;
    f.category = category::kIntegrity;
    f.title = "Unexpected model file access";
    f.description = "The deployed model " + config_.integrity.model.path +
                    " was read by " + join(reader_list) + ".";
    f.evidence = {{"path", config_.integrity.model.path},
                  {"readers", reader_list},
                  {"recent_accesses", unexpected_reads.size()}};
    f.signals = {metrics::kModelFileAccess};
    findings.push_back(std::move(f));
  }

  // Rule 4 - model integrity.
  if (active(sig::kModelIntegrityBad)) {
    const std::string expected = last_model_labels_["expected"];
    const std::string current = last_model_labels_["current"];
    Finding f;
    f.rule = rules::kModelIntegrity;
    f.severity = Severity::Critical;
    f.category = category::kIntegrity;
    f.title = "AI model integrity violation";
    f.description = "Model " + last_model_labels_["path"] +
                    " no longer matches its approved hash. EXPECTED: " +
                    short_hash(expected) + " CURRENT: " + short_hash(current);
    f.evidence = {{"path", last_model_labels_["path"]},
                  {"expected_sha256", expected},
                  {"current_sha256", current}};
    f.signals = {metrics::kModelIntegrity};
    findings.push_back(std::move(f));
  }

  // Agent self-protection - configuration integrity.
  if (active(sig::kConfigIntegrityBad)) {
    Finding f;
    f.rule = rules::kConfigIntegrity;
    f.severity = Severity::High;
    f.category = category::kIntegrity;
    f.title = "Configuration integrity violation";
    f.description = "Agent configuration " + last_config_labels_["path"] +
                    " changed after startup.";
    f.evidence = {{"path", last_config_labels_["path"]},
                  {"expected_sha256", last_config_labels_["expected"]},
                  {"current_sha256", last_config_labels_["current"]}};
    f.signals = {metrics::kConfigIntegrity};
    findings.push_back(std::move(f));
  }

  // Rule 5 - inference degradation.
  if (active(sig::kFpsDegraded)) {
    const bool gpu_high = gpu > config_.gpu.high_utilization;
    const bool cpu_high = cpu > config_.system.cpu_exhaustion * 0.85;
    Finding f;
    f.rule = rules::kInferenceDegradation;
    f.severity = Severity::Medium;
    f.category = category::kPerformance;
    f.title = "Inference performance degradation";
    std::ostringstream d;
    d << "Inference throughput is " << round1(fps) << " FPS, below the "
      << config_.ai.minimum_fps << " FPS minimum (baseline "
      << config_.ai.expected_fps << ") for more than "
      << config_.ai.degradation_seconds << " s";
    if (gpu_high || cpu_high) {
      d << " while " << (gpu_high ? "GPU" : "CPU")
        << " utilization is unusually high";
    }
    d << ".";
    f.description = d.str();
    f.evidence = {{"inference_rate", round1(fps)},
                  {"minimum_fps", config_.ai.minimum_fps},
                  {"expected_fps", config_.ai.expected_fps},
                  {"gpu_utilization", round1(gpu)},
                  {"cpu_utilization", round1(cpu)},
                  {"gpu_unusually_high", gpu_high},
                  {"cpu_unusually_high", cpu_high}};
    f.signals = {metrics::kInferenceRate};
    findings.push_back(std::move(f));
  }

  // Scenario E - resource exhaustion.
  if (active(sig::kCpuExhausted) && active(sig::kMemoryExhausted)) {
    Finding f;
    f.rule = rules::kResourceExhaustion;
    f.severity = Severity::High;
    f.category = category::kRuntimeAnomaly;
    f.title = "Resource exhaustion";
    std::ostringstream d;
    d << "CPU at " << round1(cpu) << "% and memory at " << round1(memory)
      << "% for more than " << config_.system.exhaustion_seconds
      << " s; inference at " << round1(fps) << " FPS.";
    f.description = d.str();
    f.evidence = {{"cpu_utilization", round1(cpu)},
                  {"memory_percent", round1(memory)},
                  {"inference_rate", round1(fps)}};
    f.signals = {metrics::kCpuUtilization, metrics::kMemoryPercent};
    findings.push_back(std::move(f));
  }

  // Rule 7 - telemetry heartbeat.
  if (active(sig::kHeartbeatLost)) {
    const MetricSample *hb = batch.find(metrics::kAgentHeartbeat);
    Finding f;
    f.rule = rules::kHeartbeatLost;
    f.severity = Severity::Critical;
    f.category = category::kAvailability;
    f.title = "Telemetry heartbeat lost";
    f.description = "Security telemetry unavailable. Possible collector "
                    "failure or tampering.";
    f.evidence = {{"heartbeat_age_seconds",
                   round1(value_of(batch, metrics::kAgentHeartbeat))},
                  {"timeout_seconds", config_.heartbeat.timeout_seconds},
                  {"stale_collectors", hb && hb->labels.count("stale")
                                           ? hb->labels.at("stale")
                                           : ""}};
    f.signals = {metrics::kAgentHeartbeat};
    findings.push_back(std::move(f));
  }

  return findings;
}

} // namespace edgeml
