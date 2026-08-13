# Contributing

Northstar64 accepts focused fixes, tests, documentation, and design proposals. Changes to ISA
semantics, privilege behavior, memory ordering, trace schemas, or public interfaces should begin with
an issue and usually an architecture decision record.

## Local Checks

```bash
make clean
make check
```

With CMake:

```bash
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

If `riscv64-unknown-elf-gcc` is installed:

```bash
make -C tests/integration
tests/integration/run.sh ./.build/northstar64 ./build/guest
```

## Change Contract

- Add a negative test for malformed inputs and fault paths, not only a happy-path test.
- Keep guest-visible behavior independent of host endianness and wall-clock time.
- Do not silently accept unsupported encodings or device accesses.
- Update `docs/isa-support.md` only after executable evidence exists.
- Avoid mixing refactors and semantic changes in one commit.
- Run `git diff --check` before opening a pull request.

## Commit Style

Use an imperative subject that names the behavior, for example:

```text
Reject overlapping ELF load segments
Record CSR failures as illegal-instruction traps
Add Sv39 leaf permission tests
```

## Pull Requests

Describe:

1. the architecture or engineering problem,
2. the chosen invariant,
3. tests that fail without the change,
4. known limits that remain.

