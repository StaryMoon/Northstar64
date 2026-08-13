#include "northstar64/cpu.hpp"

#include "northstar64/decode.hpp"

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>

namespace northstar64 {
namespace {

std::int64_t signed_value(std::uint64_t value) noexcept {
  return std::bit_cast<std::int64_t>(value);
}

std::uint64_t arithmetic_shift_right(std::uint64_t value, unsigned amount) noexcept {
  amount &= 63U;
  if (amount == 0U) {
    return value;
  }
  const auto shifted = value >> amount;
  if ((value & (std::uint64_t{1} << 63U)) == 0U) {
    return shifted;
  }
  return shifted | (~std::uint64_t{0} << (64U - amount));
}

std::uint32_t arithmetic_shift_right_word(std::uint32_t value, unsigned amount) noexcept {
  amount &= 31U;
  if (amount == 0U) {
    return value;
  }
  const auto shifted = value >> amount;
  if ((value & (std::uint32_t{1} << 31U)) == 0U) {
    return shifted;
  }
  return shifted | (~std::uint32_t{0} << (32U - amount));
}

std::uint64_t sign_extend_word(std::uint64_t value) noexcept {
  const auto word = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(word));
}

Address add_offset(Address base, std::int64_t offset) noexcept {
  return base + static_cast<std::uint64_t>(offset);
}

std::size_t load_width(Operation operation) {
  switch (operation) {
  case Operation::Lb:
  case Operation::Lbu:
    return 1;
  case Operation::Lh:
  case Operation::Lhu:
    return 2;
  case Operation::Lw:
  case Operation::Lwu:
    return 4;
  case Operation::Ld:
    return 8;
  default:
    throw std::logic_error("operation is not a load");
  }
}

std::size_t store_width(Operation operation) {
  switch (operation) {
  case Operation::Sb:
    return 1;
  case Operation::Sh:
    return 2;
  case Operation::Sw:
    return 4;
  case Operation::Sd:
    return 8;
  default:
    throw std::logic_error("operation is not a store");
  }
}

bool is_unsigned_load(Operation operation) noexcept {
  return operation == Operation::Lbu || operation == Operation::Lhu ||
         operation == Operation::Lwu;
}

} // namespace

Cpu::Cpu(Bus& bus, CpuConfig config) : bus_(bus), config_(config) { reset(0); }

void Cpu::reset(Address entry) {
  registers_.fill(0);
  pc_ = entry;
  privilege_ = PrivilegeLevel::Machine;
  csrs_.reset();
  sequence_ = 0;
  halted_ = false;
  halt_detail_.clear();
  terminal_trap_.reset();
}

RegisterValue Cpu::reg(std::size_t index) const {
  if (index >= registers_.size()) {
    throw std::out_of_range("RISC-V register index exceeds x31");
  }
  return registers_[index];
}

void Cpu::set_reg(std::size_t index, RegisterValue value) {
  if (index >= registers_.size()) {
    throw std::out_of_range("RISC-V register index exceeds x31");
  }
  if (index != 0U) {
    registers_[index] = value;
  }
}

void Cpu::write_register(StepRecord& record, std::uint8_t index, std::uint64_t value) {
  if (index == 0U) {
    return;
  }
  registers_[index] = value;
  record.register_write = RegisterWrite{index, value};
}

void Cpu::raise_trap(StepRecord& record, TrapCause cause, std::uint64_t value,
                     std::string detail) {
  Trap trap{cause, record.pc, value, std::move(detail)};
  record.trap = trap;
  record.retired = false;
  const auto entry = csrs_.enter_trap(trap, privilege_);
  privilege_ = entry.target;

  if (config_.trap_policy == TrapPolicy::Vector) {
    pc_ = entry.vector;
  } else {
    pc_ = trap.pc;
    halted_ = true;
    halt_detail_ = std::string(trap_name(cause)) + ": " + trap.detail;
    terminal_trap_ = trap;
  }
  record.next_pc = pc_;
  record.next_privilege = privilege_;
  record.halted = halted_;
}

std::optional<std::uint64_t> Cpu::load(StepRecord& record, Address address,
                                       std::size_t width, bool sign_extend_value) {
  if (width > 1U && address % width != 0U) {
    raise_trap(record, TrapCause::LoadAddressMisaligned, address,
               "load address is not naturally aligned");
    return std::nullopt;
  }
  auto result = bus_.read(address, width, AccessKind::Load);
  if (const auto* fault = std::get_if<BusFault>(&result)) {
    raise_trap(record, TrapCause::LoadAccessFault, address, fault->detail);
    return std::nullopt;
  }
  auto value = std::get<std::uint64_t>(result);
  if (sign_extend_value && width < 8U) {
    value = static_cast<std::uint64_t>(
        sign_extend<std::int64_t>(value, static_cast<unsigned>(width * 8U)));
  }
  return value;
}

bool Cpu::store(StepRecord& record, Address address, std::size_t width, std::uint64_t value) {
  if (width > 1U && address % width != 0U) {
    raise_trap(record, TrapCause::StoreAddressMisaligned, address,
               "store address is not naturally aligned");
    return false;
  }
  value &= mask_for_width(width);
  if (auto fault = bus_.write(address, width, value, AccessKind::Store)) {
    raise_trap(record, TrapCause::StoreAccessFault, address, fault->detail);
    return false;
  }
  record.memory_write = MemoryWrite{address, width, value};
  return true;
}

void Cpu::execute(StepRecord& record, const DecodedInstruction& instruction) {
  const auto lhs = reg(instruction.rs1);
  const auto rhs = reg(instruction.rs2);
  const auto sequential_pc = record.pc + kInstructionBytes;
  pc_ = sequential_pc;

  const auto branch = [&](bool taken) {
    if (!taken) {
      return;
    }
    const auto target = add_offset(record.pc, instruction.immediate);
    if (target % kInstructionBytes != 0U) {
      raise_trap(record, TrapCause::InstructionAddressMisaligned, target,
                 "taken branch target is not four-byte aligned");
      return;
    }
    pc_ = target;
  };

  const auto jump = [&](Address target) {
    if (target % kInstructionBytes != 0U) {
      raise_trap(record, TrapCause::InstructionAddressMisaligned, target,
                 "jump target is not four-byte aligned");
      return false;
    }
    write_register(record, instruction.rd, sequential_pc);
    pc_ = target;
    return true;
  };

  switch (instruction.operation) {
  case Operation::Lui:
    write_register(record, instruction.rd, static_cast<std::uint64_t>(instruction.immediate));
    break;
  case Operation::Auipc:
    write_register(record, instruction.rd, add_offset(record.pc, instruction.immediate));
    break;
  case Operation::Jal:
    if (!jump(add_offset(record.pc, instruction.immediate))) {
      return;
    }
    break;
  case Operation::Jalr:
    if (!jump(add_offset(lhs, instruction.immediate) & ~std::uint64_t{1})) {
      return;
    }
    break;
  case Operation::Beq:
    branch(lhs == rhs);
    break;
  case Operation::Bne:
    branch(lhs != rhs);
    break;
  case Operation::Blt:
    branch(signed_value(lhs) < signed_value(rhs));
    break;
  case Operation::Bge:
    branch(signed_value(lhs) >= signed_value(rhs));
    break;
  case Operation::Bltu:
    branch(lhs < rhs);
    break;
  case Operation::Bgeu:
    branch(lhs >= rhs);
    break;
  case Operation::Lb:
  case Operation::Lh:
  case Operation::Lw:
  case Operation::Ld:
  case Operation::Lbu:
  case Operation::Lhu:
  case Operation::Lwu: {
    const auto width = load_width(instruction.operation);
    auto value = load(record, add_offset(lhs, instruction.immediate), width,
                      !is_unsigned_load(instruction.operation));
    if (!value) {
      return;
    }
    write_register(record, instruction.rd, *value);
    break;
  }
  case Operation::Sb:
  case Operation::Sh:
  case Operation::Sw:
  case Operation::Sd:
    if (!store(record, add_offset(lhs, instruction.immediate), store_width(instruction.operation),
               rhs)) {
      return;
    }
    break;
  case Operation::Addi:
    write_register(record, instruction.rd, add_offset(lhs, instruction.immediate));
    break;
  case Operation::Slti:
    write_register(record, instruction.rd,
                   signed_value(lhs) < instruction.immediate ? 1U : 0U);
    break;
  case Operation::Sltiu:
    write_register(record, instruction.rd,
                   lhs < static_cast<std::uint64_t>(instruction.immediate) ? 1U : 0U);
    break;
  case Operation::Xori:
    write_register(record, instruction.rd,
                   lhs ^ static_cast<std::uint64_t>(instruction.immediate));
    break;
  case Operation::Ori:
    write_register(record, instruction.rd,
                   lhs | static_cast<std::uint64_t>(instruction.immediate));
    break;
  case Operation::Andi:
    write_register(record, instruction.rd,
                   lhs & static_cast<std::uint64_t>(instruction.immediate));
    break;
  case Operation::Slli:
    write_register(record, instruction.rd, lhs << static_cast<unsigned>(instruction.immediate));
    break;
  case Operation::Srli:
    write_register(record, instruction.rd, lhs >> static_cast<unsigned>(instruction.immediate));
    break;
  case Operation::Srai:
    write_register(record, instruction.rd,
                   arithmetic_shift_right(lhs, static_cast<unsigned>(instruction.immediate)));
    break;
  case Operation::Add:
    write_register(record, instruction.rd, lhs + rhs);
    break;
  case Operation::Sub:
    write_register(record, instruction.rd, lhs - rhs);
    break;
  case Operation::Sll:
    write_register(record, instruction.rd, lhs << static_cast<unsigned>(rhs & 0x3fU));
    break;
  case Operation::Slt:
    write_register(record, instruction.rd, signed_value(lhs) < signed_value(rhs) ? 1U : 0U);
    break;
  case Operation::Sltu:
    write_register(record, instruction.rd, lhs < rhs ? 1U : 0U);
    break;
  case Operation::Xor:
    write_register(record, instruction.rd, lhs ^ rhs);
    break;
  case Operation::Srl:
    write_register(record, instruction.rd, lhs >> static_cast<unsigned>(rhs & 0x3fU));
    break;
  case Operation::Sra:
    write_register(record, instruction.rd,
                   arithmetic_shift_right(lhs, static_cast<unsigned>(rhs & 0x3fU)));
    break;
  case Operation::Or:
    write_register(record, instruction.rd, lhs | rhs);
    break;
  case Operation::And:
    write_register(record, instruction.rd, lhs & rhs);
    break;
  case Operation::Addiw:
    write_register(record, instruction.rd,
                   sign_extend_word(lhs + static_cast<std::uint64_t>(instruction.immediate)));
    break;
  case Operation::Slliw:
    write_register(record, instruction.rd,
                   sign_extend_word(static_cast<std::uint32_t>(lhs) <<
                                    static_cast<unsigned>(instruction.immediate)));
    break;
  case Operation::Srliw:
    write_register(record, instruction.rd,
                   sign_extend_word(static_cast<std::uint32_t>(lhs) >>
                                    static_cast<unsigned>(instruction.immediate)));
    break;
  case Operation::Sraiw:
    write_register(record, instruction.rd,
                   sign_extend_word(arithmetic_shift_right_word(
                       static_cast<std::uint32_t>(lhs),
                       static_cast<unsigned>(instruction.immediate))));
    break;
  case Operation::Addw:
    write_register(record, instruction.rd,
                   sign_extend_word(static_cast<std::uint32_t>(lhs) +
                                    static_cast<std::uint32_t>(rhs)));
    break;
  case Operation::Subw:
    write_register(record, instruction.rd,
                   sign_extend_word(static_cast<std::uint32_t>(lhs) -
                                    static_cast<std::uint32_t>(rhs)));
    break;
  case Operation::Sllw:
    write_register(record, instruction.rd,
                   sign_extend_word(static_cast<std::uint32_t>(lhs) <<
                                    static_cast<unsigned>(rhs & 0x1fU)));
    break;
  case Operation::Srlw:
    write_register(record, instruction.rd,
                   sign_extend_word(static_cast<std::uint32_t>(lhs) >>
                                    static_cast<unsigned>(rhs & 0x1fU)));
    break;
  case Operation::Sraw:
    write_register(record, instruction.rd,
                   sign_extend_word(arithmetic_shift_right_word(
                       static_cast<std::uint32_t>(lhs), static_cast<unsigned>(rhs & 0x1fU))));
    break;
  case Operation::Fence:
  case Operation::FenceI:
    break;
  case Operation::Ecall:
    if (privilege_ == PrivilegeLevel::User) {
      raise_trap(record, TrapCause::EnvironmentCallFromUserMode, 0,
                 "environment call executed in user mode");
    } else if (privilege_ == PrivilegeLevel::Supervisor) {
      raise_trap(record, TrapCause::EnvironmentCallFromSupervisorMode, 0,
                 "environment call executed in supervisor mode");
    } else {
      raise_trap(record, TrapCause::EnvironmentCallFromMachineMode, 0,
                 "environment call executed in machine mode");
    }
    return;
  case Operation::Ebreak:
    raise_trap(record, TrapCause::Breakpoint, 0, "EBREAK requested a debugger trap");
    if (config_.halt_on_ebreak && config_.trap_policy == TrapPolicy::Vector) {
      halted_ = true;
      pc_ = record.pc;
      halt_detail_ = "breakpoint: EBREAK requested a debugger trap";
      terminal_trap_ = record.trap;
      record.next_pc = pc_;
      record.halted = true;
    }
    return;
  case Operation::Sret: {
    if (privilege_ == PrivilegeLevel::User) {
      raise_trap(record, TrapCause::IllegalInstruction, instruction.raw,
                 "SRET requires supervisor or machine privilege");
      return;
    }
    const auto transition = csrs_.return_from_trap(TrapReturnMode::Supervisor);
    pc_ = transition.pc;
    privilege_ = transition.target;
    break;
  }
  case Operation::Mret:
    if (privilege_ != PrivilegeLevel::Machine) {
      raise_trap(record, TrapCause::IllegalInstruction, instruction.raw,
                 "MRET requires machine privilege");
      return;
    }
    {
      const auto transition = csrs_.return_from_trap(TrapReturnMode::Machine);
      pc_ = transition.pc;
      privilege_ = transition.target;
    }
    break;
  case Operation::Wfi:
    if (config_.halt_on_wfi) {
      halted_ = true;
      halt_detail_ = "WFI reached with no interrupt source configured";
      record.halted = true;
    }
    break;
  case Operation::Csrrw:
  case Operation::Csrrs:
  case Operation::Csrrc:
  case Operation::Csrrwi:
  case Operation::Csrrsi:
  case Operation::Csrrci: {
    auto old_result = csrs_.read(instruction.csr, privilege_);
    if (const auto* error = std::get_if<CsrError>(&old_result)) {
      raise_trap(record, TrapCause::IllegalInstruction, instruction.raw, error->detail);
      return;
    }
    const auto old_value = std::get<std::uint64_t>(old_result);
    const bool immediate_form = instruction.operation == Operation::Csrrwi ||
                                instruction.operation == Operation::Csrrsi ||
                                instruction.operation == Operation::Csrrci;
    const auto operand = immediate_form ? static_cast<std::uint64_t>(instruction.rs1) : lhs;
    std::uint64_t new_value = operand;
    bool should_write = true;
    if (instruction.operation == Operation::Csrrs || instruction.operation == Operation::Csrrsi) {
      new_value = old_value | operand;
      should_write = operand != 0U;
    } else if (instruction.operation == Operation::Csrrc ||
               instruction.operation == Operation::Csrrci) {
      new_value = old_value & ~operand;
      should_write = operand != 0U;
    }
    if (should_write) {
      if (auto error = csrs_.write(instruction.csr, new_value, privilege_)) {
        raise_trap(record, TrapCause::IllegalInstruction, instruction.raw, error->detail);
        return;
      }
    }
    write_register(record, instruction.rd, old_value);
    break;
  }
  }

  if (!record.trap) {
    record.retired = true;
    record.next_pc = pc_;
    record.next_privilege = privilege_;
    record.halted = halted_;
  }
}

StepRecord Cpu::step() {
  StepRecord record;
  record.sequence = sequence_++;
  record.pc = pc_;
  record.privilege = privilege_;
  record.next_pc = pc_;
  record.next_privilege = privilege_;
  record.halted = halted_;

  if (halted_) {
    return record;
  }

  csrs_.tick_cycle();
  if (pc_ % kInstructionBytes != 0U) {
    raise_trap(record, TrapCause::InstructionAddressMisaligned, pc_,
               "program counter is not four-byte aligned");
    return record;
  }

  auto fetched = bus_.read(pc_, kInstructionBytes, AccessKind::InstructionFetch);
  if (const auto* fault = std::get_if<BusFault>(&fetched)) {
    raise_trap(record, TrapCause::InstructionAccessFault, pc_, fault->detail);
    return record;
  }
  record.instruction = static_cast<std::uint32_t>(std::get<std::uint64_t>(fetched));

  auto decoded = decode(record.instruction);
  if (const auto* error = std::get_if<DecodeError>(&decoded)) {
    record.assembly = ".word " + std::to_string(record.instruction);
    raise_trap(record, TrapCause::IllegalInstruction, record.instruction, error->detail);
    return record;
  }

  const auto& decoded_instruction = std::get<DecodedInstruction>(decoded);
  record.assembly = disassemble(decoded_instruction);
  execute(record, decoded_instruction);
  registers_[0] = 0;
  if (record.retired) {
    csrs_.retire_instruction();
  }
  return record;
}

RunResult Cpu::run(std::uint64_t maximum_steps, TraceSink* trace) {
  std::uint64_t attempted = 0;
  while (!halted_ && attempted < maximum_steps) {
    auto record = step();
    ++attempted;
    if (trace != nullptr) {
      trace->append(record);
    }
  }
  return RunResult{halted_ ? RunStopReason::Halted : RunStopReason::StepLimit,
                   attempted,
                   csrs_.retired_count(),
                   halted_ ? halt_detail_ : "maximum step count reached",
                   terminal_trap_};
}

} // namespace northstar64
