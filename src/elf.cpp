#include "northstar64/elf.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace northstar64 {
namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::uint32_t kLoadableSegment = 1;
constexpr std::uint16_t kElfExecutable = 2;
constexpr std::uint16_t kMachineRiscV = 243;

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw ElfError("cannot open ELF file: " + path.string());
  }
  const auto end = input.tellg();
  if (end < 0) {
    throw ElfError("cannot determine ELF file size: " + path.string());
  }
  const auto size = static_cast<std::uint64_t>(end);
  if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw ElfError("ELF file is too large for this host");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!input) {
    throw ElfError("failed while reading ELF file: " + path.string());
  }
  return bytes;
}

void require_range(std::span<const std::uint8_t> bytes, std::uint64_t offset, std::uint64_t size,
                   const char* description) {
  if (offset > bytes.size() || size > bytes.size() - offset) {
    throw ElfError(std::string(description) + " extends beyond the ELF file");
  }
}

template <typename T>
T read_little_endian(std::span<const std::uint8_t> bytes, std::uint64_t offset) {
  require_range(bytes, offset, sizeof(T), "integer field");
  T value = 0;
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    value |= static_cast<T>(bytes[static_cast<std::size_t>(offset) + index]) << (index * 8U);
  }
  return value;
}

ElfImage parse(std::span<const std::uint8_t> bytes) {
  require_range(bytes, 0, kElfHeaderSize, "ELF header");
  if (bytes[0] != 0x7fU || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
    throw ElfError("input is not an ELF file");
  }
  if (bytes[4] != 2U) {
    throw ElfError("only ELF64 images are supported");
  }
  if (bytes[5] != 1U) {
    throw ElfError("only little-endian ELF images are supported");
  }
  if (bytes[6] != 1U) {
    throw ElfError("unsupported ELF identification version");
  }

  const auto type = read_little_endian<std::uint16_t>(bytes, 16);
  if (type != kElfExecutable) {
    throw ElfError("only fixed-address ET_EXEC images are supported");
  }
  if (read_little_endian<std::uint16_t>(bytes, 18) != kMachineRiscV) {
    throw ElfError("ELF image is not for the RISC-V architecture");
  }
  if (read_little_endian<std::uint32_t>(bytes, 20) != 1U) {
    throw ElfError("unsupported ELF header version");
  }
  if (read_little_endian<std::uint16_t>(bytes, 52) != kElfHeaderSize) {
    throw ElfError("unexpected ELF64 header size");
  }

  const auto program_offset = read_little_endian<std::uint64_t>(bytes, 32);
  const auto program_entry_size = read_little_endian<std::uint16_t>(bytes, 54);
  const auto program_count = read_little_endian<std::uint16_t>(bytes, 56);
  if (program_count != 0U && program_entry_size != kProgramHeaderSize) {
    throw ElfError("unexpected ELF64 program-header size");
  }
  if (program_count > 4096U) {
    throw ElfError("ELF image has an unreasonable number of program headers");
  }
  const auto table_size = static_cast<std::uint64_t>(program_entry_size) * program_count;
  require_range(bytes, program_offset, table_size, "program-header table");

  ElfImage image;
  image.entry = read_little_endian<std::uint64_t>(bytes, 24);
  for (std::uint16_t index = 0; index < program_count; ++index) {
    const auto offset = program_offset + static_cast<std::uint64_t>(index) * program_entry_size;
    if (read_little_endian<std::uint32_t>(bytes, offset) != kLoadableSegment) {
      continue;
    }
    const auto flags = read_little_endian<std::uint32_t>(bytes, offset + 4U);
    const auto file_offset = read_little_endian<std::uint64_t>(bytes, offset + 8U);
    const auto virtual_address = read_little_endian<std::uint64_t>(bytes, offset + 16U);
    const auto physical_address = read_little_endian<std::uint64_t>(bytes, offset + 24U);
    const auto file_size = read_little_endian<std::uint64_t>(bytes, offset + 32U);
    const auto memory_size = read_little_endian<std::uint64_t>(bytes, offset + 40U);
    const auto alignment = read_little_endian<std::uint64_t>(bytes, offset + 48U);

    if (file_size > memory_size) {
      throw ElfError("PT_LOAD file size exceeds its in-memory size");
    }
    require_range(bytes, file_offset, file_size, "PT_LOAD payload");
    if (alignment != 0U && (alignment & (alignment - 1U)) != 0U) {
      throw ElfError("PT_LOAD alignment is not a power of two");
    }
    const auto load_address = physical_address == 0U ? virtual_address : physical_address;
    if (load_address != virtual_address) {
      throw ElfError("non-identity PT_LOAD virtual and physical addresses require an MMU");
    }
    if (memory_size != 0U && add_overflows(load_address, memory_size - 1U)) {
      throw ElfError("PT_LOAD memory range overflows the guest address space");
    }
    image.segments.push_back(
        ElfSegment{virtual_address, load_address, file_offset, file_size, memory_size, flags});
  }

  if (image.segments.empty()) {
    throw ElfError("ELF image contains no loadable segments");
  }

  std::sort(image.segments.begin(), image.segments.end(), [](const auto& left, const auto& right) {
    return left.load_address < right.load_address;
  });
  for (std::size_t index = 1; index < image.segments.size(); ++index) {
    const auto& previous = image.segments[index - 1U];
    const auto& current = image.segments[index];
    if (previous.memory_size != 0U &&
        current.load_address < previous.load_address + previous.memory_size) {
      throw ElfError("overlapping PT_LOAD memory ranges are rejected");
    }
  }

  const bool entry_is_executable =
      std::any_of(image.segments.begin(), image.segments.end(), [&](const auto& segment) {
        return segment.executable() && image.entry >= segment.virtual_address &&
               image.entry - segment.virtual_address < segment.memory_size;
      });
  if (!entry_is_executable) {
    throw ElfError("ELF entry point is outside every executable PT_LOAD segment");
  }
  return image;
}

} // namespace

ElfImage inspect_elf(const std::filesystem::path& path) { return parse(read_file(path)); }

ElfImage load_elf(const std::filesystem::path& path, Bus& bus) {
  const auto bytes = read_file(path);
  auto image = parse(bytes);

  for (const auto& segment : image.segments) {
    if (!bus.image_range_is_valid(segment.load_address, segment.memory_size)) {
      std::ostringstream message;
      message << "PT_LOAD range [0x" << std::hex << segment.load_address << ", 0x"
              << (segment.load_address + segment.memory_size) << ") is outside image-loadable RAM";
      throw ElfError(message.str());
    }
  }

  // Validation above is all-or-nothing: no guest memory changes before every segment is known valid.
  for (const auto& segment : image.segments) {
    const auto payload = std::span<const std::uint8_t>(bytes).subspan(
        static_cast<std::size_t>(segment.file_offset), static_cast<std::size_t>(segment.file_size));
    if (auto fault = bus.write_bytes(segment.load_address, payload)) {
      throw ElfError("unexpected bus fault while loading PT_LOAD: " + fault->detail);
    }
    for (std::uint64_t offset = segment.file_size; offset < segment.memory_size; ++offset) {
      if (auto fault = bus.write(segment.load_address + offset, 1U, 0U, AccessKind::ImageLoad)) {
        throw ElfError("unexpected bus fault while zero-filling PT_LOAD: " + fault->detail);
      }
    }
  }
  return image;
}

} // namespace northstar64
