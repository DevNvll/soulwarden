#!/usr/bin/env bash
# Make the stamina rules of a player and the dodge from the game copy of the user (stage 3.4f-B).
# The backend reads GAMEDATA_DIR/stamina-data.json at its start: the Stamina component of the player slice (the maximum, the
# regeneration rate, the winded time), the delays of the base vitals, the stamina factors of each load class, the dodge action (the
# fragment and the cost of each load class, the delay at its exit), the times of the dodge fragments (the payment, the immune window,
# the mark CanTransition, the windows of a second dodge), and the exhausted state.
# Without the file the backend starts: a character keeps its full stamina, and a dodge has no immune window.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/stamina.sh [OUTPUT.json]
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message when a file that it
# needs has no source, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "stamina: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/stamina.py" "$@"
