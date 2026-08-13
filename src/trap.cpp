#include "northstar64/trap.hpp"

namespace northstar64 {

const char* trap_name(TrapCause cause) noexcept {
  switch (cause) {
  case TrapCause::InstructionAddressMisaligned:
    return "instruction-address-misaligned";
  case TrapCause::InstructionAccessFault:
    return "instruction-access-fault";
  case TrapCause::IllegalInstruction:
    return "illegal-instruction";
  case TrapCause::Breakpoint:
    return "breakpoint";
  case TrapCause::LoadAddressMisaligned:
    return "load-address-misaligned";
  case TrapCause::LoadAccessFault:
    return "load-access-fault";
  case TrapCause::StoreAddressMisaligned:
    return "store-address-misaligned";
  case TrapCause::StoreAccessFault:
    return "store-access-fault";
  case TrapCause::EnvironmentCallFromUserMode:
    return "environment-call-from-user-mode";
  case TrapCause::EnvironmentCallFromSupervisorMode:
    return "environment-call-from-supervisor-mode";
  case TrapCause::EnvironmentCallFromMachineMode:
    return "environment-call-from-machine-mode";
  case TrapCause::InstructionPageFault:
    return "instruction-page-fault";
  case TrapCause::LoadPageFault:
    return "load-page-fault";
  case TrapCause::StorePageFault:
    return "store-page-fault";
  }
  return "unknown-trap";
}

} // namespace northstar64
