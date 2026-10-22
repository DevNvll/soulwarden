#!/usr/bin/env bash
# Make the destructible quest objects of the backend from the game copy of the user (stage 1.8e; docs/systems/quests.md).
# The backend reads GAMEDATA_DIR/quest-objects.json at its start: the placements whose slice has the variation table
# Gatherable_Quest_Damageable (the corruption nodules of 06_survivors, the monolith of 06_demonstorm2), their managed actor, position,
# GatherableController slot, variation row (InteractTag, InteractOnlyOnQuest), and gatherable row (BaseGatherTime, the respawn bounds,
# GameEventID). Without the file no quest object can be destroyed, and the rest of the backend works. The file is game data: it stays
# outside the repository.
# Usage: tools/gamedata/quest-objects.sh [OUTPUT.json]
# A test call must give an OUTPUT path outside the common game data folder.
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a source that it needs is not there or not complete (see quest_objects.py).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "quest objects: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/quest_objects.py" "$@"
