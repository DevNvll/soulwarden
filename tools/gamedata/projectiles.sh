#!/usr/bin/env bash
# Make the projectile data of the backend from the game copy of the user (stage 3.3b-S3).
# The backend reads GAMEDATA_DIR/projectile-data.json at its start. The file has the ranged attack profiles of the game (the
# range, the radius of a projectile, the range of the hit near the muzzle), the flight values of each projectile slice
# (the speed, the acceleration, the life time), the ammunition, the profile of each weapon, and the shots of the basic
# attacks (protocol notes 4.27): the backend accepts a shot of a bow, a musket, a staff, or a gauntlet and follows its
# projectile with them. Without the file the backend starts: each such shot is then refused.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/projectiles.sh [OUTPUT.json]
#        tools/gamedata/projectiles.sh --find TEXT...   print the attacks, the shots, and the weapons with a text
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
    echo "projectiles: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/projectiles.py" "$@"
