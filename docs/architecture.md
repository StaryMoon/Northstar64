# Architecture

## Design Center

Northstar64 is a deterministic machine model, not a high-performance emulator yet. The first
priority is semantic clarity: an instruction's inputs, architectural effects, and failure mode must
be visible and testable. Performance work comes after an interpreter provides a reference backend.

## Layering

### Frontend

`src/main.cpp` owns argument parsing, process exit codes, terminal streams, and file names. The core
library never reads process arguments or writes diagnostics directly.

### Image loader

`elf.cpp` parses untrusted bytes with explicit little-endian reads and range checks. It accepts only
ELF64, little-endian, RISC-V `ET_EXEC` images with identity-mapped load addresses. All PT_LOAD ranges
are validated against image-loadable RAM before the first byte is written.

This all-or-nothing range validation prevents a rejected image from leaving a partially loaded
machine. A future Sv39-aware loader may relax identity mapping, but that requires an explicit ADR.

### Physical bus

The bus owns non-overlapping `MappedDevice` regions. CPU code names an address, width, operation
kind, and value; it never downcasts a device. Device errors are translated into bus faults, then into
architecture-specific instruction/load/store traps at the CPU boundary.

Sparse RAM allocates 4 KiB host pages on the first non-zero write. Reads from untouched pages return
zero. Zero-filling ELF BSS therefore preserves sparseness.

### CPU

Decode is a pure transformation from a 32-bit word into either a typed instruction or a reasoned
decode error. Execute consumes that instruction and records at most one integer-register write and
one memory write. That is sufficient for RV64I; future atomic or vector extensions will require an
explicit trace-schema change.

The CPU tracks:

- 32 integer registers, with x0 enforced after every step
- program counter
- machine-mode CSR state
- attempted-step sequence number
- halted state and terminal trap

Synchronous exceptions do not retire. The trap captures the faulting PC, and halt-mode execution
leaves the PC at that instruction. Vector-mode execution writes trap CSRs and enters direct `mtvec`.

### Trace

Each `StepRecord` is emitted as one ordered JSON object. Numeric architecture values are fixed-width
hex strings; counters and widths are JSON numbers. No timestamp, host path, pointer, thread ID, or
unordered container is serialized.

## Invariants

1. x0 always reads zero and never appears as a register write.
2. Every mapped physical address belongs to at most one device.
3. Instruction retirement occurs exactly once after successful execution.
4. A synchronous trap preserves the faulting PC in `mepc`.
5. Failed ELF validation does not modify guest memory.
6. The same machine state and deterministic inputs produce the same `StepRecord` sequence.
7. The interpreter is the reference semantics for future optimized backends.

## Extension Points

- `MappedDevice`: CLINT, PLIC, virtio-mmio, framebuffer, and test devices
- address translation between CPU virtual accesses and the physical bus
- event source consumed at instruction boundaries for record/replay
- alternative execution backend checked against interpreter traces
- debugger sink consuming `StepRecord` and state snapshots

