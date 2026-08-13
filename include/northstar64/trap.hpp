#pragma once

#include "northstar64/types.hpp"

#include <cstdint>
#include <string>

namespace northstar64 {

enum class TrapCause : std::uint64_t {
  InstructionAddressMisaligned = 0,
  InstructionAccessFault = 1,
  IllegalInstruction = 2,
  Breakpoint = 3,
  LoadAddressMisaligned = 4,
  LoadAccessFault = 5,
  StoreAddressMisaligned = 6,
  StoreAccessFault = 7,
  EnvironmentCallFromUserMode = 8,
  EnvironmentCallFromSupervisorMode = 9,
  EnvironmentCallFromMachineMode = 11,
};

struct Trap {
  TrapCause cause{};
  Address pc{};
  std::uint64_t value{};
  std::string detail;

  friend bool operator==(const Trap&, const Trap&) = default;
};

const char* trap_name(TrapCause cause) noexcept;

} // namespace northstar64

