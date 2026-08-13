#include "northstar64/cpu.hpp"
#include "test_support.hpp"

#include <bit>
#include <cstdint>
#include <limits>

using namespace northstar64;
using namespace northstar64::test;

namespace {

class FixedRandom {
public:
  std::uint64_t next() noexcept {
    // xorshift64* with a fixed non-zero state makes failures exactly reproducible.
    state_ ^= state_ >> 12U;
    state_ ^= state_ << 25U;
    state_ ^= state_ >> 27U;
    return state_ * 0x2545f4914f6cdd1dULL;
  }

private:
  std::uint64_t state_{0x4e6f727468737461ULL};
};

std::uint64_t execute_r(std::uint8_t funct3, std::uint8_t funct7, std::uint64_t lhs,
                        std::uint64_t rhs) {
  CpuFixture fixture;
  fixture.cpu_.set_reg(1, lhs);
  fixture.cpu_.set_reg(2, rhs);
  fixture.load_words({encode_r(0x33, 3, funct3, 1, 2, funct7), kEbreak});
  const auto result = fixture.cpu_.run(4);
  CHECK(result.reason == RunStopReason::Halted);
  CHECK_EQ(result.retired_instructions, std::uint64_t{1});
  return fixture.cpu_.reg(3);
}

std::uint64_t execute_r_word(std::uint8_t funct3, std::uint8_t funct7, std::uint64_t lhs,
                             std::uint64_t rhs) {
  CpuFixture fixture;
  fixture.cpu_.set_reg(1, lhs);
  fixture.cpu_.set_reg(2, rhs);
  fixture.load_words({encode_r(0x3b, 3, funct3, 1, 2, funct7), kEbreak});
  const auto result = fixture.cpu_.run(4);
  CHECK(result.reason == RunStopReason::Halted);
  CHECK_EQ(result.retired_instructions, std::uint64_t{1});
  return fixture.cpu_.reg(3);
}

std::uint64_t signed_word_result(std::uint32_t value) {
  return static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::bit_cast<std::int32_t>(value)));
}

std::uint64_t reference_sra(std::uint64_t value, unsigned shift) {
  shift &= 63U;
  if (shift == 0U) {
    return value;
  }
  const auto logical = value >> shift;
  return (value >> 63U) == 0U ? logical : logical | (~std::uint64_t{0} << (64U - shift));
}

std::uint32_t reference_sraw(std::uint32_t value, unsigned shift) {
  shift &= 31U;
  if (shift == 0U) {
    return value;
  }
  const auto logical = value >> shift;
  return (value >> 31U) == 0U ? logical : logical | (~std::uint32_t{0} << (32U - shift));
}

} // namespace

TEST_CASE("fixed-seed property matrix covers RV64 register ALU semantics") {
  FixedRandom random;
  for (std::size_t sample = 0; sample < 256; ++sample) {
    const auto lhs = random.next();
    const auto rhs = random.next();
    const auto signed_lhs = std::bit_cast<std::int64_t>(lhs);
    const auto signed_rhs = std::bit_cast<std::int64_t>(rhs);

    CHECK_EQ(execute_r(0, 0, lhs, rhs), lhs + rhs);
    CHECK_EQ(execute_r(0, 0x20, lhs, rhs), lhs - rhs);
    CHECK_EQ(execute_r(1, 0, lhs, rhs), lhs << static_cast<unsigned>(rhs & 63U));
    CHECK_EQ(execute_r(2, 0, lhs, rhs), signed_lhs < signed_rhs ? 1ULL : 0ULL);
    CHECK_EQ(execute_r(3, 0, lhs, rhs), lhs < rhs ? 1ULL : 0ULL);
    CHECK_EQ(execute_r(4, 0, lhs, rhs), lhs ^ rhs);
    CHECK_EQ(execute_r(5, 0, lhs, rhs), lhs >> static_cast<unsigned>(rhs & 63U));
    CHECK_EQ(execute_r(5, 0x20, lhs, rhs), reference_sra(lhs, static_cast<unsigned>(rhs)));
    CHECK_EQ(execute_r(6, 0, lhs, rhs), lhs | rhs);
    CHECK_EQ(execute_r(7, 0, lhs, rhs), lhs & rhs);
  }
}

TEST_CASE("fixed-seed property matrix covers RV64 word ALU semantics") {
  FixedRandom random;
  for (std::size_t sample = 0; sample < 256; ++sample) {
    const auto lhs = random.next();
    const auto rhs = random.next();
    const auto left_word = static_cast<std::uint32_t>(lhs);
    const auto right_word = static_cast<std::uint32_t>(rhs);
    const auto shift = static_cast<unsigned>(rhs & 31U);

    CHECK_EQ(execute_r_word(0, 0, lhs, rhs), signed_word_result(left_word + right_word));
    CHECK_EQ(execute_r_word(0, 0x20, lhs, rhs), signed_word_result(left_word - right_word));
    CHECK_EQ(execute_r_word(1, 0, lhs, rhs), signed_word_result(left_word << shift));
    CHECK_EQ(execute_r_word(5, 0, lhs, rhs), signed_word_result(left_word >> shift));
    CHECK_EQ(execute_r_word(5, 0x20, lhs, rhs),
             signed_word_result(reference_sraw(left_word, shift)));
  }
}

TEST_CASE("fixed-seed property matrix covers signed and unsigned loads") {
  FixedRandom random;
  constexpr std::uint8_t signed_funct3[] = {0, 1, 2};
  constexpr std::uint8_t unsigned_funct3[] = {4, 5, 6};
  constexpr std::size_t widths[] = {1, 2, 4};

  for (std::size_t sample = 0; sample < 128; ++sample) {
    const auto value = random.next();
    for (std::size_t index = 0; index < 3; ++index) {
      CpuFixture fixture;
      const auto address = CpuFixture::kBase + 0x100;
      fixture.cpu_.set_reg(1, address);
      CHECK(!fixture.bus_.write(address, widths[index], value, AccessKind::Store));
      fixture.load_words({encode_i(0x03, 2, signed_funct3[index], 1, 0),
                          encode_i(0x03, 3, unsigned_funct3[index], 1, 0), kEbreak});
      const auto result = fixture.cpu_.run(8);
      CHECK(result.reason == RunStopReason::Halted);

      const auto masked = value & mask_for_width(widths[index]);
      const auto expected_signed = static_cast<std::uint64_t>(
          sign_extend<std::int64_t>(masked, static_cast<unsigned>(widths[index] * 8U)));
      CHECK_EQ(fixture.cpu_.reg(2), expected_signed);
      CHECK_EQ(fixture.cpu_.reg(3), masked);
    }
  }
}

TEST_CASE("register zero remains immutable under randomized destinations") {
  FixedRandom random;
  CpuFixture fixture;
  for (std::size_t sample = 0; sample < 512; ++sample) {
    fixture.cpu_.set_reg(0, random.next());
    CHECK_EQ(fixture.cpu_.reg(0), std::uint64_t{0});
  }
}
