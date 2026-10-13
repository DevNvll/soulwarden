#!/usr/bin/env bash
# Make the progression tables of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/progression-tables.json at its start. The file has the rules of
# the level (experience, attribute points, and health of each level), of the attributes (the
# health that the constitution gives), of the weapon masteries and of the trade skills (the
# experience of each rank), and the nodes of the weapon trees (protocol notes 4.12 and 4.13).
# Without the file the backend starts, sends the stored values of a character, and refuses a
# command that sets a level, experience, an attribute, a mastery, or a trade skill.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/progression.sh [OUTPUT.json]
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
    echo "progression: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/progression_tables.py" "$@"
