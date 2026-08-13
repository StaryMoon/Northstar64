#include "northstar64/cpu.hpp"
#include "northstar64/csr.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <variant>

using namespace northstar64;
using namespace northstar64::test;

TEST_CASE("CPU executes arithmetic while preserving x0") {
  CpuFixture fixture;
  fixture.load_words({encode_i(0x13, 1, 0, 0, 7), encode_i(0x13, 2, 0, 0, -3),
                      encode_r(0x33, 3, 0, 1, 2), encode_r(0x33, 4, 0, 1, 2, 0x20),
                      encode_i(0x13, 0, 0, 0, 99), kEbreak});

  const auto result = fixture.cpu_.run(32);
  CHECK(result.reason == RunStopReason::Halted);
  CHECK_EQ(result.retired_instructions, std::uint64_t{5});
  CHECK_EQ(fixture.cpu_.reg(0), std::uint64_t{0});
  CHECK_EQ(fixture.cpu_.reg(1), std::uint64_t{7});
  CHECK_EQ(fixture.cpu_.reg(2), static_cast<std::uint64_t>(-3));
  CHECK_EQ(fixture.cpu_.reg(3), std::uint64_t{4});
  CHECK_EQ(fixture.cpu_.reg(4), std::uint64_t{10});
}

TEST_CASE("CPU executes loads stores branches and jumps") {
  CpuFixture fixture;
  fixture.cpu_.set_reg(10, CpuFixture::kBase + 0x100);
  fixture.load_words({
      encode_i(0x13, 1, 0, 0, 42),
      encode_s(0x23, 3, 10, 1, 0),
      encode_i(0x03, 2, 3, 10, 0),
      encode_b(0, 1, 2, 8),
      encode_i(0x13, 3, 0, 0, 1),
      encode_j(4, 8),
      encode_i(0x13, 3, 0, 0, 2),
      kEbreak,
  });

  const auto result = fixture.cpu_.run(32);
  CHECK(result.reason == RunStopReason::Halted);
  CHECK_EQ(fixture.cpu_.reg(2), std::uint64_t{42});
  CHECK_EQ(fixture.cpu_.reg(3), std::uint64_t{0});
  CHECK_EQ(fixture.cpu_.reg(4), CpuFixture::kBase + 24);
  CHECK_EQ(std::get<std::uint64_t>(fixture.bus_.read(CpuFixture::kBase + 0x100, 8,
                                                     AccessKind::Load)),
           std::uint64_t{42});
}

TEST_CASE("CPU implements signed and word-sized shift semantics") {
  CpuFixture fixture;
  fixture.cpu_.set_reg(1, 0x8000000000000000ULL);
  fixture.cpu_.set_reg(2, 63);
  fixture.cpu_.set_reg(4, 0x0000000080000000ULL);
  fixture.load_words({encode_i(0x13, 3, 5, 1, 0x400 | 4), encode_r(0x33, 5, 5, 1, 2, 0x20),
                      encode_i(0x1b, 6, 5, 4, 0x400 | 4), kEbreak});

  const auto result = fixture.cpu_.run(16);
  CHECK(result.reason == RunStopReason::Halted);
  CHECK_EQ(fixture.cpu_.reg(3), std::uint64_t{0xf800000000000000ULL});
  CHECK_EQ(fixture.cpu_.reg(5), std::uint64_t{0xffffffffffffffffULL});
  CHECK_EQ(fixture.cpu_.reg(6), std::uint64_t{0xfffffffff8000000ULL});
}

TEST_CASE("misaligned loads produce precise traps without retirement") {
  CpuFixture fixture;
  fixture.cpu_.set_reg(1, CpuFixture::kBase + 1);
  fixture.load_words({encode_i(0x03, 2, 2, 1, 0)});

  const auto record = fixture.cpu_.step();
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::LoadAddressMisaligned);
  CHECK(!record.retired);
  CHECK_EQ(fixture.cpu_.pc(), CpuFixture::kBase);
  CHECK_EQ(fixture.cpu_.csrs().retired_count(), std::uint64_t{0});
  CHECK_EQ(std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMepc)), CpuFixture::kBase);
}

TEST_CASE("illegal instructions preserve the faulting program counter") {
  CpuFixture fixture;
  fixture.load_words({0xffffffffU});
  const auto record = fixture.cpu_.step();
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::IllegalInstruction);
  CHECK_EQ(record.trap->value, std::uint64_t{0xffffffffU});
  CHECK_EQ(record.next_pc, CpuFixture::kBase);
}

TEST_CASE("CSR instructions perform atomic read modify write") {
  CpuFixture fixture;
  fixture.cpu_.set_reg(1, CpuFixture::kBase + 0x80);
  fixture.load_words({encode_csr(2, 1, 1, csr::kMtvec), encode_csr(3, 2, 0, csr::kMtvec),
                      kEbreak});
  const auto result = fixture.cpu_.run(16);
  CHECK(result.reason == RunStopReason::Halted);
  CHECK_EQ(fixture.cpu_.reg(2), std::uint64_t{0});
  CHECK_EQ(fixture.cpu_.reg(3), CpuFixture::kBase + 0x80);
  CHECK_EQ(std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMtvec)),
           CpuFixture::kBase + 0x80);
}

TEST_CASE("CPU enforces CSR privilege and enters machine mode on failure") {
  CpuFixture fixture;
  fixture.cpu_.set_privilege(PrivilegeLevel::User);
  fixture.load_words({encode_csr(2, 2, 0, csr::kMstatus)});

  const auto record = fixture.cpu_.step();
  CHECK(record.privilege == PrivilegeLevel::User);
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::IllegalInstruction);
  CHECK(!record.retired);
  CHECK(fixture.cpu_.privilege() == PrivilegeLevel::Machine);
  CHECK_EQ(fixture.cpu_.reg(2), std::uint64_t{0});
  const auto mstatus = std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
  CHECK_EQ((mstatus & status::kMppMask) >> 11U,
           static_cast<std::uint64_t>(PrivilegeLevel::User));
}

TEST_CASE("zero-source CSR reads do not write read-only state") {
  CpuFixture fixture;
  fixture.cpu_.set_privilege(PrivilegeLevel::User);
  CHECK(!fixture.cpu_.csrs().write(csr::kMcounteren, 1));
  CHECK(!fixture.cpu_.csrs().write(csr::kScounteren, 1, PrivilegeLevel::Supervisor));
  fixture.load_words({encode_csr(2, 2, 0, csr::kCycle), kEbreak});

  const auto read_record = fixture.cpu_.step();
  CHECK(read_record.retired);
  CHECK(!read_record.trap);
  CHECK_EQ(fixture.cpu_.reg(2), std::uint64_t{1});
  const auto breakpoint = fixture.cpu_.step();
  CHECK(breakpoint.trap.has_value());
}

TEST_CASE("nonzero CSRRS attempts to write read-only state") {
  CpuFixture fixture;
  fixture.cpu_.set_privilege(PrivilegeLevel::User);
  fixture.cpu_.set_reg(1, 1);
  CHECK(!fixture.cpu_.csrs().write(csr::kMcounteren, 1));
  CHECK(!fixture.cpu_.csrs().write(csr::kScounteren, 1, PrivilegeLevel::Supervisor));
  fixture.load_words({encode_csr(2, 2, 1, csr::kCycle)});

  const auto record = fixture.cpu_.step();
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::IllegalInstruction);
  CHECK(!record.retired);
}

TEST_CASE("vectored trap policy enters mtvec and mret resumes") {
  CpuConfig config;
  config.trap_policy = TrapPolicy::Vector;
  config.halt_on_ebreak = false;
  CpuFixture fixture(config);
  const auto handler = CpuFixture::kBase + 0x100;
  CHECK(!fixture.cpu_.csrs().write(csr::kMtvec, handler));
  fixture.load_words({kEcall});
  fixture.load_words({encode_csr(1, 2, 0, csr::kMepc), encode_i(0x13, 1, 0, 1, 4),
                      encode_csr(0, 1, 1, csr::kMepc), kMret},
                     handler);

  auto trap_record = fixture.cpu_.step();
  CHECK(trap_record.trap.has_value());
  CHECK_EQ(fixture.cpu_.pc(), handler);
  CHECK(!trap_record.retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK_EQ(fixture.cpu_.pc(), CpuFixture::kBase + 4);
}

TEST_CASE("delegated user ECALL enters supervisor mode and SRET resumes user mode") {
  CpuConfig config;
  config.trap_policy = TrapPolicy::Vector;
  config.halt_on_ebreak = false;
  CpuFixture fixture(config);
  const auto handler = CpuFixture::kBase + 0x100;
  constexpr auto machine_state = status::kMie | status::kMpie | status::kMppMask;
  CHECK(!fixture.cpu_.csrs().write(csr::kMstatus, machine_state | status::kSie));
  CHECK(!fixture.cpu_.csrs().write(
      csr::kMedeleg,
      std::uint64_t{1} <<
          static_cast<std::uint64_t>(TrapCause::EnvironmentCallFromUserMode)));
  CHECK(!fixture.cpu_.csrs().write(csr::kStvec, handler, PrivilegeLevel::Supervisor));
  CHECK(!fixture.cpu_.csrs().write(csr::kMepc, 0x1110));
  CHECK(!fixture.cpu_.csrs().write(csr::kMcause, 0x2222));
  CHECK(!fixture.cpu_.csrs().write(csr::kMtval, 0x3333));
  fixture.cpu_.set_privilege(PrivilegeLevel::User);
  fixture.load_words({kEcall});
  fixture.load_words({encode_csr(1, 2, 0, csr::kSepc), encode_i(0x13, 1, 0, 1, 4),
                      encode_csr(0, 1, 1, csr::kSepc), kSret},
                     handler);

  const auto trap_record = fixture.cpu_.step();
  CHECK(trap_record.privilege == PrivilegeLevel::User);
  CHECK(trap_record.next_privilege == PrivilegeLevel::Supervisor);
  CHECK(trap_record.trap.has_value());
  CHECK(trap_record.trap->cause == TrapCause::EnvironmentCallFromUserMode);
  CHECK(!trap_record.retired);
  CHECK(fixture.cpu_.privilege() == PrivilegeLevel::Supervisor);
  CHECK_EQ(fixture.cpu_.pc(), handler);
  CHECK_EQ(std::get<std::uint64_t>(fixture.cpu_.csrs().read(
               csr::kScause, PrivilegeLevel::Supervisor)),
           static_cast<std::uint64_t>(TrapCause::EnvironmentCallFromUserMode));
  CHECK_EQ(std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMepc)),
           std::uint64_t{0x1110});
  CHECK_EQ(std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMcause)),
           std::uint64_t{0x2222});
  CHECK_EQ(std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMtval)),
           std::uint64_t{0x3333});
  const auto trapped_status =
      std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
  CHECK_EQ(trapped_status & machine_state, machine_state);
  CHECK((trapped_status & status::kSie) == 0U);
  CHECK((trapped_status & status::kSpie) != 0U);
  CHECK((trapped_status & status::kSpp) == 0U);

  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  const auto return_record = fixture.cpu_.step();
  CHECK(return_record.retired);
  CHECK(return_record.privilege == PrivilegeLevel::Supervisor);
  CHECK(return_record.next_privilege == PrivilegeLevel::User);
  CHECK(fixture.cpu_.privilege() == PrivilegeLevel::User);
  CHECK_EQ(fixture.cpu_.pc(), CpuFixture::kBase + 4);
  CHECK_EQ(fixture.cpu_.csrs().retired_count(), std::uint64_t{4});
}

TEST_CASE("non-delegated user ECALL enters machine mode and MRET resumes user mode") {
  CpuConfig config;
  config.trap_policy = TrapPolicy::Vector;
  config.halt_on_ebreak = false;
  CpuFixture fixture(config);
  const auto handler = CpuFixture::kBase + 0x100;
  CHECK(!fixture.cpu_.csrs().write(csr::kMtvec, handler));
  CHECK(!fixture.cpu_.csrs().write(csr::kMstatus, status::kMie | status::kMprv));
  fixture.cpu_.set_privilege(PrivilegeLevel::User);
  fixture.load_words({kEcall});
  fixture.load_words({encode_csr(1, 2, 0, csr::kMepc), encode_i(0x13, 1, 0, 1, 4),
                      encode_csr(0, 1, 1, csr::kMepc), kMret},
                     handler);

  const auto trap_record = fixture.cpu_.step();
  CHECK(trap_record.trap.has_value());
  CHECK(trap_record.trap->cause == TrapCause::EnvironmentCallFromUserMode);
  CHECK(trap_record.next_privilege == PrivilegeLevel::Machine);
  CHECK(!trap_record.retired);
  const auto trapped_status =
      std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
  CHECK((trapped_status & status::kMie) == 0U);
  CHECK((trapped_status & status::kMpie) != 0U);
  CHECK_EQ((trapped_status & status::kMppMask) >> 11U,
           static_cast<std::uint64_t>(PrivilegeLevel::User));

  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  const auto return_record = fixture.cpu_.step();
  CHECK(return_record.retired);
  CHECK(return_record.next_privilege == PrivilegeLevel::User);
  CHECK(fixture.cpu_.privilege() == PrivilegeLevel::User);
  CHECK_EQ(fixture.cpu_.pc(), CpuFixture::kBase + 4);
  const auto returned_status =
      std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
  CHECK((returned_status & status::kMie) != 0U);
  CHECK((returned_status & status::kMpie) != 0U);
  CHECK_EQ(returned_status & status::kMppMask, std::uint64_t{0});
  CHECK((returned_status & status::kMprv) == 0U);
  CHECK_EQ(fixture.cpu_.csrs().retired_count(), std::uint64_t{4});
}

TEST_CASE("supervisor ECALL enters machine mode and MRET restores supervisor mode") {
  CpuConfig config;
  config.trap_policy = TrapPolicy::Vector;
  config.halt_on_ebreak = false;
  CpuFixture fixture(config);
  const auto handler = CpuFixture::kBase + 0x100;
  CHECK(!fixture.cpu_.csrs().write(csr::kMtvec, handler));
  CHECK(!fixture.cpu_.csrs().write(csr::kMstatus, status::kMprv));
  fixture.cpu_.set_privilege(PrivilegeLevel::Supervisor);
  fixture.load_words({kEcall});
  fixture.load_words({encode_csr(1, 2, 0, csr::kMepc), encode_i(0x13, 1, 0, 1, 4),
                      encode_csr(0, 1, 1, csr::kMepc), kMret},
                     handler);

  const auto trap_record = fixture.cpu_.step();
  CHECK(trap_record.trap.has_value());
  CHECK(trap_record.trap->cause == TrapCause::EnvironmentCallFromSupervisorMode);
  CHECK(trap_record.next_privilege == PrivilegeLevel::Machine);
  const auto trapped_status =
      std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
  CHECK_EQ((trapped_status & status::kMppMask) >> 11U,
           static_cast<std::uint64_t>(PrivilegeLevel::Supervisor));

  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  CHECK(fixture.cpu_.step().retired);
  const auto return_record = fixture.cpu_.step();
  CHECK(return_record.retired);
  CHECK(return_record.next_privilege == PrivilegeLevel::Supervisor);
  CHECK(fixture.cpu_.privilege() == PrivilegeLevel::Supervisor);
  CHECK_EQ(fixture.cpu_.pc(), CpuFixture::kBase + 4);
  const auto returned_status =
      std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
  CHECK((returned_status & status::kMprv) == 0U);
}

TEST_CASE("machine-origin traps ignore exception delegation") {
  CpuConfig config;
  config.trap_policy = TrapPolicy::Vector;
  CpuFixture fixture(config);
  const auto machine_handler = CpuFixture::kBase + 0x100;
  const auto supervisor_handler = CpuFixture::kBase + 0x200;
  CHECK(!fixture.cpu_.csrs().write(csr::kMtvec, machine_handler));
  CHECK(!fixture.cpu_.csrs().write(csr::kStvec, supervisor_handler,
                                   PrivilegeLevel::Supervisor));
  CHECK(!fixture.cpu_.csrs().write(
      csr::kMedeleg,
      std::uint64_t{1} << static_cast<std::uint64_t>(TrapCause::IllegalInstruction)));
  fixture.load_words({0xffffffffU});

  const auto record = fixture.cpu_.step();
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::IllegalInstruction);
  CHECK(record.next_privilege == PrivilegeLevel::Machine);
  CHECK(fixture.cpu_.privilege() == PrivilegeLevel::Machine);
  CHECK_EQ(fixture.cpu_.pc(), machine_handler);
}

TEST_CASE("ECALL reports its originating privilege without retirement") {
  struct Case {
    PrivilegeLevel privilege;
    TrapCause cause;
  };
  constexpr Case cases[] = {
      {PrivilegeLevel::User, TrapCause::EnvironmentCallFromUserMode},
      {PrivilegeLevel::Supervisor, TrapCause::EnvironmentCallFromSupervisorMode},
      {PrivilegeLevel::Machine, TrapCause::EnvironmentCallFromMachineMode},
  };
  for (const auto& test_case : cases) {
    CpuFixture fixture;
    fixture.cpu_.set_privilege(test_case.privilege);
    fixture.load_words({kEcall});
    const auto record = fixture.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == test_case.cause);
    CHECK(!record.retired);
    CHECK_EQ(fixture.cpu_.csrs().retired_count(), std::uint64_t{0});
  }
}

TEST_CASE("MRET below machine mode traps as an illegal instruction") {
  constexpr PrivilegeLevel lower_modes[] = {PrivilegeLevel::User,
                                             PrivilegeLevel::Supervisor};
  for (const auto mode : lower_modes) {
    CpuFixture fixture;
    fixture.cpu_.set_privilege(mode);
    fixture.load_words({kMret});
    const auto record = fixture.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::IllegalInstruction);
    CHECK_EQ(record.trap->value, static_cast<std::uint64_t>(kMret));
    CHECK(!record.retired);
    CHECK(fixture.cpu_.privilege() == PrivilegeLevel::Machine);
    CHECK_EQ(fixture.cpu_.csrs().retired_count(), std::uint64_t{0});
  }
}

TEST_CASE("SRET in user mode traps while machine mode may execute SRET") {
  {
    CpuFixture fixture;
    fixture.cpu_.set_privilege(PrivilegeLevel::User);
    fixture.load_words({kSret});
    const auto record = fixture.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::IllegalInstruction);
    CHECK_EQ(record.trap->value, static_cast<std::uint64_t>(kSret));
    CHECK(!record.retired);
    CHECK_EQ(fixture.cpu_.csrs().retired_count(), std::uint64_t{0});
  }
  {
    CpuFixture fixture;
    CHECK(!fixture.cpu_.csrs().write(csr::kSepc, CpuFixture::kBase + 0x80,
                                     PrivilegeLevel::Supervisor));
    CHECK(!fixture.cpu_.csrs().write(csr::kMstatus,
                                     status::kSpie | status::kSpp | status::kMprv));
    fixture.load_words({kSret});
    const auto record = fixture.cpu_.step();
    CHECK(record.retired);
    CHECK(!record.trap);
    CHECK(record.next_privilege == PrivilegeLevel::Supervisor);
    CHECK(fixture.cpu_.privilege() == PrivilegeLevel::Supervisor);
    CHECK_EQ(fixture.cpu_.pc(), CpuFixture::kBase + 0x80);
    const auto returned_status =
        std::get<std::uint64_t>(fixture.cpu_.csrs().read(csr::kMstatus));
    CHECK((returned_status & status::kSie) != 0U);
    CHECK((returned_status & status::kSpie) != 0U);
    CHECK((returned_status & status::kSpp) == 0U);
    CHECK((returned_status & status::kMprv) == 0U);
  }
}

TEST_CASE("step limits remain distinct from architectural halts") {
  CpuFixture fixture;
  fixture.load_words({encode_j(0, 0)});
  const auto result = fixture.cpu_.run(10);
  CHECK(result.reason == RunStopReason::StepLimit);
  CHECK_EQ(result.attempted_steps, std::uint64_t{10});
  CHECK_EQ(result.retired_instructions, std::uint64_t{10});
}
