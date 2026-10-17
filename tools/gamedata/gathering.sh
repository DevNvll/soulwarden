#!/usr/bin/env bash
# Make the gathering data of the backend from the game copy of the user (stage 3.6a).
# The backend reads GAMEDATA_DIR/gathering-data.json at its start and GAMEDATA_DIR/gathering/SET/REGION.swgn when it needs a node of a
# region: the gatherable rows, the variation rows, the gathering slices, the natural nodes of each distribution file with the offline
# id of the client, the capital nodes, the skinning of the creature slices, the tools, the trade skill ranks, and the events
# (docs/systems/gathering.md). Without the files no node can be gathered. The files are game data: they stay outside the repository.
# The tool needs the height files of the game data folder (tools/gamedata/heights.sh) and the creature data (creatures.json) for the
# skinning.
# Usage: tools/gamedata/gathering.sh [OUTPUT.json] [--region NAME]...
#        tools/gamedata/gathering.sh --near X Y METERS     print the gathering nodes near a place (with their G) and write no file
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a file that it needs has no source and when a part of the source is not complete or not consistent (see
# gathering.py; tools/gamedata/check_gathering_tool.py is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "gathering: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/gathering.py" "$@"
