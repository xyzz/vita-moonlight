#!/bin/bash
# Script para compilar e instalar OpenSSL y curl para cross-compilación Mingw-w64 (Windows)
# Uso: ./install_openssl_and_curl_mingw.sh [ruta_instalacion]

set -e

OPENSSL_VERSION=1.1.1w
OPENSSL_TAR=openssl-$OPENSSL_VERSION.tar.gz
OPENSSL_DIR=openssl-$OPENSSL_VERSION
CURL_VERSION=8.8.0
CURL_TAR=curl-$CURL_VERSION.tar.gz
CURL_DIR=curl-$CURL_VERSION
PREFIX="${1:-$(pwd)/openssl-mingw}"

# --- OpenSSL ---
if [ ! -f "$OPENSSL_TAR" ]; then
    echo "Descargando OpenSSL $OPENSSL_VERSION..."
    wget https://www.openssl.org/source/$OPENSSL_TAR
fi
if [ ! -d "$OPENSSL_DIR" ]; then
    echo "Extrayendo $OPENSSL_TAR..."
    tar xf $OPENSSL_TAR
fi
cd $OPENSSL_DIR
echo "Configurando OpenSSL para mingw64..."
./Configure mingw64 --cross-compile-prefix=x86_64-w64-mingw32- --prefix="$PREFIX"
echo "Compilando OpenSSL..."
make -j$(nproc)
echo "Instalando OpenSSL en $PREFIX ..."
make install
cd ..

# --- curl ---
if [ ! -f "$CURL_TAR" ]; then
    echo "Descargando curl $CURL_VERSION..."
    wget https://curl.se/download/$CURL_TAR
fi
if [ ! -d "$CURL_DIR" ]; then
    echo "Extrayendo $CURL_TAR..."
    tar xf $CURL_TAR
fi
cd $CURL_DIR
echo "Configurando curl para mingw64 con OpenSSL..."
./configure \
    --host=x86_64-w64-mingw32 \
    --with-ssl=$PREFIX \
    --disable-static \
    --enable-shared \
    --without-nghttp2 \
    --without-libpsl \
    --prefix="$PREFIX"

echo "Compilando solo la librería de curl (no el ejecutable)..."
make -C lib -j$(nproc)
echo "Instalando solo la librería de curl en $PREFIX ..."
make -C lib install
cd ..

echo "\nOpenSSL y curl para Mingw-w64 instalados en: $PREFIX"
echo "\nPara usarlo en CMake, añade:"
echo "  -DOPENSSL_ROOT_DIR=$PREFIX"
echo "  -DOPENSSL_INCLUDE_DIR=$PREFIX/include"
echo "  -DCURL_INCLUDE_DIR=$PREFIX/include"
echo "  -DCURL_LIBRARY=$PREFIX/lib/libcurl.dll.a"
