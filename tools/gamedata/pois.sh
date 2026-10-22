#!/usr/bin/env bash
# Make the points of interest of the backend from the game copy of the user (stage 1.8c, docs/systems/quests.md).
# The backend reads GAMEDATA_DIR/poi-data.json at its start: the shape of each point of interest, for the quest tasks of a place and the
# location rule of a task. Without the file the backend starts, and no quest task of a place progresses.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/pois.sh [OUTPUT.json] [--region NAME]...
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
# SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "pois: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/pois.py" "$@"
