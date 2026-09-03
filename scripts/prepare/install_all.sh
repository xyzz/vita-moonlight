#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# shellcheck source=../vita-host.sh
source "$SCRIPT_DIR/../vita-host.sh"

KEEP_BUILD=0
WITH_PVR=0

usage() {
    cat <<'EOF'
Usage: scripts/prepare/install_all.sh [--keep-build] [--with-pvr]

Installs the official VitaSDK dependencies required by Moonlight and then
builds/installs the Vita-specific FFmpeg fork that provides h264_vita and the
Vita pixel formats used by the low-latency decoder path.

Use --with-pvr only when the GLES/SDL2 backend is needed. The normal GXM
backend does not require PVR_PSP2.

Linux, WSL and Windows/MSYS2 are supported.
EOF
}

for arg in "$@"; do
    case "$arg" in
        --keep-build) KEEP_BUILD=1 ;;
        --with-pvr) WITH_PVR=1 ;;
        --curl-backend=*)
            echo "[prepare] $arg is obsolete; curl now comes from the official VDPM repository." >&2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; usage; exit 1 ;;
    esac
done

vita_host_init || exit 2
vita_print_environment
vita_require_sdk || exit 2

if [ "$VITA_HOST" = "msys2" ]; then
    export PATH="$REPO_ROOT/scripts/msys2:$PATH"
fi

if ! vita_require_tools bash cmake make git patch sed grep find tar bsdtar curl unzip mktemp pkg-config fakeroot; then
    cat >&2 <<'EOF'
Install the missing host tools first.
MSYS2 example:
  pacman -S --needed --noconfirm make git wget p7zip tar cmake patch pkgconf ninja python unzip curl libarchive diffutils sed grep findutils
EOF
    exit 3
fi

if ! command -v vita-makepkg >/dev/null 2>&1 || ! command -v vdpm >/dev/null 2>&1; then
    echo "ERROR: vita-makepkg and vdpm must be available in $VITASDK/bin." >&2
    exit 3
fi

# The Windows SDK seed keeps pacman beside its embedded MSYS runtime, while
# vita-makepkg still defaults to $VITASDK/bin/pacman.
if [ -x "$VITASDK/bin/pacman" ]; then
    PACMAN="$VITASDK/bin/pacman"
elif [ -x "$VITASDK/libexec/vdpm/pacman" ]; then
    PACMAN="$VITASDK/libexec/vdpm/pacman"
elif [ -x "$VITASDK/share/vdpm/msys/usr/bin/pacman.exe" ]; then
    PACMAN="$VITASDK/share/vdpm/msys/usr/bin/pacman.exe"
else
    echo "ERROR: Could not locate the VitaSDK pacman client." >&2
    exit 3
fi
PACMAN_CONF="$VITASDK/etc/pacman.conf"
export PACMAN PACMAN_CONF

export VITA_BUILD_JOBS
export MAKEFLAGS="-j${VITA_BUILD_JOBS}"
export VDPM_NONINTERACTIVE=1

echo "[prepare] Installing official VitaSDK dependencies..."
vdpm install \
    zlib bzip2 expat curl openssl opus libvita2d freetype libpng \
    libjpeg-turbo zstd mbedtls sdl2
vita_fix_msys2_sdk_aliases

if [ "$WITH_PVR" -eq 1 ]; then
    echo "[prepare] Installing PVR_PSP2 support..."
    bash "$SCRIPT_DIR/install_psv2"
fi

clean_package_dir() {
    local package_dir="$1"
    [ -d "$package_dir" ] || return 0
    rm -rf "$package_dir/build" "$package_dir/src" "$package_dir/pkg"
    rm -f "$package_dir"/*.pkg.tar.xz "$package_dir"/*.pkg.tar.gz 2>/dev/null || true
}

install_package_dir() {
    local package_dir="$1"
    local label="$2"

    echo
    echo "==> Building $label from $package_dir"
    [ -d "$package_dir" ] || { echo "Missing package directory: $package_dir" >&2; return 1; }

    clean_package_dir "$package_dir"
    pushd "$package_dir" >/dev/null
    VITA_BUILD_JOBS="$VITA_BUILD_JOBS" vita-makepkg -f -i --noconfirm
    popd >/dev/null

    if [ "$KEEP_BUILD" -eq 0 ]; then
        clean_package_dir "$package_dir"
    fi
}

# The official FFmpeg package is a normal Vita build and intentionally lacks
# the Vita hardware-decoder extensions used by this fork. Replace only FFmpeg;
# the rest of the dependencies stay on the official VDPM packages.
install_package_dir "$SCRIPT_DIR/ffmpeg" "FFmpeg-vita"

pixfmt_header="$VITASDK/arm-vita-eabi/include/libavutil/pixfmt.h"
avcodec_archive="$VITASDK/arm-vita-eabi/lib/libavcodec.a"
if ! grep -q 'AV_PIX_FMT_VITA' "$pixfmt_header" 2>/dev/null; then
    echo "ERROR: FFmpeg-vita installed but AV_PIX_FMT_VITA is missing from pixfmt.h." >&2
    exit 8
fi
if ! grep -a -q 'h264_vita' "$avcodec_archive" 2>/dev/null; then
    echo "ERROR: FFmpeg-vita installed but h264_vita is missing from libavcodec.a." >&2
    exit 8
fi

echo
echo "[prepare] Moonlight Vita dependencies are ready in:"
echo "          $VITASDK/arm-vita-eabi"
echo "[prepare] Host: $VITA_HOST | jobs: $VITA_BUILD_JOBS"
