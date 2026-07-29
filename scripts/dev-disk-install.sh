#!/usr/bin/env bash
# Dev helper: build userspace artifacts and (re)install them onto disk.img,
# then optionally boot QEMU. Kernel stays independent of this package layout.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "[dev-disk-install] building userspace + disk image..."
xmake -j"$JOBS" userspace disk

echo "[dev-disk-install] disk.img layout (mounted as /):"
echo "  /init.exec             — PID1"
echo "  /system/bin/*.exec     — OS/GUI package (window-manager, shell, terminal, ...)"
echo "  /system/lib/*.dynlib   — dynamic libraries"
echo "  /applications/*.exec   — user apps"
echo "  /system/share/*        — icons / wallpaper"
echo "  /system/etc/environment"
echo "  (+ /dev /proc /sys /tmp virtual mounts at runtime)"
echo
echo "Boot with:  xmake run"
echo "Or:         xmake build qemu"

if [[ "${1:-}" == "--run" ]]; then
  xmake run
fi
