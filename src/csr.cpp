#include "northstar64/csr.hpp"

#include <sstream>

namespace northstar64 {
namespace {

constexpr std::uint64_t kMisa = (std::uint64_t{2} << 62U) | (std::uint64_t{1} << 8U);
constexpr std::uint64_t kMstatusMie = std::uint64_t{1} << 3U;
constexpr std::uint64_t kMstatusMpie = std::uint64_t{1} << 7U;
constexpr std::uint64_t kMstatusMpp = std::uint64_t{3} << 11U;

CsrError unknown_csr(std::uint16_t address) {
  std::ostringstream message;
  message << "CSR 0x" << std::hex << address << " is not implemented";
  return CsrError{address, message.str()};
}

CsrError read_only_csr(std::uint16_t address) {
  std::ostringstream message;
  message << "CSR 0x" << std::hex << address << " is read-only";
  return CsrError{address, message.str()};
}

} // namespace

CsrFile::CsrFile() { reset(); }

void CsrFile::reset() {
  mstatus_ = 0;
  medeleg_ = 0;
  mie_ = 0;
  mtvec_ = 0;
  mscratch_ = 0;
  mepc_ = 0;
  mcause_ = 0;
  mtval_ = 0;
  mip_ = 0;
  mcycle_ = 0;
  minstret_ = 0;
}

CsrReadResult CsrFile::read(std::uint16_t address) const {
  switch (address) {
  case csr::kMstatus:
    return mstatus_;
  case csr::kMisa:
    return kMisa;
  case csr::kMedeleg:
    return medeleg_;
  case csr::kMie:
    return mie_;
  case csr::kMtvec:
    return mtvec_;
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
    return unknown_csr(address);
  }
}

std::optional<CsrError> CsrFile::write(std::uint16_t address, std::uint64_t value) {
  switch (address) {
  case csr::kMstatus:
    mstatus_ = value;
    return std::nullopt;
  case csr::kMedeleg:
    medeleg_ = value;
    return std::nullopt;
  case csr::kMie:
    mie_ = value;
    return std::nullopt;
  case csr::kMtvec:
    if ((value & 0x3U) != 0U) {
      return CsrError{address, "only direct mtvec mode is implemented"};
    }
    mtvec_ = value;
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
    mip_ = value;
    return std::nullopt;
  case csr::kMcycle:
    mcycle_ = value;
    return std::nullopt;
  case csr::kMinstret:
    minstret_ = value;
    return std::nullopt;
  case csr::kMisa:
  case csr::kCycle:
  case csr::kInstret:
  case csr::kMhartid:
    return read_only_csr(address);
  default:
    return unknown_csr(address);
  }
}

void CsrFile::enter_trap(const Trap& trap) {
  const bool machine_interrupt_enabled = (mstatus_ & kMstatusMie) != 0U;
  if (machine_interrupt_enabled) {
    mstatus_ |= kMstatusMpie;
  } else {
    mstatus_ &= ~kMstatusMpie;
  }
  mstatus_ &= ~kMstatusMie;
  mstatus_ = (mstatus_ & ~kMstatusMpp) | kMstatusMpp;
  mepc_ = trap.pc & ~std::uint64_t{0x3};
  mcause_ = static_cast<std::uint64_t>(trap.cause);
  mtval_ = trap.value;
}

Address CsrFile::return_from_trap() {
  const bool previous_interrupt_enabled = (mstatus_ & kMstatusMpie) != 0U;
  if (previous_interrupt_enabled) {
    mstatus_ |= kMstatusMie;
  } else {
    mstatus_ &= ~kMstatusMie;
  }
  mstatus_ |= kMstatusMpie;
  mstatus_ &= ~kMstatusMpp;
  return mepc_;
}

} // namespace northstar64
