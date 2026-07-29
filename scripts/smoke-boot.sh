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

echo "[smoke] booting for ${TIMEOUT_SEC}s (serial → $LOG)..."

set +e
timeout --signal=KILL "$TIMEOUT_SEC" qemu-system-i386 \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -m 512M \
  -smp 2,sockets=1,cores=2,threads=1 \
  -display none \
  -serial stdio \
  -drive "if=none,id=vd0,file=${DISK},format=raw,cache=writethrough" \
  -device virtio-blk-pci,drive=vd0,disable-legacy=on \
  >"$LOG" 2>&1
rc=$?
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
