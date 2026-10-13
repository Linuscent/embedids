/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Alert export: console, JSON lines, syslog (local and RFC 5424/UDP) and
 * HTTP webhook.
 */

#ifndef EDGEML_AGENT_ALERT_SINKS_H
#define EDGEML_AGENT_ALERT_SINKS_H

#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "agent/alert_manager.h"

namespace edgeml {

/** JSON line written by file, syslog and webhook sinks. */
nlohmann::json alert_event_json(const Alert &alert, AlertChange change);

class ConsoleAlertSink : public AlertSink {
public:
  explicit ConsoleAlertSink(bool color = true) : color_(color) {}
  void publish(const Alert &alert, AlertChange change) override;
  const char *name() const override { return "console"; }

private:
  bool color_;
};

class JsonFileAlertSink : public AlertSink {
public:
  explicit JsonFileAlertSink(std::string path);
  bool ok() const { return static_cast<bool>(out_); }
  void publish(const Alert &alert, AlertChange change) override;
  const char *name() const override { return "json_file"; }

private:
  std::string path_;
  std::ofstream out_;
};

class SyslogAlertSink : public AlertSink {
public:
  /** remote: optional "host:port" for RFC 5424 over UDP. */
  SyslogAlertSink(bool local, const std::string &remote, std::string hostname);
  ~SyslogAlertSink() override;
  void publish(const Alert &alert, AlertChange change) override;
  const char *name() const override { return "syslog"; }

private:
  bool local_;
  int socket_ = -1;
  std::string hostname_;
};

/** Posts alert events asynchronously so detection never blocks on I/O. */
class WebhookAlertSink : public AlertSink {
public:
  explicit WebhookAlertSink(std::string url);
  ~WebhookAlertSink() override;
  void publish(const Alert &alert, AlertChange change) override;
  const char *name() const override { return "webhook"; }

private:
  void worker();

  std::string base_;
  std::string path_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  bool stopping_ = false;
  std::thread thread_;
};

} // namespace edgeml

#endif
