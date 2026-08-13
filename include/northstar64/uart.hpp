#pragma once

#include "northstar64/memory.hpp"

#include <cstdint>
#include <ostream>
#include <string>

namespace northstar64 {

class Uart16550 final : public MappedDevice {
public:
  static constexpr Address kDefaultBase = 0x10000000ULL;
  static constexpr std::uint64_t kRegisterSpan = 8;

  explicit Uart16550(Address base = kDefaultBase, std::ostream* output = nullptr);

  DeviceReadResult read(std::uint64_t offset, std::size_t width) override;
  DeviceWriteResult write(std::uint64_t offset, std::size_t width,
                          std::uint64_t value) override;

  [[nodiscard]] const std::string& transmitted() const noexcept { return transmitted_; }
  void clear_transmitted() { transmitted_.clear(); }

private:
  std::ostream* output_;
  std::string transmitted_;
};

} // namespace northstar64

