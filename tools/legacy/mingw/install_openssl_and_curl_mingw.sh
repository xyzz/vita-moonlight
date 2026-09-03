#!/usr/bin/env bash
# Legacy MinGW dependency experiment. See tools/README.md before using it.

set -euo pipefail

OPENSSL_VERSION=1.1.1w
OPENSSL_TAR=openssl-$OPENSSL_VERSION.tar.gz
OPENSSL_DIR=openssl-$OPENSSL_VERSION
CURL_VERSION=8.8.0
CURL_TAR=curl-$CURL_VERSION.tar.gz
CURL_DIR=curl-$CURL_VERSION
PREFIX="${1:-$(pwd)/openssl-mingw}"
if [[ "$PREFIX" != /* ]]; then
    PREFIX="$(pwd)/$PREFIX"
fi
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR"

echo "Downloading OpenSSL $OPENSSL_VERSION..."
wget "https://www.openssl.org/source/$OPENSSL_TAR"
tar xf "$OPENSSL_TAR"
cd "$OPENSSL_DIR"
echo "Configuring OpenSSL for MinGW-w64..."
./Configure mingw64 --cross-compile-prefix=x86_64-w64-mingw32- --prefix="$PREFIX"
make -j"$(nproc)"
make install
cd ..

echo "Downloading curl $CURL_VERSION..."
wget "https://curl.se/download/$CURL_TAR"
tar xf "$CURL_TAR"
cd "$CURL_DIR"
echo "Configuring curl for MinGW-w64..."
./configure \
    --host=x86_64-w64-mingw32 \
    --with-ssl="$PREFIX" \
    --disable-static \
    --enable-shared \
    --without-nghttp2 \
    --without-libpsl \
    --prefix="$PREFIX"

make -C lib -j"$(nproc)"
make -C lib install
cd ..

printf 'OpenSSL and curl for MinGW-w64 were installed in %s.\n' "$PREFIX"
echo "CMake options:"
echo "  -DOPENSSL_ROOT_DIR=$PREFIX"
echo "  -DOPENSSL_INCLUDE_DIR=$PREFIX/include"
echo "  -DCURL_INCLUDE_DIR=$PREFIX/include"
echo "  -DCURL_LIBRARY=$PREFIX/lib/libcurl.dll.a"
