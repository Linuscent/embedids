/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent/alert_sinks.h"

#include <cctype>
#include <cstdio>
#include <iostream>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <httplib.h>

#include "core/common/clock.h"

namespace edgeml {

namespace {

const char *ansi_color(Severity severity) {
  switch (severity) {
  case Severity::Critical:
    return "\033[1;35m";
  case Severity::High:
    return "\033[1;31m";
  case Severity::Medium:
    return "\033[1;33m";
  default:
    return "\033[1;36m";
  }
}

int syslog_priority(Severity severity) {
  switch (severity) {
  case Severity::Critical:
    return LOG_CRIT;
  case Severity::High:
    return LOG_ERR;
  case Severity::Medium:
    return LOG_WARNING;
  case Severity::Low:
    return LOG_NOTICE;
  default:
    return LOG_INFO;
  }
}

std::string upper(const char *text) {
  std::string out(text);
  for (auto &c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

void make_parent_dirs(const std::string &path) {
  for (size_t pos = path.find('/', 1); pos != std::string::npos;
       pos = path.find('/', pos + 1)) {
    mkdir(path.substr(0, pos).c_str(), 0750);
  }
}

} // namespace

nlohmann::json alert_event_json(const Alert &alert, AlertChange change) {
  nlohmann::json j = alert;
  j["event"] = to_string(change);
  return j;
}

void ConsoleAlertSink::publish(const Alert &alert, AlertChange change) {
  const std::string severity = upper(to_string(alert.severity));
  const char *reset = color_ ? "\033[0m" : "";
  const char *color = color_ ? ansi_color(alert.severity) : "";
  std::ostream &out = std::cout;
  if (change == AlertChange::Resolved) {
    out << format_iso8601(alert.resolved_ms) << " RESOLVED " << alert.alert_id
        << " " << alert.title;
    if (!alert.subject.empty()) {
      out << " (" << alert.subject << ")";
    }
    out << "\n";
  } else {
    out << format_iso8601(change == AlertChange::Escalated ? alert.updated_ms
                                                           : alert.timestamp_ms)
        << " " << color << "[" << severity << "]" << reset << " "
        << (change == AlertChange::Escalated ? "ESCALATED " : "")
        << alert.alert_id << " " << alert.title << " - " << alert.description
        << "\n";
  }
  out.flush();
}

JsonFileAlertSink::JsonFileAlertSink(std::string path)
    : path_(std::move(path)) {
  make_parent_dirs(path_);
  out_.open(path_, std::ios::app);
}

void JsonFileAlertSink::publish(const Alert &alert, AlertChange change) {
  if (!out_) {
    return;
  }
  out_ << alert_event_json(alert, change).dump() << "\n";
  out_.flush();
}

SyslogAlertSink::SyslogAlertSink(bool local, const std::string &remote,
                                 std::string hostname)
    : local_(local), hostname_(std::move(hostname)) {
  if (local_) {
    openlog("embedids-agent", LOG_PID | LOG_NDELAY, LOG_DAEMON);
  }
  const size_t colon = remote.rfind(':');
  if (remote.empty() || colon == std::string::npos) {
    return;
  }
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *result = nullptr;
  if (getaddrinfo(remote.substr(0, colon).c_str(),
                  remote.substr(colon + 1).c_str(), &hints, &result) != 0) {
    std::cerr << "syslog: cannot resolve " << remote << "\n";
    return;
  }
  socket_ =
      ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (socket_ >= 0 &&
      ::connect(socket_, result->ai_addr, result->ai_addrlen) != 0) {
    ::close(socket_);
    socket_ = -1;
  }
  freeaddrinfo(result);
}

SyslogAlertSink::~SyslogAlertSink() {
  if (socket_ >= 0) {
    ::close(socket_);
  }
  if (local_) {
    closelog();
  }
}

void SyslogAlertSink::publish(const Alert &alert, AlertChange change) {
  const std::string message = alert_event_json(alert, change).dump();
  const int priority = syslog_priority(alert.severity);
  if (local_) {
    syslog(priority, "%s", message.c_str());
  }
  if (socket_ >= 0) {
    // RFC 5424: <PRI>1 TIMESTAMP HOST APP PROCID MSGID SD MSG
    const int pri = LOG_DAEMON | priority;
    std::string line = "<" + std::to_string(pri) + ">1 " +
                       format_iso8601(SystemClock().now_ms()) + " " +
                       hostname_ + " embedids-agent " +
                       std::to_string(::getpid()) + " " + alert.rule + " - " +
                       message;
    ::send(socket_, line.data(), line.size(), 0);
  }
}

WebhookAlertSink::WebhookAlertSink(std::string url) {
  // Split "http://host:port/path" into base and path.
  const size_t scheme = url.find("://");
  const size_t path_start =
      url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  base_ = path_start == std::string::npos ? url : url.substr(0, path_start);
  path_ = path_start == std::string::npos ? "/" : url.substr(path_start);
  thread_ = std::thread(&WebhookAlertSink::worker, this);
}

WebhookAlertSink::~WebhookAlertSink() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void WebhookAlertSink::publish(const Alert &alert, AlertChange change) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 1000) {
      queue_.pop_front();
    }
    queue_.push_back(alert_event_json(alert, change).dump());
  }
  cv_.notify_one();
}

void WebhookAlertSink::worker() {
  httplib::Client client(base_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(3, 0);
  for (;;) {
    std::string body;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;
      }
      body = std::move(queue_.front());
      queue_.pop_front();
    }
    auto response = client.Post(path_, body, "application/json");
    if (!response || response->status >= 300) {
      std::cerr << "webhook: delivery to " << base_ << path_ << " failed\n";
    }
  }
}

} // namespace edgeml
