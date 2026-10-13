#!/usr/bin/env bash
# Make the creature data of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/creatures.json at its start. The file has the usual spawn
# points of the creatures of the whole world (each region of the game copy), and for each kind
# of creature its slice, the slots of its components, its vitals ids, its health, its name, and
# its action table (protocol notes 4.14). Without the file the backend starts, and the world
# has no creature.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/creatures.sh [OUTPUT.json]
#        tools/gamedata/creatures.sh [OUTPUT.json] --radius METERS [--center X Y]
#                                    a smaller file: the spawn points of a circle (with no
#                                    center: around the start place of a new character)
#        tools/gamedata/creatures.sh [OUTPUT.json] --report FILE
#                                    one line in FILE for each entry that the tool leaves out
#        tools/gamedata/creatures.sh --find TEXT...   print the kinds whose id or name has a text
# The file creature_slices.json of this folder has the slices that need a spawn action and the
# slices that the client does not show correctly (the tool leaves out their spawn points).
# --all-slices writes each slice (a file for a picture check with tools/research/creature_tour.py).
# The pak entries have Oodle data, which Python cannot read. The tool uses an Oodle library of
# this computer (SOULWARDEN_OODLE_LIB, for example liboo2core.so), or the Oodle helper of
# tools/research with a library for Windows (SOULWARDEN_OODLE_DLL), or the files that
# tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR, default
# ~/Games/soulwarden/work/assets/extract). It stops with status 1 and one message when one file
# that it needs has no source, and then writes no file. The same for an animation file that a
# slice names (its action table) and that is not in the pak files or is not correct XML.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB,
# SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "creatures: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/creature_data.py" "$@"
