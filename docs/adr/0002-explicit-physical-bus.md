# ADR 0002: Route All Physical Access Through an Explicit Bus

- Status: Accepted
- Date: 2026-08-13

## Context

Embedding address checks for RAM, UART, and future devices in the CPU makes execution logic depend on
platform composition and produces ambiguous failures at region boundaries.

## Decision

Every physical target implements `MappedDevice`. `Bus` owns disjoint regions and translates device
errors into access-kind-aware bus faults. ELF loading may write only devices that opt into image load.

## Consequences

Device access has a virtual-call cost in the reference backend. The machine gains explicit mapping
invariants, independently testable devices, and a stable attachment point for CLINT/PLIC/virtio.

