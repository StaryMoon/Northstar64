#include "northstar64/elf.hpp"
#include "northstar64/memory.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <variant>
#include <vector>

using namespace northstar64;

namespace {

class TemporaryElf {
public:
  explicit TemporaryElf(std::vector<std::uint8_t> bytes)
      : path_(std::filesystem::temp_directory_path() /
              ("northstar64-elf-" + std::to_string(counter_++) + ".elf")) {
    std::ofstream output(path_, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }

  ~TemporaryElf() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  inline static std::uint64_t counter_ = 0;
  std::filesystem::path path_;
};

template <typename T>
void put(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
}

std::vector<std::uint8_t> minimal_elf(Address load_address = 0x80000000ULL,
                                      std::uint64_t memory_size = 16) {
  std::vector<std::uint8_t> bytes(0x104, 0);
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;
  put<std::uint16_t>(bytes, 16, 2);
  put<std::uint16_t>(bytes, 18, 243);
  put<std::uint32_t>(bytes, 20, 1);
  put<std::uint64_t>(bytes, 24, load_address);
  put<std::uint64_t>(bytes, 32, 64);
  put<std::uint16_t>(bytes, 52, 64);
  put<std::uint16_t>(bytes, 54, 56);
  put<std::uint16_t>(bytes, 56, 1);

  put<std::uint32_t>(bytes, 64, 1);
  put<std::uint32_t>(bytes, 68, 5);
  put<std::uint64_t>(bytes, 72, 0x100);
  put<std::uint64_t>(bytes, 80, load_address);
  put<std::uint64_t>(bytes, 88, load_address);
  put<std::uint64_t>(bytes, 96, 4);
  put<std::uint64_t>(bytes, 104, memory_size);
  put<std::uint64_t>(bytes, 112, 0x1000);
  bytes[0x100] = 0x13;
  bytes[0x101] = 0x00;
  bytes[0x102] = 0x00;
  bytes[0x103] = 0x00;
  return bytes;
}

} // namespace

TEST_CASE("ELF loader maps payload and zero-fills BSS") {
  TemporaryElf elf(minimal_elf());
  Bus bus;
  auto& ram = bus.emplace_device<SparseRam>(0x80000000, 0x10000);
  CHECK(!bus.write(0x80000008, 1, 0xff, AccessKind::Store));

  const auto image = load_elf(elf.path(), bus);
  CHECK_EQ(image.entry, Address{0x80000000});
  CHECK_EQ(image.segments.size(), std::size_t{1});
  CHECK_EQ(std::get<std::uint64_t>(bus.read(0x80000000, 4, AccessKind::Load)),
           std::uint64_t{0x13});
  CHECK_EQ(std::get<std::uint64_t>(bus.read(0x80000008, 1, AccessKind::Load)),
           std::uint64_t{0});
  CHECK_EQ(ram.allocated_pages(), std::size_t{1});
}

TEST_CASE("ELF loader rejects images outside RAM before mutation") {
  TemporaryElf elf(minimal_elf(0x90000000));
  Bus bus;
  bus.emplace_device<SparseRam>(0x80000000, 0x10000);
  CHECK(!bus.write(0x80000000, 4, 0xdeadbeefU, AccessKind::Store));
  CHECK_THROWS(ElfError, load_elf(elf.path(), bus));
  CHECK_EQ(std::get<std::uint64_t>(bus.read(0x80000000, 4, AccessKind::Load)),
           std::uint64_t{0xdeadbeefU});
}

TEST_CASE("ELF parser rejects malformed architecture and segment sizes") {
  auto wrong_machine = minimal_elf();
  put<std::uint16_t>(wrong_machine, 18, 62);
  TemporaryElf first(std::move(wrong_machine));
  CHECK_THROWS(ElfError, inspect_elf(first.path()));

  auto oversized_file = minimal_elf();
  put<std::uint64_t>(oversized_file, 96, 32);
  put<std::uint64_t>(oversized_file, 104, 16);
  TemporaryElf second(std::move(oversized_file));
  CHECK_THROWS(ElfError, inspect_elf(second.path()));
}

TEST_CASE("ELF parser requires an executable entry point") {
  auto bytes = minimal_elf();
  put<std::uint32_t>(bytes, 68, 6);
  TemporaryElf elf(std::move(bytes));
  CHECK_THROWS(ElfError, inspect_elf(elf.path()));
}
