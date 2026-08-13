#pragma once

#include "northstar64/csr.hpp"
#include "northstar64/memory.hpp"
#include "northstar64/sv39.hpp"
#include "northstar64/trace.hpp"
#include "northstar64/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace northstar64 {

enum class TrapPolicy {
  Halt,
  Vector,
  VectorToMtvec = Vector,
};

struct CpuConfig {
  TrapPolicy trap_policy{TrapPolicy::Halt};
  bool halt_on_ebreak{true};
  bool halt_on_wfi{true};
};

enum class RunStopReason { Halted, StepLimit };

struct RunResult {
  RunStopReason reason{};
  std::uint64_t attempted_steps{};
  std::uint64_t retired_instructions{};
  std::string detail;
  std::optional<Trap> terminal_trap;
};

class Cpu {
public:
  explicit Cpu(Bus& bus, CpuConfig config = {});

  void reset(Address entry);
  [[nodiscard]] StepRecord step();
  [[nodiscard]] RunResult run(std::uint64_t maximum_steps, TraceSink* trace = nullptr);

  [[nodiscard]] RegisterValue reg(std::size_t index) const;
  void set_reg(std::size_t index, RegisterValue value);
  [[nodiscard]] Address pc() const noexcept { return pc_; }
  void set_pc(Address value) noexcept { pc_ = value; }
  [[nodiscard]] PrivilegeLevel privilege() const noexcept { return privilege_; }
  void set_privilege(PrivilegeLevel privilege) noexcept { privilege_ = privilege; }
  [[nodiscard]] bool halted() const noexcept { return halted_; }
  [[nodiscard]] const std::string& halt_detail() const noexcept { return halt_detail_; }
  [[nodiscard]] CsrFile& csrs() noexcept { return csrs_; }
  [[nodiscard]] const CsrFile& csrs() const noexcept { return csrs_; }

private:
  [[nodiscard]] std::optional<Address> translate(StepRecord& record, Address address,
                                                  VirtualAccess access);
  void write_register(StepRecord& record, std::uint8_t index, std::uint64_t value);
  void raise_trap(StepRecord& record, TrapCause cause, std::uint64_t value,
                  std::string detail);
  [[nodiscard]] std::optional<std::uint64_t> load(StepRecord& record, Address address,
                                                  std::size_t width, bool sign_extend_value);
  [[nodiscard]] bool store(StepRecord& record, Address address, std::size_t width,
                           std::uint64_t value);
  void execute(StepRecord& record, const DecodedInstruction& instruction);

  Bus& bus_;
  CpuConfig config_;
  std::array<RegisterValue, kRegisterCount> registers_{};
  Address pc_{};
  PrivilegeLevel privilege_{PrivilegeLevel::Machine};
  CsrFile csrs_;
  std::uint64_t sequence_{};
  bool halted_{};
  std::string halt_detail_;
  std::optional<Trap> terminal_trap_;
};

} // namespace northstar64
