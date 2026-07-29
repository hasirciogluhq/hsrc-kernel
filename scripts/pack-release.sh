#!/usr/bin/env bash
# Pack production-ready release artifacts:
#   - kernel + initrd
#   - executables (.elf / .dynlib)
#   - small FAT disk.img (mountable + QEMU-bootable via run.sh)
#
# Usage:
#   scripts/pack-release.sh [--skip-build] [version]
# Env:
#   DISK_SIZE_MB   production disk size (default 32)
#   DIST_DIR       output root (default dist)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SKIP_BUILD=0
VERSION=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    -*)
      echo "unknown flag: $1" >&2
      exit 2
      ;;
    *)
      VERSION="$1"
      shift
      ;;
  esac
done

if [[ -z "$VERSION" ]]; then
  VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo 0.0.0)"
fi
VERSION="${VERSION#v}"

DISK_SIZE_MB="${DISK_SIZE_MB:-32}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
NAME="hsrc-kernel-${VERSION}"
OUT="$DIST_DIR/$NAME"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "[release] version=$VERSION disk=${DISK_SIZE_MB}M out=$OUT"

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo "[release] building..."
  xmake f -y -p cross -a i386 --cross=i686-elf- || true
  DISK_SIZE_MB="$DISK_SIZE_MB" xmake -j"$JOBS"
fi

KERNEL="$ROOT/build/kernel.bin"
INITRD="$ROOT/build/drivers/initrd.img"
MKFAT="$ROOT/build/tools/mkfatimg"
PACKFAT="$ROOT/build/tools/pack_fat"

for f in "$KERNEL" "$INITRD" "$MKFAT" "$PACKFAT"; do
  if [[ ! -f "$f" ]]; then
    echo "[release] missing build artifact: $f (build first)" >&2
    exit 1
  fi
done

rm -rf "$OUT"
mkdir -p \
  "$OUT/boot" \
  "$OUT/executables/system/bin" \
  "$OUT/executables/system/lib" \
  "$OUT/executables/applications" \
  "$OUT/executables/initrd"

cp "$KERNEL" "$OUT/boot/kernel.bin"
cp "$INITRD" "$OUT/boot/initrd.img"

# System / user executables (also mirrored into the FAT image below).
SYSTEM_EXECS=(
  window-manager os-shell os-settings
  terminal files activity-monitor
)
USER_EXECS=(
  minesweeper imgui-demo libfs-demo libfs-demo2
)
DYNLIBS=(libfs)

for n in "${SYSTEM_EXECS[@]}"; do
  src="$ROOT/build/userspace/$n/$n.elf"
  [[ -f "$src" ]] || { echo "[release] missing $src" >&2; exit 1; }
  cp "$src" "$OUT/executables/system/bin/$n.elf"
done
for n in "${USER_EXECS[@]}"; do
  src="$ROOT/build/userspace/$n/$n.elf"
  [[ -f "$src" ]] || { echo "[release] missing $src" >&2; exit 1; }
  cp "$src" "$OUT/executables/applications/$n.elf"
done
for n in "${DYNLIBS[@]}"; do
  src="$ROOT/build/userspace/lib/$n.dynlib"
  [[ -f "$src" ]] || { echo "[release] missing $src" >&2; exit 1; }
  cp "$src" "$OUT/executables/system/lib/$n.dynlib"
done
cp "$ROOT/build/userspace/init/init.elf" "$OUT/executables/initrd/init"

# Compact production FAT image (mountable: mount -o loop,offset=0 disk.img /mnt)
IMG="$OUT/disk.img"
"$MKFAT" "$IMG" "$DISK_SIZE_MB"
fat_args=("$IMG")
fat_args+=("$OUT/executables/initrd/init:init")
for n in "${SYSTEM_EXECS[@]}"; do
  fat_args+=("$OUT/executables/system/bin/$n.elf:system/bin/$n.elf")
done
for n in "${USER_EXECS[@]}"; do
  fat_args+=("$OUT/executables/applications/$n.elf:applications/$n.elf")
done
for n in "${DYNLIBS[@]}"; do
  fat_args+=("$OUT/executables/system/lib/$n.dynlib:system/lib/$n.dynlib")
done
fat_args+=(
  "$ROOT/assets/os/wallpaper-default.bmp:system/share/wallpaper-default.bmp"
  "$ROOT/assets/os/icons/theme-sun.svg:system/share/theme-sun.svg"
  "$ROOT/assets/os/icons/theme-moon.svg:system/share/theme-moon.svg"
  "$ROOT/assets/os/icons/status-wifi.svg:system/share/status-wifi.svg"
  "$ROOT/assets/os/icons/status-wifi-off.svg:system/share/status-wifi-off.svg"
  "$ROOT/assets/os/icons/status-battery.svg:system/share/status-battery.svg"
  "$ROOT/assets/os/icons/status-bolt.svg:system/share/status-bolt.svg"
  "$ROOT/assets/etc/environment:system/etc/environment"
)
"$PACKFAT" "${fat_args[@]}"

# Runnable launcher — extract tarball and ./run.sh
cat >"$OUT/run.sh" <<'EOF'
#!/usr/bin/env bash
# Boot this release package with QEMU (requires qemu-system-i386).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
KERNEL="$HERE/boot/kernel.bin"
INITRD="$HERE/boot/initrd.img"
DISK="$HERE/disk.img"
MEM="${MEM:-1G}"
SMP="${SMP:-3}"

for f in "$KERNEL" "$INITRD" "$DISK"; do
  [[ -f "$f" ]] || { echo "missing $f" >&2; exit 1; }
done

exec qemu-system-i386 \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -m "$MEM" \
  -smp "${SMP},sockets=1,cores=${SMP},threads=1" \
  -vga std \
  -serial stdio \
  -drive "if=none,id=vd0,file=${DISK},format=raw,cache=writethrough" \
  -device virtio-blk-pci,drive=vd0,disable-legacy=on \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0,disable-legacy=on \
  "$@"
EOF
chmod +x "$OUT/run.sh"

cat >"$OUT/README.txt" <<EOF
hsrc-kernel ${VERSION}
======================

Contents
--------
  boot/kernel.bin     Multiboot kernel
  boot/initrd.img     kmods + /init
  disk.img            FAT16 system disk (${DISK_SIZE_MB} MiB)
  executables/        loose .elf / .dynlib copies (same as on disk)
  run.sh              QEMU launcher

Run
---
  ./run.sh

Mount the disk image (host)
---------------------------
  mkdir -p /tmp/hsrc-disk
  sudo mount -o loop disk.img /tmp/hsrc-disk
  ls /tmp/hsrc-disk/system /tmp/hsrc-disk/applications
  sudo umount /tmp/hsrc-disk

Requirements
------------
  qemu-system-i386
EOF

# Separate + combined archives for GitHub Releases
mkdir -p "$DIST_DIR"
(
  cd "$DIST_DIR"
  tar -czf "${NAME}-boot.tar.gz" -C "$NAME" boot
  tar -czf "${NAME}-executables.tar.gz" -C "$NAME" executables
  tar -czf "${NAME}-image.tar.gz" -C "$NAME" disk.img boot run.sh README.txt
  tar -czf "${NAME}.tar.gz" "$NAME"
)

# Checksums
(
  cd "$DIST_DIR"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${NAME}"*.tar.gz > "${NAME}.sha256"
  else
    sha256sum "${NAME}"*.tar.gz > "${NAME}.sha256"
  fi
)

echo "[release] artifacts:"
ls -lh "$DIST_DIR/${NAME}"*.tar.gz "$DIST_DIR/${NAME}.sha256"
echo "[release] runnable tree: $OUT"
echo "[release] try: (cd $OUT && ./run.sh)"
