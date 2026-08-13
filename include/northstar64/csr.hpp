#pragma once

#include "northstar64/trap.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace northstar64 {

namespace csr {
constexpr std::uint16_t kMstatus = 0x300;
constexpr std::uint16_t kMisa = 0x301;
constexpr std::uint16_t kMedeleg = 0x302;
constexpr std::uint16_t kMie = 0x304;
constexpr std::uint16_t kMtvec = 0x305;
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

struct CsrError {
  std::uint16_t address{};
  std::string detail;
};

using CsrReadResult = std::variant<std::uint64_t, CsrError>;

class CsrFile {
public:
  CsrFile();

  void reset();
  [[nodiscard]] CsrReadResult read(std::uint16_t address) const;
  [[nodiscard]] std::optional<CsrError> write(std::uint16_t address, std::uint64_t value);

  void tick_cycle() noexcept { ++mcycle_; }
  void retire_instruction() noexcept { ++minstret_; }
  void enter_trap(const Trap& trap);
  [[nodiscard]] Address return_from_trap();

  [[nodiscard]] Address trap_vector() const noexcept { return mtvec_ & ~std::uint64_t{0x3}; }
  [[nodiscard]] std::uint64_t cycle_count() const noexcept { return mcycle_; }
  [[nodiscard]] std::uint64_t retired_count() const noexcept { return minstret_; }

private:
  std::uint64_t mstatus_{};
  std::uint64_t medeleg_{};
  std::uint64_t mie_{};
  std::uint64_t mtvec_{};
  std::uint64_t mscratch_{};
  std::uint64_t mepc_{};
  std::uint64_t mcause_{};
  std::uint64_t mtval_{};
  std::uint64_t mip_{};
  std::uint64_t mcycle_{};
  std::uint64_t minstret_{};
};

} // namespace northstar64

