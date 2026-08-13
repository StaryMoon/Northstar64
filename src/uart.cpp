#include "northstar64/uart.hpp"

namespace northstar64 {

Uart16550::Uart16550(Address base, std::ostream* output)
    : MappedDevice(base, kRegisterSpan, "uart16550"), output_(output) {}

DeviceReadResult Uart16550::read(std::uint64_t offset, std::size_t width) {
  if (width != 1U) {
    return DeviceError{"the UART model only supports byte accesses"};
  }
  switch (offset) {
  case 0:
    return std::uint64_t{0};
  case 5:
    // THR empty and transmitter empty. Input is deliberately not modelled yet.
    return std::uint64_t{0x60};
  default:
    return std::uint64_t{0};
  }
}

DeviceWriteResult Uart16550::write(std::uint64_t offset, std::size_t width,
                                   std::uint64_t value) {
  if (width != 1U) {
    return DeviceError{"the UART model only supports byte accesses"};
  }
  if (offset == 0U) {
    const auto character = static_cast<char>(value & 0xffU);
    transmitted_.push_back(character);
    if (output_ != nullptr) {
      output_->put(character);
      output_->flush();
    }
  }
  return std::nullopt;
}

} // namespace northstar64

