#include "northstar64/decode.hpp"
#include "test_support.hpp"

#include <variant>

using namespace northstar64;
using namespace northstar64::test;

TEST_CASE("decoder extracts signed I immediates") {
  const auto raw = encode_i(0x13, 3, 0, 2, -17);
  const auto result = decode(raw);
  CHECK(std::holds_alternative<DecodedInstruction>(result));
  const auto& instruction = std::get<DecodedInstruction>(result);
  CHECK(instruction.operation == Operation::Addi);
  CHECK_EQ(instruction.rd, std::uint8_t{3});
  CHECK_EQ(instruction.rs1, std::uint8_t{2});
  CHECK_EQ(instruction.immediate, std::int64_t{-17});
  CHECK_EQ(disassemble(instruction), std::string("addi x3, x2, -17"));
}

TEST_CASE("decoder reconstructs branch and jump immediates") {
  const auto branch = std::get<DecodedInstruction>(decode(encode_b(1, 4, 5, -2048)));
  CHECK(branch.operation == Operation::Bne);
  CHECK_EQ(branch.immediate, std::int64_t{-2048});

  const auto jump = std::get<DecodedInstruction>(decode(encode_j(1, 0x7fe)));
  CHECK(jump.operation == Operation::Jal);
  CHECK_EQ(jump.immediate, std::int64_t{0x7fe});
}

TEST_CASE("decoder distinguishes logical and arithmetic shifts") {
  const auto srli = std::get<DecodedInstruction>(decode(encode_i(0x13, 1, 5, 2, 63)));
  CHECK(srli.operation == Operation::Srli);

  const auto srai_raw = encode_i(0x13, 1, 5, 2, 0x400 | 17);
  const auto srai = std::get<DecodedInstruction>(decode(srai_raw));
  CHECK(srai.operation == Operation::Srai);
  CHECK_EQ(srai.immediate, std::int64_t{17});
}

TEST_CASE("decoder rejects compressed and extension encodings") {
  CHECK(std::holds_alternative<DecodeError>(decode(0x0001U)));
  CHECK(std::holds_alternative<DecodeError>(decode(encode_r(0x33, 1, 0, 2, 3, 1))));
}

TEST_CASE("decoder accepts machine control and CSR instructions") {
  CHECK(std::get<DecodedInstruction>(decode(0x30200073U)).operation == Operation::Mret);
  const auto csr_instruction =
      std::get<DecodedInstruction>(decode(encode_csr(2, 1, 3, csr::kMtvec)));
  CHECK(csr_instruction.operation == Operation::Csrrw);
  CHECK_EQ(csr_instruction.csr, csr::kMtvec);
}

