#include "northstar64/decode.hpp"
#include "northstar64/elf.hpp"
#include "northstar64/machine.hpp"
#include "northstar64/trace.hpp"
#include "northstar64/version.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace northstar64;

void usage(std::ostream& output) {
  output << "Northstar64 - deterministic RV64 machine laboratory\n\n"
         << "Usage:\n"
         << "  northstar64 run <guest.elf> [--max-steps N] [--trace FILE]"
            " [--trap-policy halt|vector]\n"
         << "  northstar64 inspect <guest.elf>\n"
         << "  northstar64 decode <32-bit-word>\n"
         << "  northstar64 verify-trace <left.jsonl> <right.jsonl>\n"
         << "  northstar64 version\n";
}

std::uint64_t parse_unsigned(std::string_view value, const char* label) {
  int base = 10;
  if (value.starts_with("0x") || value.starts_with("0X")) {
    value.remove_prefix(2);
    base = 16;
  }
  if (value.empty()) {
    throw std::invalid_argument(std::string(label) + " is empty");
  }
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    throw std::invalid_argument(std::string("invalid ") + label + ": " + std::string(value));
  }
  return parsed;
}

int inspect_command(const std::filesystem::path& path) {
  const auto image = inspect_elf(path);
  std::cout << "ELF64 RISC-V\nentry: 0x" << std::hex << image.entry << std::dec
            << "\nloadable segments: " << image.segments.size() << '\n';
  for (std::size_t index = 0; index < image.segments.size(); ++index) {
    const auto& segment = image.segments[index];
    std::cout << "  [" << index << "] load=0x" << std::hex << segment.load_address
              << " vaddr=0x" << segment.virtual_address << " filesz=0x" << segment.file_size
              << " memsz=0x" << segment.memory_size << std::dec << " flags="
              << (segment.readable() ? 'R' : '-') << (segment.writable() ? 'W' : '-')
              << (segment.executable() ? 'X' : '-') << '\n';
  }
  return EXIT_SUCCESS;
}

int decode_command(std::string_view word) {
  const auto parsed = parse_unsigned(word, "instruction word");
  if (parsed > 0xffffffffULL) {
    throw std::invalid_argument("instruction word exceeds 32 bits");
  }
  auto result = decode(static_cast<std::uint32_t>(parsed));
  if (const auto* error = std::get_if<DecodeError>(&result)) {
    std::cerr << "decode error: " << error->detail << '\n';
    return EXIT_FAILURE;
  }
  std::cout << disassemble(std::get<DecodedInstruction>(result)) << '\n';
  return EXIT_SUCCESS;
}

int verify_trace_command(const std::filesystem::path& left, const std::filesystem::path& right) {
  const auto difference = compare_trace_files(left, right);
  if (!difference) {
    std::cout << "traces are byte-for-byte identical\n";
    return EXIT_SUCCESS;
  }
  std::cerr << "trace divergence at line " << difference->line << "\nleft:  " << difference->left
            << "\nright: " << difference->right << '\n';
  return EXIT_FAILURE;
}

int run_command(int argc, char** argv) {
  if (argc < 3) {
    throw std::invalid_argument("run requires a guest ELF path");
  }
  const std::filesystem::path path = argv[2];
  std::uint64_t maximum_steps = 1'000'000;
  std::filesystem::path trace_path;
  TrapPolicy trap_policy = TrapPolicy::Halt;

  for (int index = 3; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--max-steps") {
      if (++index >= argc) {
        throw std::invalid_argument("--max-steps requires a value");
      }
      maximum_steps = parse_unsigned(argv[index], "maximum step count");
    } else if (argument == "--trace") {
      if (++index >= argc) {
        throw std::invalid_argument("--trace requires a file path");
      }
      trace_path = argv[index];
    } else if (argument == "--trap-policy") {
      if (++index >= argc) {
        throw std::invalid_argument("--trap-policy requires halt or vector");
      }
      const std::string_view value = argv[index];
      if (value == "halt") {
        trap_policy = TrapPolicy::Halt;
      } else if (value == "vector") {
        trap_policy = TrapPolicy::Vector;
      } else {
        throw std::invalid_argument("--trap-policy requires halt or vector");
      }
    } else {
      throw std::invalid_argument("unknown run option: " + std::string(argument));
    }
  }

  MachineConfig machine_config;
  machine_config.cpu.trap_policy = trap_policy;
  Machine machine(machine_config, &std::cout);
  const auto image = machine.load(path);
  std::unique_ptr<JsonlTraceWriter> trace;
  if (!trace_path.empty()) {
    trace = std::make_unique<JsonlTraceWriter>(trace_path);
  }
  const auto result = machine.run(maximum_steps, trace.get());
  if (trace) {
    trace->flush();
  }

  std::cerr << "\n[Northstar64] entry=0x" << std::hex << image.entry << std::dec
            << " attempted=" << result.attempted_steps
            << " retired=" << result.retired_instructions << " stop="
            << (result.reason == RunStopReason::Halted ? "halted" : "step-limit")
            << " detail=\"" << result.detail << "\"\n";
  if (result.reason == RunStopReason::StepLimit) {
    return 2;
  }
  if (result.terminal_trap && result.terminal_trap->cause != TrapCause::Breakpoint) {
    return 3;
  }
  return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      usage(std::cerr);
      return EXIT_FAILURE;
    }
    const std::string_view command = argv[1];
    if (command == "version" || command == "--version") {
      std::cout << "Northstar64 " << northstar64::kVersion << '\n';
      return EXIT_SUCCESS;
    }
    if (command == "inspect" && argc == 3) {
      return inspect_command(argv[2]);
    }
    if (command == "decode" && argc == 3) {
      return decode_command(argv[2]);
    }
    if (command == "verify-trace" && argc == 4) {
      return verify_trace_command(argv[2], argv[3]);
    }
    if (command == "run") {
      return run_command(argc, argv);
    }
    usage(std::cerr);
    return EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
