#!/usr/bin/env bash
# Make the navigation data of the backend from the game copy of the user.
# The backend reads the tiles near the players from GAMEDATA_DIR/navigation/r_+XX_+YY.swnav: a
# creature goes on a path of this data (around a wall, through a gate, up stairs), stands on
# the floor of its place, and does not see through a wall (protocol notes 4.19). The game copy
# has no navigation mesh: the tool makes the data from the collision shapes of the world (the
# terrain with the cookies of the placements, the solid objects, the water, and the navigation
# marks of the level designers).
# Without the files the backend starts, and a creature walks on the straight line on the terrain.
# The files are game data: they stay outside the repository.
# Usage: tools/gamedata/navigation.sh [OUTPUT_DIR] [--region NAME ...] [--jobs N] [--force] [--no-heights]
#        tools/gamedata/navigation.sh [OUTPUT_DIR] --spawn-points CREATURES.json [REPORT_DIR]
#                                     which spawn points of the creature data are on a free floor
# The tool needs Python 3, a C compiler and a C++ compiler (clang or gcc), and an Oodle library
# of this computer (SOULWARDEN_OODLE_LIB, for example liboo2core.so) or the Oodle helper of
# tools/research (SOULWARDEN_OODLE_DLL). The world needs about 1 hour with 4 processes, 1.3 GB in
# the game data folder, and 2 GB of work files (SOULWARDEN_NAVIGATION_WORK). A second run makes
# only the regions that are not complete. At its end the tool makes the terrain heights
# (tools/gamedata/heights.sh) again, from the terrain after the cookies.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_NAVIGATION_WORK (default
# ~/Games/soulwarden/work/navigation-build), SOULWARDEN_PYTHON (default python3), CC, CXX.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "navigation: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/navigation_data.py" "$@"
