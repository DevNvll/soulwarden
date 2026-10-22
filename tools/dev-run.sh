#!/usr/bin/env bash
# Development loop: restart the backend and the game with protocol capture on.
# The game window opens on an isolated agent desktop, not on the user desktop.
# Usage: tools/dev-run.sh [run-label] [-- game arguments]
# The game gets the arguments of tools/dev-env.sh (dev_sound_arguments: -sys_audio_disable 1, no sound) before the game arguments.
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
# has the mark SOULWARDEN_DEV_RUN=1, which tools/dev-stop.sh uses, and its data directory
# (SOULWARDEN_DEV_RUN_DIR).
# One development run at a time (stage 1.8f): the script stops the earlier runs of its own data
# directory only. When a run of a different data directory is alive, it starts nothing and stops
# nothing (status 1): it prints that run (its data directory and its processes) and the command
# that stops it.
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

# The check of the other runs and the start are one step for each start of this script on the computer: two starts at one
# time do not both see no other run. The lock is not in a data directory, and the launcher does not get it (7>&- below).
run_key="$(dev_run_key "${data_dir}")"
start_lock="${XDG_RUNTIME_DIR:-/tmp}/soulwarden-dev-run.lock"
if command -v flock >/dev/null 2>&1; then
    exec 7>>"${start_lock}"
    if ! flock -n 7; then
        echo "dev-run: a different tools/dev-run.sh starts a game now. The game was not started. Start again later." >&2
        exit 1
    fi
fi
# A run of a different data directory: no start, no stop (the incident of 2026-10-22: a new run stopped a run of a different
# data directory and prefix).
declare -A other_runs=()
while read -r process directory; do
    if [[ -n "${process}" && "${directory}" != "${run_key}" ]]; then
        other_runs["${directory}"]+=" ${process}"
    fi
done < <(dev_processes_with_directory)
if (( ${#other_runs[@]} > 0 )); then
    for directory in "${!other_runs[@]}"; do
        echo "dev-run: a development run of the data directory ${directory} is alive (processes:${other_runs[${directory}]})." >&2
        if [[ "${directory}" == "unknown" ]]; then
            echo "dev-run: stop it with tools/dev-stop.sh (this stops each development run)." >&2
        else
            echo "dev-run: stop it with: tools/dev-stop.sh --data-dir ${directory}" >&2
        fi
    done
    echo "dev-run: the game was not started, and no process was stopped. One development run at a time." >&2
    exit 1
fi
"${repository_directory}/tools/dev-stop.sh" --data-dir "${run_key}"
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

if ! omarchy-agent-desktop status "${desktop_id}" >/dev/null 2>&1; then
    omarchy-agent-desktop create "Soulwarden game" --id "${desktop_id}" --mode "${desktop_mode}" >/dev/null 7>&-
fi
launcher_environment=("${dev_mark}" "SOULWARDEN_DEV_RUN_DIR=${run_key}" "SOULWARDEN_COMPAT_DIR=${dev_compat_dir}" "SOULWARDEN_DATA_DIR=${data_dir}"
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
    "${repository_directory}/linux-compat/run-soulwarden.sh" "${dev_sound_arguments[@]}" "$@" \
    >"${data_dir}/logs/launch-${label}.json" 2>&1 7>&- || true
exec 7>&-
cat "${data_dir}/logs/launch-${label}.json"
