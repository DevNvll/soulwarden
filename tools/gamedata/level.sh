#!/usr/bin/env bash
# Make the level data set of an own level for the backend (stage DG2, docs/systems/instances.md, "The level data").
# The backend reads GAMEDATA_DIR/levels/LEVEL when the first instance of that level starts: the spawn points of its layers with their kinds
# and slices, its navigation data and terrain heights, and the objects of its capitals. The level and its terrain folder come from its map
# row (javelindata_gamemodemaps). Without a set an instance of the level has no content.
# The files are game data: they stay outside the repository.
# Usage: tools/gamedata/level.sh LEVEL [GAMEDATA_DIR] [--jobs N]    LEVEL: a level or a map id ("DungeonAmrine")
#        tools/gamedata/level.sh --list [GAMEDATA_DIR]              each own level of the map table and its set
# Amrine needs about 1 minute and 2.3 MB. The tool needs what tools/gamedata/creatures.sh and navigation.sh need: an Oodle library of this
# computer (SOULWARDEN_OODLE_LIB), a C and a C++ compiler.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
# SOULWARDEN_NAVIGATION_WORK (default ~/Games/soulwarden/work/navigation-build), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "level: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/level_data.py" "$@"
