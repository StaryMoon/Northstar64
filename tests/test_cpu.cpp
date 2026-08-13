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
  config.trap_policy = TrapPolicy::VectorToMtvec;
  config.halt_on_ebreak = false;
  CpuFixture fixture(config);
  const auto handler = CpuFixture::kBase + 0x100;
  CHECK(!fixture.cpu_.csrs().write(csr::kMtvec, handler));
  fixture.load_words({0x00000073U});
  fixture.load_words({encode_csr(1, 2, 0, csr::kMepc), encode_i(0x13, 1, 0, 1, 4),
                      encode_csr(0, 1, 1, csr::kMepc), 0x30200073U},
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

TEST_CASE("step limits remain distinct from architectural halts") {
  CpuFixture fixture;
  fixture.load_words({encode_j(0, 0)});
  const auto result = fixture.cpu_.run(10);
  CHECK(result.reason == RunStopReason::StepLimit);
  CHECK_EQ(result.attempted_steps, std::uint64_t{10});
  CHECK_EQ(result.retired_instructions, std::uint64_t{10});
}
