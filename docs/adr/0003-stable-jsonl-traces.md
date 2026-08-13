# ADR 0003: Use Stable JSONL as the First Execution Evidence Format

- Status: Accepted
- Date: 2026-08-13

## Context

Binary traces are compact but difficult to inspect during early architecture work. Pretty terminal
logs are readable but unstable and hard to consume incrementally.

## Decision

Emit one ordered JSON object per attempted step. Exclude host timing and addresses. Compare traces as
byte streams and report the first divergent line.

## Consequences

Trace files are larger than a binary format. They are streamable, diffable, language-neutral, and
usable as CI artifacts. A binary format may be added later with JSONL retained as the debug surface.

