#!/usr/bin/env bash
# Development loop: restart the backend and the game with protocol capture on.
# The game window opens on an isolated agent desktop, not on the user desktop.
# Usage: tools/dev-run.sh [run-label] [-- game arguments]
# The agent desktop starts the launcher with the environment of the compositor, not with the
# environment of this script. The variables below go to the launcher by name. An identity or
# seed variable that is not set stays not set.
# The data directory is the development data directory (tools/dev-env.sh:
# ~/Games/soulwarden/work/dev-data), not the player data of the user. It stays between runs: its
# database has the characters of the earlier runs. SOULWARDEN_DATA_DIR=<absolute path> selects a
# different one: give the same value to tools/hx, tools/shot, and the scenario scripts of that
# run.
# A development run is separate from a game that the user started (tools/dev-env.sh): it has
# its own Proton prefix (SOULWARDEN_COMPAT_DIR selects a different one), and each of its processes
# has the mark SOULWARDEN_DEV_RUN=1, which tools/dev-stop.sh uses.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${repository_directory}/tools/dev-env.sh"
data_dir="${dev_data_dir}"
desktop_id="${SOULWARDEN_DESKTOP_ID:-soulwarden-game}"
desktop_mode="${SOULWARDEN_DESKTOP_MODE:-1280x720@60}"
label="${1:-run}"
shift || true
if [[ "${1:-}" == "--" ]]; then
    shift
fi

# The private display is off only for the value 0. A different value must not turn it off.
private_display="${SOULWARDEN_PRIVATE_DISPLAY:-1}"
if [[ "${private_display}" != "0" && "${private_display}" != "1" ]]; then
    echo "dev-run: SOULWARDEN_PRIVATE_DISPLAY must be 0 or 1. The game was not started." >&2
    exit 1
fi
if [[ "${private_display}" == "1" ]] && ! command -v gamescope >/dev/null 2>&1; then
    echo "dev-run: gamescope was not found. The game was not started (no private display)." >&2
    exit 1
fi
if [[ "${data_dir%/}" == "${user_data_dir%/}" ]]; then
    echo "dev-run: SOULWARDEN_DATA_DIR is the data directory of the user (${data_dir}). The accounts and" \
        "the characters of this run stay in the player data of the user." >&2
fi

# The resolved paths: a link or a "/." names the same prefix (code review of stage 3.3b-S2b, finding F1).
if [[ "$(realpath -m -- "${dev_compat_dir}")" == "$(realpath -m -- "${user_compat_dir}")" ]]; then
    echo "dev-run: SOULWARDEN_COMPAT_DIR is the Proton prefix of a start with no development variable." \
        "This run stops a game that the user started with that prefix." >&2
fi

"${repository_directory}/tools/dev-stop.sh"
mkdir -p "${data_dir}/logs"
# The launcher of a game holds this lock. After the stop of the development runs, a lock that
# is held is of a game that the user started with this data directory. This script must not
# remove the captures and the log of that game. The script holds the lock while it removes
# the files: a different start cannot take the data directory between the check and the
# removal.
if command -v flock >/dev/null 2>&1; then
    exec 8>>"${data_dir}/run.lock"
    if ! flock -n 8; then
        echo "dev-run: a game that is not a development run uses the data directory ${data_dir}." \
            "The game was not started. Set SOULWARDEN_DATA_DIR to a different directory." >&2
        exit 1
    fi
fi
rm -rf "${data_dir}/captures"
: >"${data_dir}/logs/backend.log"
# The launcher takes the lock itself: the script gives it back now. A different start that
# takes the lock before the launcher makes the launcher stop with a message ("another game
# process uses this data directory"), and this script removed no file of that start.
exec 8>&-

# A development run makes no sound: the master volume of each account in the settings of the
# development prefix is 0 before the start (the game keeps one settings file for each account).
# The game is stopped here, so it does not write the old value back. tools/dev-mute.sh (with
# tools/dev_mute.py) opens each folder and file one time, checks the open descriptor, and writes
# through it: it changes nothing in the prefix of the user. SOULWARDEN_DEV_SOUND=1 keeps the
# volumes of the prefix.
"${repository_directory}/tools/dev-mute.sh" "${dev_compat_dir}" "${user_compat_dir}"

if ! omarchy-agent-desktop status "${desktop_id}" >/dev/null 2>&1; then
    omarchy-agent-desktop create "Soulwarden game" --id "${desktop_id}" --mode "${desktop_mode}" >/dev/null
fi
launcher_environment=("${dev_mark}" "SOULWARDEN_COMPAT_DIR=${dev_compat_dir}" "SOULWARDEN_DATA_DIR=${data_dir}"
    SOULWARDEN_CAPTURE=1 SOULWARDEN_HARNESS=1
    "SOULWARDEN_TRACE=${SOULWARDEN_TRACE:-}" "SOULWARDEN_PROBE=${SOULWARDEN_PROBE:-}"
    "SOULWARDEN_NO_REP_LISTENER=${SOULWARDEN_NO_REP_LISTENER:-0}"
    # A development run has the development commands of the world (tools/world), and its
    # sessions observe other entities: thus an entity of tools/world shows in the client.
    SOULWARDEN_DEV_COMMANDS=1 "SOULWARDEN_OBSERVE_PLAYERS=${SOULWARDEN_OBSERVE_PLAYERS:-1}")
for name in SOULWARDEN_GAMEDATA_DIR SOULWARDEN_GAME_DIR SOULWARDEN_STEAM_DIR SOULWARDEN_PROTON_DIR \
    SOULWARDEN_STEAM_ID SOULWARDEN_PERSONA_NAME SOULWARDEN_SEED_CHARACTER SOULWARDEN_BACKEND_EXE SOULWARDEN_HOOK_DLL \
    SOULWARDEN_RESEARCH_SESSION SOULWARDEN_TOOL_WINDOW SOULWARDEN_TOOL_WINDOW_KEY SOULWARDEN_TOOL_WINDOW_FAULT \
    SOULWARDEN_EXACT_POSITION SOULWARDEN_EXACT_POSITION_FAULT SOULWARDEN_MELEE_REPORT \
    SOULWARDEN_CREATURE_BEHAVIOR; do
    if [[ -n "${!name:-}" ]]; then
        launcher_environment+=("${name}=${!name}")
    fi
done
# The game gets a private X server. On the X server of the user session, the pointer and the
# input focus are common to all X11 applications: pointer moves and focus changes of the test
# harness then reach the other games of the user. gamescope is one Wayland window on the agent
# desktop and has its own Xwayland for the game. SOULWARDEN_PRIVATE_DISPLAY=0 turns this off.
display_wrapper=()
if [[ "${private_display}" == "1" ]]; then
    game_size="${SOULWARDEN_GAME_SIZE:-1256x660}"
    display_wrapper=(gamescope --backend wayland -W "${game_size%x*}" -H "${game_size#*x}"
        -w "${game_size%x*}" -h "${game_size#*x}" -r 60 --)
else
    echo "dev-run: SOULWARDEN_PRIVATE_DISPLAY=0. The game is on the X server of the user session:" \
        "the input of the harness can reach the other X11 applications." >&2
fi
omarchy-agent-desktop run --wait "${SOULWARDEN_WINDOW_WAIT:-120}" --json "${desktop_id}" -- \
    env "${launcher_environment[@]}" "${display_wrapper[@]}" \
    "${repository_directory}/linux-compat/run-soulwarden.sh" "$@" \
    >"${data_dir}/logs/launch-${label}.json" 2>&1 || true
cat "${data_dir}/logs/launch-${label}.json"
