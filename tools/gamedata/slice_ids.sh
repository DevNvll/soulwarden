#!/usr/bin/env bash
# Make the ids of the player slice for the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/slice-ids.json at its start. The file has the id of each
# faceted component of slices/player.dynamicslice: with them the game server checks that the
# target of each component call of a client is a facet of the entity of the call (protocol
# notes 4.2). Without the file the game server makes no check of a target.
# The ids are game data: they stay outside the repository.
# Usage: tools/gamedata/slice_ids.sh [OUTPUT.json]
# The pak entry of the slice has Oodle data, which Python cannot read. The tool uses the Oodle
# helper of tools/research when SOULWARDEN_OODLE_DLL names an Oodle library, and if not, the file
# that tools/research/pakx.py extracted (SOULWARDEN_SLICE_FILE, default
# ~/Games/soulwarden/work/assets/extract/slices/player.dynamicslice). It stops with status 1 and
# one message when it has no source, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_SLICE_FILE,
# SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "slice-ids: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/slice_ids.py" "$@"
