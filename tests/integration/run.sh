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
