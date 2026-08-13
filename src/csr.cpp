#include "northstar64/csr.hpp"

#include <sstream>
#include <utility>

namespace northstar64 {
namespace {

constexpr std::uint64_t kMisa = (std::uint64_t{2} << 62U) | (std::uint64_t{1} << 8U);
constexpr std::uint64_t kCounterCycle = std::uint64_t{1} << 0U;
constexpr std::uint64_t kCounterTime = std::uint64_t{1} << 1U;
constexpr std::uint64_t kCounterInstret = std::uint64_t{1} << 2U;
constexpr std::uint64_t kCounterEnableMask = kCounterCycle | kCounterTime | kCounterInstret;
constexpr std::uint64_t kSupervisorSoftwareInterrupt = std::uint64_t{1} << 1U;
constexpr std::uint64_t kSupervisorTimerInterrupt = std::uint64_t{1} << 5U;
constexpr std::uint64_t kSupervisorExternalInterrupt = std::uint64_t{1} << 9U;
constexpr std::uint64_t kMachineSoftwareInterrupt = std::uint64_t{1} << 3U;
constexpr std::uint64_t kMachineTimerInterrupt = std::uint64_t{1} << 7U;
constexpr std::uint64_t kMachineExternalInterrupt = std::uint64_t{1} << 11U;
constexpr std::uint64_t kSupervisorInterruptMask =
    kSupervisorSoftwareInterrupt | kSupervisorTimerInterrupt | kSupervisorExternalInterrupt;
constexpr std::uint64_t kImplementedInterruptMask =
    kSupervisorInterruptMask | kMachineSoftwareInterrupt | kMachineTimerInterrupt |
    kMachineExternalInterrupt;
constexpr std::uint64_t kDelegatableExceptionMask =
    (std::uint64_t{1} << 0U) | (std::uint64_t{1} << 1U) | (std::uint64_t{1} << 2U) |
    (std::uint64_t{1} << 3U) | (std::uint64_t{1} << 4U) | (std::uint64_t{1} << 5U) |
    (std::uint64_t{1} << 6U) | (std::uint64_t{1} << 7U) | (std::uint64_t{1} << 8U) |
    (std::uint64_t{1} << 9U) | (std::uint64_t{1} << 12U) |
    (std::uint64_t{1} << 13U) | (std::uint64_t{1} << 15U);
constexpr std::uint64_t kSatpModeMask = std::uint64_t{0xf} << 60U;
constexpr std::uint64_t kSatpModeSv39 = std::uint64_t{8} << 60U;

bool is_implemented(std::uint16_t address) noexcept {
  switch (address) {
  case csr::kSstatus:
  case csr::kSie:
  case csr::kStvec:
  case csr::kScounteren:
  case csr::kSscratch:
  case csr::kSepc:
  case csr::kScause:
  case csr::kStval:
  case csr::kSip:
  case csr::kSatp:
  case csr::kMstatus:
  case csr::kMisa:
  case csr::kMedeleg:
  case csr::kMideleg:
  case csr::kMie:
  case csr::kMtvec:
  case csr::kMcounteren:
  case csr::kMscratch:
  case csr::kMepc:
  case csr::kMcause:
  case csr::kMtval:
  case csr::kMip:
  case csr::kMcycle:
  case csr::kMinstret:
  case csr::kCycle:
  case csr::kInstret:
  case csr::kMhartid:
    return true;
  default:
    return false;
  }
}

std::uint64_t counter_bit(std::uint16_t address) noexcept {
  switch (address) {
  case csr::kCycle:
    return kCounterCycle;
  case csr::kInstret:
    return kCounterInstret;
  default:
    return 0;
  }
}

CsrError make_error(CsrErrorKind kind, std::uint16_t address, PrivilegeLevel privilege,
                    std::string detail) {
  return CsrError{kind, address, privilege, std::move(detail)};
}

std::string csr_hex(std::uint16_t address) {
  std::ostringstream output;
  output << "0x" << std::hex << address;
  return output.str();
}

std::uint64_t normalize_mstatus(std::uint64_t value) noexcept {
  auto normalized = value & status::kMstatusWritableMask;
  const auto mpp = (normalized & status::kMppMask) >> 11U;
  if (mpp == 2U) {
    normalized &= ~status::kMppMask;
  }
  return normalized;
}

} // namespace

CsrFile::CsrFile() { reset(); }

void CsrFile::reset() {
  mstatus_ = 0;
  medeleg_ = 0;
  mideleg_ = 0;
  mie_ = 0;
  mtvec_ = 0;
  mcounteren_ = 0;
  mscratch_ = 0;
  mepc_ = 0;
  mcause_ = 0;
  mtval_ = 0;
  mip_ = 0;
  stvec_ = 0;
  scounteren_ = 0;
  sscratch_ = 0;
  sepc_ = 0;
  scause_ = 0;
  stval_ = 0;
  satp_ = 0;
  mcycle_ = 0;
  minstret_ = 0;
}

std::optional<CsrError> CsrFile::validate_access(std::uint16_t address,
                                                PrivilegeLevel privilege, bool write) const {
  const auto required = csr_minimum_privilege(address);
  if (!privilege_at_least(privilege, required)) {
    return make_error(CsrErrorKind::PrivilegeViolation, address, privilege,
                      std::string("CSR ") + csr_hex(address) + " requires " +
                          privilege_name(required) + " privilege, current mode is " +
                          privilege_name(privilege));
  }
  if (!is_implemented(address)) {
    return make_error(CsrErrorKind::Unimplemented, address, privilege,
                      "CSR " + csr_hex(address) + " is not implemented");
  }
  if (write && csr_is_read_only(address)) {
    return make_error(CsrErrorKind::ReadOnly, address, privilege,
                      "CSR " + csr_hex(address) + " is read-only");
  }

  const auto requested_counter = counter_bit(address);
  if (!write && requested_counter != 0U && privilege != PrivilegeLevel::Machine) {
    const bool machine_enabled = (mcounteren_ & requested_counter) != 0U;
    const bool supervisor_enabled = privilege != PrivilegeLevel::User ||
                                    (scounteren_ & requested_counter) != 0U;
    if (!machine_enabled || !supervisor_enabled) {
      return make_error(CsrErrorKind::CounterDisabled, address, privilege,
                        "CSR " + csr_hex(address) + " is disabled by counter-enable state");
    }
  }
  return std::nullopt;
}

CsrReadResult CsrFile::read(std::uint16_t address, PrivilegeLevel privilege) const {
  if (auto error = validate_access(address, privilege, false)) {
    return *error;
  }

  switch (address) {
  case csr::kSstatus:
    return (mstatus_ & status::kSstatusWritableMask) | status::kUxl64;
  case csr::kSie:
    return mie_ & mideleg_ & kSupervisorInterruptMask;
  case csr::kStvec:
    return stvec_;
  case csr::kScounteren:
    return scounteren_;
  case csr::kSscratch:
    return sscratch_;
  case csr::kSepc:
    return sepc_;
  case csr::kScause:
    return scause_;
  case csr::kStval:
    return stval_;
  case csr::kSip:
    return mip_ & mideleg_ & kSupervisorInterruptMask;
  case csr::kSatp:
    return satp_;
  case csr::kMstatus:
    return mstatus_ | status::kMstatusFixedValue;
  case csr::kMisa:
    return kMisa;
  case csr::kMedeleg:
    return medeleg_;
  case csr::kMideleg:
    return mideleg_;
  case csr::kMie:
    return mie_;
  case csr::kMtvec:
    return mtvec_;
  case csr::kMcounteren:
    return mcounteren_;
  case csr::kMscratch:
    return mscratch_;
  case csr::kMepc:
    return mepc_;
  case csr::kMcause:
    return mcause_;
  case csr::kMtval:
    return mtval_;
  case csr::kMip:
    return mip_;
  case csr::kMcycle:
  case csr::kCycle:
    return mcycle_;
  case csr::kMinstret:
  case csr::kInstret:
    return minstret_;
  case csr::kMhartid:
    return std::uint64_t{0};
  default:
    break;
  }
  return make_error(CsrErrorKind::Unimplemented, address, privilege,
                    "CSR " + csr_hex(address) + " is not implemented");
}

std::optional<CsrError> CsrFile::write(std::uint16_t address, std::uint64_t value,
                                      PrivilegeLevel privilege) {
  if (auto error = validate_access(address, privilege, true)) {
    return error;
  }

  switch (address) {
  case csr::kSstatus:
    mstatus_ = (mstatus_ & ~status::kSstatusWritableMask) |
               (value & status::kSstatusWritableMask);
    return std::nullopt;
  case csr::kSie: {
    const auto visible = mideleg_ & kSupervisorInterruptMask;
    mie_ = (mie_ & ~visible) | (value & visible);
    return std::nullopt;
  }
  case csr::kStvec:
    stvec_ = value & ~std::uint64_t{0x3};
    return std::nullopt;
  case csr::kScounteren:
    scounteren_ = value & kCounterEnableMask;
    return std::nullopt;
  case csr::kSscratch:
    sscratch_ = value;
    return std::nullopt;
  case csr::kSepc:
    sepc_ = value & ~std::uint64_t{0x3};
    return std::nullopt;
  case csr::kScause:
    scause_ = value;
    return std::nullopt;
  case csr::kStval:
    stval_ = value;
    return std::nullopt;
  case csr::kSip: {
    const auto writable = mideleg_ & kSupervisorSoftwareInterrupt;
    mip_ = (mip_ & ~writable) | (value & writable);
    return std::nullopt;
  }
  case csr::kSatp: {
    const auto mode = value & kSatpModeMask;
    if (mode == 0U) {
      satp_ = 0;
    } else if (mode == kSatpModeSv39) {
      satp_ = value;
    }
    return std::nullopt;
  }
  case csr::kMstatus:
    mstatus_ = normalize_mstatus(value);
    return std::nullopt;
  case csr::kMisa:
    // This implementation fixes MXL=64 and the I extension. A write to the WARL
    // register remains legal but cannot change either fixed field.
    return std::nullopt;
  case csr::kMedeleg:
    medeleg_ = value & kDelegatableExceptionMask;
    return std::nullopt;
  case csr::kMideleg:
    mideleg_ = value & kSupervisorInterruptMask;
    return std::nullopt;
  case csr::kMie:
    mie_ = value & kImplementedInterruptMask;
    return std::nullopt;
  case csr::kMtvec:
    mtvec_ = value & ~std::uint64_t{0x3};
    return std::nullopt;
  case csr::kMcounteren:
    mcounteren_ = value & kCounterEnableMask;
    return std::nullopt;
  case csr::kMscratch:
    mscratch_ = value;
    return std::nullopt;
  case csr::kMepc:
    mepc_ = value & ~std::uint64_t{0x3};
    return std::nullopt;
  case csr::kMcause:
    mcause_ = value;
    return std::nullopt;
  case csr::kMtval:
    mtval_ = value;
    return std::nullopt;
  case csr::kMip:
    mip_ = value & kImplementedInterruptMask;
    return std::nullopt;
  case csr::kMcycle:
    mcycle_ = value;
    return std::nullopt;
  case csr::kMinstret:
    minstret_ = value;
    return std::nullopt;
  default:
    break;
  }
  return make_error(CsrErrorKind::Unimplemented, address, privilege,
                    "CSR " + csr_hex(address) + " is not implemented");
}

TrapEntry CsrFile::enter_trap(const Trap& trap, PrivilegeLevel origin) {
  const auto cause = static_cast<std::uint64_t>(trap.cause);
  const bool delegated = origin != PrivilegeLevel::Machine && cause < 64U &&
                         (medeleg_ & (std::uint64_t{1} << cause)) != 0U;
  if (delegated) {
    const bool supervisor_interrupt_enabled = (mstatus_ & status::kSie) != 0U;
    if (supervisor_interrupt_enabled) {
      mstatus_ |= status::kSpie;
    } else {
      mstatus_ &= ~status::kSpie;
    }
    mstatus_ &= ~status::kSie;
    if (origin == PrivilegeLevel::Supervisor) {
      mstatus_ |= status::kSpp;
    } else {
      mstatus_ &= ~status::kSpp;
    }
    sepc_ = trap.pc & ~std::uint64_t{0x3};
    scause_ = cause;
    stval_ = trap.value;
    return TrapEntry{PrivilegeLevel::Supervisor, stvec_ & ~std::uint64_t{0x3}};
  }

  const bool machine_interrupt_enabled = (mstatus_ & status::kMie) != 0U;
  if (machine_interrupt_enabled) {
    mstatus_ |= status::kMpie;
  } else {
    mstatus_ &= ~status::kMpie;
  }
  mstatus_ &= ~status::kMie;
  mstatus_ = (mstatus_ & ~status::kMppMask) |
             (static_cast<std::uint64_t>(origin) << 11U);
  mepc_ = trap.pc & ~std::uint64_t{0x3};
  mcause_ = cause;
  mtval_ = trap.value;
  return TrapEntry{PrivilegeLevel::Machine, mtvec_ & ~std::uint64_t{0x3}};
}

TrapReturn CsrFile::return_from_trap(TrapReturnMode mode) {
  if (mode == TrapReturnMode::Supervisor) {
    const auto target = (mstatus_ & status::kSpp) != 0U ? PrivilegeLevel::Supervisor
                                                        : PrivilegeLevel::User;
    const bool previous_interrupt_enabled = (mstatus_ & status::kSpie) != 0U;
    if (previous_interrupt_enabled) {
      mstatus_ |= status::kSie;
    } else {
      mstatus_ &= ~status::kSie;
    }
    mstatus_ |= status::kSpie;
    mstatus_ &= ~status::kSpp;
    mstatus_ &= ~status::kMprv;
    return TrapReturn{target, sepc_};
  }

  const auto mpp = static_cast<std::uint8_t>((mstatus_ & status::kMppMask) >> 11U);
  const auto target = mpp == static_cast<std::uint8_t>(PrivilegeLevel::Machine)
                          ? PrivilegeLevel::Machine
                      : mpp == static_cast<std::uint8_t>(PrivilegeLevel::Supervisor)
                          ? PrivilegeLevel::Supervisor
                          : PrivilegeLevel::User;
  const bool previous_interrupt_enabled = (mstatus_ & status::kMpie) != 0U;
  if (previous_interrupt_enabled) {
    mstatus_ |= status::kMie;
  } else {
    mstatus_ &= ~status::kMie;
  }
  mstatus_ |= status::kMpie;
  mstatus_ &= ~status::kMppMask;
  if (target != PrivilegeLevel::Machine) {
    mstatus_ &= ~status::kMprv;
  }
  return TrapReturn{target, mepc_};
}

} // namespace northstar64
