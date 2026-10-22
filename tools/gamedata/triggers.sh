#!/usr/bin/env bash
# Make the trigger volumes of the quests from the game copy of the user (stage 1.8b, docs/systems/quests.md).
# The backend reads GAMEDATA_DIR/trigger-data.json at its start: the volume of each tag of a TaskTriggerArea (TP_TargetTag).
# Without the file the backend starts, and a task of a trigger area waits.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/triggers.sh [OUTPUT.json] [--region NAME]... [--quests QUEST_DATA.json]
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
# SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR,
# SOULWARDEN_NAVIGATION_WORK (the folder of the index of the pak files; default ~/Games/soulwarden/work/navigation-build).
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "triggers: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/triggers.py" "$@"
