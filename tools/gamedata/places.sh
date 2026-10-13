#!/usr/bin/env bash
# Make the travel places of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/travel-places.json at its start. The file has the places of the
# world to which the travel command can move a character: the spirit shrines and the inns of
# each territory, from the territory landmarks of the region files, with the name of each one
# and a position on the ground (protocol notes 4.16).
# Without the file the backend starts, and the travel command takes only a position in numbers.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/places.sh [OUTPUT.json]
#        tools/gamedata/places.sh --list     print the places, and write no file
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a
# native Oodle library (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle
# library for Windows (SOULWARDEN_OODLE_DLL), or the files that tools/research/pakx.py extracted
# (SOULWARDEN_EXTRACT_DIR, default ~/Games/soulwarden/work/assets/extract). It stops with status 1 and one
# message when a file that it needs has no source, and when a spirit shrine or an inn has a
# position with a wrong form, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB,
# SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "places: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/travel_places.py" "$@"
