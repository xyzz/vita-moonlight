set -e

echo "Build libromfs-generator"

PROJECT_PATH=$(dirname "$0")
LIBROMFS_PATH="${PROJECT_PATH}/library/borealis/library/lib/extern/libromfs/generator"
BUILD_DIR="build_libromfs_generator"

cd "${PROJECT_PATH}"

cmake -B "${BUILD_DIR}" "${LIBROMFS_PATH}"
make -C "${BUILD_DIR}"

cp "${BUILD_DIR}/libromfs-generator" "${PROJECT_PATH}"
echo "Build libromfs-generator: ${PROJECT_PATH}/libromfs-generator"

rm -rf "${BUILD_DIR}"
echo "Remove temp build dir: ${BUILD_DIR}"
