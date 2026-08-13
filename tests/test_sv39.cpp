#include "northstar64/sv39.hpp"
#include "test_support.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <variant>

using namespace northstar64;

namespace {

constexpr Address kRamBase = 0x00100000;
constexpr std::uint64_t kRamSize = 0x01000000;
constexpr Address kRoot = 0x00100000;
constexpr Address kLevelOne = 0x00101000;
constexpr Address kLevelZero = 0x00102000;
constexpr std::uint64_t kV = std::uint64_t{1} << 0U;
constexpr std::uint64_t kR = std::uint64_t{1} << 1U;
constexpr std::uint64_t kW = std::uint64_t{1} << 2U;
constexpr std::uint64_t kX = std::uint64_t{1} << 3U;
constexpr std::uint64_t kU = std::uint64_t{1} << 4U;
constexpr std::uint64_t kA = std::uint64_t{1} << 6U;
constexpr std::uint64_t kD = std::uint64_t{1} << 7U;
constexpr std::uint64_t kPpnMask = (std::uint64_t{1} << 44U) - 1U;

std::uint64_t ppn(Address address) { return address >> 12U; }

std::uint64_t table_pte(Address table) { return (ppn(table) << 10U) | kV; }

std::uint64_t leaf_pte(Address physical_base, std::uint64_t flags = kR | kW | kA | kD) {
  return (ppn(physical_base) << 10U) | kV | flags;
}

Address make_virtual(std::uint64_t vpn2, std::uint64_t vpn1, std::uint64_t vpn0,
                     std::uint64_t offset) {
  auto address = (vpn2 << 30U) | (vpn1 << 21U) | (vpn0 << 12U) | offset;
  if ((vpn2 & 0x100U) != 0U) {
    address |= ~((std::uint64_t{1} << 39U) - 1U);
  }
  return address;
}

std::array<std::uint64_t, 3> vpn(Address address) {
  return {(address >> 12U) & 0x1ffU, (address >> 21U) & 0x1ffU,
          (address >> 30U) & 0x1ffU};
}

bool reference_is_canonical(Address virtual_address) {
  constexpr auto low_max = Address{0x0000003fffffffffULL};
  constexpr auto high_min = Address{0xffffffc000000000ULL};
  return virtual_address <= low_max || virtual_address >= high_min;
}

class Sv39Fixture {
public:
  Sv39Fixture() : ram_(&bus_.emplace_device<SparseRam>(kRamBase, kRamSize)) {}

  void write_pte(Address table, std::uint64_t index, std::uint64_t pte) {
    const auto result = bus_.write(table + index * 8U, 8U, pte, AccessKind::ImageLoad);
    CHECK(!result);
  }

  void install_path(Address virtual_address, int leaf_level, std::uint64_t leaf) {
    const auto indices = vpn(virtual_address);
    if (leaf_level == 2) {
      write_pte(kRoot, indices[2], leaf);
      return;
    }
    write_pte(kRoot, indices[2], table_pte(kLevelOne));
    if (leaf_level == 1) {
      write_pte(kLevelOne, indices[1], leaf);
      return;
    }
    write_pte(kLevelOne, indices[1], table_pte(kLevelZero));
    write_pte(kLevelZero, indices[0], leaf);
  }

  Sv39Context context(VirtualAccess access = VirtualAccess::Load,
                      PrivilegeLevel privilege = PrivilegeLevel::Supervisor) const {
    return Sv39Context{ppn(kRoot), privilege, access, false, false};
  }

  Bus bus_;
  SparseRam* ram_;
};

Sv39Translation translation(const Sv39Result& result) {
  CHECK(std::holds_alternative<Sv39Translation>(result));
  return std::get<Sv39Translation>(result);
}

Sv39Fault walk_fault(const Sv39Result& result) {
  CHECK(std::holds_alternative<Sv39Fault>(result));
  return std::get<Sv39Fault>(result);
}

struct ReferenceResult {
  bool success{};
  Address physical_address{};
  int leaf_level{};
  Sv39FaultReason reason{};
};

ReferenceResult reference_walk(const std::map<Address, std::uint64_t>& entries,
                               Address virtual_address, const Sv39Context& context) {
  if (!reference_is_canonical(virtual_address)) {
    return {false, 0, -1, Sv39FaultReason::NonCanonicalAddress};
  }
  const auto indices = vpn(virtual_address);
  auto table = (context.root_ppn & kPpnMask) << 12U;
  for (int level = 2; level >= 0; --level) {
    const auto address = table + indices[static_cast<std::size_t>(level)] * 8U;
    const auto found = entries.find(address);
    const auto pte = found == entries.end() ? 0U : found->second;
    const bool valid = (pte & kV) != 0U;
    const bool readable = (pte & kR) != 0U;
    const bool writable = (pte & kW) != 0U;
    const bool executable = (pte & kX) != 0U;
    if (!valid) {
      return {false, 0, level, Sv39FaultReason::InvalidPte};
    }
    if (!readable && writable) {
      return {false, 0, level, Sv39FaultReason::ReservedWriteOnlyPte};
    }
    if (!readable && !executable) {
      if (level == 0) {
        return {false, 0, level, Sv39FaultReason::WalkExhausted};
      }
      table = ((pte >> 10U) & kPpnMask) << 12U;
      continue;
    }

    const auto leaf_ppn = (pte >> 10U) & kPpnMask;
    const auto low_bits = static_cast<unsigned>(level) * 9U;
    const auto low_mask = low_bits == 0U ? 0U : (std::uint64_t{1} << low_bits) - 1U;
    if ((leaf_ppn & low_mask) != 0U) {
      return {false, 0, level, Sv39FaultReason::MisalignedSuperpage};
    }
    const bool user = (pte & kU) != 0U;
    if ((context.privilege == PrivilegeLevel::User && !user) ||
        (context.privilege == PrivilegeLevel::Supervisor && user &&
         (context.access == VirtualAccess::InstructionFetch || !context.sum))) {
      return {false, 0, level, Sv39FaultReason::PrivilegeViolation};
    }
    const bool allowed = context.access == VirtualAccess::InstructionFetch
                             ? executable
                         : context.access == VirtualAccess::Load
                             ? readable || (context.mxr && executable)
                             : writable;
    if (!allowed) {
      return {false, 0, level, Sv39FaultReason::PermissionViolation};
    }
    if ((pte & kA) == 0U) {
      return {false, 0, level, Sv39FaultReason::AccessedBitClear};
    }
    if (context.access == VirtualAccess::Store && (pte & kD) == 0U) {
      return {false, 0, level, Sv39FaultReason::DirtyBitClear};
    }
    const auto offset_bits = 12U + static_cast<unsigned>(level) * 9U;
    const auto offset_mask = (std::uint64_t{1} << offset_bits) - 1U;
    return {true, ((leaf_ppn << 12U) & ~offset_mask) |
                      (virtual_address & offset_mask),
            level, Sv39FaultReason::InvalidPte};
  }
  return {false, 0, 0, Sv39FaultReason::WalkExhausted};
}

class FixedRandom {
public:
  std::uint64_t next() noexcept {
    state_ ^= state_ >> 12U;
    state_ ^= state_ << 25U;
    state_ ^= state_ >> 27U;
    return state_ * 0x2545f4914f6cdd1dULL;
  }

private:
  std::uint64_t state_{0x5376333957616c6bULL};
};

} // namespace

TEST_CASE("Sv39 canonical addresses sign extend bit 38") {
  CHECK(is_sv39_canonical(0));
  CHECK(is_sv39_canonical(0x0000003fffffffffULL));
  CHECK(is_sv39_canonical(0xffffffc000000000ULL));
  CHECK(is_sv39_canonical(0xffffffffffffffffULL));
  CHECK(!is_sv39_canonical(0x0000004000000000ULL));
  CHECK(!is_sv39_canonical(0xffffffbfffffffffULL));
}

TEST_CASE("Sv39 translates leaves at every page-table level") {
  struct Case {
    int level;
    Address physical_base;
  };
  constexpr Case cases[] = {
      {0, 0x00845000},
      {1, 0x00a00000},
      {2, 0x40000000},
  };
  const auto virtual_address = make_virtual(1, 2, 3, 0x456);
  for (const auto& test_case : cases) {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, test_case.level, leaf_pte(test_case.physical_base));
    const auto result = translation(walk_sv39(fixture.bus_, virtual_address, fixture.context()));
    const auto offset_bits = 12U + static_cast<unsigned>(test_case.level) * 9U;
    const auto offset_mask = (std::uint64_t{1} << offset_bits) - 1U;
    CHECK_EQ(result.physical_address,
             test_case.physical_base | (virtual_address & offset_mask));
    CHECK_EQ(result.leaf_level, test_case.level);
    CHECK_EQ(result.pte, leaf_pte(test_case.physical_base));
  }
}

TEST_CASE("Sv39 reports canonicality and physical walk faults with provenance") {
  Sv39Fixture fixture;
  const auto noncanonical = Address{0x0000004000000000ULL};
  const auto canonical_fault = walk_fault(walk_sv39(fixture.bus_, noncanonical,
                                                    fixture.context()));
  CHECK(canonical_fault.reason == Sv39FaultReason::NonCanonicalAddress);
  CHECK_EQ(canonical_fault.virtual_address, noncanonical);
  CHECK_EQ(canonical_fault.level, -1);
  CHECK_EQ(canonical_fault.pte_address, Address{0});

  Sv39Context unmapped = fixture.context();
  unmapped.root_ppn = 0x4000;
  const auto virtual_address = make_virtual(4, 5, 6, 7);
  const auto bus_fault = walk_fault(walk_sv39(fixture.bus_, virtual_address, unmapped));
  CHECK(bus_fault.reason == Sv39FaultReason::PageTableAccess);
  CHECK_EQ(bus_fault.virtual_address, virtual_address);
  CHECK_EQ(bus_fault.level, 2);
  CHECK_EQ(bus_fault.pte_address, Address{0x04000000} + vpn(virtual_address)[2] * 8U);
}

TEST_CASE("Sv39 rejects invalid reserved and malformed page-table entries") {
  const auto virtual_address = make_virtual(7, 8, 9, 0x123);
  const auto index = vpn(virtual_address)[2];
  struct Case {
    std::uint64_t pte;
    Sv39FaultReason reason;
  };
  const Case cases[] = {
      {0, Sv39FaultReason::InvalidPte},
      {kV | kW, Sv39FaultReason::ReservedWriteOnlyPte},
      {kV | (std::uint64_t{1} << 63U), Sv39FaultReason::ReservedPteBits},
      {table_pte(kLevelOne) | kU, Sv39FaultReason::ReservedNonLeafPte},
      {leaf_pte(0x40200000), Sv39FaultReason::MisalignedSuperpage},
  };
  for (const auto& test_case : cases) {
    Sv39Fixture fixture;
    fixture.write_pte(kRoot, index, test_case.pte);
    const auto result = walk_fault(walk_sv39(fixture.bus_, virtual_address,
                                             fixture.context()));
    CHECK(result.reason == test_case.reason);
    CHECK_EQ(result.level, 2);
    CHECK_EQ(result.pte_address, kRoot + index * 8U);
    CHECK_EQ(result.pte, test_case.pte);
  }

  Sv39Fixture exhausted;
  const auto indices = vpn(virtual_address);
  exhausted.write_pte(kRoot, indices[2], table_pte(kLevelOne));
  exhausted.write_pte(kLevelOne, indices[1], table_pte(kLevelZero));
  exhausted.write_pte(kLevelZero, indices[0], table_pte(kRoot));
  const auto result = walk_fault(walk_sv39(exhausted.bus_, virtual_address,
                                           exhausted.context()));
  CHECK(result.reason == Sv39FaultReason::WalkExhausted);
  CHECK_EQ(result.level, 0);
}

TEST_CASE("Sv39 enforces U SUM and supervisor execute restrictions") {
  const auto virtual_address = make_virtual(10, 11, 12, 0x88);
  {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, 0, leaf_pte(0x00900000));
    const auto result = walk_fault(walk_sv39(
        fixture.bus_, virtual_address,
        fixture.context(VirtualAccess::Load, PrivilegeLevel::User)));
    CHECK(result.reason == Sv39FaultReason::PrivilegeViolation);
  }
  {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, 0, leaf_pte(0x00900000, kR | kU | kA));
    auto context = fixture.context();
    const auto blocked = walk_fault(walk_sv39(fixture.bus_, virtual_address, context));
    CHECK(blocked.reason == Sv39FaultReason::PrivilegeViolation);
    context.sum = true;
    CHECK(std::holds_alternative<Sv39Translation>(
        walk_sv39(fixture.bus_, virtual_address, context)));
    context.access = VirtualAccess::Store;
    fixture.install_path(virtual_address, 0,
                         leaf_pte(0x00900000, kR | kW | kU | kA | kD));
    CHECK(std::holds_alternative<Sv39Translation>(
        walk_sv39(fixture.bus_, virtual_address, context)));
  }
  {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, 0, leaf_pte(0x00900000, kX | kU | kA));
    auto context = fixture.context(VirtualAccess::InstructionFetch);
    context.sum = true;
    const auto result = walk_fault(walk_sv39(fixture.bus_, virtual_address, context));
    CHECK(result.reason == Sv39FaultReason::PrivilegeViolation);
  }
}

TEST_CASE("Sv39 enforces access permissions MXR and software-managed A D bits") {
  const auto virtual_address = make_virtual(13, 14, 15, 0x44);
  {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, 0, leaf_pte(0x00910000, kX | kA));
    auto context = fixture.context(VirtualAccess::Load);
    const auto blocked = walk_fault(walk_sv39(fixture.bus_, virtual_address, context));
    CHECK(blocked.reason == Sv39FaultReason::PermissionViolation);
    context.mxr = true;
    CHECK(std::holds_alternative<Sv39Translation>(
        walk_sv39(fixture.bus_, virtual_address, context)));
  }
  {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, 0, leaf_pte(0x00910000, kR));
    const auto result = walk_fault(walk_sv39(fixture.bus_, virtual_address,
                                             fixture.context(VirtualAccess::Load)));
    CHECK(result.reason == Sv39FaultReason::AccessedBitClear);
  }
  {
    Sv39Fixture fixture;
    const auto pte = leaf_pte(0x00910000, kR | kW | kA);
    fixture.install_path(virtual_address, 0, pte);
    const auto result = walk_fault(walk_sv39(fixture.bus_, virtual_address,
                                             fixture.context(VirtualAccess::Store)));
    CHECK(result.reason == Sv39FaultReason::DirtyBitClear);
    const auto stored = fixture.bus_.read(result.pte_address, 8U, AccessKind::PageTableWalk);
    CHECK_EQ(std::get<std::uint64_t>(stored), pte);
  }
  {
    Sv39Fixture fixture;
    fixture.install_path(virtual_address, 0, leaf_pte(0x00910000, kR | kA));
    const auto result = walk_fault(walk_sv39(
        fixture.bus_, virtual_address, fixture.context(VirtualAccess::InstructionFetch)));
    CHECK(result.reason == Sv39FaultReason::PermissionViolation);
  }
}

TEST_CASE("Sv39 rejects machine privilege in an explicit translation context") {
  Sv39Fixture fixture;
  const auto virtual_address = make_virtual(1, 1, 1, 0);
  const auto result = walk_fault(walk_sv39(
      fixture.bus_, virtual_address,
      fixture.context(VirtualAccess::Load, PrivilegeLevel::Machine)));
  CHECK(result.reason == Sv39FaultReason::PrivilegeViolation);
  CHECK_EQ(result.virtual_address, virtual_address);
  CHECK_EQ(result.level, -1);
}

TEST_CASE("Sv39 fault reasons have stable diagnostic names") {
  struct Case {
    Sv39FaultReason reason;
    const char* name;
  };
  constexpr Case cases[] = {
      {Sv39FaultReason::NonCanonicalAddress, "non-canonical-address"},
      {Sv39FaultReason::PageTableAccess, "page-table-access"},
      {Sv39FaultReason::InvalidPte, "invalid-pte"},
      {Sv39FaultReason::ReservedWriteOnlyPte, "reserved-write-only-pte"},
      {Sv39FaultReason::ReservedPteBits, "reserved-pte-bits"},
      {Sv39FaultReason::ReservedNonLeafPte, "reserved-non-leaf-pte"},
      {Sv39FaultReason::MisalignedSuperpage, "misaligned-superpage"},
      {Sv39FaultReason::PrivilegeViolation, "privilege-violation"},
      {Sv39FaultReason::PermissionViolation, "permission-violation"},
      {Sv39FaultReason::AccessedBitClear, "accessed-bit-clear"},
      {Sv39FaultReason::DirtyBitClear, "dirty-bit-clear"},
      {Sv39FaultReason::WalkExhausted, "walk-exhausted"},
  };
  for (const auto& test_case : cases) {
    CHECK_EQ(std::string(sv39_fault_reason_name(test_case.reason)),
             std::string(test_case.name));
  }
}

TEST_CASE("fixed-seed generated Sv39 mappings agree with an independent reference walker") {
  FixedRandom random;
  for (std::size_t sample = 0; sample < 256; ++sample) {
    Sv39Fixture fixture;
    std::map<Address, std::uint64_t> entries;
    const auto virtual_address = make_virtual(random.next() & 0x1ffU, random.next() & 0x1ffU,
                                              random.next() & 0x1ffU, random.next() & 0xfffU);
    const auto level = static_cast<int>(random.next() % 3U);
    const auto indices = vpn(virtual_address);
    auto physical_base = (random.next() & kPpnMask) << 12U;
    const auto low_bits = static_cast<unsigned>(level) * 9U;
    if (low_bits != 0U) {
      physical_base &= ~((std::uint64_t{1} << (12U + low_bits)) - 1U);
    }
    const auto access = static_cast<VirtualAccess>(random.next() % 3U);
    const auto privilege = (random.next() & 1U) == 0U ? PrivilegeLevel::User
                                                       : PrivilegeLevel::Supervisor;
    std::uint64_t flags = kA;
    if ((random.next() & 1U) != 0U) {
      flags |= kR;
    }
    if ((flags & kR) != 0U && (random.next() & 1U) != 0U) {
      flags |= kW;
    }
    if ((random.next() & 1U) != 0U) {
      flags |= kX;
    }
    if ((flags & (kR | kX)) == 0U) {
      flags |= kR;
    }
    if ((random.next() & 1U) != 0U) {
      flags |= kU;
    }
    if ((random.next() & 1U) != 0U) {
      flags |= kD;
    }
    const auto leaf = leaf_pte(physical_base, flags);
    fixture.install_path(virtual_address, level, leaf);
    entries[kRoot + indices[2] * 8U] =
        level == 2 ? leaf : table_pte(kLevelOne);
    if (level < 2) {
      entries[kLevelOne + indices[1] * 8U] =
          level == 1 ? leaf : table_pte(kLevelZero);
    }
    if (level == 0) {
      entries[kLevelZero + indices[0] * 8U] = leaf;
    }

    auto context = fixture.context(access, privilege);
    context.sum = (random.next() & 1U) != 0U;
    context.mxr = (random.next() & 1U) != 0U;
    const auto expected = reference_walk(entries, virtual_address, context);
    const auto actual = walk_sv39(fixture.bus_, virtual_address, context);
    if (expected.success) {
      const auto observed = translation(actual);
      CHECK_EQ(observed.physical_address, expected.physical_address);
      CHECK_EQ(observed.leaf_level, expected.leaf_level);
    } else {
      const auto observed = walk_fault(actual);
      CHECK(observed.reason == expected.reason);
      CHECK_EQ(observed.level, expected.leaf_level);
      CHECK_EQ(observed.virtual_address, virtual_address);
    }
  }
}
