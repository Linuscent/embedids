/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure parsers for /proc and /sys text formats. Kept free of I/O so they can
 * be unit tested with captured fixtures from real devices.
 */

#ifndef EDGEML_COLLECTORS_LINUX_PROC_PARSERS_H
#define EDGEML_COLLECTORS_LINUX_PROC_PARSERS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace edgeml::procfs {

struct CpuTimes {
  uint64_t total = 0;
  uint64_t idle = 0; /**< idle + iowait */
};

/** Aggregate "cpu" line of /proc/stat. */
std::optional<CpuTimes> parse_cpu_times(const std::string &proc_stat);
/** Boot time ("btime") from /proc/stat, seconds since epoch. */
std::optional<uint64_t> parse_boot_time(const std::string &proc_stat);
/** Utilization percent between two samples. */
double cpu_utilization(const CpuTimes &previous, const CpuTimes &current);

struct MemInfo {
  uint64_t total_kb = 0;
  uint64_t available_kb = 0;
};
std::optional<MemInfo> parse_meminfo(const std::string &meminfo);

std::optional<double> parse_loadavg(const std::string &loadavg);

/** Real UID from /proc/<pid>/status. */
std::optional<uint32_t> parse_status_uid(const std::string &status);

struct ProcStat {
  std::string comm;
  char state = '?';
  uint64_t start_ticks = 0; /**< field 22, clock ticks after boot */
  int ppid = 0;
};
/** /proc/<pid>/stat; handles spaces and parentheses inside comm. */
std::optional<ProcStat> parse_pid_stat(const std::string &stat);

struct TcpEntry {
  std::string local;  /**< ip:port */
  std::string remote; /**< ip:port */
  int local_port = 0;
  int state =
      0; /**< kernel TCP state (1 = ESTABLISHED, 2 = SYN_SENT, 10 = LISTEN) */
  uint32_t uid = 0;
  uint64_t inode = 0;
};
inline constexpr int kTcpEstablished = 1;
inline constexpr int kTcpSynSent = 2;
inline constexpr int kTcpListen = 10;

/** /proc/net/tcp or /proc/net/tcp6. */
std::vector<TcpEntry> parse_proc_net_tcp(const std::string &content, bool ipv6);

struct InterfaceCounters {
  uint64_t rx_bytes = 0;
  uint64_t tx_bytes = 0;
};
/** Sum of /proc/net/dev counters, excluding loopback (or one interface). */
InterfaceCounters parse_proc_net_dev(const std::string &content,
                                     const std::string &only_interface = "");

bool is_loopback_address(const std::string &address_with_port);

} // namespace edgeml::procfs

#endif
