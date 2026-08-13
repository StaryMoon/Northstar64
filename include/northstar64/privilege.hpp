#pragma once

#include <cstdint>

namespace northstar64 {

enum class PrivilegeLevel : std::uint8_t {
  User = 0,
  Supervisor = 1,
  Machine = 3,
};

constexpr bool privilege_at_least(PrivilegeLevel current, PrivilegeLevel required) noexcept {
  return static_cast<std::uint8_t>(current) >= static_cast<std::uint8_t>(required);
}

const char* privilege_name(PrivilegeLevel privilege) noexcept;

} // namespace northstar64

