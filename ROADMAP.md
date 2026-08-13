# Roadmap

Northstar64 grows vertically: each milestone adds one complete machine/kernel capability and the
evidence needed to defend it. Dates are intentionally omitted. A milestone closes when its exit
criteria pass, not when enough code has accumulated.

## 0.1 - Deterministic Machine Baseline

**Status:** implemented

- RV64I integer execution and word operations
- machine-mode CSRs and synchronous traps
- explicit bus, sparse RAM, and UART
- strict ELF64 loader
- deterministic traces and CLI
- host matrix, sanitizers, and real RV64I guest integration

Exit criterion: a cross-compiled freestanding guest prints over UART, exercises RAM, stops at a
breakpoint, and produces identical traces across repeated runs.

## 0.2 - Privilege and Virtual Memory

- [x] model M/S/U privilege transitions
- [x] supervisor CSRs and synchronous trap delegation
- [x] implement precise `MRET`/`SRET` state restoration
- [x] implement an independent Sv39 page-table walker
- connect CPU virtual accesses and add a software TLB
- enforce R/W/X/U/A/D permissions and canonical virtual addresses
- add `sfence.vma`
- differential page-table tests against an independent reference walker

Exit criterion: a supervisor guest enters user mode, faults on forbidden memory, handles the trap,
and resumes; randomized page-table cases agree with the reference model.

## 0.3 - Interrupt-Capable Platform

- deterministic virtual clock
- CLINT machine timer and software interrupts
- PLIC external interrupt routing
- UART receive interrupts
- WFI wakeup semantics
- record/replay format for asynchronous input

Exit criterion: timer-driven preemption is replayable instruction-for-instruction from an event log.

## 0.4 - Northstar Kernel Foundations

- freestanding kernel build that boots on Northstar64 and QEMU `virt`
- physical page allocator and Sv39 kernel page tables
- trap frames, system calls, and copy-in/copy-out
- preemptive round-robin scheduler
- user ELF loader and two isolated user processes
- serial shell with `ps`, `mem`, and `uptime`

Exit criterion: the same kernel image runs on both machines and demonstrates process isolation under
negative tests, including invalid pointers and executable-data violations.

## 0.5 - Files and Processes

- VFS contracts, inode/dentry cache, and file descriptors
- in-memory filesystem followed by a documented on-disk filesystem
- `fork`, `exec`, `wait`, pipes, and redirection
- buffer cache with crash-consistency tests
- host-side image inspector and fsck tool

Exit criterion: a scripted shell pipeline survives an emulator restart with a validated filesystem
image.

## 0.6 - Network Stack

- virtio-mmio transport and virtio block/network devices
- Ethernet, ARP, IPv4, ICMP, UDP, and a deliberately bounded TCP implementation
- packet capture and deterministic packet replay
- protocol state-machine tests with loss, duplication, and reordering

Exit criterion: a user process serves a file over TCP, and the same captured packet schedule replays
to the same state transitions.

## Long-Term Work

- RV64M/A/C extensions and upstream architecture tests
- GDB remote serial protocol and reverse stepping from checkpoints
- multi-hart memory model experiments
- tiered basic-block translation with interpreter differential testing
- fuzzing of decoders, ELF images, page tables, and device state machines
