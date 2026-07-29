#!/usr/bin/env bash
# Headless QEMU smoke boot — verifies kernel reaches scheduler_start.
# Usage: scripts/smoke-boot.sh [timeout_sec]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TIMEOUT_SEC="${1:-30}"
KERNEL="${KERNEL:-$ROOT/build/kernel.bin}"
INITRD="${INITRD:-$ROOT/build/drivers/initrd.img}"
DISK="${DISK:-$ROOT/disk.img}"
LOG="${SMOKE_LOG:-$ROOT/build/smoke-boot.log}"

for f in "$KERNEL" "$INITRD" "$DISK"; do
  if [[ ! -f "$f" ]]; then
    echo "[smoke] missing artifact: $f" >&2
    exit 1
  fi
done

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"

QEMU_ARGS=(
  -kernel "$KERNEL"
  -initrd "$INITRD"
  -m 512M
  -smp 4,sockets=1,cores=4,threads=1
  -display none
  -serial stdio
  # snapshot=on: no exclusive write lock on host disk.img
  -drive "if=none,id=vd0,file=${DISK},format=raw,snapshot=on"
  -device virtio-blk-pci,drive=vd0,disable-legacy=on
)

echo "[smoke] booting for ${TIMEOUT_SEC}s (serial → $LOG)..."

set +e
if command -v timeout >/dev/null 2>&1; then
  timeout --signal=KILL "$TIMEOUT_SEC" qemu-system-i386 "${QEMU_ARGS[@]}" >"$LOG" 2>&1
  rc=$?
elif command -v gtimeout >/dev/null 2>&1; then
  gtimeout --signal=KILL "$TIMEOUT_SEC" qemu-system-i386 "${QEMU_ARGS[@]}" >"$LOG" 2>&1
  rc=$?
else
  # macOS / environments without GNU timeout
  qemu-system-i386 "${QEMU_ARGS[@]}" >"$LOG" 2>&1 &
  qpid=$!
  sleep "$TIMEOUT_SEC"
  kill -KILL "$qpid" 2>/dev/null || true
  wait "$qpid" 2>/dev/null
  rc=0
fi
set -e

# timeout returns 124 (or 137 with KILL); that is expected.
if grep -q '\[boot\] scheduler_start' "$LOG"; then
  echo "[smoke] OK — reached [boot] scheduler_start"
  if grep -q '\[boot\] GUI stack ready' "$LOG"; then
    echo "[smoke] OK — GUI stack ready"
  elif grep -q '\[boot\] GUI stack unavailable' "$LOG"; then
    echo "[smoke] WARN — GUI stack unavailable (console mode)"
  fi
  exit 0
fi

echo "[smoke] FAIL — did not see [boot] scheduler_start (qemu rc=$rc)" >&2
echo "---- last 80 lines ----" >&2
tail -n 80 "$LOG" >&2 || true
exit 1
