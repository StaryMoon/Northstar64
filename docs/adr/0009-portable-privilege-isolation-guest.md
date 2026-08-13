# ADR 0009: Validate Privilege Isolation With One Portable Guest Image

- Status: Accepted
- Date: 2026-08-13

## Context

Unit tests can establish individual CSR, trap, and translation rules while still missing a defect in
their composition. Testing only a Northstar64-specific guest would also create circular evidence: an
emulator defect and a guest defect could agree with each other.

The integration image must therefore exercise the complete M-to-S-to-U path on both Northstar64 and
an independent implementation. QEMU `virt` is the comparison platform, but lower-privilege physical
access there requires PMP configuration. Northstar64 intentionally does not claim PMP support yet.

## Decision

Build one freestanding `RV64I_Zicsr_Zifencei` ELF with the following fixed contract:

1. M mode installs `mtvec`, probes a TOR PMP entry for QEMU, delegates user environment calls and
   load/store page faults, then enters S mode with `MRET`.
2. The PMP writes are capability probes. A precise illegal-instruction trap may skip only those two
   writes while a machine-scratch guard is set. Any other M-mode exception emits a failure marker.
   QEMU accepts the writes; Northstar64 takes the guarded path without pretending to implement PMP.
3. S mode constructs one three-level Sv39 tree. Supervisor code, page tables, stacks, RAM, and UART
   remain supervisor-only. Four U pages map code, writable data, read-only data, and a user stack.
4. S mode enters U mode with `SRET`. User output crosses an `ECALL` boundary; U mode cannot write the
   UART directly.
5. User code proves a mapped data round trip, loads an unmapped page, and stores to its read-only
   page. The S trap handler verifies `scause` and `stval`, advances `sepc`, restores a full integer
   context from an independent supervisor stack, and resumes U mode after each fault.
6. Completion is a deterministic UART transcript ending in `NS64:PASS:ISOLATION`. Northstar64 also
   runs the ELF twice and requires byte-identical JSONL traces. QEMU runs the same ELF and must emit
   the identical transcript.

The host CLI exposes vectoring explicitly as `--trap-policy vector`; halt-on-first-trap remains the
default for debugging and backward compatibility.

## Consequences

The v0.2 privilege and virtual-memory milestone now has end-to-end evidence independent of the unit
fixtures. The guest is intentionally a conformance program, not the Northstar kernel: it has no
allocator, scheduler, interrupts, user ELF loader, or general syscall ABI.

The low 1 GiB and RAM are mapped with supervisor-only gigapages for this fixed test image. That
choice is compact and auditable but is not a future kernel memory policy. PMP remains unsupported by
Northstar64; the guarded probe must be removed once PMP becomes part of the advertised platform.
