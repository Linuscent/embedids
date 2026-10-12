/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fixtures are trimmed captures from Ubuntu 22.04 and Jetson L4T 36.
 */

#include <gtest/gtest.h>

#include "collectors/linux/proc_parsers.h"

using namespace edgeml::procfs;

TEST(ProcParsersTest, CpuTimesAndUtilization) {
  const std::string first =
      "cpu  100 0 100 700 100 0 0 0 0 0\ncpu0 50 0 50 350 50 0 0 0 0 0\n"
      "btime 1790000000\n";
  const std::string second = "cpu  200 0 200 800 100 0 0 0 0 0\n";
  auto a = parse_cpu_times(first);
  auto b = parse_cpu_times(second);
  ASSERT_TRUE(a && b);
  EXPECT_EQ(a->total, 1000u);
  EXPECT_EQ(a->idle, 800u);
  EXPECT_NEAR(cpu_utilization(*a, *b), 66.67, 0.01);
  EXPECT_EQ(cpu_utilization(*b, *a), 0.0);
  EXPECT_EQ(parse_boot_time(first).value(), 1790000000u);
  EXPECT_FALSE(parse_cpu_times("intr 1 2 3").has_value());
}

TEST(ProcParsersTest, Meminfo) {
  auto info = parse_meminfo("MemTotal:        7802964 kB\n"
                            "MemFree:          512000 kB\n"
                            "MemAvailable:    4681778 kB\n"
                            "Buffers:           10000 kB\n");
  ASSERT_TRUE(info);
  EXPECT_EQ(info->total_kb, 7802964u);
  EXPECT_EQ(info->available_kb, 4681778u);

  auto legacy = parse_meminfo("MemTotal: 1000 kB\nMemFree: 100 kB\n"
                              "Buffers: 50 kB\nCached: 250 kB\n");
  ASSERT_TRUE(legacy);
  EXPECT_EQ(legacy->available_kb, 400u);
}

TEST(ProcParsersTest, LoadavgAndStatusUid) {
  EXPECT_DOUBLE_EQ(parse_loadavg("1.52 1.20 0.90 2/512 4242\n").value(), 1.52);
  EXPECT_EQ(
      parse_status_uid("Name:\tbash\nUid:\t1000\t1000\t1000\t1000\n").value(),
      1000u);
  EXPECT_FALSE(parse_status_uid("Name:\tbash\n").has_value());
}

TEST(ProcParsersTest, PidStatHandlesHostileCommNames) {
  const std::string stat =
      "4242 (evil) (x y)) S 1 4242 4242 0 -1 4194560 100 0 0 0 5 3 0 0 20 0 "
      "1 0 123456 1000000 200 18446744073709551615";
  auto parsed = parse_pid_stat(stat);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->comm, "evil) (x y)");
  EXPECT_EQ(parsed->state, 'S');
  EXPECT_EQ(parsed->ppid, 1);
  EXPECT_EQ(parsed->start_ticks, 123456u);
  EXPECT_FALSE(parse_pid_stat("garbage").has_value());
}

TEST(ProcParsersTest, NetTcpIpv4) {
  const std::string content =
      "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when "
      "retrnsmt   uid  timeout inode\n"
      "   0: 00000000:1F91 00000000:0000 0A 00000000:00000000 00:00000000 "
      "00000000   999        0 1111 1 0000000000000000 100 0 0 10 0\n"
      "   1: 0500000A:D2F0 1400000A:075B 01 00000000:00000000 02:000A7D1A "
      "00000000  1000        0 2222 2 0000000000000000 20 4 30 10 -1\n"
      "   2: 0100007F:A000 0100007F:1F91 01 00000000:00000000 00:00000000 "
      "00000000     0        0 3333 1 0000000000000000 20 4 30 10 -1\n";
  auto entries = parse_proc_net_tcp(content, false);
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].state, kTcpListen);
  EXPECT_EQ(entries[0].local_port, 8081);
  EXPECT_EQ(entries[1].local, "10.0.0.5:54000");
  EXPECT_EQ(entries[1].remote, "10.0.0.20:1883");
  EXPECT_EQ(entries[1].state, kTcpEstablished);
  EXPECT_EQ(entries[1].uid, 1000u);
  EXPECT_TRUE(is_loopback_address(entries[2].remote));
  EXPECT_FALSE(is_loopback_address(entries[1].remote));
}

TEST(ProcParsersTest, NetTcpIpv6) {
  const std::string content =
      "  sl  local_address                         remote_address              "
      "          st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout "
      "inode\n"
      "   0: 0000000000000000FFFF00000500000A:D2F0 "
      "0000000000000000FFFF00002D7100CB:20FB 01 00000000:00000000 00:00000000 "
      "00000000  1000        0 4444 1 0000000000000000 20 4 30 10 -1\n"
      "   1: 00000000000000000000000001000000:1F91 "
      "00000000000000000000000001000000:B000 01 00000000:00000000 00:00000000 "
      "00000000     0        0 5555 1 0000000000000000 20 4 30 10 -1\n";
  auto entries = parse_proc_net_tcp(content, true);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].remote, "203.0.113.45:8443");
  EXPECT_EQ(entries[1].remote, "[0:0:0:0:0:0:0:1]:45056");
  EXPECT_TRUE(is_loopback_address(entries[1].remote));
}

TEST(ProcParsersTest, NetDev) {
  const std::string content =
      "Inter-|   Receive                                                |  "
      "Transmit\n"
      " face |bytes    packets errs drop fifo frame compressed multicast|bytes "
      "   packets errs drop fifo colls carrier compressed\n"
      "    lo: 5000 50 0 0 0 0 0 0 5000 50 0 0 0 0 0 0\n"
      "  eth0: 1000 10 0 0 0 0 0 0 2000 20 0 0 0 0 0 0\n"
      " wlan0: 300 3 0 0 0 0 0 0 400 4 0 0 0 0 0 0\n";
  auto all = parse_proc_net_dev(content);
  EXPECT_EQ(all.rx_bytes, 1300u);
  EXPECT_EQ(all.tx_bytes, 2400u);
  auto eth0 = parse_proc_net_dev(content, "eth0");
  EXPECT_EQ(eth0.rx_bytes, 1000u);
  EXPECT_EQ(eth0.tx_bytes, 2000u);
}
