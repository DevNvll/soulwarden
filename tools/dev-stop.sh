#!/usr/bin/env bash
# Stop the processes of development runs: the game, its backend, gamescope, the launcher, and
# each Wine process. Only a process that has the mark of a development run in its environment
# is stopped (SOULWARDEN_DEV_RUN=1, which tools/dev-run.sh gives to the launcher). A game that the
# user started from the Steam library or with linux-compat/run-soulwarden.sh has no mark: it and
# its backend continue.
# Usage: tools/dev-stop.sh                       stop all development runs
#        tools/dev-stop.sh --data-dir DIR        stop only the development runs of the data directory DIR
#                                                (tools/dev-run.sh does this for its own directory)
#        tools/dev-stop.sh --check [--data-dir DIR]
#                                                print the processes of development runs (of DIR) that are
#                                                alive, with their data directories. Status 0: none.
#                                                Status 1: one or more.
# Status 2: an option that is not known (nothing is stopped).
set -uo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dev-env.sh"

check=0
only=""
while [[ $# -gt 0 ]]; do
    case "$1" in
    --check)
        check=1
        shift
        ;;
    --data-dir)
        if [[ $# -lt 2 || -z "$2" ]]; then
            echo "dev-stop: --data-dir needs a directory. Nothing was stopped." >&2
            exit 2
        fi
        only="$(dev_run_key "$2")"
        shift 2
        ;;
    *)
        echo "dev-stop: the option $1 is not known. Nothing was stopped." >&2
        sed -n 's/^# Usage: /  /p; s/^#        /  /p' "${BASH_SOURCE[0]}" >&2
        exit 2
        ;;
    esac
done

# The processes of the selection: each development run, or the runs of one data directory.
selected() {
    if [[ -n "${only}" ]]; then
        dev_processes_of "${only}"
    else
        dev_processes
    fi
}

# The processes of the selection whose command line has the pattern (as pgrep -f).
selected_matching() {
    local process matches
    matches=" $(pgrep -f "$1" 2>/dev/null | tr '\n' ' ')"
    for process in $(selected); do
        if [[ "${matches}" == *" ${process} "* ]]; then
            echo "${process}"
        fi
    done
}

game_pattern='Bin64.NewWorld[.]exe'
# The backend by the program of its command line. The name of the process is not sufficient:
# the kernel keeps only its first 15 characters, and pgrep -x then finds no process.
backend_pattern='^([^ ]*/)?soulwarden-backend( |$)'

if (( check )); then
    remaining="$(selected)"
    if [[ -z "${remaining}" ]]; then
        echo "dev-stop: no process of a development run${only:+ of the data directory ${only}}"
        exit 0
    fi
    for process in ${remaining}; do
        echo "dev-stop: process ${process} ($(cat "/proc/${process}/comm" 2>/dev/null || echo gone)) of a development run is alive" \
            "(data directory $(dev_run_directory "${process}"))"
    done
    exit 1
fi

# The game first. Its launcher then asks the backend to stop, and the backend saves the state
# of each session.
games="$(selected_matching "${game_pattern}")"
if [[ -n "${games}" ]]; then
    # shellcheck disable=SC2086
    kill ${games} 2>/dev/null || true
fi
for ((attempt = 0; attempt < 60; ++attempt)); do
    games="$(selected_matching "${game_pattern}")"
    backends="$(selected_matching "${backend_pattern}")"
    if [[ -z "${games}" && -z "${backends}" ]]; then
        break
    fi
    if (( attempt == 30 )); then
        # shellcheck disable=SC2086
        [[ -n "${games}" ]] && kill -9 ${games} 2>/dev/null
        # shellcheck disable=SC2086
        [[ -n "${backends}" ]] && kill ${backends} 2>/dev/null
    fi
    sleep 0.1
done
for signal in TERM KILL; do
    remaining="$(selected)"
    [[ -z "${remaining}" ]] && break
    # shellcheck disable=SC2086
    kill -s "${signal}" ${remaining} 2>/dev/null || true
    sleep 0.5
done
exit 0
