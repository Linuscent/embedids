/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/linux/proc_parsers.h"

#include <cstdio>
#include <sstream>

namespace edgeml::procfs {

namespace {

std::string field_line(const std::string &content, const std::string &key) {
  std::istringstream in(content);
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, key.size(), key) == 0) {
      return line;
    }
  }
  return {};
}

uint32_t hex_u32(const std::string &hex) {
  return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
}

std::string decode_ipv4(const std::string &hex) {
  const uint32_t raw = hex_u32(hex); // little-endian in /proc
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", raw & 0xff,
                (raw >> 8) & 0xff, (raw >> 16) & 0xff, (raw >> 24) & 0xff);
  return buffer;
}

std::string decode_ipv6(const std::string &hex) {
  if (hex.size() != 32) {
    return hex;
  }
  uint8_t bytes[16];
  for (int word = 0; word < 4; word++) {
    const uint32_t raw = hex_u32(hex.substr(word * 8, 8));
    for (int b = 0; b < 4; b++) {
      bytes[word * 4 + b] = static_cast<uint8_t>((raw >> (8 * b)) & 0xff);
    }
  }
  bool mapped = bytes[10] == 0xff && bytes[11] == 0xff;
  for (int i = 0; i < 10 && mapped; i++) {
    mapped = bytes[i] == 0;
  }
  if (mapped) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", bytes[12], bytes[13],
                  bytes[14], bytes[15]);
    return buffer;
  }
  std::string out = "[";
  for (int group = 0; group < 8; group++) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%x",
                  (bytes[group * 2] << 8) | bytes[group * 2 + 1]);
    if (group) {
      out += ':';
    }
    out += buffer;
  }
  return out + "]";
}

} // namespace

std::optional<CpuTimes> parse_cpu_times(const std::string &proc_stat) {
  std::istringstream in(proc_stat);
  std::string label;
  if (!(in >> label) || label != "cpu") {
    return std::nullopt;
  }
  CpuTimes times;
  uint64_t value;
  for (int index = 0; index < 10 && in >> value; index++) {
    // guest/guest_nice (8, 9) are already included in user/nice.
    if (index < 8) {
      times.total += value;
    }
    if (index == 3 || index == 4) {
      times.idle += value;
    }
  }
  if (times.total == 0) {
    return std::nullopt;
  }
  return times;
}

std::optional<uint64_t> parse_boot_time(const std::string &proc_stat) {
  const std::string line = field_line(proc_stat, "btime ");
  if (line.empty()) {
    return std::nullopt;
  }
  return std::stoull(line.substr(6));
}

double cpu_utilization(const CpuTimes &previous, const CpuTimes &current) {
  if (current.total <= previous.total) {
    return 0.0;
  }
  const double total = static_cast<double>(current.total - previous.total);
  const double idle = static_cast<double>(current.idle - previous.idle);
  double busy = 100.0 * (total - idle) / total;
  return busy < 0 ? 0 : (busy > 100 ? 100 : busy);
}

std::optional<MemInfo> parse_meminfo(const std::string &meminfo) {
  MemInfo info;
  std::istringstream in(meminfo);
  std::string key;
  uint64_t value;
  std::string unit;
  bool have_available = false;
  uint64_t free_kb = 0, buffers_kb = 0, cached_kb = 0;
  while (in >> key >> value) {
    std::getline(in, unit);
    if (key == "MemTotal:") {
      info.total_kb = value;
    } else if (key == "MemAvailable:") {
      info.available_kb = value;
      have_available = true;
    } else if (key == "MemFree:") {
      free_kb = value;
    } else if (key == "Buffers:") {
      buffers_kb = value;
    } else if (key == "Cached:") {
      cached_kb = value;
    }
  }
  if (info.total_kb == 0) {
    return std::nullopt;
  }
  if (!have_available) {
    info.available_kb = free_kb + buffers_kb + cached_kb;
  }
  return info;
}

std::optional<double> parse_loadavg(const std::string &loadavg) {
  std::istringstream in(loadavg);
  double one_minute;
  if (in >> one_minute) {
    return one_minute;
  }
  return std::nullopt;
}

std::optional<uint32_t> parse_status_uid(const std::string &status) {
  const std::string line = field_line(status, "Uid:");
  if (line.empty()) {
    return std::nullopt;
  }
  std::istringstream in(line.substr(4));
  uint32_t real_uid;
  if (in >> real_uid) {
    return real_uid;
  }
  return std::nullopt;
}

std::optional<ProcStat> parse_pid_stat(const std::string &stat) {
  const size_t open = stat.find('(');
  const size_t close = stat.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open ||
      close + 2 > stat.size()) {
    return std::nullopt;
  }
  ProcStat result;
  result.comm = stat.substr(open + 1, close - open - 1);
  std::istringstream in(stat.substr(close + 2));
  std::string field;
  // Field numbering per proc(5): state is field 3.
  for (int index = 3; in >> field; index++) {
    if (index == 3) {
      result.state = field.empty() ? '?' : field[0];
    } else if (index == 4) {
      result.ppid = std::stoi(field);
    } else if (index == 22) {
      result.start_ticks = std::stoull(field);
      return result;
    }
  }
  return std::nullopt;
}

std::vector<TcpEntry> parse_proc_net_tcp(const std::string &content,
                                         bool ipv6) {
  std::vector<TcpEntry> entries;
  std::istringstream in(content);
  std::string line;
  std::getline(in, line); // header
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string slot, local, remote, state, queues, timer, retransmits;
    uint32_t uid = 0;
    std::string timeout;
    uint64_t inode = 0;
    if (!(fields >> slot >> local >> remote >> state >> queues >> timer >>
          retransmits >> uid >> timeout >> inode)) {
      continue;
    }
    const size_t lc = local.find(':');
    const size_t rc = remote.find(':');
    if (lc == std::string::npos || rc == std::string::npos) {
      continue;
    }
    try {
      TcpEntry entry;
      const std::string local_ip = local.substr(0, lc);
      const std::string remote_ip = remote.substr(0, rc);
      entry.local_port = static_cast<int>(hex_u32(local.substr(lc + 1)));
      const int remote_port = static_cast<int>(hex_u32(remote.substr(rc + 1)));
      entry.local = (ipv6 ? decode_ipv6(local_ip) : decode_ipv4(local_ip)) +
                    ":" + std::to_string(entry.local_port);
      entry.remote = (ipv6 ? decode_ipv6(remote_ip) : decode_ipv4(remote_ip)) +
                     ":" + std::to_string(remote_port);
      entry.state = static_cast<int>(hex_u32(state));
      entry.uid = uid;
      entry.inode = inode;
      entries.push_back(std::move(entry));
    } catch (const std::exception &) {
      continue;
    }
  }
  return entries;
}

InterfaceCounters parse_proc_net_dev(const std::string &content,
                                     const std::string &only_interface) {
  InterfaceCounters totals;
  std::istringstream in(content);
  std::string line;
  while (std::getline(in, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string name = line.substr(0, colon);
    name.erase(0, name.find_first_not_of(' '));
    if (only_interface.empty() ? name == "lo" : name != only_interface) {
      continue;
    }
    std::istringstream fields(line.substr(colon + 1));
    uint64_t values[16] = {};
    int count = 0;
    while (count < 16 && fields >> values[count]) {
      count++;
    }
    if (count >= 9) {
      totals.rx_bytes += values[0];
      totals.tx_bytes += values[8];
    }
  }
  return totals;
}

bool is_loopback_address(const std::string &address_with_port) {
  static const std::string v6_loopback = "[0:0:0:0:0:0:0:1]";
  return address_with_port.compare(0, 4, "127.") == 0 ||
         address_with_port.compare(0, v6_loopback.size(), v6_loopback) == 0;
}

} // namespace edgeml::procfs
