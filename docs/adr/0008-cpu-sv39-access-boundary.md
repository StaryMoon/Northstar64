# ADR 0008: Route CPU Architectural Addresses Through a Single Sv39 Boundary

- Status: Accepted
- Date: 2026-08-13

## Context

The independent walker establishes page-table semantics, but instruction fetches, loads, and stores
still reached the physical bus directly. Connecting each operation separately would duplicate mode
selection, SUM/MXR state, fault classification, and trace behavior. It could also lose the original
virtual address after translation.

## Decision

The CPU uses one `translate` boundary for instruction fetch, load, and store. Translation is active
when current privilege is S or U and `satp.MODE=Sv39`. Machine mode and Bare use an explicit identity
result. MPRV does not yet override data-access privilege.

The boundary reads root PPN from `satp` and SUM/MXR from `mstatus`, invokes the independent walker,
and classifies outcomes before the physical access:

- page-table semantic rejection becomes cause 12, 13, or 15 according to the original operation;
- inability to read a PTE through the physical bus becomes the corresponding access-fault cause;
- a bus failure after successful translation is also an access fault;
- `tval`, diagnostic provenance, and the architectural operation retain the original virtual
  address in every case.

Misalignment is checked on the architectural virtual address before translation. A faulting
instruction never retires and does not publish a register or memory write.

`StepRecord` preserves `pc` and data-operation inputs as virtual addresses. It adds independent
instruction/data translation records containing virtual and physical addresses. A memory write
retains its v0.1 architectural `address`, `width`, and `value` fields and appends the translated
physical target without changing their order or meaning.

`SFENCE.VMA` is decoded with both register operands and requires S or M privilege. It currently
retires without an internal action because the machine has no TLB. This establishes the invalidation
instruction and privilege contract before cached translations exist.

## Evidence

CPU-level fixtures construct real three-level page tables in guest RAM. Tests execute user code from
a mapped virtual page, load and store through a separate virtual data page, and inspect both physical
RAM and translation records. Negative tests cover unmapped and NX fetches, unmapped loads, read-only
stores, SUM, MXR, delegated page faults, physical target failures, page-table physical failures,
Bare identity access, M-mode bypass, and illegal U-mode `SFENCE.VMA`.

## Consequences

S/U guest execution now has working Sv39 address translation with precise synchronous faults. The
walker remains independently testable, while the CPU owns architecture-specific cause selection and
retirement behavior.

There is still no TLB, cross-page split-access machinery, MPRV data override, hardware A/D update,
PMP/PMA enforcement, or upstream architecture-suite claim. These remain separate reviewable changes.
