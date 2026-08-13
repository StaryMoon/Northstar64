# ADR 0007: Establish Sv39 Semantics in an Independent, Side-Effect-Free Walker

- Status: Accepted
- Date: 2026-08-13

## Context

Connecting translation directly inside fetch, load, and store would make early virtual-memory
failures ambiguous: a bad physical bus access, page-table defect, trap-routing defect, or CPU
execution defect could produce the same guest symptom. Sv39 also contains policy choices that need
to be explicit, especially A/D handling and optional PTE extensions.

The normative reference is the RISC-V Privileged ISA supervisor chapter. The implemented profile
does not include Svnapot, Svpbmt, or hardware A/D updates.

## Decision

Implement `walk_sv39` as a component independent of the CPU. Its complete input is:

- root physical page number;
- original 64-bit virtual address;
- effective U or S privilege;
- instruction-fetch, load, or store access type;
- current SUM and MXR values.

The walker reads complete 64-bit PTEs through `Bus` using a distinct `PageTableWalk` access kind.
It returns either `Sv39Translation` or `Sv39Fault`. Successful translations preserve the leaf level,
PTE address, and PTE value. Faults preserve the original virtual address plus the level, PTE address,
PTE value, reason, and diagnostic detail available at the decision point.
Faults raised before the first PTE read use level `-1` and PTE address/value zero.

The implemented algorithm validates:

1. sign extension of virtual-address bit 38;
2. each PTE's V and reserved W-without-R encoding;
3. high bits reserved by the profile and reserved U/A/D state on non-leaf PTEs;
4. 1 GiB and 2 MiB leaf physical alignment;
5. U/S access, including SUM for S-mode load/store and the unconditional S-mode execute ban on U
   pages;
6. R/W/X permissions and MXR for loads only;
7. A for every access and D for stores.

The A/D policy follows Svade-style software management: clear A or D causes a typed fault. The
walker never updates a PTE. This keeps the reference implementation deterministic and avoids an
atomic read-modify-write contract before the memory subsystem supports one.

## Evidence

Directed tests cover both canonical address regions, every leaf level, physical-walk failures,
invalid and reserved PTEs, misaligned superpages, U/S/SUM/MXR permissions, and A/D failures. A
fixed-seed generator builds 256 valid page-table shapes and compares production results against a
separate test-only reference algorithm.

## Consequences

Sv39 semantics can be reviewed and fuzzed without running guest instructions. CPU integration can
later translate fetch/load/store and map typed outcomes to architecture-specific access/page-fault
causes without duplicating the page-table algorithm.

This decision does not provide a TLB, `SFENCE.VMA`, PMP/PMA checks, CPU-connected virtual memory, or
atomic hardware A/D updates. Those capabilities require separate contracts and evidence.
