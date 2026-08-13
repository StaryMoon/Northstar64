#pragma once

#include "northstar64/cpu.hpp"
#include "northstar64/elf.hpp"
#include "northstar64/memory.hpp"
#include "northstar64/uart.hpp"

#include <cstdint>
#include <filesystem>
#include <ostream>

namespace northstar64 {

struct MachineConfig {
  Address ram_base{0x80000000ULL};
  std::uint64_t ram_size{128ULL * 1024ULL * 1024ULL};
  Address uart_base{Uart16550::kDefaultBase};
  CpuConfig cpu{};
};

class Machine {
public:
  explicit Machine(MachineConfig config = {}, std::ostream* uart_output = nullptr);

  ElfImage load(const std::filesystem::path& path);
  [[nodiscard]] RunResult run(std::uint64_t maximum_steps, TraceSink* trace = nullptr);

  [[nodiscard]] Cpu& cpu() noexcept { return cpu_; }
  [[nodiscard]] const Cpu& cpu() const noexcept { return cpu_; }
  [[nodiscard]] Bus& bus() noexcept { return bus_; }
  [[nodiscard]] SparseRam& ram() noexcept { return *ram_; }
  [[nodiscard]] Uart16550& uart() noexcept { return *uart_; }

private:
  Bus bus_;
  SparseRam* ram_;
  Uart16550* uart_;
  Cpu cpu_;
};

} // namespace northstar64

