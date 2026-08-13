#pragma once

#include "northstar64/privilege.hpp"
#include "northstar64/trap.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace northstar64 {

namespace csr {
constexpr std::uint16_t kSstatus = 0x100;
constexpr std::uint16_t kSie = 0x104;
constexpr std::uint16_t kStvec = 0x105;
constexpr std::uint16_t kScounteren = 0x106;
constexpr std::uint16_t kSscratch = 0x140;
constexpr std::uint16_t kSepc = 0x141;
constexpr std::uint16_t kScause = 0x142;
constexpr std::uint16_t kStval = 0x143;
constexpr std::uint16_t kSip = 0x144;
constexpr std::uint16_t kSatp = 0x180;

constexpr std::uint16_t kMstatus = 0x300;
constexpr std::uint16_t kMisa = 0x301;
constexpr std::uint16_t kMedeleg = 0x302;
constexpr std::uint16_t kMideleg = 0x303;
constexpr std::uint16_t kMie = 0x304;
constexpr std::uint16_t kMtvec = 0x305;
constexpr std::uint16_t kMcounteren = 0x306;
constexpr std::uint16_t kMscratch = 0x340;
constexpr std::uint16_t kMepc = 0x341;
constexpr std::uint16_t kMcause = 0x342;
constexpr std::uint16_t kMtval = 0x343;
constexpr std::uint16_t kMip = 0x344;
constexpr std::uint16_t kMcycle = 0xB00;
constexpr std::uint16_t kMinstret = 0xB02;
constexpr std::uint16_t kCycle = 0xC00;
constexpr std::uint16_t kInstret = 0xC02;
constexpr std::uint16_t kMhartid = 0xF14;
} // namespace csr

namespace status {
constexpr std::uint64_t kSie = std::uint64_t{1} << 1U;
constexpr std::uint64_t kMie = std::uint64_t{1} << 3U;
constexpr std::uint64_t kSpie = std::uint64_t{1} << 5U;
constexpr std::uint64_t kMpie = std::uint64_t{1} << 7U;
constexpr std::uint64_t kSpp = std::uint64_t{1} << 8U;
constexpr std::uint64_t kMppMask = std::uint64_t{3} << 11U;
constexpr std::uint64_t kMprv = std::uint64_t{1} << 17U;
constexpr std::uint64_t kSum = std::uint64_t{1} << 18U;
constexpr std::uint64_t kMxr = std::uint64_t{1} << 19U;
constexpr std::uint64_t kUxlMask = std::uint64_t{3} << 32U;
constexpr std::uint64_t kSxlMask = std::uint64_t{3} << 34U;
constexpr std::uint64_t kUxl64 = std::uint64_t{2} << 32U;
constexpr std::uint64_t kSxl64 = std::uint64_t{2} << 34U;
constexpr std::uint64_t kSstatusWritableMask =
    kSie | kSpie | kSpp | kSum | kMxr;
constexpr std::uint64_t kMstatusWritableMask =
    kSie | kMie | kSpie | kMpie | kSpp | kMppMask | kMprv | kSum | kMxr;
constexpr std::uint64_t kMstatusFixedValue = kUxl64 | kSxl64;
} // namespace status

enum class CsrErrorKind {
  Unimplemented,
  PrivilegeViolation,
  ReadOnly,
  InvalidValue,
  CounterDisabled,
};

struct CsrError {
  CsrErrorKind kind{};
  std::uint16_t address{};
  PrivilegeLevel privilege{PrivilegeLevel::Machine};
  std::string detail;

  friend bool operator==(const CsrError&, const CsrError&) = default;
};

using CsrReadResult = std::variant<std::uint64_t, CsrError>;

struct TrapEntry {
  PrivilegeLevel target{PrivilegeLevel::Machine};
  Address vector{};

  friend bool operator==(const TrapEntry&, const TrapEntry&) = default;
};

enum class TrapReturnMode {
  Supervisor,
  Machine,
};

struct TrapReturn {
  PrivilegeLevel target{PrivilegeLevel::Machine};
  Address pc{};

  friend bool operator==(const TrapReturn&, const TrapReturn&) = default;
};

constexpr PrivilegeLevel csr_minimum_privilege(std::uint16_t address) noexcept {
  const auto encoded = static_cast<std::uint8_t>((address >> 8U) & 0x3U);
  if (encoded == 0U) {
    return PrivilegeLevel::User;
  }
  if (encoded == 1U) {
    return PrivilegeLevel::Supervisor;
  }
  return PrivilegeLevel::Machine;
}

constexpr bool csr_is_read_only(std::uint16_t address) noexcept {
  return ((address >> 10U) & 0x3U) == 0x3U;
}

class CsrFile {
public:
  CsrFile();

  void reset();
  [[nodiscard]] CsrReadResult read(std::uint16_t address,
                                   PrivilegeLevel privilege = PrivilegeLevel::Machine) const;
  [[nodiscard]] std::optional<CsrError>
  write(std::uint16_t address, std::uint64_t value,
        PrivilegeLevel privilege = PrivilegeLevel::Machine);

  void tick_cycle() noexcept { ++mcycle_; }
  void retire_instruction() noexcept { ++minstret_; }
  [[nodiscard]] TrapEntry enter_trap(
      const Trap& trap, PrivilegeLevel origin = PrivilegeLevel::Machine);
  [[nodiscard]] TrapReturn return_from_trap(TrapReturnMode mode);
  [[nodiscard]] std::uint64_t cycle_count() const noexcept { return mcycle_; }
  [[nodiscard]] std::uint64_t retired_count() const noexcept { return minstret_; }

private:
  [[nodiscard]] std::optional<CsrError> validate_access(std::uint16_t address,
                                                       PrivilegeLevel privilege,
                                                       bool write) const;

  std::uint64_t mstatus_{};
  std::uint64_t medeleg_{};
  std::uint64_t mideleg_{};
  std::uint64_t mie_{};
  std::uint64_t mtvec_{};
  std::uint64_t mcounteren_{};
  std::uint64_t mscratch_{};
  std::uint64_t mepc_{};
  std::uint64_t mcause_{};
  std::uint64_t mtval_{};
  std::uint64_t mip_{};

  std::uint64_t stvec_{};
  std::uint64_t scounteren_{};
  std::uint64_t sscratch_{};
  std::uint64_t sepc_{};
  std::uint64_t scause_{};
  std::uint64_t stval_{};
  std::uint64_t satp_{};

  std::uint64_t mcycle_{};
  std::uint64_t minstret_{};
};

} // namespace northstar64
