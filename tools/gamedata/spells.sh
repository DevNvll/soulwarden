#!/usr/bin/env bash
# Make the spell entity data of the backend from the game copy of the user (stage 3.3b-S2a).
# The backend reads GAMEDATA_DIR/spell-data.json at its start. The file has, for each spell row with a prefab slice, the
# slice asset, the slots of its replicated components, its interest radius, and its delayed events (protocol notes 4.25):
# the backend makes such a spell (the vortex of Gravity Well) as an entity of the world and sends its record.
# Without the file the backend starts: a spell then has its hits and no entity that a client sees.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/spells.sh [OUTPUT.json]
#        tools/gamedata/spells.sh --find TEXT...   print the spells with a text and their slice
# The pak entries have Oodle data: the tool uses SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL with the helper, or the
# files that tools/research/pakx.py extracted (see tools/gamedata/creature_data.py). It stops with status 1 and one
# message when a file that it needs has no source, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "spells: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/spells.py" "$@"
