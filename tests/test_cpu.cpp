#include "northstar64/cpu.hpp"
#include "northstar64/csr.hpp"
#include "test_support.hpp"

#include <array>
#include <cstdint>
#include <variant>

using namespace northstar64;
using namespace northstar64::test;

namespace {

constexpr Address kSv39Root = CpuFixture::kBase + 0x10000;
constexpr Address kSv39LevelOne = CpuFixture::kBase + 0x11000;
constexpr Address kSv39LevelZero = CpuFixture::kBase + 0x12000;
constexpr Address kPhysicalCode = CpuFixture::kBase + 0x20000;
constexpr Address kPhysicalData = CpuFixture::kBase + 0x21000;
constexpr Address kVirtualCode = 0x0000000040000000ULL;
constexpr Address kVirtualData = 0x0000000040001000ULL;
constexpr std::uint64_t kPteV = std::uint64_t{1} << 0U;
constexpr std::uint64_t kPteR = std::uint64_t{1} << 1U;
constexpr std::uint64_t kPteW = std::uint64_t{1} << 2U;
constexpr std::uint64_t kPteX = std::uint64_t{1} << 3U;
constexpr std::uint64_t kPteU = std::uint64_t{1} << 4U;
constexpr std::uint64_t kPteA = std::uint64_t{1} << 6U;
constexpr std::uint64_t kPteD = std::uint64_t{1} << 7U;

std::array<std::uint64_t, 3> virtual_page_numbers(Address address) {
  return {(address >> 12U) & 0x1ffU, (address >> 21U) & 0x1ffU,
          (address >> 30U) & 0x1ffU};
}

std::uint64_t page_table_pointer(Address address) {
  return ((address >> 12U) << 10U) | kPteV;
}

std::uint64_t leaf_entry(Address address, std::uint64_t flags) {
  return ((address >> 12U) << 10U) | kPteV | flags;
}

class CpuSv39Fixture {
public:
  explicit CpuSv39Fixture(CpuConfig config = {}) : machine_(config) {}

  void write_pte(Address table, std::uint64_t index, std::uint64_t pte) {
    CHECK(!machine_.bus_.write(table + index * 8U, 8U, pte, AccessKind::ImageLoad));
  }

  void map_page(Address virtual_address, Address physical_address, std::uint64_t flags) {
    const auto vpn = virtual_page_numbers(virtual_address);
    write_pte(kSv39Root, vpn[2], page_table_pointer(kSv39LevelOne));
    write_pte(kSv39LevelOne, vpn[1], page_table_pointer(kSv39LevelZero));
    write_pte(kSv39LevelZero, vpn[0], leaf_entry(physical_address, flags));
  }

  void enable(PrivilegeLevel privilege, Address pc = kVirtualCode) {
    const auto satp = (std::uint64_t{8} << 60U) | (kSv39Root >> 12U);
    CHECK(!machine_.cpu_.csrs().write(csr::kSatp, satp));
    machine_.cpu_.set_privilege(privilege);
    machine_.cpu_.set_pc(pc);
  }

  CpuFixture machine_;
};

} // namespace

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

TEST_CASE("CPU executes mapped user code and data through Sv39") {
  CpuSv39Fixture fixture;
  fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
  fixture.map_page(kVirtualData, kPhysicalData,
                   kPteR | kPteW | kPteU | kPteA | kPteD);
  fixture.machine_.load_words({encode_i(0x03, 2, 3, 10, 0),
                               encode_i(0x13, 2, 0, 2, 1),
                               encode_s(0x23, 3, 10, 2, 0), kEbreak},
                              kPhysicalCode);
  CHECK(!fixture.machine_.bus_.write(kPhysicalData, 8U, 41, AccessKind::ImageLoad));
  fixture.machine_.cpu_.set_reg(10, kVirtualData);
  fixture.enable(PrivilegeLevel::User);

  const auto load_record = fixture.machine_.cpu_.step();
  CHECK(load_record.retired);
  CHECK(load_record.instruction_translation.has_value());
  CHECK_EQ(load_record.instruction_translation->virtual_address, kVirtualCode);
  CHECK_EQ(load_record.instruction_translation->physical_address, kPhysicalCode);
  CHECK(load_record.data_translation.has_value());
  CHECK_EQ(load_record.data_translation->virtual_address, kVirtualData);
  CHECK_EQ(load_record.data_translation->physical_address, kPhysicalData);
  CHECK_EQ(fixture.machine_.cpu_.reg(2), std::uint64_t{41});

  const auto add_record = fixture.machine_.cpu_.step();
  CHECK(add_record.retired);
  CHECK(add_record.instruction_translation.has_value());
  CHECK_EQ(add_record.instruction_translation->virtual_address, kVirtualCode + 4);
  CHECK_EQ(add_record.instruction_translation->physical_address, kPhysicalCode + 4);
  CHECK(!add_record.data_translation);

  const auto store_record = fixture.machine_.cpu_.step();
  CHECK(store_record.retired);
  CHECK(store_record.data_translation.has_value());
  CHECK(store_record.memory_write.has_value());
  CHECK_EQ(store_record.memory_write->address, kVirtualData);
  CHECK_EQ(store_record.memory_write->physical_address, kPhysicalData);
  CHECK_EQ(store_record.memory_write->value, std::uint64_t{42});
  CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.bus_.read(
               kPhysicalData, 8U, AccessKind::Load)),
           std::uint64_t{42});

  const auto breakpoint = fixture.machine_.cpu_.step();
  CHECK(breakpoint.trap.has_value());
  CHECK(breakpoint.trap->cause == TrapCause::Breakpoint);
  CHECK_EQ(fixture.machine_.cpu_.csrs().retired_count(), std::uint64_t{3});
}

TEST_CASE("Sv39 instruction faults distinguish unmapped and NX pages") {
  {
    CpuSv39Fixture fixture;
    fixture.enable(PrivilegeLevel::User);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::InstructionPageFault);
    CHECK_EQ(record.trap->value, kVirtualCode);
    CHECK(!record.retired);
    CHECK(!record.instruction_translation);
    CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.cpu_.csrs().read(csr::kMepc)),
             kVirtualCode);
    CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.cpu_.csrs().read(csr::kMtval)),
             kVirtualCode);
  }
  {
    CpuSv39Fixture fixture;
    fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteU | kPteA);
    fixture.machine_.load_words({kEbreak}, kPhysicalCode);
    fixture.enable(PrivilegeLevel::User);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::InstructionPageFault);
    CHECK_EQ(record.trap->value, kVirtualCode);
    CHECK(record.trap->detail.find("permission-violation") != std::string::npos);
    CHECK(!record.retired);
  }
}

TEST_CASE("Sv39 load and store permission faults are precise and non-retired") {
  {
    CpuSv39Fixture fixture;
    fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
    fixture.machine_.load_words({encode_i(0x03, 2, 3, 10, 0)}, kPhysicalCode);
    fixture.machine_.cpu_.set_reg(10, kVirtualData);
    fixture.enable(PrivilegeLevel::User);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::LoadPageFault);
    CHECK_EQ(record.trap->value, kVirtualData);
    CHECK(record.instruction_translation.has_value());
    CHECK(!record.data_translation);
    CHECK_EQ(fixture.machine_.cpu_.reg(2), std::uint64_t{0});
    CHECK_EQ(fixture.machine_.cpu_.csrs().retired_count(), std::uint64_t{0});
  }
  {
    CpuSv39Fixture fixture;
    fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
    fixture.map_page(kVirtualData, kPhysicalData, kPteR | kPteU | kPteA | kPteD);
    fixture.machine_.load_words({encode_s(0x23, 3, 10, 2, 0)}, kPhysicalCode);
    CHECK(!fixture.machine_.bus_.write(kPhysicalData, 8U, 7, AccessKind::ImageLoad));
    fixture.machine_.cpu_.set_reg(2, 99);
    fixture.machine_.cpu_.set_reg(10, kVirtualData);
    fixture.enable(PrivilegeLevel::User);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::StorePageFault);
    CHECK_EQ(record.trap->value, kVirtualData);
    CHECK(!record.retired);
    CHECK(!record.memory_write);
    CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.bus_.read(
                 kPhysicalData, 8U, AccessKind::Load)),
             std::uint64_t{7});
    CHECK_EQ(fixture.machine_.cpu_.csrs().retired_count(), std::uint64_t{0});
  }
}

TEST_CASE("delegated Sv39 page faults enter supervisor trap state") {
  CpuConfig config;
  config.trap_policy = TrapPolicy::Vector;
  CpuSv39Fixture fixture(config);
  constexpr Address handler = 0x0000000040002000ULL;
  fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
  fixture.machine_.load_words({encode_i(0x03, 2, 3, 10, 0)}, kPhysicalCode);
  fixture.machine_.cpu_.set_reg(10, kVirtualData);
  CHECK(!fixture.machine_.cpu_.csrs().write(
      csr::kMedeleg,
      std::uint64_t{1} << static_cast<std::uint64_t>(TrapCause::LoadPageFault)));
  CHECK(!fixture.machine_.cpu_.csrs().write(csr::kStvec, handler,
                                             PrivilegeLevel::Supervisor));
  fixture.enable(PrivilegeLevel::User);

  const auto record = fixture.machine_.cpu_.step();
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::LoadPageFault);
  CHECK(record.next_privilege == PrivilegeLevel::Supervisor);
  CHECK_EQ(record.next_pc, handler);
  CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.cpu_.csrs().read(
               csr::kSepc, PrivilegeLevel::Supervisor)),
           kVirtualCode);
  CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.cpu_.csrs().read(
               csr::kScause, PrivilegeLevel::Supervisor)),
           static_cast<std::uint64_t>(TrapCause::LoadPageFault));
  CHECK_EQ(std::get<std::uint64_t>(fixture.machine_.cpu_.csrs().read(
               csr::kStval, PrivilegeLevel::Supervisor)),
           kVirtualData);
}

TEST_CASE("Sv39 CPU accesses honor MXR and SUM") {
  {
    CpuSv39Fixture fixture;
    fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
    fixture.map_page(kVirtualData, kPhysicalData, kPteX | kPteU | kPteA);
    fixture.machine_.load_words({encode_i(0x03, 2, 3, 10, 0)}, kPhysicalCode);
    CHECK(!fixture.machine_.bus_.write(kPhysicalData, 8U, 123, AccessKind::ImageLoad));
    fixture.machine_.cpu_.set_reg(10, kVirtualData);
    CHECK(!fixture.machine_.cpu_.csrs().write(csr::kMstatus, status::kMxr));
    fixture.enable(PrivilegeLevel::User);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.retired);
    CHECK_EQ(fixture.machine_.cpu_.reg(2), std::uint64_t{123});
  }
  {
    CpuSv39Fixture fixture;
    fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteA);
    fixture.map_page(kVirtualData, kPhysicalData,
                     kPteR | kPteW | kPteU | kPteA | kPteD);
    fixture.machine_.load_words({encode_i(0x03, 2, 3, 10, 0),
                                 encode_s(0x23, 3, 10, 2, 0)},
                                kPhysicalCode);
    CHECK(!fixture.machine_.bus_.write(kPhysicalData, 8U, 55, AccessKind::ImageLoad));
    fixture.machine_.cpu_.set_reg(10, kVirtualData);
    CHECK(!fixture.machine_.cpu_.csrs().write(csr::kMstatus, status::kSum));
    fixture.enable(PrivilegeLevel::Supervisor);
    CHECK(fixture.machine_.cpu_.step().retired);
    CHECK_EQ(fixture.machine_.cpu_.reg(2), std::uint64_t{55});
    CHECK(fixture.machine_.cpu_.step().retired);
  }
}

TEST_CASE("Sv39 physical target faults remain access faults") {
  CpuSv39Fixture fixture;
  constexpr Address unmapped_physical = CpuFixture::kBase + 0x200000;
  fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
  fixture.map_page(kVirtualData, unmapped_physical, kPteR | kPteU | kPteA);
  fixture.machine_.load_words({encode_i(0x03, 2, 3, 10, 0)}, kPhysicalCode);
  fixture.machine_.cpu_.set_reg(10, kVirtualData);
  fixture.enable(PrivilegeLevel::User);

  const auto record = fixture.machine_.cpu_.step();
  CHECK(record.trap.has_value());
  CHECK(record.trap->cause == TrapCause::LoadAccessFault);
  CHECK_EQ(record.trap->value, kVirtualData);
  CHECK(record.data_translation.has_value());
  CHECK_EQ(record.data_translation->physical_address, unmapped_physical);
}

TEST_CASE("unmapped Sv39 page tables raise original access-specific faults") {
  {
    CpuSv39Fixture fixture;
    const auto satp = (std::uint64_t{8} << 60U) | ((CpuFixture::kBase + 0x200000) >> 12U);
    CHECK(!fixture.machine_.cpu_.csrs().write(csr::kSatp, satp));
    fixture.machine_.cpu_.set_privilege(PrivilegeLevel::User);
    fixture.machine_.cpu_.set_pc(kVirtualCode);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::InstructionAccessFault);
    CHECK_EQ(record.trap->value, kVirtualCode);
    CHECK(!record.retired);
  }
  struct Case {
    std::uint32_t instruction;
    TrapCause cause;
  };
  constexpr Case cases[] = {
      {encode_i(0x03, 2, 3, 10, 0), TrapCause::LoadAccessFault},
      {encode_s(0x23, 3, 10, 2, 0), TrapCause::StoreAccessFault},
  };
  constexpr Address isolated_data = 0x0000000080001000ULL;
  for (const auto& test_case : cases) {
    CpuSv39Fixture fixture;
    fixture.map_page(kVirtualCode, kPhysicalCode, kPteR | kPteX | kPteU | kPteA);
    fixture.machine_.load_words({test_case.instruction}, kPhysicalCode);
    fixture.machine_.cpu_.set_reg(10, isolated_data);
    fixture.enable(PrivilegeLevel::User);
    const auto root_index = virtual_page_numbers(isolated_data)[2];
    fixture.write_pte(kSv39Root, root_index,
                      page_table_pointer(CpuFixture::kBase + 0x200000));
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == test_case.cause);
    CHECK_EQ(record.trap->value, isolated_data);
    CHECK(!record.retired);
  }
}

TEST_CASE("machine mode bypasses Sv39 while Bare lower modes use identity addresses") {
  {
    CpuSv39Fixture fixture;
    fixture.machine_.load_words({encode_i(0x13, 1, 0, 0, 9)}, kPhysicalCode);
    const auto satp = (std::uint64_t{8} << 60U) | ((CpuFixture::kBase + 0x200000) >> 12U);
    CHECK(!fixture.machine_.cpu_.csrs().write(csr::kSatp, satp));
    fixture.machine_.cpu_.set_pc(kPhysicalCode);
    const auto record = fixture.machine_.cpu_.step();
    CHECK(record.retired);
    CHECK_EQ(fixture.machine_.cpu_.reg(1), std::uint64_t{9});
    CHECK(record.instruction_translation.has_value());
    CHECK_EQ(record.instruction_translation->virtual_address, kPhysicalCode);
    CHECK_EQ(record.instruction_translation->physical_address, kPhysicalCode);
  }
  {
    CpuFixture fixture;
    fixture.cpu_.set_privilege(PrivilegeLevel::User);
    fixture.load_words({encode_i(0x13, 1, 0, 0, 11)});
    const auto record = fixture.cpu_.step();
    CHECK(record.retired);
    CHECK_EQ(fixture.cpu_.reg(1), std::uint64_t{11});
    CHECK(record.instruction_translation.has_value());
    CHECK_EQ(record.instruction_translation->virtual_address, CpuFixture::kBase);
    CHECK_EQ(record.instruction_translation->physical_address, CpuFixture::kBase);
  }
}

TEST_CASE("SFENCE VMA is a privilege-checked no-op without a TLB") {
  {
    CpuFixture fixture;
    fixture.cpu_.set_privilege(PrivilegeLevel::Supervisor);
    fixture.load_words({encode_sfence_vma(1, 2)});
    const auto record = fixture.cpu_.step();
    CHECK(record.retired);
    CHECK(!record.trap);
  }
  {
    CpuFixture fixture;
    fixture.cpu_.set_privilege(PrivilegeLevel::User);
    fixture.load_words({encode_sfence_vma(0, 0)});
    const auto record = fixture.cpu_.step();
    CHECK(record.trap.has_value());
    CHECK(record.trap->cause == TrapCause::IllegalInstruction);
    CHECK_EQ(record.trap->value,
             static_cast<std::uint64_t>(encode_sfence_vma(0, 0)));
    CHECK(!record.retired);
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
