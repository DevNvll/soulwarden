#!/usr/bin/env bash
# Make the item catalog of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/item-catalog.json at its start. The file has each item of
# the item tables of the game with its index on the wire (the client builds one list from 43
# item tables and 2 house item tables: protocol notes 4.10), its id, the text key of its
# name, its type, its class, its weight, its stack limit, its durability, and its gear score.
# Without the file the backend starts and gives no item.
# The catalog is game data: it stays outside the repository.
# Usage: tools/gamedata/items.sh [OUTPUT.json]
#        tools/gamedata/items.sh --find ID...     print the catalog line of each item id
# The pak entries of the tables have Oodle data, which Python cannot read. The tool uses the
# Oodle helper of tools/research when SOULWARDEN_OODLE_DLL names an Oodle library, and if not, the
# files that tools/research/pakx.py extracted (SOULWARDEN_DATATABLES_DIR, default
# ~/Games/soulwarden/work/assets/extract/sharedassets/springboardentitites/datatables). It stops
# with status 1 and one message when it has no source, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_DATATABLES_DIR,
# SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "items: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/item_catalog.py" "$@"
