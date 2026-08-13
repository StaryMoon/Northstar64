#include "northstar64/trace.hpp"
#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace northstar64;

namespace {

class TemporaryFile {
public:
  explicit TemporaryFile(std::string_view suffix)
      : path_(std::filesystem::temp_directory_path() /
              ("northstar64-test-" + std::to_string(counter_++) + std::string(suffix))) {}

  ~TemporaryFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  inline static std::uint64_t counter_ = 0;
  std::filesystem::path path_;
};

} // namespace

TEST_CASE("trace records have stable ordered JSON fields") {
  StepRecord record;
  record.sequence = 7;
  record.pc = 0x80000000;
  record.instruction = 0x02a00093;
  record.assembly = "addi x1, x0, 42";
  record.next_pc = 0x80000004;
  record.next_privilege = PrivilegeLevel::User;
  record.retired = true;
  record.register_write = RegisterWrite{1, 42};
  record.instruction_translation = AddressTranslation{0x40000000, 0x80020000};
  record.data_translation = AddressTranslation{0x40001000, 0x80021000};
  record.memory_write = MemoryWrite{0x40001000, 8, 42, 0x80021000};

  const auto formatted = format_trace_record(record);
  CHECK(formatted.starts_with(
      "{\"sequence\":7,\"pc\":\"0x0000000080000000\",\"privilege\":\"M\","
      "\"instruction\":\"0x02a00093\""));
  CHECK(formatted.find("\"register_write\":{\"index\":1,\"value\":\"0x000000000000002a\"}") !=
        std::string::npos);
  CHECK(formatted.find("\"next_privilege\":\"U\"") != std::string::npos);
  CHECK(formatted.find(
            "\"instruction_translation\":{\"virtual_address\":\"0x0000000040000000\","
            "\"physical_address\":\"0x0000000080020000\"}") != std::string::npos);
  CHECK(formatted.find(
            "\"data_translation\":{\"virtual_address\":\"0x0000000040001000\","
            "\"physical_address\":\"0x0000000080021000\"}") != std::string::npos);
  CHECK(formatted.find(
            "\"memory_write\":{\"address\":\"0x0000000040001000\",\"width\":8,"
            "\"value\":\"0x000000000000002a\","
            "\"physical_address\":\"0x0000000080021000\"}") != std::string::npos);
  CHECK(formatted.ends_with("\"trap\":null}"));
}

TEST_CASE("trace verification finds the first divergent line") {
  TemporaryFile left("-left.jsonl");
  TemporaryFile right("-right.jsonl");
  {
    std::ofstream output(left.path());
    output << "one\ntwo\nthree\n";
  }
  {
    std::ofstream output(right.path());
    output << "one\nchanged\nthree\n";
  }
  const auto difference = compare_trace_files(left.path(), right.path());
  CHECK(difference.has_value());
  CHECK_EQ(difference->line, std::size_t{2});
  CHECK_EQ(difference->left, std::string("two"));
  CHECK_EQ(difference->right, std::string("changed"));
}

TEST_CASE("trace verification accepts byte-identical executions") {
  TemporaryFile left("-left.jsonl");
  TemporaryFile right("-right.jsonl");
  {
    std::ofstream output(left.path());
    output << "same\ntrace\n";
  }
  std::filesystem::copy_file(left.path(), right.path());
  CHECK(!compare_trace_files(left.path(), right.path()));
}
