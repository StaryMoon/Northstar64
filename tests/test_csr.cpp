#include "northstar64/csr.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <variant>

using namespace northstar64;

namespace {

std::uint64_t read_value(const CsrFile& csrs, std::uint16_t address,
                         PrivilegeLevel privilege = PrivilegeLevel::Machine) {
  const auto result = csrs.read(address, privilege);
  CHECK(std::holds_alternative<std::uint64_t>(result));
  return std::get<std::uint64_t>(result);
}

CsrError read_error(const CsrFile& csrs, std::uint16_t address,
                    PrivilegeLevel privilege) {
  const auto result = csrs.read(address, privilege);
  CHECK(std::holds_alternative<CsrError>(result));
  return std::get<CsrError>(result);
}

} // namespace

TEST_CASE("CSR address encoding derives privilege and read-only state") {
  CHECK(csr_minimum_privilege(0x000) == PrivilegeLevel::User);
  CHECK(csr_minimum_privilege(csr::kSstatus) == PrivilegeLevel::Supervisor);
  CHECK(csr_minimum_privilege(csr::kMstatus) == PrivilegeLevel::Machine);
  CHECK(csr_minimum_privilege(csr::kMhartid) == PrivilegeLevel::Machine);
  CHECK(!csr_is_read_only(csr::kMstatus));
  CHECK(csr_is_read_only(csr::kCycle));
  CHECK(csr_is_read_only(csr::kMhartid));
}

TEST_CASE("CSR access rejects insufficient privilege before lookup") {
  CsrFile csrs;
  const auto user_machine = read_error(csrs, csr::kMstatus, PrivilegeLevel::User);
  CHECK(user_machine.kind == CsrErrorKind::PrivilegeViolation);
  const auto user_supervisor = read_error(csrs, csr::kSstatus, PrivilegeLevel::User);
  CHECK(user_supervisor.kind == CsrErrorKind::PrivilegeViolation);
  const auto supervisor_machine = read_error(csrs, csr::kMie, PrivilegeLevel::Supervisor);
  CHECK(supervisor_machine.kind == CsrErrorKind::PrivilegeViolation);

  CHECK_EQ(read_value(csrs, csr::kSstatus, PrivilegeLevel::Supervisor), status::kUxl64);
  CHECK_EQ(read_value(csrs, csr::kMstatus, PrivilegeLevel::Machine),
           status::kMstatusFixedValue);
}

TEST_CASE("CSR writes reject addresses encoded as read-only") {
  CsrFile csrs;
  const auto cycle_error = csrs.write(csr::kCycle, 1, PrivilegeLevel::Machine);
  CHECK(cycle_error.has_value());
  CHECK(cycle_error->kind == CsrErrorKind::ReadOnly);
  const auto hart_error = csrs.write(csr::kMhartid, 1, PrivilegeLevel::Machine);
  CHECK(hart_error.has_value());
  CHECK(hart_error->kind == CsrErrorKind::ReadOnly);
}

TEST_CASE("misa is a fixed WARL register rather than a read-only address") {
  CsrFile csrs;
  const auto before = read_value(csrs, csr::kMisa);
  CHECK(!csrs.write(csr::kMisa, ~std::uint64_t{0}));
  CHECK_EQ(read_value(csrs, csr::kMisa), before);
}

TEST_CASE("sstatus is a masked view of mstatus") {
  CsrFile csrs;
  const auto machine_value = status::kMie | status::kMpie | status::kMprv | status::kSie |
                             status::kSpie | status::kSpp | status::kSum | status::kMxr;
  CHECK(!csrs.write(csr::kMstatus, machine_value));
  CHECK_EQ(read_value(csrs, csr::kSstatus, PrivilegeLevel::Supervisor),
           (machine_value & status::kSstatusWritableMask) | status::kUxl64);

  CHECK(!csrs.write(csr::kSstatus, status::kSie | status::kMie,
                    PrivilegeLevel::Supervisor));
  const auto mstatus = read_value(csrs, csr::kMstatus);
  CHECK((mstatus & status::kSie) != 0U);
  CHECK((mstatus & status::kMie) != 0U);
  CHECK((mstatus & status::kSum) == 0U);
  CHECK((mstatus & status::kMpie) != 0U);
  CHECK((mstatus & status::kMprv) != 0U);
  CHECK_EQ(mstatus & (status::kUxlMask | status::kSxlMask),
           status::kMstatusFixedValue);
}

TEST_CASE("sie and sip expose only delegated supervisor interrupt state") {
  CsrFile csrs;
  constexpr auto supervisor_software = std::uint64_t{1} << 1U;
  constexpr auto supervisor_timer = std::uint64_t{1} << 5U;
  constexpr auto machine_timer = std::uint64_t{1} << 7U;
  CHECK(!csrs.write(csr::kMideleg, supervisor_software | supervisor_timer));
  CHECK(!csrs.write(csr::kMie, supervisor_software | supervisor_timer | machine_timer));
  CHECK_EQ(read_value(csrs, csr::kSie, PrivilegeLevel::Supervisor),
           supervisor_software | supervisor_timer);

  CHECK(!csrs.write(csr::kSie, supervisor_software, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kMie), supervisor_software | machine_timer);

  CHECK(!csrs.write(csr::kMip, supervisor_software | supervisor_timer | machine_timer));
  CHECK(!csrs.write(csr::kSip, 0, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kSip, PrivilegeLevel::Supervisor), supervisor_timer);
  CHECK_EQ(read_value(csrs, csr::kMip), supervisor_timer | machine_timer);
}

TEST_CASE("counter access requires both machine and supervisor enables") {
  CsrFile csrs;
  csrs.tick_cycle();
  const auto supervisor_disabled = read_error(csrs, csr::kCycle, PrivilegeLevel::Supervisor);
  CHECK(supervisor_disabled.kind == CsrErrorKind::CounterDisabled);

  CHECK(!csrs.write(csr::kMcounteren, 1));
  CHECK_EQ(read_value(csrs, csr::kCycle, PrivilegeLevel::Supervisor), std::uint64_t{1});
  const auto user_disabled = read_error(csrs, csr::kCycle, PrivilegeLevel::User);
  CHECK(user_disabled.kind == CsrErrorKind::CounterDisabled);

  CHECK(!csrs.write(csr::kScounteren, 1, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kCycle, PrivilegeLevel::User), std::uint64_t{1});
}

TEST_CASE("satp accepts Bare and Sv39 while preserving state on unsupported modes") {
  CsrFile csrs;
  const auto sv39 = (std::uint64_t{8} << 60U) | (std::uint64_t{0x1234} << 44U) | 0xabcdeU;
  CHECK(!csrs.write(csr::kSatp, sv39, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kSatp, PrivilegeLevel::Supervisor), sv39);

  const auto unsupported = (std::uint64_t{9} << 60U) | 0x555U;
  CHECK(!csrs.write(csr::kSatp, unsupported, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kSatp, PrivilegeLevel::Supervisor), sv39);

  CHECK(!csrs.write(csr::kSatp, 0, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kSatp, PrivilegeLevel::Supervisor), std::uint64_t{0});
}

TEST_CASE("WARL state masks unsupported bits and aligns trap PCs") {
  CsrFile csrs;
  CHECK(!csrs.write(csr::kMstatus, ~std::uint64_t{0}));
  CHECK_EQ(read_value(csrs, csr::kMstatus),
           status::kMstatusWritableMask | status::kMstatusFixedValue);
  CHECK(!csrs.write(csr::kMstatus, std::uint64_t{2} << 11U));
  CHECK_EQ(read_value(csrs, csr::kMstatus) & status::kMppMask, std::uint64_t{0});
  CHECK_EQ(read_value(csrs, csr::kMstatus) & (status::kUxlMask | status::kSxlMask),
           status::kMstatusFixedValue);
  CHECK(!csrs.write(csr::kMepc, 0x80000003));
  CHECK_EQ(read_value(csrs, csr::kMepc), std::uint64_t{0x80000000});
  CHECK(!csrs.write(csr::kStvec, 0x80000001, PrivilegeLevel::Supervisor));
  CHECK_EQ(read_value(csrs, csr::kStvec, PrivilegeLevel::Supervisor),
           std::uint64_t{0x80000000});
}
