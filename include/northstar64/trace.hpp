#pragma once

#include "northstar64/decode.hpp"
#include "northstar64/trap.hpp"
#include "northstar64/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace northstar64 {

struct RegisterWrite {
  std::uint8_t index{};
  std::uint64_t value{};

  friend bool operator==(const RegisterWrite&, const RegisterWrite&) = default;
};

struct MemoryWrite {
  Address address{};
  std::size_t width{};
  std::uint64_t value{};

  friend bool operator==(const MemoryWrite&, const MemoryWrite&) = default;
};

struct StepRecord {
  std::uint64_t sequence{};
  Address pc{};
  std::uint32_t instruction{};
  std::string assembly;
  Address next_pc{};
  bool retired{};
  bool halted{};
  std::optional<RegisterWrite> register_write;
  std::optional<MemoryWrite> memory_write;
  std::optional<Trap> trap;

  friend bool operator==(const StepRecord&, const StepRecord&) = default;
};

class TraceSink {
public:
  virtual ~TraceSink() = default;
  virtual void append(const StepRecord& record) = 0;
};

class JsonlTraceWriter final : public TraceSink {
public:
  explicit JsonlTraceWriter(const std::filesystem::path& path);
  void append(const StepRecord& record) override;
  void flush();

private:
  std::ofstream output_;
};

struct TraceDifference {
  std::size_t line{};
  std::string left;
  std::string right;
};

std::string format_trace_record(const StepRecord& record);
std::optional<TraceDifference> compare_trace_files(const std::filesystem::path& left,
                                                   const std::filesystem::path& right);

} // namespace northstar64

