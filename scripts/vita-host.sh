#!/usr/bin/env bash
# Shared host helpers for VitaSDK builds on Linux and Windows/MSYS2.

vita_host_init() {
    local uname_s
    uname_s="$(uname -s 2>/dev/null || echo unknown)"

    case "$uname_s" in
        MSYS*|MINGW*|CYGWIN*)
            VITA_HOST="msys2"
            ;;
        Linux*)
            if grep -qi microsoft /proc/version 2>/dev/null; then
                echo "[!] WSL is intentionally unsupported by this project. Use MSYS2 on Windows." >&2
                return 1
            else
                VITA_HOST="linux"
            fi
            ;;
        *)
            VITA_HOST="unknown"
            ;;
    esac

    if [ -z "${VITASDK:-}" ]; then
        if [ -d /usr/local/vitasdk ]; then
            VITASDK=/usr/local/vitasdk
        else
            echo "[!] VITASDK is not set and /usr/local/vitasdk was not found." >&2
            return 1
        fi
    elif [ "$VITA_HOST" = "msys2" ] && command -v cygpath >/dev/null 2>&1; then
        case "$VITASDK" in
            [A-Za-z]:\\*|[A-Za-z]:/*)
                VITASDK="$(cygpath -u "$VITASDK")"
                ;;
        esac
    fi

    export VITA_HOST
    VITASDK="${VITASDK%/}"
    export VITASDK
    export PATH="$VITASDK/bin:$PATH"

    # Use MSYS2's emulated symlink format when Windows native symlink creation
    # is unavailable. This lets bsdtar/vita-makepkg unpack source archives and
    # Vita packages that contain symlinks without requiring Developer Mode.
    if [ "$VITA_HOST" = "msys2" ]; then
        case " ${MSYS:-} " in
            *" winsymlinks:"*) ;;
            *) MSYS="${MSYS:+$MSYS }winsymlinks:sys"; export MSYS ;;
        esac
    fi

    # CMake runs inside MSYS2/Linux, so the official POSIX target wrapper can
    # be executed directly. It resets host pkg-config variables and confines
    # package lookup to the Vita sysroot.
    if [ -x "$VITASDK/bin/arm-vita-eabi-pkg-config" ]; then
        VITA_PKG_CONFIG_EXECUTABLE="$VITASDK/bin/arm-vita-eabi-pkg-config"
        export VITA_PKG_CONFIG_EXECUTABLE
    fi

    local detected_jobs
    detected_jobs="$(nproc 2>/dev/null || echo 4)"
    if [ "$detected_jobs" -lt 1 ] 2>/dev/null; then
        detected_jobs=1
    fi
    if [ "$detected_jobs" -gt 8 ] 2>/dev/null; then
        detected_jobs=8
    fi
    VITA_BUILD_JOBS="${VITA_BUILD_JOBS:-$detected_jobs}"
    export VITA_BUILD_JOBS

    if command -v ninja >/dev/null 2>&1; then
        VITA_CMAKE_GENERATOR="Ninja"
    else
        VITA_CMAKE_GENERATOR="Unix Makefiles"
    fi
    export VITA_CMAKE_GENERATOR

    case "$VITA_HOST" in
        msys2) VITA_BUILD_SUFFIX="msys2" ;;
        *)     VITA_BUILD_SUFFIX="$VITA_HOST" ;;
    esac
    export VITA_BUILD_SUFFIX
}

# Some Vita packages use compatibility symlinks. On Windows/MSYS2 those links
# can fail when Windows symlink privileges are unavailable. Repair only missing
# aliases with ordinary copies so generic linker/pkg-config names remain usable.
vita_fix_msys2_sdk_aliases() {
    [ "${VITA_HOST:-}" = "msys2" ] || return 0

    local src dst
    while IFS='|' read -r src dst; do
        [ -n "$src" ] || continue
        if [ ! -e "$dst" ] && [ -f "$src" ]; then
            echo "[+] Repairing MSYS2 SDK alias: ${dst#$VITASDK/}"
            cp -p "$src" "$dst" || return 1
        fi
    done <<EOF
$VITASDK/arm-vita-eabi/lib/libpng16.a|$VITASDK/arm-vita-eabi/lib/libpng.a
$VITASDK/arm-vita-eabi/lib/pkgconfig/libpng16.pc|$VITASDK/arm-vita-eabi/lib/pkgconfig/libpng.pc
$VITASDK/arm-vita-eabi/bin/libpng16-config|$VITASDK/arm-vita-eabi/bin/libpng-config
EOF
}

# CMake persists absolute toolchain/compiler paths. If a build directory was
# configured with another VitaSDK installation, reusing that cache silently
# points the next build at the old SDK. Drop only the generated build directory
# when the cached toolchain no longer matches the active VITASDK.
vita_reset_stale_cmake_cache() {
    local build_dir="$1"
    local cache="$build_dir/CMakeCache.txt"
    [ -f "$cache" ] || return 0

    local cached_toolchain cached_sdk cached_compiler expected_toolchain expected_native expected_sdk_native stale
    cached_toolchain="$(sed -n 's/^CMAKE_TOOLCHAIN_FILE:[^=]*=//p' "$cache" | head -n1 | tr '\\' '/')"
    cached_sdk="$(sed -n 's/^VITASDK:[^=]*=//p' "$cache" | head -n1 | tr '\\' '/')"
    cached_compiler="$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$cache" | head -n1 | tr '\\' '/')"
    expected_toolchain="$VITASDK/share/vita.toolchain.cmake"
    expected_native="$expected_toolchain"
    expected_sdk_native="$VITASDK"
    if [ "${VITA_HOST:-}" = "msys2" ] && command -v cygpath >/dev/null 2>&1; then
        expected_native="$(cygpath -m "$expected_toolchain")"
        expected_sdk_native="$(cygpath -m "$VITASDK")"
    fi

    stale=0
    if [ -n "$cached_toolchain" ] && \
       [ "${cached_toolchain,,}" != "${expected_toolchain,,}" ] && \
       [ "${cached_toolchain,,}" != "${expected_native,,}" ]; then
        stale=1
    fi
    if [ -n "$cached_sdk" ] && \
       [ "${cached_sdk,,}" != "${VITASDK,,}" ] && \
       [ "${cached_sdk,,}" != "${expected_sdk_native,,}" ]; then
        stale=1
    fi
    if [ -n "$cached_compiler" ]; then
        case "${cached_compiler,,}" in
            "${VITASDK,,}"/bin/*|"${expected_sdk_native,,}"/bin/*) ;;
            *) stale=1 ;;
        esac
    fi

    if [ "$stale" -eq 1 ]; then
        echo "[+] CMake cache uses a different VitaSDK; regenerating $build_dir..."
        echo "    cached SDK: ${cached_sdk:-unknown}"
        echo "    active SDK: $expected_sdk_native"
        cmake -E remove_directory "$build_dir" || return 1
    fi
}

vita_require_tools() {
    local missing=0 tool
    for tool in "$@"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "[!] Missing host tool: $tool" >&2
            missing=$((missing + 1))
        fi
    done
    [ "$missing" -eq 0 ]
}

vita_require_sdk() {
    local tool
    for tool in arm-vita-eabi-gcc arm-vita-eabi-g++ vita-mksfoex vita-make-fself vita-pack-vpk; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "[!] Missing VitaSDK tool: $tool (VITASDK=$VITASDK)" >&2
            return 1
        fi
    done
    if [ ! -f "$VITASDK/share/vita.toolchain.cmake" ]; then
        echo "[!] Missing $VITASDK/share/vita.toolchain.cmake" >&2
        return 1
    fi
}

vita_print_environment() {
    echo "[+] Host:       $VITA_HOST"
    echo "[+] VITASDK:    $VITASDK"
    echo "[+] Generator:  $VITA_CMAKE_GENERATOR"
    echo "[+] Build jobs: $VITA_BUILD_JOBS"
    if [ -n "${VITA_PKG_CONFIG_EXECUTABLE:-}" ]; then
        echo "[+] pkg-config: $VITA_PKG_CONFIG_EXECUTABLE"
    fi
}

vita_title_id() {
    sed -nE 's/.*set\(PSN_TITLE_ID[[:space:]]+"?([^" )]+)"?.*/\1/p' CMakeLists.txt | head -n1
}

vita_ping() {
    local ip="$1"
    if [ "$VITA_HOST" = "msys2" ]; then
        ping -n 1 -w 2000 "$ip" >/dev/null 2>&1
    else
        ping -c 1 -W 2 "$ip" >/dev/null 2>&1
    fi
}

vita_open_dir() {
    local path="$1"
    if [ "$VITA_HOST" = "msys2" ]; then
        explorer.exe "$(cygpath -w "$path")" >/dev/null 2>&1 || true
    elif command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$path" >/dev/null 2>&1 || true
    fi
}
