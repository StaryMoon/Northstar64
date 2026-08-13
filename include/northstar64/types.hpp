#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace northstar64 {

using Address = std::uint64_t;
using RegisterValue = std::uint64_t;

constexpr std::size_t kRegisterCount = 32;
constexpr std::size_t kInstructionBytes = 4;

template <typename T>
constexpr T sign_extend(std::uint64_t value, unsigned bits) {
  static_assert(std::is_signed_v<T>, "sign_extend requires a signed destination type");
  const auto sign_bit = std::uint64_t{1} << (bits - 1U);
  const auto mask = bits == 64U ? std::numeric_limits<std::uint64_t>::max()
                                : ((std::uint64_t{1} << bits) - 1U);
  const auto narrowed = value & mask;
  return static_cast<T>((narrowed ^ sign_bit) - sign_bit);
}

constexpr bool add_overflows(Address base, std::uint64_t size) {
  return size > std::numeric_limits<Address>::max() - base;
}

constexpr std::uint64_t mask_for_width(std::size_t width) {
  return width == 8U ? std::numeric_limits<std::uint64_t>::max()
                     : ((std::uint64_t{1} << (width * 8U)) - 1U);
}

} // namespace northstar64

