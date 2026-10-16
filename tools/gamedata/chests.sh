#!/usr/bin/env bash
# Make the chests of the backend from the game copy of the user (stage 3.5b.1).
# The backend reads GAMEDATA_DIR/chest-data.json at its start. The file has each chest placement of the main world: its placement and
# its managed actor id, its variation row, its gatherable row (the open time, the reset values, the restrictions), its loot root, the
# loot tags and the content level of its territories, and its state (ordinary, or unavailable with its reason), and the census of the
# gatherable records of the layer metadata (docs/systems/chests.md). Without the file no chest opens.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/chests.sh [OUTPUT.json] [--region NAME]... [--radius METERS [--center X Y]]
#        tools/gamedata/chests.sh --list STATE_OR_KIND     print the chests of a state (ordinary, unavailable) or a kind, and write no file
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a file that it needs has no source and when a part of the source is not complete or not consistent (see
# chests.py; tools/gamedata/check_chests_tool.py is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "chests: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/chests.py" "$@"
