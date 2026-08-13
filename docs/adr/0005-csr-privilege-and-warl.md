# ADR 0005: Derive CSR Access from Address Encoding and Model WARL Choices Explicitly

- Status: Accepted
- Date: 2026-08-13

## Context

A supervisor kernel needs current privilege state and supervisor CSR access. Hard-coding access rules
inside individual instruction cases would duplicate architecture policy and make legal zero-source
reads of read-only CSRs difficult to distinguish from illegal writes.

## Decision

Every CSR access first checks the address-encoded minimum privilege (`csr[9:8]`) and read-only class
(`csr[11:10]`). Implemented-register and counter-enable checks follow. CSR instructions decide
whether they perform a write before invoking the write path, so `CSRRS/CSRRC` with a zero source can
legally read a read-only CSR.

Supervisor CSRs are views of machine state where the architecture defines aliases. `sstatus` masks
`mstatus`; `sie/sip` expose delegated supervisor interrupt bits. SXL and UXL are fixed read-only to
64 bits. `misa` is a legal fixed WARL register: writes succeed but cannot change RV64I. Trap-vector
mode is normalized to direct, and unsupported `satp.MODE` writes leave the complete register
unchanged.

## Consequences

Access failures carry a structured reason and current privilege. The current PR does not delegate
traps or implement `SRET`; lower-mode exceptions still enter M mode. Those transitions remain a
separate contract so CSR access policy can be reviewed and tested independently.

That separate transition contract was subsequently adopted in
[ADR 0006](0006-delegated-traps-and-xret.md).
