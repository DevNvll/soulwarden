#!/usr/bin/env bash
# Make the conversation data of the persons from the game copy of the user (stage 2.5c).
# The backend reads GAMEDATA_DIR/conversation-data.json at its start: the NPC rows with their conversation states in authored order, the
# states with their requirements, topics, and services, and the topics with their requirements (docs/systems/persons.md). Without the
# file a person keeps its conversation, with no state selection, no topic, and no service. The file is game data: it stays outside the
# repository.
# Usage: tools/gamedata/conversations.sh [OUTPUT.json]
#        tools/gamedata/conversations.sh --npc ID...     print NPC rows with their states and topics (no file)
# A test call must give an OUTPUT path outside the common game data folder.
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a table type has no source, when a table has rows and no id column, or when two NPC or topic ids have one CRC-32 (see
# conversations.py; tools/gamedata/check_conversations_tool.py is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "conversations: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/conversations.py" "$@"
