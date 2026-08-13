#include "northstar64/sv39.hpp"

#include <array>
#include <utility>

namespace northstar64 {
namespace {

constexpr std::uint64_t kVpnMask = 0x1ffU;
constexpr std::uint64_t kPpnMask = (std::uint64_t{1} << 44U) - 1U;
constexpr std::uint64_t kPteReservedHighMask = ~((std::uint64_t{1} << 54U) - 1U);
constexpr std::uint64_t kPteV = std::uint64_t{1} << 0U;
constexpr std::uint64_t kPteR = std::uint64_t{1} << 1U;
constexpr std::uint64_t kPteW = std::uint64_t{1} << 2U;
constexpr std::uint64_t kPteX = std::uint64_t{1} << 3U;
constexpr std::uint64_t kPteU = std::uint64_t{1} << 4U;
constexpr std::uint64_t kPteA = std::uint64_t{1} << 6U;
constexpr std::uint64_t kPteD = std::uint64_t{1} << 7U;

Sv39Fault fault(Sv39FaultReason reason, Address virtual_address, int level,
                Address pte_address, std::uint64_t pte, std::string detail) {
  return Sv39Fault{reason, virtual_address, level, pte_address, pte, std::move(detail)};
}

std::array<std::uint64_t, 3> virtual_page_numbers(Address virtual_address) noexcept {
  return {(virtual_address >> 12U) & kVpnMask, (virtual_address >> 21U) & kVpnMask,
          (virtual_address >> 30U) & kVpnMask};
}

bool access_is_permitted(std::uint64_t pte, VirtualAccess access, bool mxr) noexcept {
  switch (access) {
  case VirtualAccess::InstructionFetch:
    return (pte & kPteX) != 0U;
  case VirtualAccess::Load:
    return (pte & kPteR) != 0U || (mxr && (pte & kPteX) != 0U);
  case VirtualAccess::Store:
    return (pte & kPteW) != 0U;
  }
  return false;
}

} // namespace

bool is_sv39_canonical(Address virtual_address) noexcept {
  const auto upper = virtual_address >> 39U;
  const bool sign = ((virtual_address >> 38U) & 1U) != 0U;
  return upper == (sign ? ((std::uint64_t{1} << 25U) - 1U) : 0U);
}

Sv39Result walk_sv39(Bus& bus, Address virtual_address, const Sv39Context& context) {
  if (!is_sv39_canonical(virtual_address)) {
    return fault(Sv39FaultReason::NonCanonicalAddress, virtual_address, -1, 0, 0,
                 "virtual address is not sign-extended from bit 38");
  }
  if (context.privilege != PrivilegeLevel::Supervisor &&
      context.privilege != PrivilegeLevel::User) {
    return fault(Sv39FaultReason::PrivilegeViolation, virtual_address, -1, 0, 0,
                 "Sv39 translation context requires supervisor or user privilege");
  }

  const auto vpn = virtual_page_numbers(virtual_address);
  auto table_address = (context.root_ppn & kPpnMask) << 12U;

  for (int level = 2; level >= 0; --level) {
    const auto index = vpn[static_cast<std::size_t>(level)];
    if (add_overflows(table_address, index * 8U)) {
      return fault(Sv39FaultReason::PageTableAccess, virtual_address, level, table_address, 0,
                   "page-table entry address overflows physical address space");
    }
    const auto pte_address = table_address + index * 8U;
    const auto read = bus.read(pte_address, 8U, AccessKind::PageTableWalk);
    if (const auto* bus_fault = std::get_if<BusFault>(&read)) {
      return fault(Sv39FaultReason::PageTableAccess, virtual_address, level, pte_address, 0,
                   bus_fault->detail);
    }
    const auto pte = std::get<std::uint64_t>(read);
    if ((pte & kPteReservedHighMask) != 0U) {
      return fault(Sv39FaultReason::ReservedPteBits, virtual_address, level, pte_address, pte,
                   "PTE uses high bits reserved by the implemented Sv39 profile");
    }
    if ((pte & kPteV) == 0U) {
      return fault(Sv39FaultReason::InvalidPte, virtual_address, level, pte_address, pte,
                   "page-table entry is not valid");
    }
    if ((pte & kPteR) == 0U && (pte & kPteW) != 0U) {
      return fault(Sv39FaultReason::ReservedWriteOnlyPte, virtual_address, level, pte_address,
                   pte, "W=1 with R=0 is a reserved page-table entry encoding");
    }

    const bool leaf = (pte & (kPteR | kPteX)) != 0U;
    const auto ppn = (pte >> 10U) & kPpnMask;
    if (!leaf) {
      if ((pte & (kPteU | kPteA | kPteD)) != 0U) {
        return fault(Sv39FaultReason::ReservedNonLeafPte, virtual_address, level, pte_address, pte,
                     "non-leaf PTE has reserved U, A, or D state");
      }
      if (level == 0) {
        return fault(Sv39FaultReason::WalkExhausted, virtual_address, level, pte_address, pte,
                     "level-zero entry is not a leaf");
      }
      table_address = ppn << 12U;
      continue;
    }

    const auto low_ppn_bits = static_cast<unsigned>(level) * 9U;
    if (low_ppn_bits != 0U) {
      const auto low_mask = (std::uint64_t{1} << low_ppn_bits) - 1U;
      if ((ppn & low_mask) != 0U) {
        return fault(Sv39FaultReason::MisalignedSuperpage, virtual_address, level, pte_address,
                     pte, "leaf PPN is not aligned for its superpage level");
      }
    }

    const bool user_page = (pte & kPteU) != 0U;
    if (context.privilege == PrivilegeLevel::User && !user_page) {
      return fault(Sv39FaultReason::PrivilegeViolation, virtual_address, level, pte_address, pte,
                   "user mode cannot access a supervisor page");
    }
    if (context.privilege == PrivilegeLevel::Supervisor && user_page &&
        (context.access == VirtualAccess::InstructionFetch || !context.sum)) {
      return fault(Sv39FaultReason::PrivilegeViolation, virtual_address, level, pte_address, pte,
                   context.access == VirtualAccess::InstructionFetch
                       ? "supervisor mode cannot execute from a user page"
                       : "SUM is clear for supervisor access to a user page");
    }
    if (!access_is_permitted(pte, context.access, context.mxr)) {
      return fault(Sv39FaultReason::PermissionViolation, virtual_address, level, pte_address, pte,
                   "leaf permissions reject the requested access type");
    }
    if ((pte & kPteA) == 0U) {
      return fault(Sv39FaultReason::AccessedBitClear, virtual_address, level, pte_address, pte,
                   "software-managed accessed bit is clear");
    }
    if (context.access == VirtualAccess::Store && (pte & kPteD) == 0U) {
      return fault(Sv39FaultReason::DirtyBitClear, virtual_address, level, pte_address, pte,
                   "software-managed dirty bit is clear for a store");
    }

    const auto offset_bits = 12U + static_cast<unsigned>(level) * 9U;
    const auto offset_mask = (std::uint64_t{1} << offset_bits) - 1U;
    const auto physical_address = ((ppn << 12U) & ~offset_mask) |
                                  (virtual_address & offset_mask);
    return Sv39Translation{physical_address, level, pte_address, pte};
  }

  return fault(Sv39FaultReason::WalkExhausted, virtual_address, 0, 0, 0,
               "Sv39 walk ended without a leaf entry");
}

const char* sv39_fault_reason_name(Sv39FaultReason reason) noexcept {
  switch (reason) {
  case Sv39FaultReason::NonCanonicalAddress:
    return "non-canonical-address";
  case Sv39FaultReason::PageTableAccess:
    return "page-table-access";
  case Sv39FaultReason::InvalidPte:
    return "invalid-pte";
  case Sv39FaultReason::ReservedWriteOnlyPte:
    return "reserved-write-only-pte";
  case Sv39FaultReason::ReservedPteBits:
    return "reserved-pte-bits";
  case Sv39FaultReason::ReservedNonLeafPte:
    return "reserved-non-leaf-pte";
  case Sv39FaultReason::MisalignedSuperpage:
    return "misaligned-superpage";
  case Sv39FaultReason::PrivilegeViolation:
    return "privilege-violation";
  case Sv39FaultReason::PermissionViolation:
    return "permission-violation";
  case Sv39FaultReason::AccessedBitClear:
    return "accessed-bit-clear";
  case Sv39FaultReason::DirtyBitClear:
    return "dirty-bit-clear";
  case Sv39FaultReason::WalkExhausted:
    return "walk-exhausted";
  }
  return "unknown";
}

} // namespace northstar64
