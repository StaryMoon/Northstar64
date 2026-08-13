#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <qemu-system-riscv64> <isolation-guest.elf>" >&2
  exit 64
fi

qemu="$1"
guest="$2"
script_dir="$(cd "$(dirname "$0")" && pwd)"
work_dir="$(mktemp -d)"
pid=""

cleanup() {
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  rm -rf "$work_dir"
}
trap cleanup EXIT

"$qemu" \
  -machine virt \
  -bios none \
  -display none \
  -monitor none \
  -serial stdio \
  -kernel "$guest" \
  >"$work_dir/qemu.stdout" 2>"$work_dir/qemu.stderr" &
pid=$!

complete=false
for _ in $(seq 1 100); do
  if grep -qF 'NS64:PASS:ISOLATION' "$work_dir/qemu.stdout"; then
    complete=true
    break
  fi
  if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" || true
    echo "QEMU exited before the isolation guest completed" >&2
    cat "$work_dir/qemu.stderr" >&2
    exit 1
  fi
  sleep 0.05
done

if [[ "$complete" != true ]]; then
  echo "timed out waiting for the QEMU isolation marker" >&2
  cat "$work_dir/qemu.stdout" >&2
  cat "$work_dir/qemu.stderr" >&2
  exit 1
fi

# Let the byte following the marker reach the host file before terminating a
# guest that deliberately remains parked in WFI.
sleep 0.1
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""

cmp "$script_dir/isolation.expected" "$work_dir/qemu.stdout"
echo "QEMU isolation guest passed"
