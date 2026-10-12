/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <httplib.h>

#include <gtest/gtest.h>

#include "agent/alert_sinks.h"
#include "support/test_support.h"

using namespace edgeml;

namespace {

Alert sample_alert() {
  Alert alert;
  alert.alert_id = "ALT-000007";
  alert.timestamp_ms = 1791324064000ULL;
  alert.device_id = "demo-edge-001";
  alert.severity = Severity::Critical;
  alert.category = category::kIntegrity;
  alert.rule = "model_integrity_violation";
  alert.title = "AI model integrity violation";
  alert.description = "hash mismatch";
  alert.evidence = {{"expected_sha256", "83ff"}, {"current_sha256", "a7bc"}};
  return alert;
}

} // namespace

TEST(AlertSinksTest, JsonLinesFile) {
  test::TempDir dir;
  const std::string path = dir.file("nested/alerts.jsonl");
  {
    JsonFileAlertSink sink(path);
    ASSERT_TRUE(sink.ok());
    Alert alert = sample_alert();
    sink.publish(alert, AlertChange::Raised);
    alert.active = false;
    alert.resolved_ms = alert.timestamp_ms + 5000;
    sink.publish(alert, AlertChange::Resolved);
  }
  std::ifstream in(path);
  std::string line;
  std::vector<nlohmann::json> events;
  while (std::getline(in, line)) {
    events.push_back(nlohmann::json::parse(line));
  }
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0]["event"], "raised");
  EXPECT_EQ(events[0]["alert_id"], "ALT-000007");
  EXPECT_EQ(events[0]["severity"], "critical");
  EXPECT_EQ(events[0]["evidence"]["current_sha256"], "a7bc");
  EXPECT_EQ(events[1]["event"], "resolved");
  EXPECT_EQ(events[1]["status"], "resolved");
}

TEST(AlertSinksTest, RemoteSyslogRfc5424OverUdp) {
  const int receiver = ::socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(receiver, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(
      ::bind(receiver, reinterpret_cast<sockaddr *>(&address), sizeof(address)),
      0);
  socklen_t length = sizeof(address);
  ASSERT_EQ(
      ::getsockname(receiver, reinterpret_cast<sockaddr *>(&address), &length),
      0);
  timeval timeout{2, 0};
  setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  SyslogAlertSink sink(false,
                       "127.0.0.1:" + std::to_string(ntohs(address.sin_port)),
                       "edge-host");
  sink.publish(sample_alert(), AlertChange::Raised);

  char buffer[8192] = {};
  const ssize_t received = ::recv(receiver, buffer, sizeof(buffer) - 1, 0);
  ::close(receiver);
  ASSERT_GT(received, 0);
  const std::string message(buffer, static_cast<size_t>(received));
  // <PRI> = facility daemon (3) * 8 + severity crit (2) = 26
  EXPECT_EQ(message.rfind("<26>1 ", 0), 0u) << message;
  EXPECT_NE(message.find(" edge-host embedids-agent "), std::string::npos);
  EXPECT_NE(message.find(" model_integrity_violation - {"), std::string::npos);
  const auto json = nlohmann::json::parse(message.substr(message.find('{')));
  EXPECT_EQ(json["alert_id"], "ALT-000007");
}

TEST(AlertSinksTest, WebhookPostsAsynchronously) {
  httplib::Server server;
  std::mutex mutex;
  std::vector<nlohmann::json> received;
  server.Post("/hooks/embedids",
              [&](const httplib::Request &req, httplib::Response &res) {
                std::lock_guard<std::mutex> lock(mutex);
                received.push_back(nlohmann::json::parse(req.body));
                res.status = 204;
              });
  const int port = server.bind_to_any_port("127.0.0.1");
  ASSERT_GT(port, 0);
  std::thread thread([&] { server.listen_after_bind(); });
  server.wait_until_ready();

  {
    WebhookAlertSink sink("http://127.0.0.1:" + std::to_string(port) +
                          "/hooks/embedids");
    sink.publish(sample_alert(), AlertChange::Raised);
    sink.publish(sample_alert(), AlertChange::Escalated);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (received.size() == 2) {
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  server.stop();
  thread.join();

  ASSERT_EQ(received.size(), 2u);
  EXPECT_EQ(received[0]["event"], "raised");
  EXPECT_EQ(received[1]["event"], "escalated");
  EXPECT_EQ(received[0]["rule"], "model_integrity_violation");
}
