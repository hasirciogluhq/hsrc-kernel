#!/usr/bin/env bash
# CI deps: apt prebuilt packages + official xmake install (curl | bash).
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ci-install-deps.sh is for Linux CI only" >&2
  exit 1
fi

BIN_DIR="${HOME}/.local/bin"
mkdir -p "$BIN_DIR"
export PATH="$BIN_DIR:$PATH"

export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -qq
sudo apt-get install -y -qq --no-install-recommends \
  curl ca-certificates nasm qemu-system-x86 \
  gcc-i686-linux-gnu g++-i686-linux-gnu binutils-i686-linux-gnu

# i686-elf-* aliases → Debian cross packages (prebuilt).
for pair in \
  "gcc:i686-linux-gnu-gcc" \
  "g++:i686-linux-gnu-g++" \
  "ld:i686-linux-gnu-ld" \
  "objcopy:i686-linux-gnu-objcopy" \
  "nm:i686-linux-gnu-nm" \
  "ar:i686-linux-gnu-ar" \
  "as:i686-linux-gnu-as" \
  "ranlib:i686-linux-gnu-ranlib" \
  "strip:i686-linux-gnu-strip"
do
  short="${pair%%:*}"
  real="${pair##*:}"
  sudo ln -sfn "$(command -v "$real")" "/usr/local/bin/i686-elf-$short"
done

# Official: curl -fsSL https://xmake.io/shget.text | bash
# Installer often exits 1 after "source ~/.xmake/profile" even on success.
if ! command -v xmake >/dev/null 2>&1; then
  set +e
  curl -fsSL https://xmake.io/shget.text | bash
  set -e
  # shellcheck disable=SC1090
  source "$HOME/.xmake/profile" 2>/dev/null || true
  export PATH="$BIN_DIR:$PATH"
  if ! command -v xmake >/dev/null 2>&1; then
    echo "[ci] xmake not found after install" >&2
    exit 1
  fi
fi

if [[ -n "${GITHUB_PATH:-}" ]]; then
  echo "$BIN_DIR" >>"$GITHUB_PATH"
fi

echo "[ci] ok"
i686-elf-gcc --version | head -1
nasm -v
qemu-system-i386 --version | head -1
xmake --version | head -1
