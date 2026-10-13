#!/usr/bin/env bash
# Make the appearance tables of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/appearance-tables.json at its start. The file has the name
# lists of the character creation databases of the game: with them the backend converts the
# appearance names of a character to the list indexes of the world state. Without the file
# each character has the default appearance in the world.
# The tables are game data: they stay outside the repository.
# Usage: tools/gamedata/appearance.sh [OUTPUT.json]
# The tool reads the pak files of the game copy directly. It needs Python 3 with its standard
# library and no other file. It stops with status 1 and one message when the game copy or a
# database is not there, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3).
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "appearance: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/appearance_tables.py" "$@"
