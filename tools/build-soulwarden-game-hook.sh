#!/usr/bin/env bash
# Build the game hook DLL with llvm-mingw.
# The CMake build directory is outside the repository (SOULWARDEN_BUILD_ROOT): its files have
# absolute paths of the computer.
set -euo pipefail

repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${SOULWARDEN_BUILD_ROOT:-${HOME}/Games/soulwarden/work/build}"
cmake_build_directory="${SOULWARDEN_CMAKE_BUILD_DIR:-${build_root}/llvm-mingw-game-hook}"
output_file="${SOULWARDEN_GAME_HOOK_OUTPUT:-${repository_directory}/apps/game-hook/dist/steam_api64.dll}"
toolchain_file="${repository_directory}/cmake/llvm-mingw-x64.cmake"

cmake \
    -S "${repository_directory}" \
    -B "${cmake_build_directory}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${toolchain_file}" \
    -DSOULWARDEN_BUILD_GAME_HOOK=ON \
    -DSOULWARDEN_BUILD_BACKEND=OFF >/dev/null

cmake --build "${cmake_build_directory}" --target steam_api64
mkdir -p "$(dirname -- "${output_file}")"
install -m 644 "${cmake_build_directory}/steam_api64.dll" "${output_file}"
