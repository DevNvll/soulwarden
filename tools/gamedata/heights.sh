#!/usr/bin/env bash
# Make the terrain heights of the backend from the game copy of the user.
# The backend reads the tile of a place from GAMEDATA_DIR/heights/r_+XX_+YY.swh when a creature
# needs the height of the ground there: a creature that walks stands on the terrain (protocol
# notes 4.17). The files have the terrain only: a floor of a building, a bridge, and a cave
# are not in them (the navigation data of stage 3.4d has them).
# Without the files the backend starts, and no creature leaves its place.
# The files are game data: they stay outside the repository.
# Usage: tools/gamedata/heights.sh [OUTPUT_DIR] [--region NAME ...] [--jobs N]
#        tools/gamedata/heights.sh [OUTPUT_DIR] --height X Y     print the height of a position from the files
# The pak entries of the height maps are stored data: the tool needs no Oodle library. It needs
# 2 to 4 minutes for the 49 regions of the world (136 MB).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3).
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "heights: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/height_maps.py" "$@"
