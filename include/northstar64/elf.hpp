#pragma once

#include "northstar64/memory.hpp"
#include "northstar64/types.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace northstar64 {

struct ElfSegment {
  Address virtual_address{};
  Address load_address{};
  std::uint64_t file_offset{};
  std::uint64_t file_size{};
  std::uint64_t memory_size{};
  std::uint32_t flags{};

  [[nodiscard]] bool readable() const noexcept { return (flags & 0x4U) != 0U; }
  [[nodiscard]] bool writable() const noexcept { return (flags & 0x2U) != 0U; }
  [[nodiscard]] bool executable() const noexcept { return (flags & 0x1U) != 0U; }
};

struct ElfImage {
  Address entry{};
  std::vector<ElfSegment> segments;
};

class ElfError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

ElfImage inspect_elf(const std::filesystem::path& path);
ElfImage load_elf(const std::filesystem::path& path, Bus& bus);

} // namespace northstar64

