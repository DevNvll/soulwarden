#!/usr/bin/env bash
# Build and run the test program of the backend (native target). The tests run the game
# server with made-up clients: no socket, no game process, and a database in memory.
# Usage: tools/test-soulwarden-backend.sh [--write-golden] [--write-trace] [--list] [CASE ...]
#   --write-golden   write the stored bytes of case T2 again (apps/backend/tests/golden). Do
#                    this only for a change of the bytes that is intended, with a live run.
#   --write-trace    write the stored trace of the decisions of the behavior of case BTD1 again
#                    (apps/backend/tests/golden/behavior_trace.txt). Do this only for a change of
#                    the behavior that is intended (a later stage of the trees), with its source.
#   --list           print the cases
#   CASE             run only these cases, for example: T1 T2
# Status 0: each case passed. The output lists each case.
# The CMake build directory is outside the repository (SOULWARDEN_BUILD_ROOT), and the files that a
# case writes are in that directory.
set -euo pipefail

repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${SOULWARDEN_BUILD_ROOT:-${HOME}/Games/soulwarden/work/build}"
cmake_build_directory="${SOULWARDEN_BACKEND_CMAKE_BUILD_DIR:-${build_root}/native-backend}"

cmake \
    -S "${repository_directory}" \
    -B "${cmake_build_directory}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DSOULWARDEN_BUILD_GAME_HOOK=OFF \
    -DSOULWARDEN_BUILD_BACKEND=ON >/dev/null

cmake --build "${cmake_build_directory}" --target soulwarden_backend_tests
exec "${cmake_build_directory}/apps/backend/soulwarden-backend-tests" \
    --golden-dir "${repository_directory}/apps/backend/tests/golden" \
    --work-dir "${cmake_build_directory}/test-work" \
    "$@"
