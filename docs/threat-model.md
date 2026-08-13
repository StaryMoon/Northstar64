# Threat Model

Northstar64 parses and executes untrusted guest images. Version 0.1 treats the host process and local
operator as trusted; guest bytes are not trusted.

## Protected Assets

- host memory safety
- files outside explicitly named trace outputs
- process availability within configured resource limits
- integrity of machine state after a rejected image

## Controls

- ELF offsets and sizes are bounds-checked before reads
- integer range overflow is checked before guest address use
- PT_LOAD ranges must fit image-loadable RAM, never MMIO
- all loadable ranges validate before mutation begins
- bus mappings reject overlap
- unsupported access widths return errors
- execution has an explicit maximum attempted-step count
- CI runs AddressSanitizer, UndefinedBehaviorSanitizer, and CodeQL

## Known Limits

- a valid ELF may request zero-fill proportional to configured RAM and consume CPU time
- trace output can consume disk proportional to the step limit
- there is no process sandbox, seccomp profile, or privilege drop
- checksums and trace equality establish integrity relative to trusted files, not authenticity against
  a malicious host user
- denial-of-service resistance has not been fuzzed or independently audited

Do not expose the CLI as a multi-tenant network service without an outer sandbox and resource quotas.

