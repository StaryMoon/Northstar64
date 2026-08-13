# Determinism and Replay

## Guarantee

For version 0.1, identical guest bytes, machine configuration, initial state, and maximum step count
produce a byte-identical JSONL trace on supported hosts.

This guarantee covers architectural evidence, not wall-clock runtime. It does not promise equal host
performance or a stable trace schema across major versions.

## Why It Holds

- one execution thread and one hart
- no host clock, random source, filesystem call, network input, or UART input during execution
- explicit little-endian device and ELF semantics
- stable device traversal ordered by base address
- stable JSON field ordering and fixed-width architecture values
- no pointer values or host-dependent type names in the trace

Each attempted step records both `privilege` and `next_privilege`. Equal values describe an
ordinary instruction or an in-mode trap; a different pair exposes a trap-entry or xRET privilege
edge directly. The 0.2 development schema inserts `next_privilege` immediately after `next_pc`.

## Comparison

`verify-trace` compares streams without loading them into memory and reports the first different line.
Because one line corresponds to one attempted step, this gives a direct divergence sequence number.

```bash
northstar64 verify-trace expected.jsonl observed.jsonl
```

The verifier establishes equality of the recorded evidence. It does not prove the ISA implementation
is correct; independent architecture tests and differential testing are required for that claim.

## Future External Events

Timer, UART receive, disk completion, and network input will enter through a versioned event log:

```text
event sequence | injection boundary | device | payload | integrity digest
```

Replay will consume events only at recorded instruction boundaries. The trace will record event-log
identity, while event payloads remain separate so large packets do not inflate every step record.
