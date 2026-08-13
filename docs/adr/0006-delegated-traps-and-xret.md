# ADR 0006: Centralize Delegated Trap Entry and xRET State Transitions

- Status: Accepted
- Date: 2026-08-13

## Context

The machine-mode baseline routed every synchronous exception to `mtvec` and treated every `ECALL`
as cause 11. That model cannot support a supervisor kernel: user system calls need to enter S mode,
while machine-owned exceptions must remain in M mode, and both paths must preserve independent
return stacks.

Implementing these mutations directly in CPU instruction cases would split privilege policy across
decode, execute, and trap code. It would also make partial state updates possible when an xRET is
illegal.

## Decision

`CsrFile` owns architectural trap-stack mutations. `enter_trap` receives the complete trap and its
origin privilege, then returns a `TrapEntry` containing the selected target privilege and direct
vector. It consults `medeleg` only for exceptions originating in S or U mode. An M-origin exception
always remains in M mode.

Delegated entry writes `sepc/scause/stval`, copies `SIE` to `SPIE`, clears `SIE`, and records the
origin in `SPP`. It does not change the machine trap bank or `MIE/MPIE/MPP`. Non-delegated entry
performs the corresponding machine-bank transition and does not change supervisor stack fields.

`return_from_trap` receives an explicit SRET or MRET mode and returns a `TrapReturn` containing the
restored PC and target privilege. It applies the architectural sequence:

1. copy `xPIE` into `xIE`;
2. select the target privilege from `xPP`;
3. set `xPIE` to one;
4. reset `xPP` to the least privileged supported mode;
5. clear `MPRV` when the target is below M mode.

The CPU checks instruction privilege before calling the state machine. `MRET` requires M mode;
`SRET` is legal in S or M mode. A failed legality check raises an illegal-instruction exception and
does not mutate return state. The CPU derives `ECALL` cause from the current privilege and applies
the transition result as one architectural edge.

`StepRecord` exposes both `privilege` and `next_privilege`. This keeps trap and xRET transitions
visible in deterministic JSONL traces without serializing the complete CSR file.

## Consequences

Supervisor and machine trap state can now nest independently, and transition behavior has directed
tests for U-to-S, U-to-M, S-to-M, M-origin non-delegation, status-bit restoration, illegal xRET, and
non-retirement. The public trap-policy name is now target-neutral because vectoring may select
`stvec` or `mtvec`. The v0.1 `VectorToMtvec` enumerator remains as a source-compatible alias.

This decision covers synchronous exceptions and direct trap-vector mode only. Interrupt selection,
vectored mode, `TSR`, debug return, and virtual-memory fault generation remain future work.
