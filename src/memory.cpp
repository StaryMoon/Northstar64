#include "northstar64/memory.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace northstar64 {
namespace {

bool valid_width(std::size_t width) noexcept {
  return width == 1U || width == 2U || width == 4U || width == 8U;
}

bool ranges_overlap(Address first_base, std::uint64_t first_size, Address second_base,
                    std::uint64_t second_size) noexcept {
  if (first_size == 0U || second_size == 0U) {
    return false;
  }
  const auto first_end = first_base + first_size - 1U;
  const auto second_end = second_base + second_size - 1U;
  return first_base <= second_end && second_base <= first_end;
}

BusFault make_fault(Address address, std::size_t width, AccessKind kind, std::string detail) {
  return BusFault{address, width, kind, std::move(detail)};
}

} // namespace

MappedDevice::MappedDevice(Address base, std::uint64_t size, std::string name)
    : base_(base), size_(size), name_(std::move(name)) {
  if (size_ == 0U) {
    throw std::invalid_argument("a mapped device cannot have zero size");
  }
  if (add_overflows(base_, size_ - 1U)) {
    throw std::invalid_argument("mapped device address range overflows");
  }
  if (name_.empty()) {
    throw std::invalid_argument("a mapped device requires a name");
  }
}

bool MappedDevice::contains(Address address, std::size_t width) const noexcept {
  if (!valid_width(width) || address < base_) {
    return false;
  }
  const auto offset = address - base_;
  return offset < size_ && static_cast<std::uint64_t>(width) <= size_ - offset;
}

SparseRam::SparseRam(Address base, std::uint64_t size, std::string name)
    : MappedDevice(base, size, std::move(name)) {}

DeviceReadResult SparseRam::read(std::uint64_t offset, std::size_t width) {
  if (!valid_width(width) || offset >= size() || static_cast<std::uint64_t>(width) > size() - offset) {
    return DeviceError{"RAM read exceeds the mapped range"};
  }

  std::uint64_t value = 0;
  for (std::size_t index = 0; index < width; ++index) {
    value |= static_cast<std::uint64_t>(read_byte(offset + index)) << (index * 8U);
  }
  return value;
}

DeviceWriteResult SparseRam::write(std::uint64_t offset, std::size_t width,
                                   std::uint64_t value) {
  if (!valid_width(width) || offset >= size() || static_cast<std::uint64_t>(width) > size() - offset) {
    return DeviceError{"RAM write exceeds the mapped range"};
  }

  for (std::size_t index = 0; index < width; ++index) {
    const auto byte = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
    write_byte(offset + index, byte);
  }
  return std::nullopt;
}

std::uint8_t SparseRam::read_byte(std::uint64_t offset) const {
  const auto page_number = offset / kPageSize;
  const auto page_offset = static_cast<std::size_t>(offset % kPageSize);
  const auto iterator = pages_.find(page_number);
  if (iterator == pages_.end()) {
    return 0;
  }
  return (*iterator->second)[page_offset];
}

void SparseRam::write_byte(std::uint64_t offset, std::uint8_t value) {
  const auto page_number = offset / kPageSize;
  const auto page_offset = static_cast<std::size_t>(offset % kPageSize);
  const auto existing = pages_.find(page_number);
  if (value == 0U && existing == pages_.end()) {
    return;
  }
  auto& page = pages_[page_number];
  if (!page) {
    page = std::make_unique<Page>();
    page->fill(0);
  }
  (*page)[page_offset] = value;
}

void SparseRam::clear() { pages_.clear(); }

void Bus::add_device(std::unique_ptr<MappedDevice> device) {
  if (!device) {
    throw std::invalid_argument("cannot map a null device");
  }
  for (const auto& existing : devices_) {
    if (ranges_overlap(existing->base(), existing->size(), device->base(), device->size())) {
      throw std::invalid_argument("device '" + std::string(device->name()) + "' overlaps '" +
                                  std::string(existing->name()) + "'");
    }
  }
  devices_.push_back(std::move(device));
  std::sort(devices_.begin(), devices_.end(), [](const auto& left, const auto& right) {
    return left->base() < right->base();
  });
}

MappedDevice* Bus::find(Address address, std::size_t width) noexcept {
  for (auto& device : devices_) {
    if (device->contains(address, width)) {
      return device.get();
    }
  }
  return nullptr;
}

const MappedDevice* Bus::find(Address address, std::size_t width) const noexcept {
  for (const auto& device : devices_) {
    if (device->contains(address, width)) {
      return device.get();
    }
  }
  return nullptr;
}

BusReadResult Bus::read(Address address, std::size_t width, AccessKind kind) {
  if (!valid_width(width)) {
    return make_fault(address, width, kind, "unsupported access width");
  }
  auto* device = find(address, width);
  if (device == nullptr) {
    return make_fault(address, width, kind, "address is not mapped");
  }
  auto result = device->read(address - device->base(), width);
  if (const auto* error = std::get_if<DeviceError>(&result)) {
    return make_fault(address, width, kind,
                      "device '" + std::string(device->name()) + "': " + error->detail);
  }
  return std::get<std::uint64_t>(result);
}

BusWriteResult Bus::write(Address address, std::size_t width, std::uint64_t value,
                          AccessKind kind) {
  if (!valid_width(width)) {
    return make_fault(address, width, kind, "unsupported access width");
  }
  auto* device = find(address, width);
  if (device == nullptr) {
    return make_fault(address, width, kind, "address is not mapped");
  }
  if (auto error = device->write(address - device->base(), width, value)) {
    return make_fault(address, width, kind,
                      "device '" + std::string(device->name()) + "': " + error->detail);
  }
  return std::nullopt;
}

BusWriteResult Bus::write_bytes(Address address, std::span<const std::uint8_t> bytes,
                                AccessKind kind) {
  if (bytes.empty()) {
    return std::nullopt;
  }
  if (add_overflows(address, static_cast<std::uint64_t>(bytes.size() - 1U))) {
    return make_fault(address, bytes.size(), kind, "byte range overflows the address space");
  }
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (auto fault = write(address + index, 1U, bytes[index], kind)) {
      return fault;
    }
  }
  return std::nullopt;
}

bool Bus::image_range_is_valid(Address address, std::uint64_t size) const noexcept {
  if (size == 0U) {
    return true;
  }
  if (add_overflows(address, size - 1U)) {
    return false;
  }
  const auto* device = find(address, 1U);
  return device != nullptr && device->accepts_image() &&
         device->contains(address + size - 1U, 1U);
}

const char* access_kind_name(AccessKind kind) noexcept {
  switch (kind) {
  case AccessKind::InstructionFetch:
    return "instruction-fetch";
  case AccessKind::Load:
    return "load";
  case AccessKind::Store:
    return "store";
  case AccessKind::PageTableWalk:
    return "page-table-walk";
  case AccessKind::ImageLoad:
    return "image-load";
  }
  return "unknown";
}

} // namespace northstar64
