#!/usr/bin/env bash
# Legacy MinGW dependency experiment. See tools/README.md before using it.

set -euo pipefail

EXPAT_VERSION=2.6.2
MINGW_PREFIX=x86_64-w64-mingw32
INSTALL_PREFIX=/usr/${MINGW_PREFIX}

sudo apt update
sudo apt install -y mingw-w64 g++-mingw-w64 cmake make wget tar

ARCHIVE="expat-${EXPAT_VERSION}.tar.gz"
SOURCE_DIR="expat-${EXPAT_VERSION}"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR"

wget "https://github.com/libexpat/libexpat/releases/download/R_${EXPAT_VERSION//./_}/$ARCHIVE"
tar -xzf "$ARCHIVE"
cd "$SOURCE_DIR"
mkdir build-mingw
cd build-mingw
cmake .. \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_RC_COMPILER=${MINGW_PREFIX}-windres \
  -DCMAKE_C_COMPILER=${MINGW_PREFIX}-gcc \
  -DCMAKE_CXX_COMPILER=${MINGW_PREFIX}-g++ \
  -DEXPAT_BUILD_DOCS=OFF \
  -DEXPAT_BUILD_EXAMPLES=OFF \
  -DEXPAT_BUILD_TESTS=OFF \
  -DEXPAT_BUILD_TOOLS=OFF \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"

make -j"$(nproc)"
sudo make install

printf 'Expat for MinGW was installed in %s.\n' "$INSTALL_PREFIX"
