/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Generic Linux collectors built on /proc and /sys (hardware porting
 * phase 1). They run unchanged on Jetson's L4T userspace.
 */

#ifndef EDGEML_COLLECTORS_LINUX_LINUX_COLLECTORS_H
#define EDGEML_COLLECTORS_LINUX_LINUX_COLLECTORS_H

#include <cstdint>
#include <string>

#include "collectors/common/collector.h"
#include "collectors/linux/proc_parsers.h"

namespace edgeml {

class ProcCpuCollector : public MetricCollector {
public:
  explicit ProcCpuCollector(std::string proc_root = "/proc")
      : proc_root_(std::move(proc_root)) {}
  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "proc-cpu"; }

private:
  std::string proc_root_;
  procfs::CpuTimes previous_;
};

class ProcMemoryCollector : public MetricCollector {
public:
  explicit ProcMemoryCollector(std::string proc_root = "/proc")
      : proc_root_(std::move(proc_root)) {}
  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "proc-memory"; }

private:
  std::string proc_root_;
};

class ProcProcessCollector : public MetricCollector {
public:
  ProcProcessCollector(std::string proc_root = "/proc",
                       bool ignore_kernel_threads = true)
      : proc_root_(std::move(proc_root)),
        ignore_kernel_threads_(ignore_kernel_threads) {}
  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "proc-process"; }

private:
  std::string proc_root_;
  bool ignore_kernel_threads_;
  uint64_t boot_time_ms_ = 0;
  long ticks_per_second_ = 100;
};

class LinuxNetworkCollector : public MetricCollector {
public:
  LinuxNetworkCollector(std::string proc_root = "/proc",
                        std::string interface = "")
      : proc_root_(std::move(proc_root)), interface_(std::move(interface)) {}
  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "linux-network"; }

private:
  std::string proc_root_;
  std::string interface_;
  procfs::InterfaceCounters previous_;
  uint64_t previous_ms_ = 0;
};

class LinuxThermalCollector : public MetricCollector {
public:
  explicit LinuxThermalCollector(std::string sys_root = "/sys")
      : sys_root_(std::move(sys_root)) {}
  bool initialize() override;
  bool collect(MetricBatch &batch) override;
  const char *name() const override { return "linux-thermal"; }
  bool heartbeat_required() const override { return false; }

private:
  std::string sys_root_;
};

} // namespace edgeml

#endif
