#!/usr/bin/env bash
# Make the interaction objects of the backend from the game copy of the user (stage 2.5a).
# The backend reads GAMEDATA_DIR/interaction-data.json at its start. The file has each object of the level data that a
# player can use with the key of an interaction (a lore page, a person, a storage shed, a gathering node, a door): its ids on
# the wire, its place, its Interact components, and its options with their rows of the user interface database (protocol
# notes 4.35, docs/systems/interactions.md). Without the file the backend starts with an empty catalog: it refuses each
# request of an interaction, and a read lore page is not kept.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/interactions.sh [OUTPUT.json] [--region NAME]... [--radius METERS [--center X Y]]
#        tools/gamedata/interactions.sh --list KIND     print the objects of a kind (lorePage, npc, ...), and write no file
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a file that it needs has no source and when a part of the source is not complete or not consistent (see
# interactions.py; tools/gamedata/check_interactions_tool.py is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "interactions: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/interactions.py" "$@"
