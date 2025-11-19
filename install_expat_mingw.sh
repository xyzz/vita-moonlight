#!/bin/bash
# Script para descargar, compilar e instalar Expat para MinGW en WSL2
# Uso: ./install_expat_mingw.sh

set -e

# Configuración
EXPAT_VERSION=2.6.2
MINGW_PREFIX=x86_64-w64-mingw32
INSTALL_PREFIX=/usr/${MINGW_PREFIX}

# Instala dependencias necesarias
sudo apt update
sudo apt install -y mingw-w64 g++-mingw-w64 cmake make wget tar

tar -xzf expat-${EXPAT_VERSION}.tar.gz
ARCHIVE=expat-${EXPAT_VERSION}.tar.gz
DIR=expat-${EXPAT_VERSION}
# Borra archivos previos si existen
rm -rf "$DIR" build-mingw "$ARCHIVE"
# Descarga y descomprime Expat
wget https://github.com/libexpat/libexpat/releases/download/R_${EXPAT_VERSION//./_}/$ARCHIVE
tar -xzf $ARCHIVE
cd $DIR
# Prepara la compilación cruzada
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
  -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}

# Compila e instala
make
sudo make install

echo "\nExpat instalado para MinGW en ${INSTALL_PREFIX}"
