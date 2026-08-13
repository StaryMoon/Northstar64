# ISA Support Contract

## Implemented in 0.1

| Extension | Instructions / behavior |
| --- | --- |
| RV64I upper/jump | `LUI`, `AUIPC`, `JAL`, `JALR` |
| RV64I branches | `BEQ`, `BNE`, `BLT`, `BGE`, `BLTU`, `BGEU` |
| RV64I loads | `LB`, `LH`, `LW`, `LD`, `LBU`, `LHU`, `LWU` |
| RV64I stores | `SB`, `SH`, `SW`, `SD` |
| RV64I immediate ALU | `ADDI`, `SLTI`, `SLTIU`, `XORI`, `ORI`, `ANDI`, shifts |
| RV64I register ALU | `ADD`, `SUB`, `SLL`, `SLT`, `SLTU`, `XOR`, `SRL`, `SRA`, `OR`, `AND` |
| RV64I word ALU | `ADDIW`, immediate shifts, `ADDW`, `SUBW`, register shifts |
| Ordering | `FENCE`, `FENCE.I` in a single-hart interpreter |
| Zicsr | six CSR read/modify/write instructions |
| Privileged subset | `ECALL`, `EBREAK`, `MRET`, `WFI`, direct machine trap entry |

## Implemented on `main` toward 0.2

The development branch models current M/S/U privilege, enforces CSR address-encoded access rules,
and exposes the supervisor CSR bank (`sstatus`, `sie`, `stvec`, `scounteren`, trap state, `sip`, and
`satp`). User counter reads pass through both machine and supervisor enable state. The `SXL`/`UXL`
fields are fixed to 64 bits. `satp` accepts Bare and Sv39 state encodings; address translation has not
been connected to CPU accesses yet.

Synchronous exceptions consult `medeleg` only when they originate below M mode. Delegated traps
write `sepc/scause/stval`, enter direct `stvec`, and update `SIE/SPIE/SPP`. All other traps write the
machine bank, enter direct `mtvec`, and update `MIE/MPIE/MPP`. `ECALL` reports cause 8, 9, or 11 for
U, S, or M origin. `SRET` and `MRET` restore their interrupt-enable and previous-privilege stacks;
illegal lower-privilege execution traps without retiring.

An independent Sv39 walker implements canonical-address checks, three-level traversal, all three
leaf sizes, superpage alignment, U/S/SUM/MXR/R/W/X checks, reserved PTE validation, and Svade-style
A/D faults. It reads PTEs through the physical bus and returns fault provenance without modifying
page tables. This is reference semantics for the upcoming CPU integration, not a claim that current
guest fetch/load/store operations use virtual addresses.

The advertised machine ISA is `RV64I_Zicsr_Zifencei`. The current `misa` value reports RV64 with
the `I` bit; `Zicsr` and `Zifencei` are not represented by legacy `misa` extension letters.

## Intentional Constraints

- exactly one hart
- four-byte instruction alignment because the C extension is absent
- naturally aligned multi-byte loads and stores
- direct `mtvec`/`stvec` mode only
- no asynchronous interrupt source in 0.1
- `WFI` is a configurable host stop point until interrupts exist
- `FENCE`/`FENCE.I` retire as no-ops because there is one interpreter hart and no instruction cache

## Unsupported

- M, A, F, D, C, V, and bit-manipulation extensions
- CPU-connected Sv39 translation, `SFENCE.VMA`, and TLB behavior; Sv48
- asynchronous interrupt prioritization and delivery
- hypervisor state
- performance counters beyond deterministic `mcycle` and `minstret`
- upstream architectural compliance status

Unsupported or reserved encodings trap as illegal instructions; they are not silently treated as
no-ops.
