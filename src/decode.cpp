#include "northstar64/decode.hpp"

#include "northstar64/types.hpp"

#include <iomanip>
#include <sstream>

namespace northstar64 {
namespace {

std::uint8_t field(std::uint32_t raw, unsigned shift, std::uint32_t mask) {
  return static_cast<std::uint8_t>((raw >> shift) & mask);
}

std::int64_t immediate_i(std::uint32_t raw) {
  return sign_extend<std::int64_t>(raw >> 20U, 12U);
}

std::int64_t immediate_s(std::uint32_t raw) {
  const auto value = ((raw >> 25U) << 5U) | ((raw >> 7U) & 0x1fU);
  return sign_extend<std::int64_t>(value, 12U);
}

std::int64_t immediate_b(std::uint32_t raw) {
  const auto value = ((raw >> 31U) << 12U) | (((raw >> 7U) & 0x1U) << 11U) |
                     (((raw >> 25U) & 0x3fU) << 5U) | (((raw >> 8U) & 0xfU) << 1U);
  return sign_extend<std::int64_t>(value, 13U);
}

std::int64_t immediate_u(std::uint32_t raw) {
  return sign_extend<std::int64_t>(raw & 0xfffff000U, 32U);
}

std::int64_t immediate_j(std::uint32_t raw) {
  const auto value = ((raw >> 31U) << 20U) | (((raw >> 12U) & 0xffU) << 12U) |
                     (((raw >> 20U) & 0x1U) << 11U) | (((raw >> 21U) & 0x3ffU) << 1U);
  return sign_extend<std::int64_t>(value, 21U);
}

DecodeResult error(std::uint32_t raw, std::string detail) {
  return DecodeError{raw, std::move(detail)};
}

DecodedInstruction instruction(std::uint32_t raw, Operation operation, std::int64_t immediate = 0) {
  return DecodedInstruction{operation,
                            raw,
                            field(raw, 7U, 0x1fU),
                            field(raw, 15U, 0x1fU),
                            field(raw, 20U, 0x1fU),
                            immediate,
                            static_cast<std::uint16_t>((raw >> 20U) & 0xfffU)};
}

DecodeResult decode_branch(std::uint32_t raw, std::uint8_t funct3) {
  switch (funct3) {
  case 0:
    return instruction(raw, Operation::Beq, immediate_b(raw));
  case 1:
    return instruction(raw, Operation::Bne, immediate_b(raw));
  case 4:
    return instruction(raw, Operation::Blt, immediate_b(raw));
  case 5:
    return instruction(raw, Operation::Bge, immediate_b(raw));
  case 6:
    return instruction(raw, Operation::Bltu, immediate_b(raw));
  case 7:
    return instruction(raw, Operation::Bgeu, immediate_b(raw));
  default:
    return error(raw, "reserved branch funct3");
  }
}

DecodeResult decode_load(std::uint32_t raw, std::uint8_t funct3) {
  constexpr Operation operations[] = {Operation::Lb,  Operation::Lh,  Operation::Lw,
                                      Operation::Ld,  Operation::Lbu, Operation::Lhu,
                                      Operation::Lwu};
  if (funct3 > 6U) {
    return error(raw, "reserved load funct3");
  }
  return instruction(raw, operations[funct3], immediate_i(raw));
}

DecodeResult decode_store(std::uint32_t raw, std::uint8_t funct3) {
  constexpr Operation operations[] = {Operation::Sb, Operation::Sh, Operation::Sw, Operation::Sd};
  if (funct3 > 3U) {
    return error(raw, "reserved store funct3");
  }
  return instruction(raw, operations[funct3], immediate_s(raw));
}

DecodeResult decode_op_imm(std::uint32_t raw, std::uint8_t funct3) {
  switch (funct3) {
  case 0:
    return instruction(raw, Operation::Addi, immediate_i(raw));
  case 2:
    return instruction(raw, Operation::Slti, immediate_i(raw));
  case 3:
    return instruction(raw, Operation::Sltiu, immediate_i(raw));
  case 4:
    return instruction(raw, Operation::Xori, immediate_i(raw));
  case 6:
    return instruction(raw, Operation::Ori, immediate_i(raw));
  case 7:
    return instruction(raw, Operation::Andi, immediate_i(raw));
  case 1:
    if ((raw >> 26U) != 0U) {
      return error(raw, "reserved SLLI encoding");
    }
    return instruction(raw, Operation::Slli, static_cast<std::int64_t>((raw >> 20U) & 0x3fU));
  case 5: {
    const auto funct6 = (raw >> 26U) & 0x3fU;
    if (funct6 == 0U) {
      return instruction(raw, Operation::Srli, static_cast<std::int64_t>((raw >> 20U) & 0x3fU));
    }
    if (funct6 == 0x10U) {
      return instruction(raw, Operation::Srai, static_cast<std::int64_t>((raw >> 20U) & 0x3fU));
    }
    return error(raw, "reserved right-shift immediate encoding");
  }
  default:
    return error(raw, "reserved OP-IMM funct3");
  }
}

DecodeResult decode_op(std::uint32_t raw, std::uint8_t funct3) {
  const auto funct7 = (raw >> 25U) & 0x7fU;
  if (funct7 == 0x20U) {
    if (funct3 == 0U) {
      return instruction(raw, Operation::Sub);
    }
    if (funct3 == 5U) {
      return instruction(raw, Operation::Sra);
    }
    return error(raw, "reserved alternate OP encoding");
  }
  if (funct7 != 0U) {
    return error(raw, "unsupported OP extension encoding");
  }
  constexpr Operation operations[] = {Operation::Add,  Operation::Sll, Operation::Slt,
                                      Operation::Sltu, Operation::Xor, Operation::Srl,
                                      Operation::Or,   Operation::And};
  return instruction(raw, operations[funct3]);
}

DecodeResult decode_op_imm_32(std::uint32_t raw, std::uint8_t funct3) {
  if (funct3 == 0U) {
    return instruction(raw, Operation::Addiw, immediate_i(raw));
  }
  if (funct3 == 1U && (raw >> 25U) == 0U) {
    return instruction(raw, Operation::Slliw, static_cast<std::int64_t>((raw >> 20U) & 0x1fU));
  }
  if (funct3 == 5U) {
    const auto funct7 = raw >> 25U;
    if (funct7 == 0U) {
      return instruction(raw, Operation::Srliw, static_cast<std::int64_t>((raw >> 20U) & 0x1fU));
    }
    if (funct7 == 0x20U) {
      return instruction(raw, Operation::Sraiw, static_cast<std::int64_t>((raw >> 20U) & 0x1fU));
    }
  }
  return error(raw, "reserved OP-IMM-32 encoding");
}

DecodeResult decode_op_32(std::uint32_t raw, std::uint8_t funct3) {
  const auto funct7 = raw >> 25U;
  if (funct7 == 0x20U) {
    if (funct3 == 0U) {
      return instruction(raw, Operation::Subw);
    }
    if (funct3 == 5U) {
      return instruction(raw, Operation::Sraw);
    }
    return error(raw, "reserved alternate OP-32 encoding");
  }
  if (funct7 != 0U) {
    return error(raw, "unsupported OP-32 extension encoding");
  }
  switch (funct3) {
  case 0:
    return instruction(raw, Operation::Addw);
  case 1:
    return instruction(raw, Operation::Sllw);
  case 5:
    return instruction(raw, Operation::Srlw);
  default:
    return error(raw, "reserved OP-32 funct3");
  }
}

DecodeResult decode_system(std::uint32_t raw, std::uint8_t funct3) {
  if (funct3 == 0U) {
    if ((raw & 0xfe007fffU) == 0x12000073U) {
      return instruction(raw, Operation::SfenceVma);
    }
    switch (raw) {
    case 0x00000073U:
      return instruction(raw, Operation::Ecall);
    case 0x00100073U:
      return instruction(raw, Operation::Ebreak);
    case 0x10200073U:
      return instruction(raw, Operation::Sret);
    case 0x30200073U:
      return instruction(raw, Operation::Mret);
    case 0x10500073U:
      return instruction(raw, Operation::Wfi);
    default:
      return error(raw, "unsupported privileged instruction");
    }
  }
  switch (funct3) {
  case 1:
    return instruction(raw, Operation::Csrrw);
  case 2:
    return instruction(raw, Operation::Csrrs);
  case 3:
    return instruction(raw, Operation::Csrrc);
  case 5:
    return instruction(raw, Operation::Csrrwi);
  case 6:
    return instruction(raw, Operation::Csrrsi);
  case 7:
    return instruction(raw, Operation::Csrrci);
  default:
    return error(raw, "reserved SYSTEM funct3");
  }
}

} // namespace

DecodeResult decode(std::uint32_t raw) {
  if ((raw & 0x3U) != 0x3U) {
    return error(raw, "compressed instructions are not implemented");
  }

  const auto opcode = raw & 0x7fU;
  const auto funct3 = field(raw, 12U, 0x7U);
  switch (opcode) {
  case 0x37:
    return instruction(raw, Operation::Lui, immediate_u(raw));
  case 0x17:
    return instruction(raw, Operation::Auipc, immediate_u(raw));
  case 0x6f:
    return instruction(raw, Operation::Jal, immediate_j(raw));
  case 0x67:
    if (funct3 != 0U) {
      return error(raw, "reserved JALR funct3");
    }
    return instruction(raw, Operation::Jalr, immediate_i(raw));
  case 0x63:
    return decode_branch(raw, funct3);
  case 0x03:
    return decode_load(raw, funct3);
  case 0x23:
    return decode_store(raw, funct3);
  case 0x13:
    return decode_op_imm(raw, funct3);
  case 0x33:
    return decode_op(raw, funct3);
  case 0x1b:
    return decode_op_imm_32(raw, funct3);
  case 0x3b:
    return decode_op_32(raw, funct3);
  case 0x0f:
    if (funct3 == 0U) {
      return instruction(raw, Operation::Fence);
    }
    if (funct3 == 1U && raw == 0x0000100fU) {
      return instruction(raw, Operation::FenceI);
    }
    return error(raw, "reserved MISC-MEM encoding");
  case 0x73:
    return decode_system(raw, funct3);
  default:
    return error(raw, "unknown opcode");
  }
}

const char* operation_name(Operation operation) noexcept {
  switch (operation) {
#define NORTHSTAR64_OPERATION_CASE(name, text)                                                     \
  case Operation::name:                                                                            \
    return text
    NORTHSTAR64_OPERATION_CASE(Lui, "lui");
    NORTHSTAR64_OPERATION_CASE(Auipc, "auipc");
    NORTHSTAR64_OPERATION_CASE(Jal, "jal");
    NORTHSTAR64_OPERATION_CASE(Jalr, "jalr");
    NORTHSTAR64_OPERATION_CASE(Beq, "beq");
    NORTHSTAR64_OPERATION_CASE(Bne, "bne");
    NORTHSTAR64_OPERATION_CASE(Blt, "blt");
    NORTHSTAR64_OPERATION_CASE(Bge, "bge");
    NORTHSTAR64_OPERATION_CASE(Bltu, "bltu");
    NORTHSTAR64_OPERATION_CASE(Bgeu, "bgeu");
    NORTHSTAR64_OPERATION_CASE(Lb, "lb");
    NORTHSTAR64_OPERATION_CASE(Lh, "lh");
    NORTHSTAR64_OPERATION_CASE(Lw, "lw");
    NORTHSTAR64_OPERATION_CASE(Ld, "ld");
    NORTHSTAR64_OPERATION_CASE(Lbu, "lbu");
    NORTHSTAR64_OPERATION_CASE(Lhu, "lhu");
    NORTHSTAR64_OPERATION_CASE(Lwu, "lwu");
    NORTHSTAR64_OPERATION_CASE(Sb, "sb");
    NORTHSTAR64_OPERATION_CASE(Sh, "sh");
    NORTHSTAR64_OPERATION_CASE(Sw, "sw");
    NORTHSTAR64_OPERATION_CASE(Sd, "sd");
    NORTHSTAR64_OPERATION_CASE(Addi, "addi");
    NORTHSTAR64_OPERATION_CASE(Slti, "slti");
    NORTHSTAR64_OPERATION_CASE(Sltiu, "sltiu");
    NORTHSTAR64_OPERATION_CASE(Xori, "xori");
    NORTHSTAR64_OPERATION_CASE(Ori, "ori");
    NORTHSTAR64_OPERATION_CASE(Andi, "andi");
    NORTHSTAR64_OPERATION_CASE(Slli, "slli");
    NORTHSTAR64_OPERATION_CASE(Srli, "srli");
    NORTHSTAR64_OPERATION_CASE(Srai, "srai");
    NORTHSTAR64_OPERATION_CASE(Add, "add");
    NORTHSTAR64_OPERATION_CASE(Sub, "sub");
    NORTHSTAR64_OPERATION_CASE(Sll, "sll");
    NORTHSTAR64_OPERATION_CASE(Slt, "slt");
    NORTHSTAR64_OPERATION_CASE(Sltu, "sltu");
    NORTHSTAR64_OPERATION_CASE(Xor, "xor");
    NORTHSTAR64_OPERATION_CASE(Srl, "srl");
    NORTHSTAR64_OPERATION_CASE(Sra, "sra");
    NORTHSTAR64_OPERATION_CASE(Or, "or");
    NORTHSTAR64_OPERATION_CASE(And, "and");
    NORTHSTAR64_OPERATION_CASE(Addiw, "addiw");
    NORTHSTAR64_OPERATION_CASE(Slliw, "slliw");
    NORTHSTAR64_OPERATION_CASE(Srliw, "srliw");
    NORTHSTAR64_OPERATION_CASE(Sraiw, "sraiw");
    NORTHSTAR64_OPERATION_CASE(Addw, "addw");
    NORTHSTAR64_OPERATION_CASE(Subw, "subw");
    NORTHSTAR64_OPERATION_CASE(Sllw, "sllw");
    NORTHSTAR64_OPERATION_CASE(Srlw, "srlw");
    NORTHSTAR64_OPERATION_CASE(Sraw, "sraw");
    NORTHSTAR64_OPERATION_CASE(Fence, "fence");
    NORTHSTAR64_OPERATION_CASE(FenceI, "fence.i");
    NORTHSTAR64_OPERATION_CASE(SfenceVma, "sfence.vma");
    NORTHSTAR64_OPERATION_CASE(Ecall, "ecall");
    NORTHSTAR64_OPERATION_CASE(Ebreak, "ebreak");
    NORTHSTAR64_OPERATION_CASE(Sret, "sret");
    NORTHSTAR64_OPERATION_CASE(Mret, "mret");
    NORTHSTAR64_OPERATION_CASE(Wfi, "wfi");
    NORTHSTAR64_OPERATION_CASE(Csrrw, "csrrw");
    NORTHSTAR64_OPERATION_CASE(Csrrs, "csrrs");
    NORTHSTAR64_OPERATION_CASE(Csrrc, "csrrc");
    NORTHSTAR64_OPERATION_CASE(Csrrwi, "csrrwi");
    NORTHSTAR64_OPERATION_CASE(Csrrsi, "csrrsi");
    NORTHSTAR64_OPERATION_CASE(Csrrci, "csrrci");
#undef NORTHSTAR64_OPERATION_CASE
  }
  return "unknown";
}

std::string disassemble(const DecodedInstruction& decoded) {
  std::ostringstream output;
  output << operation_name(decoded.operation);

  const auto registers = [&] {
    output << " x" << static_cast<unsigned>(decoded.rd) << ", x"
           << static_cast<unsigned>(decoded.rs1) << ", x" << static_cast<unsigned>(decoded.rs2);
  };
  const auto immediate = [&] {
    output << " x" << static_cast<unsigned>(decoded.rd) << ", x"
           << static_cast<unsigned>(decoded.rs1) << ", " << decoded.immediate;
  };

  switch (decoded.operation) {
  case Operation::Lui:
  case Operation::Auipc:
    output << " x" << static_cast<unsigned>(decoded.rd) << ", " << decoded.immediate;
    break;
  case Operation::Jal:
    output << " x" << static_cast<unsigned>(decoded.rd) << ", " << decoded.immediate;
    break;
  case Operation::Jalr:
  case Operation::Addi:
  case Operation::Slti:
  case Operation::Sltiu:
  case Operation::Xori:
  case Operation::Ori:
  case Operation::Andi:
  case Operation::Slli:
  case Operation::Srli:
  case Operation::Srai:
  case Operation::Addiw:
  case Operation::Slliw:
  case Operation::Srliw:
  case Operation::Sraiw:
    immediate();
    break;
  case Operation::Lb:
  case Operation::Lh:
  case Operation::Lw:
  case Operation::Ld:
  case Operation::Lbu:
  case Operation::Lhu:
  case Operation::Lwu:
    output << " x" << static_cast<unsigned>(decoded.rd) << ", " << decoded.immediate << "(x"
           << static_cast<unsigned>(decoded.rs1) << ")";
    break;
  case Operation::Sb:
  case Operation::Sh:
  case Operation::Sw:
  case Operation::Sd:
    output << " x" << static_cast<unsigned>(decoded.rs2) << ", " << decoded.immediate << "(x"
           << static_cast<unsigned>(decoded.rs1) << ")";
    break;
  case Operation::Beq:
  case Operation::Bne:
  case Operation::Blt:
  case Operation::Bge:
  case Operation::Bltu:
  case Operation::Bgeu:
    output << " x" << static_cast<unsigned>(decoded.rs1) << ", x"
           << static_cast<unsigned>(decoded.rs2) << ", " << decoded.immediate;
    break;
  case Operation::Add:
  case Operation::Sub:
  case Operation::Sll:
  case Operation::Slt:
  case Operation::Sltu:
  case Operation::Xor:
  case Operation::Srl:
  case Operation::Sra:
  case Operation::Or:
  case Operation::And:
  case Operation::Addw:
  case Operation::Subw:
  case Operation::Sllw:
  case Operation::Srlw:
  case Operation::Sraw:
    registers();
    break;
  case Operation::Csrrw:
  case Operation::Csrrs:
  case Operation::Csrrc:
  case Operation::Csrrwi:
  case Operation::Csrrsi:
  case Operation::Csrrci:
    output << " x" << static_cast<unsigned>(decoded.rd) << ", 0x" << std::hex << decoded.csr
           << std::dec << ", "
           << ((decoded.operation == Operation::Csrrwi || decoded.operation == Operation::Csrrsi ||
                decoded.operation == Operation::Csrrci)
                   ? std::to_string(decoded.rs1)
                   : "x" + std::to_string(decoded.rs1));
    break;
  case Operation::Fence:
  case Operation::FenceI:
  case Operation::Ecall:
  case Operation::Ebreak:
  case Operation::Sret:
  case Operation::Mret:
  case Operation::Wfi:
    break;
  case Operation::SfenceVma:
    output << " x" << static_cast<unsigned>(decoded.rs1) << ", x"
           << static_cast<unsigned>(decoded.rs2);
    break;
  }
  return output.str();
}

} // namespace northstar64
