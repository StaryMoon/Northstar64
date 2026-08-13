# ADR 0004: Keep the Kernel Portable Between Northstar64 and QEMU virt

- Status: Accepted
- Date: 2026-08-13

## Context

A kernel that runs only on its sibling emulator cannot distinguish a kernel defect from a machine
defect. A machine that runs only its sibling kernel has the same circular validation problem.

## Decision

The Northstar kernel will isolate platform code and target both Northstar64 and QEMU `virt`. The
emulator integration suite will continue using independent freestanding guests.

## Consequences

Platform interfaces and device assumptions must be documented. Work is duplicated at a small HAL
boundary, but each side receives an independent execution environment for diagnosis.

