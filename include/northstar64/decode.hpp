#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace northstar64 {

enum class Operation {
  Lui,
  Auipc,
  Jal,
  Jalr,
  Beq,
  Bne,
  Blt,
  Bge,
  Bltu,
  Bgeu,
  Lb,
  Lh,
  Lw,
  Ld,
  Lbu,
  Lhu,
  Lwu,
  Sb,
  Sh,
  Sw,
  Sd,
  Addi,
  Slti,
  Sltiu,
  Xori,
  Ori,
  Andi,
  Slli,
  Srli,
  Srai,
  Add,
  Sub,
  Sll,
  Slt,
  Sltu,
  Xor,
  Srl,
  Sra,
  Or,
  And,
  Addiw,
  Slliw,
  Srliw,
  Sraiw,
  Addw,
  Subw,
  Sllw,
  Srlw,
  Sraw,
  Fence,
  FenceI,
  Ecall,
  Ebreak,
  Mret,
  Wfi,
  Csrrw,
  Csrrs,
  Csrrc,
  Csrrwi,
  Csrrsi,
  Csrrci,
};

struct DecodedInstruction {
  Operation operation{};
  std::uint32_t raw{};
  std::uint8_t rd{};
  std::uint8_t rs1{};
  std::uint8_t rs2{};
  std::int64_t immediate{};
  std::uint16_t csr{};

  friend bool operator==(const DecodedInstruction&, const DecodedInstruction&) = default;
};

struct DecodeError {
  std::uint32_t raw{};
  std::string detail;
};

using DecodeResult = std::variant<DecodedInstruction, DecodeError>;

DecodeResult decode(std::uint32_t instruction);
std::string disassemble(const DecodedInstruction& instruction);
const char* operation_name(Operation operation) noexcept;

} // namespace northstar64

