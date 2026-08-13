#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <northstar64-binary> <guest-build-dir>" >&2
  exit 64
fi

emulator="$1"
guest_dir="$2"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

"$emulator" inspect "$guest_dir/hello.elf" >"$work_dir/inspect.txt"
grep -F "ELF64 RISC-V" "$work_dir/inspect.txt"
grep -F "flags=R-X" "$work_dir/inspect.txt"
grep -F "flags=RW-" "$work_dir/inspect.txt"

"$emulator" run "$guest_dir/hello.elf" --max-steps 1000 \
  --trace "$work_dir/first.jsonl" >"$work_dir/guest.stdout" 2>"$work_dir/guest.stderr"
printf 'NS64\n' >"$work_dir/expected.stdout"
cmp "$work_dir/expected.stdout" "$work_dir/guest.stdout"
grep -F 'stop=halted' "$work_dir/guest.stderr"
grep -F 'retired=' "$work_dir/guest.stderr"
grep -F '"name":"breakpoint"' "$work_dir/first.jsonl"

"$emulator" run "$guest_dir/hello.elf" --max-steps 1000 \
  --trace "$work_dir/second.jsonl" >/dev/null 2>/dev/null
"$emulator" verify-trace "$work_dir/first.jsonl" "$work_dir/second.jsonl"

"$emulator" inspect "$guest_dir/isolation.elf" >"$work_dir/isolation.inspect.txt"
grep -F "ELF64 RISC-V" "$work_dir/isolation.inspect.txt"
grep -F "flags=R-X" "$work_dir/isolation.inspect.txt"
grep -F "flags=RW-" "$work_dir/isolation.inspect.txt"

"$emulator" run "$guest_dir/isolation.elf" --max-steps 20000 \
  --trap-policy vector --trace "$work_dir/isolation-first.jsonl" \
  >"$work_dir/isolation.stdout" 2>"$work_dir/isolation.stderr"
cmp "$(dirname "$0")/isolation.expected" "$work_dir/isolation.stdout"
grep -F 'stop=halted' "$work_dir/isolation.stderr"
grep -F 'WFI reached with no interrupt source configured' "$work_dir/isolation.stderr"

test "$(grep -cF '"name":"environment-call-from-user-mode"' \
  "$work_dir/isolation-first.jsonl")" -eq 6
test "$(grep -cF '"name":"load-page-fault"' \
  "$work_dir/isolation-first.jsonl")" -eq 1
test "$(grep -cF '"name":"store-page-fault"' \
  "$work_dir/isolation-first.jsonl")" -eq 1
grep -F '"name":"load-page-fault","value":"0x0000000040004000"' \
  "$work_dir/isolation-first.jsonl"
grep -F '"name":"store-page-fault","value":"0x0000000040002000"' \
  "$work_dir/isolation-first.jsonl"
grep -F '"privilege":"M"' "$work_dir/isolation-first.jsonl" | grep -F '"next_privilege":"S"'
grep -F '"privilege":"S"' "$work_dir/isolation-first.jsonl" | grep -F '"next_privilege":"U"'
grep -F '"privilege":"U"' "$work_dir/isolation-first.jsonl" | grep -F '"next_privilege":"S"'

"$emulator" run "$guest_dir/isolation.elf" --max-steps 20000 \
  --trap-policy vector --trace "$work_dir/isolation-second.jsonl" >/dev/null 2>/dev/null
"$emulator" verify-trace "$work_dir/isolation-first.jsonl" \
  "$work_dir/isolation-second.jsonl"

set +e
"$emulator" run "$guest_dir/loop.elf" --max-steps 32 >/dev/null 2>"$work_dir/loop.stderr"
status=$?
set -e
if [[ $status -ne 2 ]]; then
  echo "expected loop guest to stop with exit code 2, got $status" >&2
  exit 1
fi
grep -F 'attempted=32' "$work_dir/loop.stderr"
grep -F 'stop=step-limit' "$work_dir/loop.stderr"

echo "integration guest passed"
