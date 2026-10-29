#!/usr/bin/env bash
# Make the game modes of the backend from the game copy of the user (stage DG1).
# The backend reads GAMEDATA_DIR/game-modes.json at its start. The file has the maps of the game modes with a level of their own or of
# the main world: the level text and the terrain text of the level information, the start places, and the rows of the mode (the levels,
# the group sizes, the kind of the mode). The development command "instance enter" takes the start of a map (docs/systems/instances.md).
# Without the file the backend starts, and "instance enter" is refused. The file is game data: it stays outside the repository.
# Usage: tools/gamedata/game-modes.sh [OUTPUT.json]
#        tools/gamedata/game-modes.sh --list     print the maps, and write no file
# A test call must give an OUTPUT path outside the common game data folder.
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR, default ~/Games/soulwarden/work/assets/extract). It stops with
# status 1 and one message, and writes no file, when a file that it needs has no source, when two modes or two maps have one id, when
# a map names a mode that is not there, or when a start place has a wrong form (see game_modes.py; tools/gamedata/check_game_modes_tool.py
# is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "game-modes: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/game_modes.py" "$@"
