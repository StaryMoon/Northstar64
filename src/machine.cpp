#include "northstar64/machine.hpp"

namespace northstar64 {

Machine::Machine(MachineConfig config, std::ostream* uart_output)
    : ram_(&bus_.emplace_device<SparseRam>(config.ram_base, config.ram_size)),
      uart_(&bus_.emplace_device<Uart16550>(config.uart_base, uart_output)), cpu_(bus_, config.cpu) {}

ElfImage Machine::load(const std::filesystem::path& path) {
  ram_->clear();
  uart_->clear_transmitted();
  auto image = load_elf(path, bus_);
  cpu_.reset(image.entry);
  return image;
}

RunResult Machine::run(std::uint64_t maximum_steps, TraceSink* trace) {
  return cpu_.run(maximum_steps, trace);
}

} // namespace northstar64
