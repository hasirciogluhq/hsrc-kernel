#!/usr/bin/env bash
# Install host deps + i686-elf cross toolchain aliases for CI / Linux builders.
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ci-install-deps.sh is intended for Linux CI (got $(uname -s))" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive

sudo apt-get update -y
sudo apt-get install -y --no-install-recommends \
  build-essential \
  curl \
  ca-certificates \
  nasm \
  qemu-system-x86 \
  gcc-i686-linux-gnu \
  g++-i686-linux-gnu \
  binutils-i686-linux-gnu \
  xz-utils \
  gzip

# Project expects i686-elf-* prefix; map from the Debian cross packages.
prefix="/usr/local/bin"
sudo mkdir -p "$prefix"
link_tool() {
  local short="$1"
  local real="$2"
  local src
  src="$(command -v "$real")"
  sudo ln -sfn "$src" "$prefix/i686-elf-$short"
}
link_tool gcc i686-linux-gnu-gcc
link_tool g++ i686-linux-gnu-g++
link_tool ld i686-linux-gnu-ld
link_tool objcopy i686-linux-gnu-objcopy
link_tool nm i686-linux-gnu-nm
link_tool ar i686-linux-gnu-ar
link_tool as i686-linux-gnu-as
link_tool ranlib i686-linux-gnu-ranlib
link_tool strip i686-linux-gnu-strip

export PATH="$HOME/.local/bin:$PATH"
if ! command -v xmake >/dev/null 2>&1; then
  # Official installer often exits 1 after printing "source ~/.xmake/profile"
  # even when the install succeeded — ignore that and verify below.
  set +e
  curl -fsSL https://xmake.io/shget.text | bash
  install_rc=$?
  set -e
  # shellcheck disable=SC1090
  source "$HOME/.xmake/profile" 2>/dev/null || true
  export PATH="$HOME/.local/bin:$PATH"
  if ! command -v xmake >/dev/null 2>&1; then
    echo "[ci] xmake install failed (installer rc=$install_rc)" >&2
    exit 1
  fi
fi

echo "[ci] toolchain:"
i686-elf-gcc --version | head -1
i686-elf-ld --version | head -1
nasm -v
qemu-system-i386 --version | head -1
xmake --version
