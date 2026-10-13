#!/usr/bin/env bash
# Stop the processes of development runs: the game, its backend, gamescope, the launcher, and
# each Wine process. Only a process that has the mark of a development run in its environment
# is stopped (SOULWARDEN_DEV_RUN=1, which tools/dev-run.sh gives to the launcher). A game that the
# user started from the Steam library or with linux-compat/run-soulwarden.sh has no mark: it and
# its backend continue.
# Usage: tools/dev-stop.sh           stop all development runs
#        tools/dev-stop.sh --check   print the processes of development runs that are alive.
#                                    Status 0: none. Status 1: one or more.
set -uo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dev-env.sh"

game_pattern='Bin64.NewWorld[.]exe'
# The backend by the program of its command line. The name of the process is not sufficient:
# the kernel keeps only its first 15 characters, and pgrep -x then finds no process.
backend_pattern='^([^ ]*/)?soulwarden-backend( |$)'

if [[ "${1:-}" == "--check" ]]; then
    remaining="$(dev_processes)"
    if [[ -z "${remaining}" ]]; then
        echo "dev-stop: no process of a development run"
        exit 0
    fi
    for process in ${remaining}; do
        echo "dev-stop: process ${process} ($(cat "/proc/${process}/comm" 2>/dev/null || echo gone)) of a development run is alive"
    done
    exit 1
fi

# The game first. Its launcher then asks the backend to stop, and the backend saves the state
# of each session.
games="$(dev_processes_matching -f "${game_pattern}")"
if [[ -n "${games}" ]]; then
    # shellcheck disable=SC2086
    kill ${games} 2>/dev/null || true
fi
for ((attempt = 0; attempt < 60; ++attempt)); do
    games="$(dev_processes_matching -f "${game_pattern}")"
    backends="$(dev_processes_matching -f "${backend_pattern}")"
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
    remaining="$(dev_processes)"
    [[ -z "${remaining}" ]] && break
    # shellcheck disable=SC2086
    kill -s "${signal}" ${remaining} 2>/dev/null || true
    sleep 0.5
done
exit 0
