#pragma once

#include "northstar64/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace northstar64 {

enum class AccessKind { InstructionFetch, Load, Store, ImageLoad };

struct BusFault {
  Address address{};
  std::size_t width{};
  AccessKind kind{};
  std::string detail;

  friend bool operator==(const BusFault&, const BusFault&) = default;
};

struct DeviceError {
  std::string detail;
};

using DeviceReadResult = std::variant<std::uint64_t, DeviceError>;
using DeviceWriteResult = std::optional<DeviceError>;
using BusReadResult = std::variant<std::uint64_t, BusFault>;
using BusWriteResult = std::optional<BusFault>;

class MappedDevice {
public:
  MappedDevice(Address base, std::uint64_t size, std::string name);
  virtual ~MappedDevice() = default;

  MappedDevice(const MappedDevice&) = delete;
  MappedDevice& operator=(const MappedDevice&) = delete;
  MappedDevice(MappedDevice&&) = delete;
  MappedDevice& operator=(MappedDevice&&) = delete;

  [[nodiscard]] Address base() const noexcept { return base_; }
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] std::string_view name() const noexcept { return name_; }
  [[nodiscard]] bool contains(Address address, std::size_t width) const noexcept;
  [[nodiscard]] virtual bool accepts_image() const noexcept { return false; }

  virtual DeviceReadResult read(std::uint64_t offset, std::size_t width) = 0;
  virtual DeviceWriteResult write(std::uint64_t offset, std::size_t width,
                                  std::uint64_t value) = 0;

private:
  Address base_;
  std::uint64_t size_;
  std::string name_;
};

class SparseRam final : public MappedDevice {
public:
  static constexpr std::size_t kPageSize = 4096;

  SparseRam(Address base, std::uint64_t size, std::string name = "ram");

  [[nodiscard]] bool accepts_image() const noexcept override { return true; }
  DeviceReadResult read(std::uint64_t offset, std::size_t width) override;
  DeviceWriteResult write(std::uint64_t offset, std::size_t width,
                          std::uint64_t value) override;

  [[nodiscard]] std::size_t allocated_pages() const noexcept { return pages_.size(); }
  void clear();

private:
  using Page = std::array<std::uint8_t, kPageSize>;

  [[nodiscard]] std::uint8_t read_byte(std::uint64_t offset) const;
  void write_byte(std::uint64_t offset, std::uint8_t value);

  std::map<std::uint64_t, std::unique_ptr<Page>> pages_;
};

class Bus {
public:
  template <typename Device, typename... Args>
  Device& emplace_device(Args&&... args) {
    auto candidate = std::make_unique<Device>(std::forward<Args>(args)...);
    Device& reference = *candidate;
    add_device(std::move(candidate));
    return reference;
  }

  void add_device(std::unique_ptr<MappedDevice> device);

  [[nodiscard]] BusReadResult read(Address address, std::size_t width, AccessKind kind);
  [[nodiscard]] BusWriteResult write(Address address, std::size_t width, std::uint64_t value,
                                     AccessKind kind);
  [[nodiscard]] BusWriteResult write_bytes(Address address, std::span<const std::uint8_t> bytes,
                                           AccessKind kind = AccessKind::ImageLoad);
  [[nodiscard]] bool image_range_is_valid(Address address, std::uint64_t size) const noexcept;
  [[nodiscard]] const std::vector<std::unique_ptr<MappedDevice>>& devices() const noexcept {
    return devices_;
  }

private:
  [[nodiscard]] MappedDevice* find(Address address, std::size_t width) noexcept;
  [[nodiscard]] const MappedDevice* find(Address address, std::size_t width) const noexcept;

  std::vector<std::unique_ptr<MappedDevice>> devices_;
};

const char* access_kind_name(AccessKind kind) noexcept;

} // namespace northstar64
