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

The advertised machine ISA is `RV64I_Zicsr_Zifencei`. The current `misa` value reports RV64 with
the `I` bit; `Zicsr` and `Zifencei` are not represented by legacy `misa` extension letters.

## Intentional Constraints

- exactly one hart
- machine mode only; all `ecall` instructions currently report machine-mode cause 11
- four-byte instruction alignment because the C extension is absent
- naturally aligned multi-byte loads and stores
- direct `mtvec` mode only
- no asynchronous interrupt source in 0.1
- `WFI` is a configurable host stop point until interrupts exist
- `FENCE`/`FENCE.I` retire as no-ops because there is one interpreter hart and no instruction cache

## Unsupported

- M, A, F, D, C, V, and bit-manipulation extensions
- S/U modes and hypervisor state
- Sv39/Sv48 virtual memory
- delegated traps and interrupt prioritization
- performance counters beyond deterministic `mcycle` and `minstret`
- upstream architectural compliance status

Unsupported or reserved encodings trap as illegal instructions; they are not silently treated as
no-ops.

