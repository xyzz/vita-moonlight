#!/bin/bash
set -e

PROJECT_ROOT=$(pwd)
HOST_SOURCE_DIR="$PROJECT_ROOT/third_party/libmoonmic/host"
OUTPUT_DIR="$PROJECT_ROOT/cmake-build-psv/host"

install_deps() {
    echo "Checking dependencies..."
    MISSING_PACKAGES=""

    # Linux dependencies
    if ! dpkg -s build-essential >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES build-essential"; fi
    if ! dpkg -s cmake >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES cmake"; fi
    if ! dpkg -s pkg-config >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES pkg-config"; fi
    if ! dpkg -s libopus-dev >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES libopus-dev"; fi
    if ! dpkg -s libglfw3-dev >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES libglfw3-dev"; fi
    if ! dpkg -s libgl1-mesa-dev >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES libgl1-mesa-dev"; fi
    if ! dpkg -s libpulse-dev >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES libpulse-dev"; fi
    if ! dpkg -s nlohmann-json3-dev >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES nlohmann-json3-dev"; fi
    if ! dpkg -s libpng-dev >/dev/null 2>&1; then MISSING_PACKAGES="$MISSING_PACKAGES libpng-dev"; fi

    # Windows cross-compiler
    if ! command -v x86_64-w64-mingw32-g++ &> /dev/null; then
        MISSING_PACKAGES="$MISSING_PACKAGES g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64"
    fi

    if [ ! -z "$MISSING_PACKAGES" ]; then
        echo "Missing packages found: $MISSING_PACKAGES"
        echo "Installing missing dependencies (requires sudo)..."
        sudo apt-get update
        sudo apt-get install -y $MISSING_PACKAGES
    else
        echo "All dependencies are installed."
    fi
}

install_deps

mkdir -p "$OUTPUT_DIR/windows"
mkdir -p "$OUTPUT_DIR/linux"

# Do not leak Vita toolchain settings into host builds.
echo "Clearing Vita-specific environment variables..."
unset CC
unset CXX
unset AR
unset RANLIB
unset NM
unset STRIP
unset CFLAGS
unset CXXFLAGS
unset LDFLAGS
unset CMAKE_C_COMPILER
unset CMAKE_CXX_COMPILER
unset CMAKE_AR
unset CMAKE_RANLIB
unset CMAKE_NM
unset CMAKE_STRIP
unset CMAKE_TOOLCHAIN_FILE

echo "=== Building MoonMic Host Applications ==="

echo "--- Building for Linux ---"
mkdir -p "$PROJECT_ROOT/cmake-build-psv/third_party/libmoonmic/build-linux"
cd "$PROJECT_ROOT/cmake-build-psv/third_party/libmoonmic/build-linux"
cmake "${HOST_SOURCE_DIR}" -DCMAKE_BUILD_TYPE=Release -DHOST_TARGET_LINUX=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_TOOLCHAIN_FILE=""
make -j$(nproc)
cp moonmic-host "$OUTPUT_DIR/linux/"
echo "Linux build complete: $OUTPUT_DIR/linux/moonmic-host"

echo "--- Building for Windows (MinGW) ---"
mkdir -p "$PROJECT_ROOT/cmake-build-psv/third_party/libmoonmic/build-windows"
cd "$PROJECT_ROOT/cmake-build-psv/third_party/libmoonmic/build-windows"

# Crear archivo toolchain temporal
cat > toolchain-mingw.cmake <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

cmake "${HOST_SOURCE_DIR}" -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -DCMAKE_BUILD_TYPE=Release -DHOST_TARGET_WINDOWS=ON
make -j$(nproc)
cp moonmic-host.exe "$OUTPUT_DIR/windows/"
echo "Windows build complete: $OUTPUT_DIR/windows/moonmic-host.exe"

echo "=== Host builds finished ==="
echo "Binaries available in: $OUTPUT_DIR"
