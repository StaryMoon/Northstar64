# Changelog

All notable changes to Northstar64 are documented here. The project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) while the public interfaces stabilize.

## [Unreleased]

### Added

- Explicit M/S/U privilege state and privilege-aware CSR instruction execution.
- Supervisor CSR views, counter-enable gates, and Bare/Sv39 `satp` WARL state.
- Structured CSR access errors and privilege information in deterministic traces.

## [0.1.0] - 2026-08-13

### Added

- Dependency-free C++20 RV64I interpreter with RV64 word operations.
- Machine-mode CSR file, direct trap entry, `mret`, `ecall`, and `ebreak` behavior.
- Explicit physical bus, sparse RAM, and a minimal 16550 UART.
- Strict ELF64 RISC-V loader with bounds, mapping, overlap, and entry-point checks.
- Deterministic JSONL traces and a first-divergence verifier.
- `run`, `inspect`, `decode`, `verify-trace`, and `version` CLI commands.
- Unit tests, sanitizer checks, compiler matrix, CodeQL, and a cross-compiled RV64I guest test.

[Unreleased]: https://github.com/StaryMoon/Northstar64/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/StaryMoon/Northstar64/releases/tag/v0.1.0
