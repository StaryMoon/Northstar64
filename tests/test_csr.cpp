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

TEST_CASE("delegated traps update only supervisor trap state") {
  CsrFile csrs;
  constexpr auto machine_stack = status::kMie | status::kMpie | status::kMppMask |
                                 status::kMprv;
  constexpr auto initial_status = machine_stack | status::kSie;
  constexpr auto supervisor_handler = std::uint64_t{0x80000100};
  CHECK(!csrs.write(csr::kMstatus, initial_status));
  CHECK(!csrs.write(csr::kMedeleg,
                    std::uint64_t{1} <<
                        static_cast<std::uint64_t>(TrapCause::EnvironmentCallFromUserMode)));
  CHECK(!csrs.write(csr::kStvec, supervisor_handler, PrivilegeLevel::Supervisor));
  CHECK(!csrs.write(csr::kMepc, 0x1110));
  CHECK(!csrs.write(csr::kMcause, 0x2222));
  CHECK(!csrs.write(csr::kMtval, 0x3333));

  const Trap trap{TrapCause::EnvironmentCallFromUserMode, 0x80000002, 0x55,
                  "delegated test trap"};
  const auto entry = csrs.enter_trap(trap, PrivilegeLevel::User);
  CHECK(entry.target == PrivilegeLevel::Supervisor);
  CHECK_EQ(entry.vector, supervisor_handler);
  CHECK_EQ(read_value(csrs, csr::kSepc, PrivilegeLevel::Supervisor),
           std::uint64_t{0x80000000});
  CHECK_EQ(read_value(csrs, csr::kScause, PrivilegeLevel::Supervisor),
           static_cast<std::uint64_t>(TrapCause::EnvironmentCallFromUserMode));
  CHECK_EQ(read_value(csrs, csr::kStval, PrivilegeLevel::Supervisor), std::uint64_t{0x55});
  CHECK_EQ(read_value(csrs, csr::kMepc), std::uint64_t{0x1110});
  CHECK_EQ(read_value(csrs, csr::kMcause), std::uint64_t{0x2222});
  CHECK_EQ(read_value(csrs, csr::kMtval), std::uint64_t{0x3333});

  const auto trapped_status = read_value(csrs, csr::kMstatus);
  CHECK_EQ(trapped_status & machine_stack, initial_status & machine_stack);
  CHECK((trapped_status & status::kSie) == 0U);
  CHECK((trapped_status & status::kSpie) != 0U);
  CHECK((trapped_status & status::kSpp) == 0U);

  const auto returned = csrs.return_from_trap(TrapReturnMode::Supervisor);
  CHECK(returned.target == PrivilegeLevel::User);
  CHECK_EQ(returned.pc, std::uint64_t{0x80000000});
  const auto returned_status = read_value(csrs, csr::kMstatus);
  CHECK((returned_status & status::kSie) != 0U);
  CHECK((returned_status & status::kSpie) != 0U);
  CHECK((returned_status & status::kSpp) == 0U);
  CHECK((returned_status & status::kMprv) == 0U);
}

TEST_CASE("supervisor trap stack records an S-mode origin") {
  CsrFile csrs;
  CHECK(!csrs.write(csr::kMedeleg,
                    std::uint64_t{1} <<
                        static_cast<std::uint64_t>(TrapCause::EnvironmentCallFromSupervisorMode)));
  const Trap trap{TrapCause::EnvironmentCallFromSupervisorMode, 0x80000040, 0,
                  "supervisor ecall"};
  const auto entry = csrs.enter_trap(trap, PrivilegeLevel::Supervisor);
  CHECK(entry.target == PrivilegeLevel::Supervisor);
  const auto trapped_status = read_value(csrs, csr::kMstatus);
  CHECK((trapped_status & status::kSpp) != 0U);
  CHECK((trapped_status & status::kSpie) == 0U);

  const auto returned = csrs.return_from_trap(TrapReturnMode::Supervisor);
  CHECK(returned.target == PrivilegeLevel::Supervisor);
  CHECK_EQ(returned.pc, std::uint64_t{0x80000040});
  const auto returned_status = read_value(csrs, csr::kMstatus);
  CHECK((returned_status & status::kSie) == 0U);
  CHECK((returned_status & status::kSpie) != 0U);
  CHECK((returned_status & status::kSpp) == 0U);
}

TEST_CASE("machine traps never delegate and preserve supervisor trap state") {
  CsrFile csrs;
  constexpr auto supervisor_stack = status::kSie | status::kSpie | status::kSpp;
  constexpr auto initial_status = supervisor_stack | status::kMie | status::kMprv;
  constexpr auto machine_handler = std::uint64_t{0x80000200};
  CHECK(!csrs.write(csr::kMstatus, initial_status));
  CHECK(!csrs.write(csr::kMedeleg,
                    std::uint64_t{1} <<
                        static_cast<std::uint64_t>(TrapCause::IllegalInstruction)));
  CHECK(!csrs.write(csr::kMtvec, machine_handler));
  CHECK(!csrs.write(csr::kSepc, 0x4444, PrivilegeLevel::Supervisor));
  CHECK(!csrs.write(csr::kScause, 0x5555, PrivilegeLevel::Supervisor));
  CHECK(!csrs.write(csr::kStval, 0x6666, PrivilegeLevel::Supervisor));

  const Trap trap{TrapCause::IllegalInstruction, 0x80000020, 0xffffffff,
                  "machine illegal instruction"};
  const auto entry = csrs.enter_trap(trap, PrivilegeLevel::Machine);
  CHECK(entry.target == PrivilegeLevel::Machine);
  CHECK_EQ(entry.vector, machine_handler);
  CHECK_EQ(read_value(csrs, csr::kMepc), std::uint64_t{0x80000020});
  CHECK_EQ(read_value(csrs, csr::kMcause),
           static_cast<std::uint64_t>(TrapCause::IllegalInstruction));
  CHECK_EQ(read_value(csrs, csr::kMtval), std::uint64_t{0xffffffff});
  CHECK_EQ(read_value(csrs, csr::kSepc, PrivilegeLevel::Supervisor), std::uint64_t{0x4444});
  CHECK_EQ(read_value(csrs, csr::kScause, PrivilegeLevel::Supervisor), std::uint64_t{0x5555});
  CHECK_EQ(read_value(csrs, csr::kStval, PrivilegeLevel::Supervisor), std::uint64_t{0x6666});

  const auto trapped_status = read_value(csrs, csr::kMstatus);
  CHECK_EQ(trapped_status & supervisor_stack, initial_status & supervisor_stack);
  CHECK((trapped_status & status::kMie) == 0U);
  CHECK((trapped_status & status::kMpie) != 0U);
  CHECK_EQ((trapped_status & status::kMppMask) >> 11U,
           static_cast<std::uint64_t>(PrivilegeLevel::Machine));

  const auto returned = csrs.return_from_trap(TrapReturnMode::Machine);
  CHECK(returned.target == PrivilegeLevel::Machine);
  CHECK_EQ(returned.pc, std::uint64_t{0x80000020});
  const auto returned_status = read_value(csrs, csr::kMstatus);
  CHECK((returned_status & status::kMie) != 0U);
  CHECK((returned_status & status::kMpie) != 0U);
  CHECK_EQ(returned_status & status::kMppMask, std::uint64_t{0});
  CHECK((returned_status & status::kMprv) != 0U);
}
