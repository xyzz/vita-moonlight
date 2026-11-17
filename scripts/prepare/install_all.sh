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

if command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1; then
    PYTHON=python
else
    echo "ERROR: Python 3 is required to select a compatible VitaSDK snapshot." >&2
    exit 3
fi

if ! command -v vita-makepkg >/dev/null 2>&1 || ! command -v vdpm >/dev/null 2>&1; then
    echo "ERROR: vita-makepkg and vdpm must be available in $VITASDK/bin." >&2
    exit 3
fi

# The Windows SDK seed keeps pacman inside VDPM's isolated MSYS runtime.
# Route vita-makepkg through VDPM so it can supply the native SDK paths.
if [ "$VITA_HOST" = "msys2" ] && [ -x "$VITASDK/bin/vdpm.exe" ]; then
    PACMAN="$REPO_ROOT/scripts/msys2/vdpm-pacman"
elif [ -x "$VITASDK/bin/pacman" ]; then
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

VDPM_PACKAGES=(
    zlib bzip2 expat curl openssl opus libvita2d freetype libpng
    libjpeg-turbo zstd mbedtls sdl2
)

SDK_CHANNEL="${VITASDK_CHANNEL:-}"
if [ -z "$SDK_CHANNEL" ]; then
    SDK_CHANNEL="$("$PYTHON" -c 'import json, sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["channel"])' \
        "$VITASDK/var/lib/vdpm/channel.json")"
fi
if [ -z "$SDK_CHANNEL" ]; then
    echo "ERROR: Could not determine the selected VitaSDK channel." >&2
    exit 4
fi

snapshot_dir="$(mktemp -d)"
trap 'rm -rf "$snapshot_dir"' EXIT
candidate_list="$snapshot_dir/candidates.tsv"

echo "[prepare] Resolving the newest complete package snapshot for $SDK_CHANNEL..."
"$PYTHON" "$SCRIPT_DIR/select_vitasdk_snapshot.py" \
    "$snapshot_dir/candidates" "${VDPM_PACKAGES[@]}" \
    > "$candidate_list"

install_succeeded=0
while IFS=$'\t' read -r snapshot_release snapshot_manifest; do
    [ -n "$snapshot_release" ] && [ -n "$snapshot_manifest" ] || continue

    vdpm_manifest="$snapshot_manifest"
    if [ "$VITA_HOST" = "msys2" ]; then
        vdpm_manifest="$(cygpath -w "$snapshot_manifest")"
    fi

    staged_manifest="$VITASDK/var/lib/vdpm/channel.json.staged"
    selected_manifest="$VITASDK/var/lib/vdpm/channel.json"
    refresh_log="$snapshot_dir/vdpm-refresh.log"

    rm -f "$staged_manifest"
    echo "[prepare] Trying package snapshot $snapshot_release..."
    if VITASDK_CHANNEL_MANIFEST="$vdpm_manifest" vdpm refresh "$SDK_CHANNEL" >"$refresh_log" 2>&1; then
        cat "$refresh_log"
    elif [ "$VITA_HOST" = "msys2" ] && \
         grep -Fq "could not select the refreshed series" "$refresh_log" && \
         [ -f "$staged_manifest" ] && [ -f "$selected_manifest" ]; then
        cat "$refresh_log"
        echo "[prepare] Completing Windows channel selection after VDPM's rename failed."
        if ! powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass \
            -File "$(cygpath -w "$SCRIPT_DIR/../msys2/select-vdpm-channel.ps1")" \
            "$(cygpath -w "$staged_manifest")" \
            "$(cygpath -w "$selected_manifest")" \
            "$(cygpath -w "$snapshot_manifest")"; then
            echo "ERROR: Could not select the refreshed VitaSDK channel safely." >&2
            exit 4
        fi
    else
        cat "$refresh_log" >&2
        echo "[prepare] Refresh failed for $snapshot_release; trying the previous complete snapshot." >&2
        continue
    fi

    if vdpm install "${VDPM_PACKAGES[@]}"; then
        echo "[prepare] Selected package snapshot: $snapshot_release"
        install_succeeded=1
        break
    fi

    echo "[prepare] Dependency installation failed for $snapshot_release; trying the previous complete snapshot." >&2
done < "$candidate_list"

if [ "$install_succeeded" -ne 1 ]; then
    echo "ERROR: No validated VitaSDK package snapshot could install all Moonlight dependencies." >&2
    exit 4
fi

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
