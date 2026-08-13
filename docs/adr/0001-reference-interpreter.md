# ADR 0001: Keep an Interpreter as the Reference Backend

- Status: Accepted
- Date: 2026-08-13

## Context

Dynamic translation would improve throughput, but it couples decoding, control-flow discovery,
cache invalidation, and host-code generation before the architecture semantics are stable.

## Decision

The direct interpreter is the normative backend. Future threaded or JIT backends must be checked
against interpreter state and trace outcomes over generated instruction streams.

## Consequences

Early execution is slower. In exchange, each instruction has one reviewable implementation and one
precise failure boundary. Optimization can proceed without replacing the semantic oracle.

