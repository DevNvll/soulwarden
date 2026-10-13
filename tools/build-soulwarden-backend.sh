#!/usr/bin/env bash
# Build the standalone backend. The default is a native Linux program.
# Set SOULWARDEN_BACKEND_TARGET=windows for the MinGW executable.
# The CMake build directory is outside the repository (SOULWARDEN_BUILD_ROOT): its files have
# absolute paths of the computer.
set -euo pipefail

repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
backend_directory="${repository_directory}/apps/backend"
build_root="${SOULWARDEN_BUILD_ROOT:-${HOME}/Games/soulwarden/work/build}"
target="${SOULWARDEN_BACKEND_TARGET:-native}"

if [[ "${target}" == "windows" ]]; then
    cmake_build_directory="${SOULWARDEN_BACKEND_CMAKE_BUILD_DIR:-${build_root}/llvm-mingw-backend}"
    toolchain=(-DCMAKE_TOOLCHAIN_FILE="${repository_directory}/cmake/llvm-mingw-x64.cmake")
    product="soulwarden-backend.exe"
else
    cmake_build_directory="${SOULWARDEN_BACKEND_CMAKE_BUILD_DIR:-${build_root}/native-backend}"
    toolchain=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
    product="soulwarden-backend"
fi

cmake \
    -S "${repository_directory}" \
    -B "${cmake_build_directory}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    "${toolchain[@]}" \
    -DSOULWARDEN_BUILD_GAME_HOOK=OFF \
    -DSOULWARDEN_BUILD_BACKEND=ON >/dev/null

cmake --build "${cmake_build_directory}" --target soulwarden_backend
mkdir -p "${backend_directory}/dist"
install -m 755 "${cmake_build_directory}/apps/backend/${product}" "${backend_directory}/dist/${product}"
