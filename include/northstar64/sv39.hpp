#pragma once

#include "northstar64/memory.hpp"
#include "northstar64/privilege.hpp"
#include "northstar64/types.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace northstar64 {

enum class VirtualAccess { InstructionFetch, Load, Store };

struct Sv39Context {
  std::uint64_t root_ppn{};
  PrivilegeLevel privilege{PrivilegeLevel::Supervisor};
  VirtualAccess access{VirtualAccess::Load};
  bool sum{};
  bool mxr{};
};

enum class Sv39FaultReason {
  NonCanonicalAddress,
  PageTableAccess,
  InvalidPte,
  ReservedWriteOnlyPte,
  ReservedPteBits,
  ReservedNonLeafPte,
  MisalignedSuperpage,
  PrivilegeViolation,
  PermissionViolation,
  AccessedBitClear,
  DirtyBitClear,
  WalkExhausted,
};

struct Sv39Fault {
  Sv39FaultReason reason{};
  Address virtual_address{};
  int level{};
  Address pte_address{};
  std::uint64_t pte{};
  std::string detail;

  friend bool operator==(const Sv39Fault&, const Sv39Fault&) = default;
};

struct Sv39Translation {
  Address physical_address{};
  int leaf_level{};
  Address pte_address{};
  std::uint64_t pte{};

  friend bool operator==(const Sv39Translation&, const Sv39Translation&) = default;
};

using Sv39Result = std::variant<Sv39Translation, Sv39Fault>;

[[nodiscard]] bool is_sv39_canonical(Address virtual_address) noexcept;
[[nodiscard]] Sv39Result walk_sv39(Bus& bus, Address virtual_address,
                                   const Sv39Context& context);
const char* sv39_fault_reason_name(Sv39FaultReason reason) noexcept;

} // namespace northstar64
