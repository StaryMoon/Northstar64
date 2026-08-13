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
- current M/S/U privilege and machine/supervisor CSR state
- attempted-step sequence number
- halted state and terminal trap

Synchronous exceptions do not retire. The CSR state machine selects M or S according to origin and
`medeleg`, atomically updates the selected trap bank and privilege stack, and returns a target mode
and direct trap vector to the CPU. M-origin exceptions never delegate. Halt-mode execution leaves
the PC at the faulting instruction after recording architectural trap state; vector-mode execution
enters the selected direct `mtvec` or `stvec` address.

`MRET` and `SRET` reverse the corresponding privilege stack before the CPU applies the returned PC
and mode. Their legality is checked before any return state is mutated. Traces retain both the
before and after privilege so trap and return edges can be inspected without an internal snapshot.

### Address translation

`sv39.cpp` is an independent translation component between a future CPU virtual-access boundary and
the physical bus. Its input names the root PPN, effective U/S privilege, access type, SUM, and MXR.
Its result is either a physical address with leaf-level/PTE provenance or a typed fault with the
original virtual address, walk level, PTE address, and PTE value.

The walker supports 4 KiB, 2 MiB, and 1 GiB leaves and validates canonical addresses, reserved PTE
encodings, superpage alignment, U/S permissions, SUM, MXR, and R/W/X. It implements software-managed
A/D behavior: clear A, or clear D on a store, returns a fault without modifying the PTE. Physical PTE
reads use the explicit `PageTableWalk` bus access kind.

The CPU calls the walker for S/U instruction fetches, loads, and stores when `satp.MODE=Sv39`.
Machine-mode accesses and all accesses under Bare use identity translation. A translation rejection
becomes the access-specific page-fault cause; failure to read a PTE from the physical bus and failure
after a successful translation become access faults. All architectural trap values retain the
original virtual address.

`SFENCE.VMA` is decoded and privilege-checked as the translation-cache invalidation boundary. It
retires without a cache action because there is no TLB yet. Traces keep architectural PC/data
addresses while recording virtual/physical translation pairs and physical memory-write targets.

### Trace

Each `StepRecord` is emitted as one ordered JSON object. Numeric architecture values are fixed-width
hex strings; counters and widths are JSON numbers. No timestamp, host path, pointer, thread ID, or
unordered container is serialized.

## Invariants

1. x0 always reads zero and never appears as a register write.
2. Every mapped physical address belongs to at most one device.
3. Instruction retirement occurs exactly once after successful execution.
4. A synchronous trap preserves the faulting PC in exactly one selected `xepc` trap bank.
5. Failed ELF validation does not modify guest memory.
6. The same machine state and deterministic inputs produce the same `StepRecord` sequence.
7. The interpreter is the reference semantics for future optimized backends.
8. An exception originating in M mode never delegates to S mode.
9. The standalone Sv39 walker never mutates guest page tables.
10. A translated access fault records the original virtual address as trap value.

## Extension Points

- `MappedDevice`: CLINT, PLIC, virtio-mmio, framebuffer, and test devices
- address translation between CPU virtual accesses and the physical bus
- event source consumed at instruction boundaries for record/replay
- alternative execution backend checked against interpreter traces
- debugger sink consuming `StepRecord` and state snapshots
