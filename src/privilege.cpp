#include "northstar64/privilege.hpp"

namespace northstar64 {

const char* privilege_name(PrivilegeLevel privilege) noexcept {
  switch (privilege) {
  case PrivilegeLevel::User:
    return "U";
  case PrivilegeLevel::Supervisor:
    return "S";
  case PrivilegeLevel::Machine:
    return "M";
  }
  return "reserved";
}

} // namespace northstar64

