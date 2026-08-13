#pragma once

#include "northstar64/cpu.hpp"
#include "northstar64/memory.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace northstar64::test {

struct TestCase {
  std::string name;
  std::function<void()> function;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> function) {
    registry().push_back(TestCase{std::move(name), std::move(function)});
  }
};

class Failure : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

inline void check(bool condition, std::string_view expression, std::string_view file, int line) {
  if (!condition) {
    std::ostringstream message;
    message << file << ':' << line << ": CHECK(" << expression << ") failed";
    throw Failure(message.str());
  }
}

template <typename Left, typename Right>
void check_equal(const Left& left, const Right& right, std::string_view left_expression,
                 std::string_view right_expression, std::string_view file, int line) {
  if (!(left == right)) {
    std::ostringstream message;
    message << file << ':' << line << ": CHECK_EQ(" << left_expression << ", " << right_expression
            << ") failed: " << left << " != " << right;
    throw Failure(message.str());
  }
}

template <typename Exception, typename Function>
void check_throws(Function&& function, std::string_view expression, std::string_view file, int line) {
  try {
    function();
  } catch (const Exception&) {
    return;
  } catch (const std::exception& error) {
    std::ostringstream message;
    message << file << ':' << line << ": CHECK_THROWS(" << expression
            << ") caught the wrong exception: " << error.what();
    throw Failure(message.str());
  }
  std::ostringstream message;
  message << file << ':' << line << ": CHECK_THROWS(" << expression << ") did not throw";
  throw Failure(message.str());
}

constexpr std::uint32_t encode_r(std::uint8_t opcode, std::uint8_t rd, std::uint8_t funct3,
                                 std::uint8_t rs1, std::uint8_t rs2, std::uint8_t funct7 = 0) {
  return (static_cast<std::uint32_t>(funct7) << 25U) |
         (static_cast<std::uint32_t>(rs2) << 20U) |
         (static_cast<std::uint32_t>(rs1) << 15U) |
         (static_cast<std::uint32_t>(funct3) << 12U) |
         (static_cast<std::uint32_t>(rd) << 7U) | opcode;
}

constexpr std::uint32_t encode_i(std::uint8_t opcode, std::uint8_t rd, std::uint8_t funct3,
                                 std::uint8_t rs1, std::int32_t immediate) {
  return ((static_cast<std::uint32_t>(immediate) & 0xfffU) << 20U) |
         (static_cast<std::uint32_t>(rs1) << 15U) |
         (static_cast<std::uint32_t>(funct3) << 12U) |
         (static_cast<std::uint32_t>(rd) << 7U) | opcode;
}

constexpr std::uint32_t encode_s(std::uint8_t opcode, std::uint8_t funct3, std::uint8_t rs1,
                                 std::uint8_t rs2, std::int32_t immediate) {
  const auto encoded = static_cast<std::uint32_t>(immediate) & 0xfffU;
  return ((encoded >> 5U) << 25U) | (static_cast<std::uint32_t>(rs2) << 20U) |
         (static_cast<std::uint32_t>(rs1) << 15U) |
         (static_cast<std::uint32_t>(funct3) << 12U) | ((encoded & 0x1fU) << 7U) | opcode;
}

constexpr std::uint32_t encode_b(std::uint8_t funct3, std::uint8_t rs1, std::uint8_t rs2,
                                 std::int32_t immediate) {
  const auto encoded = static_cast<std::uint32_t>(immediate) & 0x1fffU;
  return (((encoded >> 12U) & 0x1U) << 31U) | (((encoded >> 5U) & 0x3fU) << 25U) |
         (static_cast<std::uint32_t>(rs2) << 20U) |
         (static_cast<std::uint32_t>(rs1) << 15U) |
         (static_cast<std::uint32_t>(funct3) << 12U) |
         (((encoded >> 1U) & 0xfU) << 8U) | (((encoded >> 11U) & 0x1U) << 7U) | 0x63U;
}

constexpr std::uint32_t encode_u(std::uint8_t opcode, std::uint8_t rd, std::int32_t immediate) {
  return (static_cast<std::uint32_t>(immediate) & 0xfffff000U) |
         (static_cast<std::uint32_t>(rd) << 7U) | opcode;
}

constexpr std::uint32_t encode_j(std::uint8_t rd, std::int32_t immediate) {
  const auto encoded = static_cast<std::uint32_t>(immediate) & 0x1fffffU;
  return (((encoded >> 20U) & 0x1U) << 31U) | (((encoded >> 1U) & 0x3ffU) << 21U) |
         (((encoded >> 11U) & 0x1U) << 20U) | (((encoded >> 12U) & 0xffU) << 12U) |
         (static_cast<std::uint32_t>(rd) << 7U) | 0x6fU;
}

constexpr std::uint32_t encode_csr(std::uint8_t rd, std::uint8_t funct3, std::uint8_t rs1,
                                   std::uint16_t address) {
  return (static_cast<std::uint32_t>(address) << 20U) |
         (static_cast<std::uint32_t>(rs1) << 15U) |
         (static_cast<std::uint32_t>(funct3) << 12U) |
         (static_cast<std::uint32_t>(rd) << 7U) | 0x73U;
}

inline constexpr std::uint32_t kEbreak = 0x00100073U;

class CpuFixture {
public:
  static constexpr Address kBase = 0x80000000ULL;

  explicit CpuFixture(CpuConfig config = {})
      : ram_(&bus_.emplace_device<SparseRam>(kBase, 1024U * 1024U)), cpu_(bus_, config) {
    cpu_.reset(kBase);
  }

  void load_words(std::initializer_list<std::uint32_t> words, Address address = kBase) {
    for (const auto word : words) {
      const auto fault = bus_.write(address, 4U, word, AccessKind::ImageLoad);
      if (fault) {
        throw Failure("test fixture could not load instruction word: " + fault->detail);
      }
      address += 4U;
    }
  }

  Bus bus_;
  SparseRam* ram_;
  Cpu cpu_;
};

} // namespace northstar64::test

#define NORTHSTAR64_CONCAT_IMPL(left, right) left##right
#define NORTHSTAR64_CONCAT(left, right) NORTHSTAR64_CONCAT_IMPL(left, right)
#define TEST_CASE(name)                                                                              \
  static void NORTHSTAR64_CONCAT(test_function_, __LINE__)();                                        \
  static ::northstar64::test::Registrar NORTHSTAR64_CONCAT(test_registrar_, __LINE__)(                \
      name, NORTHSTAR64_CONCAT(test_function_, __LINE__));                                           \
  static void NORTHSTAR64_CONCAT(test_function_, __LINE__)()
#define CHECK(expression)                                                                            \
  ::northstar64::test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(left, right)                                                                         \
  ::northstar64::test::check_equal((left), (right), #left, #right, __FILE__, __LINE__)
#define CHECK_THROWS(exception, expression)                                                           \
  ::northstar64::test::check_throws<exception>([&] { (void)(expression); }, #expression, __FILE__,    \
                                                __LINE__)

