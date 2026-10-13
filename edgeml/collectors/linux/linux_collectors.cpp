/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "collectors/linux/linux_collectors.h"

#include <cctype>
#include <set>

#include <dirent.h>
#include <unistd.h>

#include "collectors/common/file_util.h"
#include "core/common/clock.h"

namespace edgeml {

bool ProcCpuCollector::initialize() {
  auto content = read_text_file(proc_root_ + "/stat");
  if (!content) {
    return false;
  }
  auto times = procfs::parse_cpu_times(*content);
  if (!times) {
    return false;
  }
  previous_ = *times;
  return true;
}

bool ProcCpuCollector::collect(MetricBatch &batch) {
  auto content = read_text_file(proc_root_ + "/stat");
  if (!content) {
    return false;
  }
  auto times = procfs::parse_cpu_times(*content);
  if (!times) {
    return false;
  }
  batch.add("system", metrics::kCpuUtilization,
            procfs::cpu_utilization(previous_, *times), "percent");
  previous_ = *times;

  if (auto load = read_text_file(proc_root_ + "/loadavg")) {
    if (auto value = procfs::parse_loadavg(*load)) {
      batch.add("system", metrics::kLoadAverage, *value, "load");
    }
  }
  return true;
}

bool ProcMemoryCollector::initialize() {
  return read_text_file(proc_root_ + "/meminfo").has_value();
}

bool ProcMemoryCollector::collect(MetricBatch &batch) {
  auto content = read_text_file(proc_root_ + "/meminfo");
  if (!content) {
    return false;
  }
  auto info = procfs::parse_meminfo(*content);
  if (!info) {
    return false;
  }
  const double used_kb = static_cast<double>(
      info->total_kb - std::min(info->available_kb, info->total_kb));
  batch.add("system", metrics::kMemoryUsed, used_kb / 1024.0, "MB");
  batch.add("system", metrics::kMemoryPercent,
            100.0 * used_kb / static_cast<double>(info->total_kb), "percent");
  return true;
}

bool ProcProcessCollector::initialize() {
  ticks_per_second_ = sysconf(_SC_CLK_TCK);
  if (ticks_per_second_ <= 0) {
    ticks_per_second_ = 100;
  }
  auto stat = read_text_file(proc_root_ + "/stat");
  if (!stat) {
    return false;
  }
  if (auto btime = procfs::parse_boot_time(*stat)) {
    boot_time_ms_ = *btime * 1000ULL;
  }
  return true;
}

bool ProcProcessCollector::collect(MetricBatch &batch) {
  DIR *dir = opendir(proc_root_.c_str());
  if (dir == nullptr) {
    return false;
  }
  size_t total = 0;
  std::vector<ProcessInfo> processes;
  while (dirent *entry = readdir(dir)) {
    const char *name = entry->d_name;
    if (!std::isdigit(static_cast<unsigned char>(name[0]))) {
      continue;
    }
    const std::string base = proc_root_ + "/" + name;
    auto stat_text = read_text_file(base + "/stat");
    if (!stat_text) {
      continue; // process exited
    }
    auto stat = procfs::parse_pid_stat(*stat_text);
    if (!stat) {
      continue;
    }
    total++;
    const int pid = std::atoi(name);
    const bool kernel_thread = pid == 2 || stat->ppid == 2;
    if (kernel_thread && ignore_kernel_threads_) {
      continue;
    }
    ProcessInfo process;
    process.pid = pid;
    process.name = stat->comm;
    if (auto status = read_text_file(base + "/status")) {
      process.uid = procfs::parse_status_uid(*status).value_or(0);
    }
    process.start_time_ms =
        boot_time_ms_ +
        stat->start_ticks * 1000ULL / static_cast<uint64_t>(ticks_per_second_);
    processes.push_back(std::move(process));
  }
  closedir(dir);

  batch.has_process_inventory = true;
  batch.processes = std::move(processes);
  batch.add("system", metrics::kProcessCount, static_cast<double>(total),
            "count");
  return true;
}

bool LinuxNetworkCollector::initialize() {
  auto dev = read_text_file(proc_root_ + "/net/dev");
  if (!dev) {
    return false;
  }
  previous_ = procfs::parse_proc_net_dev(*dev, interface_);
  previous_ms_ = monotonic_ms();
  return true;
}

bool LinuxNetworkCollector::collect(MetricBatch &batch) {
  std::vector<procfs::TcpEntry> entries;
  for (const auto &[file, ipv6] :
       {std::pair<const char *, bool>{"/net/tcp", false},
        {"/net/tcp6", true}}) {
    if (auto content = read_text_file(proc_root_ + file)) {
      auto parsed = procfs::parse_proc_net_tcp(*content, ipv6);
      entries.insert(entries.end(), parsed.begin(), parsed.end());
    }
  }

  std::set<int> listening;
  for (const auto &entry : entries) {
    if (entry.state == procfs::kTcpListen) {
      listening.insert(entry.local_port);
    }
  }

  std::vector<ConnectionInfo> connections;
  size_t established = 0;
  size_t pending = 0;
  for (const auto &entry : entries) {
    if (procfs::is_loopback_address(entry.remote)) {
      continue;
    }
    if (entry.state == procfs::kTcpSynSent) {
      pending++;
    }
    if (entry.state != procfs::kTcpEstablished) {
      continue;
    }
    established++;
    ConnectionInfo connection;
    connection.destination = entry.remote;
    connection.outbound = listening.count(entry.local_port) == 0;
    connections.push_back(std::move(connection));
  }

  size_t outbound = 0;
  for (const auto &connection : connections) {
    outbound += connection.outbound ? 1 : 0;
  }

  batch.has_connection_inventory = true;
  batch.connections = std::move(connections);
  batch.add("network", metrics::kConnectionsTotal,
            static_cast<double>(established), "count");
  batch.add("network", metrics::kConnectionsOutbound,
            static_cast<double>(outbound), "count");
  batch.add("network", metrics::kFailedConnections,
            static_cast<double>(pending), "count");

  if (auto dev = read_text_file(proc_root_ + "/net/dev")) {
    const auto counters = procfs::parse_proc_net_dev(*dev, interface_);
    const uint64_t now = monotonic_ms();
    const double seconds = static_cast<double>(now - previous_ms_) / 1000.0;
    if (seconds > 0 && counters.tx_bytes >= previous_.tx_bytes &&
        counters.rx_bytes >= previous_.rx_bytes) {
      batch.add("network", metrics::kBytesOut,
                static_cast<double>(counters.tx_bytes - previous_.tx_bytes) /
                    seconds,
                "bytes/s");
      batch.add("network", metrics::kBytesIn,
                static_cast<double>(counters.rx_bytes - previous_.rx_bytes) /
                    seconds,
                "bytes/s");
    }
    previous_ = counters;
    previous_ms_ = now;
  }
  return true;
}

bool LinuxThermalCollector::initialize() {
  return read_text_file(sys_root_ + "/class/thermal/thermal_zone0/temp")
      .has_value();
}

bool LinuxThermalCollector::collect(MetricBatch &batch) {
  double hottest = -1000.0;
  for (int zone = 0; zone < 32; zone++) {
    auto text = read_text_file(sys_root_ + "/class/thermal/thermal_zone" +
                               std::to_string(zone) + "/temp");
    if (!text) {
      break;
    }
    try {
      hottest = std::max(hottest, std::stod(*text) / 1000.0);
    } catch (const std::exception &) {
    }
  }
  if (hottest < -273.0) {
    return false;
  }
  batch.add("system", metrics::kTemperature, hottest, "celsius");
  return true;
}

} // namespace edgeml
