#include "northstar64/trace.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace northstar64 {
namespace {

std::string escape_json(std::string_view input) {
  std::ostringstream output;
  for (const char raw_character : input) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character < 0x20U) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned>(character) << std::dec;
      } else {
        output << static_cast<char>(character);
      }
    }
  }
  return output.str();
}

std::string hex_value(std::uint64_t value, unsigned digits) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(static_cast<int>(digits)) << std::setfill('0') << value;
  return output.str();
}

} // namespace

JsonlTraceWriter::JsonlTraceWriter(const std::filesystem::path& path)
    : output_(path, std::ios::out | std::ios::trunc) {
  if (!output_) {
    throw std::runtime_error("cannot open trace output: " + path.string());
  }
}

void JsonlTraceWriter::append(const StepRecord& record) {
  output_ << format_trace_record(record) << '\n';
  if (!output_) {
    throw std::runtime_error("failed while writing execution trace");
  }
}

void JsonlTraceWriter::flush() {
  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed while flushing execution trace");
  }
}

std::string format_trace_record(const StepRecord& record) {
  std::ostringstream output;
  output << "{\"sequence\":" << record.sequence << ",\"pc\":\"" << hex_value(record.pc, 16)
         << "\",\"privilege\":\"" << privilege_name(record.privilege)
         << "\",\"instruction\":\"" << hex_value(record.instruction, 8) << "\",\"assembly\":\""
         << escape_json(record.assembly) << "\",\"next_pc\":\"" << hex_value(record.next_pc, 16)
         << "\",\"retired\":" << (record.retired ? "true" : "false")
         << ",\"halted\":" << (record.halted ? "true" : "false") << ",\"register_write\":";

  if (record.register_write) {
    output << "{\"index\":" << static_cast<unsigned>(record.register_write->index)
           << ",\"value\":\"" << hex_value(record.register_write->value, 16) << "\"}";
  } else {
    output << "null";
  }
  output << ",\"memory_write\":";
  if (record.memory_write) {
    output << "{\"address\":\"" << hex_value(record.memory_write->address, 16)
           << "\",\"width\":" << record.memory_write->width << ",\"value\":\""
           << hex_value(record.memory_write->value,
                        static_cast<unsigned>(record.memory_write->width * 2U))
           << "\"}";
  } else {
    output << "null";
  }
  output << ",\"trap\":";
  if (record.trap) {
    output << "{\"cause\":" << static_cast<std::uint64_t>(record.trap->cause) << ",\"name\":\""
           << trap_name(record.trap->cause) << "\",\"value\":\""
           << hex_value(record.trap->value, 16) << "\",\"detail\":\""
           << escape_json(record.trap->detail) << "\"}";
  } else {
    output << "null";
  }
  output << '}';
  return output.str();
}

std::optional<TraceDifference> compare_trace_files(const std::filesystem::path& left,
                                                   const std::filesystem::path& right) {
  std::ifstream left_input(left);
  if (!left_input) {
    throw std::runtime_error("cannot open left trace: " + left.string());
  }
  std::ifstream right_input(right);
  if (!right_input) {
    throw std::runtime_error("cannot open right trace: " + right.string());
  }

  std::size_t line_number = 0;
  while (true) {
    std::string left_line;
    std::string right_line;
    const auto has_left = static_cast<bool>(std::getline(left_input, left_line));
    const auto has_right = static_cast<bool>(std::getline(right_input, right_line));
    if (!has_left && !has_right) {
      return std::nullopt;
    }
    ++line_number;
    if (has_left != has_right || left_line != right_line) {
      return TraceDifference{line_number, has_left ? left_line : "<end-of-file>",
                             has_right ? right_line : "<end-of-file>"};
    }
  }
}

} // namespace northstar64
