#!/usr/bin/env bash
# Make the mission data of the backend from the game copy of the user (stage QG6a).
# The backend reads GAMEDATA_DIR/mission-data.json at its start: the mission rows of the boards (the town project missions and the open world
# missions, with their objective, their override cells, their events, and their territory rule), the projects of the towns, the game events
# of the missions, the mission weights, and the flexible boards (docs/systems/mission-boards.md). Without the file the open of a board
# is refused, and the other systems do not change. The file is game data: it stays outside the repository.
# Usage: tools/gamedata/missions.sh [OUTPUT.json]
#        tools/gamedata/missions.sh --mission ID...     print missions with their project and events (no file)
# A test call must give an OUTPUT path outside the common game data folder.
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a file that it needs has no source, when two missions have one CRC-32, or when a territory cell has a wrong form (see
# missions.py; tools/gamedata/check_missions_tool.py is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "missions: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/missions.py" "$@"
