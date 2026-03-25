#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}/.."

usage() {
  cat <<EOF
Usage: $0 [--curl-backend=mbedtls|openssl]

This script builds and installs the PS Vita packages under
scripts/prepare: mbedtls, ffmpeg, sdl2 and curl. It expects:
 - VITASDK environment variable set to the Vita toolchain root
 - vita-makepkg and vdpm available in PATH (recommended; see Dockerfiles)

By default the script builds curl with mbedtls (to match FFmpeg's default
in this repo). Use --curl-backend=openssl to build curl with OpenSSL.

This script mirrors the approach used by the project's Dockerfiles and
will run vita-makepkg in the corresponding folder and then install the
resulting package with vdpm.
EOF
}

# defaults
CURL_BACKEND="mbedtls"

for arg in "$@"; do
  case $arg in
    --curl-backend=*) CURL_BACKEND="${arg#*=}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $arg"; usage; exit 1 ;;
  esac
done

if [ -z "${VITASDK:-}" ]; then
  echo "ERROR: VITASDK is not set. Export VITASDK to your Vita toolchain path." >&2
  exit 2
fi

if ! command -v vita-makepkg >/dev/null 2>&1 || ! command -v vdpm >/dev/null 2>&1; then
  cat <<MSG
ERROR: This script expects both 'vita-makepkg' and 'vdpm' to be in PATH.
Prefer running inside the provided Dockerfile image (see scripts/prepare/Dockerfile)
or install the vita packaging tools in your host environment.
MSG
  exit 3
fi

# vita-makepkg relies on bsdtar (libarchive). Check it's present and give
# an actionable hint if it's missing.
if ! command -v bsdtar >/dev/null 2>&1; then
  echo "'bsdtar' not found in PATH. Attempting to install automatically..."

  # Helper to run installer with or without sudo depending on current privileges
  run_install() {
    if [ "$(id -u)" -eq 0 ]; then
      eval "$@"
    else
      if command -v sudo >/dev/null 2>&1; then
        sudo sh -c "$@"
      else
        echo "No sudo available and not running as root; cannot install packages automatically." >&2
        return 1
      fi
    fi
  }

  installed=0
  # Debian/Ubuntu
  if command -v apt-get >/dev/null 2>&1; then
    echo "Detected apt-get; installing libarchive-tools..."
    run_install "apt-get update -y && apt-get install -y libarchive-tools" && installed=1 || installed=0
  fi

  # Fedora/RHEL (dnf/yum)
  if [ "$installed" -eq 0 ] && command -v dnf >/dev/null 2>&1; then
    echo "Detected dnf; installing libarchive..."
    run_install "dnf install -y libarchive" && installed=1 || installed=0
  elif [ "$installed" -eq 0 ] && command -v yum >/dev/null 2>&1; then
    echo "Detected yum; installing libarchive..."
    run_install "yum install -y libarchive" && installed=1 || installed=0
  fi

  # Arch Linux
  if [ "$installed" -eq 0 ] && command -v pacman >/dev/null 2>&1; then
    echo "Detected pacman; installing libarchive..."
    run_install "pacman -Sy --noconfirm libarchive" && installed=1 || installed=0
  fi

  # openSUSE
  if [ "$installed" -eq 0 ] && command -v zypper >/dev/null 2>&1; then
    echo "Detected zypper; installing libarchive-tools..."
    run_install "zypper -n install libarchive-tools" && installed=1 || installed=0
  fi

  # Alpine
  if [ "$installed" -eq 0 ] && command -v apk >/dev/null 2>&1; then
    echo "Detected apk; installing libarchive..."
    run_install "apk add --no-cache libarchive" && installed=1 || installed=0
  fi

  if [ "$installed" -eq 0 ]; then
    cat <<MSG >&2
ERROR: Could not install 'bsdtar' automatically.
Please install 'bsdtar' (part of libarchive) using your system package manager.
Examples:
  Debian/Ubuntu: sudo apt-get update && sudo apt-get install -y libarchive-tools
  Fedora:        sudo dnf install -y libarchive
  Arch:          sudo pacman -Sy --noconfirm libarchive
  openSUSE:      sudo zypper -n install libarchive-tools
  Alpine:        sudo apk add --no-cache libarchive
Or run this script inside the provided Docker image which bundles the host tools.
MSG
    exit 4
  fi

  # re-check
  if ! command -v bsdtar >/dev/null 2>&1; then
    echo "Installation attempted but 'bsdtar' is still missing; aborting." >&2
    exit 4
  fi
fi

echo "Using VITASDK=${VITASDK}"
echo "curl backend requested: ${CURL_BACKEND}"

pushd "$REPO_ROOT" >/dev/null

# Ensure PVR/PVR_PSP2 stubs are installed via install_psv2 if present.
# The helper script was moved into the prepare folder; check there first.
INSTALL_PSV2_PATH="$SCRIPT_DIR/install_psv2"
if [ -x "$INSTALL_PSV2_PATH" ]; then
  echo "Running $INSTALL_PSV2_PATH to ensure PVR stubs are installed into VITASDK (needed for SDL2/PVR)..."
  "$INSTALL_PSV2_PATH"
else
  # Fall back to project root for backwards compatibility
  if [ -x "$REPO_ROOT/install_psv2" ]; then
    echo "Running $REPO_ROOT/install_psv2 to ensure PVR stubs are installed into VITASDK (needed for SDL2/PVR)..."
    "$REPO_ROOT/install_psv2"
  else
    echo "Warning: install_psv2 not found or not executable. Make sure PVR stubs are present if you need PVR support."
  fi
fi

install_package_dir() {
  local pkgdir="$1"
  echo "\n==> Installing package from $pkgdir"
  if [ ! -d "$pkgdir" ]; then
    echo "Directory $pkgdir not found, skipping." >&2
    return 1
  fi
  # Try to uninstall any previously installed package entries that mention this package
  uninstall_existing_for_pkgdir "$pkgdir"

  pushd "$pkgdir" >/dev/null
  # run vita-makepkg
  echo "Running vita-makepkg in $pkgdir..."
  # Remove any previously built package archives in this directory to avoid
  # "A package has already been built" errors from vita-makepkg.
  rm -f ./*-arm.tar.xz ./*-arm.tar.gz || true

  # Ensure previous build artifacts are removed so vita-makepkg starts clean.
  rm -rf build src || true

  if vita-makepkg; then
    # find produced archive
    armpkg=$(ls -1t ./*-arm.tar.xz 2>/dev/null || true)
    if [ -z "$armpkg" ]; then
      echo "No -arm.tar.xz produced; check vita-makepkg output." >&2
      popd >/dev/null
      return 2
    fi
    # Use vdpm to install (force replace if package was previously installed)
    echo "Installing ${armpkg%%$'\n'*} via vdpm (-f)..."
    vdpm -f ${armpkg%%$'\n'*}
  else
    echo "vita-makepkg failed in $pkgdir" >&2
    popd >/dev/null
    return 3
  fi
  popd >/dev/null
  return 0
}

# Try to find and uninstall previously installed packages that correspond to a package dir
uninstall_existing_for_pkgdir() {
  local pkgdir="$1"
  # candidate token: directory name (e.g. 'mbedtls', 'curl', 'ffmpeg', 'sdl2')
  local token="$(basename "$pkgdir")"
  if [ -z "${VITASDK:-}" ]; then
    return 0
  fi
  local pkg_list="$VITASDK/etc/vdpm/packages.list"
  if [ ! -f "$pkg_list" ]; then
    return 0
  fi

  echo "Checking for previously installed vdpm packages matching '$token'..."
  # Find PACKAGE lines that contain the token
  mapfile -t matches < <(grep "^PACKAGE .*${token}" "$pkg_list" || true)
  if [ ${#matches[@]} -eq 0 ]; then
    # Try looser match: any PACKAGE line containing the token
    mapfile -t matches < <(grep "^PACKAGE .*${token}" "$pkg_list" || true)
  fi

  for m in "${matches[@]}"; do
    # strip leading 'PACKAGE '
    local pkgname="${m#PACKAGE }"
    if [ -n "$pkgname" ]; then
      # sanitize pkgname: remove any leading ./ or path components and strip archive extensions
      pkgname="${pkgname##*/}"
      pkgname="${pkgname#./}"
      pkgname="${pkgname%.tar.xz}"
      pkgname="${pkgname%.tar.gz}"
      echo "Uninstalling existing package entry: $pkgname"
      # use vdpm uninstall mode
      if command -v vdpm >/dev/null 2>&1; then
        if ! vdpm -u "$pkgname"; then
          # Fallback: try the base package token (e.g. curl from curl-8.9.1-2-arm)
          local base_pkg="${pkgname%%-[0-9]*}"
          if [ -n "$base_pkg" ] && [ "$base_pkg" != "$pkgname" ]; then
            echo "vdpm uninstall failed for $pkgname; trying fallback package name: $base_pkg"
            if ! vdpm -u "$base_pkg"; then
              echo "vdpm uninstall failed for fallback package $base_pkg" >&2
            fi
          else
            echo "vdpm uninstall failed for $pkgname" >&2
          fi
        fi
      else
        echo "vdpm not found: cannot uninstall $pkgname" >&2
      fi
    fi
  done
}

# Build/install order: mbedtls -> ffmpeg -> sdl2 -> curl
echo "Installing mbedtls..."
install_package_dir "$SCRIPT_DIR/mbedtls" || { echo "Failed to install mbedtls"; exit 4; }

echo "Installing ffmpeg (configured for mbedtls by default)..."
install_package_dir "$SCRIPT_DIR/ffmpeg" || { echo "Failed to install ffmpeg"; exit 5; }

echo "Installing sdl2..."
install_package_dir "$SCRIPT_DIR/sdl2" || { echo "Failed to install sdl2"; exit 6; }

echo "Installing curl (backend=${CURL_BACKEND})..."
# By default the VITABUILD in scripts/prepare/curl is configured for mbedtls.
if [ "$CURL_BACKEND" = "mbedtls" ]; then
  install_package_dir "$SCRIPT_DIR/curl" || { echo "Failed to install curl (mbedtls)"; exit 7; }
else
  # Build curl with OpenSSL: create a temporary package dir and edit VITABUILD to enable OpenSSL
  if [ "$CURL_BACKEND" != "openssl" ]; then
    echo "Unknown curl backend '$CURL_BACKEND'" >&2
    exit 9
  fi
  echo "Building curl with OpenSSL: creating temporary copy of scripts/prepare/curl and patching VITABUILD..."
  TMPDIR=$(mktemp -d)
  cp -r "$SCRIPT_DIR/curl/" "$TMPDIR/curl"
  VITABUILD_TMP="$TMPDIR/curl/VITABUILD"
  if [ -f "$VITABUILD_TMP" ]; then
    # Switch CMake flags: set DCURL_USE_OPENSSL=ON and DCURL_USE_MBEDTLS=OFF
    sed -i "s/-DCURL_USE_MBEDTLS=ON/-DCURL_USE_MBEDTLS=OFF/g" "$VITABUILD_TMP" || true
    sed -i "s/-DCURL_USE_OPENSSL=OFF/-DCURL_USE_OPENSSL=ON/g" "$VITABUILD_TMP" || true
  else
    echo "VITABUILD not found in temporary curl dir; aborting" >&2
    rm -rf "$TMPDIR"
    exit 10
  fi

  # Run vita-makepkg in the temporary dir and install generated package
  pushd "$TMPDIR/curl" >/dev/null
  if vita-makepkg; then
    armpkg=$(ls -1t ./*-arm.tar.xz 2>/dev/null || true)
    if [ -z "$armpkg" ]; then
      echo "No -arm.tar.xz produced for OpenSSL curl; check vita-makepkg output." >&2
      popd >/dev/null
      rm -rf "$TMPDIR"
      exit 11
    fi
    vdpm -f ${armpkg%%$'\n'*}
  else
    echo "vita-makepkg failed for OpenSSL curl" >&2
    popd >/dev/null
    rm -rf "$TMPDIR"
    exit 12
  fi
  popd >/dev/null
  rm -rf "$TMPDIR"
fi

# Cleanup function to remove downloaded files and build artifacts
cleanup_package_artifacts() {
  local pkgdir="$1"
  echo "Cleaning up build artifacts in $pkgdir..."
  if [ ! -d "$pkgdir" ]; then
    return 0
  fi

  pushd "$pkgdir" >/dev/null

  # Remove build and src directories created by vita-makepkg
  rm -rf build src || true

  # Remove downloaded source archives (tar.gz, tar.xz, etc.)
  rm -f *.tar.gz *.tar.xz *.tar.bz2 *.zip || true

  # Remove generated package files
  rm -f ./*-arm.tar.xz ./*-arm.tar.gz || true

  # Remove local packaging staging/output directory created by vita-makepkg
  rm -rf pkg || true

  # Remove any extracted source directories
  # These are typically named like: curl-8.9.1, mbedtls-3.4.1, FFmpeg-n6.0, SDL-<gitrev>
  for dir in curl-* mbedtls-* FFmpeg-* SDL-* ffmpeg-*; do
    if [ -d "$dir" ]; then
      echo "  Removing extracted source directory: $dir"
      rm -rf "$dir" || true
    fi
  done

  popd >/dev/null
}

echo "\nAll packages processed. You should now have the libraries installed into $VITASDK/arm-vita-eabi/lib and include/"

# Clean up all build artifacts and downloaded files
echo "\n==> Cleaning up downloaded files and build artifacts..."
cleanup_package_artifacts "$SCRIPT_DIR/mbedtls"
cleanup_package_artifacts "$SCRIPT_DIR/ffmpeg"
cleanup_package_artifacts "$SCRIPT_DIR/sdl2"
cleanup_package_artifacts "$SCRIPT_DIR/curl"

# If curl was built with OpenSSL using a temporary directory, it was already cleaned up above
echo "Cleanup complete. Package directories now contain only VITABUILD, patches and helper scripts."

echo "If you used Docker, the Dockerfile already automates this (see scripts/prepare/Dockerfile and scripts/prepare/gxm.Dockerfile)."

popd >/dev/null

exit 0
