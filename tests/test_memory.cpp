#include "northstar64/memory.hpp"
#include "northstar64/uart.hpp"
#include "test_support.hpp"

#include <memory>
#include <variant>

using namespace northstar64;

TEST_CASE("sparse RAM is zero-filled and allocates on demand") {
  SparseRam ram(0x1000, 0x10000);
  CHECK_EQ(std::get<std::uint64_t>(ram.read(0x2345, 8)), std::uint64_t{0});
  CHECK_EQ(ram.allocated_pages(), std::size_t{0});
  CHECK(!ram.write(0x2345, 1, 0));
  CHECK_EQ(ram.allocated_pages(), std::size_t{0});
  CHECK(!ram.write(0x2345, 4, 0xaabbccddU));
  CHECK_EQ(ram.allocated_pages(), std::size_t{1});
  CHECK_EQ(std::get<std::uint64_t>(ram.read(0x2345, 4)), std::uint64_t{0xaabbccddU});
}

TEST_CASE("bus preserves little-endian values across pages") {
  Bus bus;
  bus.emplace_device<SparseRam>(0x80000000, 0x20000);
  CHECK(!bus.write(0x80000fff, 8, 0x8877665544332211ULL, AccessKind::Store));
  const auto result = bus.read(0x80000fff, 8, AccessKind::Load);
  CHECK_EQ(std::get<std::uint64_t>(result), std::uint64_t{0x8877665544332211ULL});
}

TEST_CASE("bus rejects overlaps and unmapped accesses") {
  Bus bus;
  bus.emplace_device<SparseRam>(0x1000, 0x1000, "first");
  CHECK_THROWS(std::invalid_argument,
               bus.emplace_device<SparseRam>(0x1800, 0x1000, "overlap"));
  const auto result = bus.read(0x5000, 4, AccessKind::Load);
  CHECK(std::holds_alternative<BusFault>(result));
  CHECK_EQ(std::get<BusFault>(result).address, Address{0x5000});
}

TEST_CASE("image ranges cannot target MMIO") {
  Bus bus;
  bus.emplace_device<SparseRam>(0x80000000, 0x1000);
  bus.emplace_device<Uart16550>(0x10000000);
  CHECK(bus.image_range_is_valid(0x80000000, 0x1000));
  CHECK(!bus.image_range_is_valid(0x10000000, 1));
  CHECK(!bus.image_range_is_valid(0x80000fff, 2));
}
