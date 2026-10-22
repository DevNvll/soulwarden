#!/usr/bin/env bash
# Common values of the development scripts. A script reads this file with "source".
#
# A development run (tools/dev-run.sh, tools/scenarios) and a game that the user starts
# (Steam library entry, linux-compat/run-soulwarden.sh) are separate:
# - A development run has its own Proton prefix. The launcher stops all processes of its
#   prefix before a start, thus two starts with one prefix stop each other.
# - Each process of a development run has the mark SOULWARDEN_DEV_RUN=1 in its environment.
#   tools/dev-stop.sh stops only the processes that have the mark. Each process also has the data
#   directory of its run (SOULWARDEN_DEV_RUN_DIR): tools/dev-run.sh stops only the runs of its own
#   data directory, and it starts no game while a run of a different data directory is alive.

# Proton prefix of a development run. SOULWARDEN_COMPAT_DIR selects a different one.
dev_compat_dir="${SOULWARDEN_COMPAT_DIR:-${HOME}/Games/soulwarden/proton-dev/1063730}"
# Prefix of a start with no development variable (the default of linux-compat/run-soulwarden.sh).
user_compat_dir="${HOME}/Games/soulwarden/proton/1063730"
dev_mark="SOULWARDEN_DEV_RUN=1"
# A development run makes no sound: tools/dev-run.sh gives the game the startup setting sys_audio_disable 1 ("1: use
# NullAudioSystem (disable all audio functionality)"; the game log then has "<Audio>: Skipping Audio System Initialization - it
# was disabled by startup settings!"). The form "-NAME VALUE" is the one that the client takes (live check of 2026-10-15; the
# form "+NAME VALUE" has no effect). No file of a prefix changes (code review of stage 3.3b-S6, finding S6-R1).
# SOULWARDEN_DEV_SOUND=1 gives the game its sound.
dev_sound_arguments=()
if [[ "${SOULWARDEN_DEV_SOUND:-0}" != "1" ]]; then
    dev_sound_arguments=(-sys_audio_disable 1)
fi

# Data directory of a development run and of each development tool (tools/hx, tools/shot,
# tools/world, the scenario scripts, the research tools): database, logs, captures, harness
# files. It stays between runs, thus a run enters the world with a character that is there
# already. SOULWARDEN_DATA_DIR selects a different one. This is the only place of the default: a
# Python tool reads it with tools/dev_env.py.
dev_data_dir="${SOULWARDEN_DATA_DIR:-${HOME}/Games/soulwarden/work/dev-data}"
# Data directory of a start with no development variable (the default of
# linux-compat/run-soulwarden.sh): the player data of the user.
user_data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/soulwarden"

# Prints the id of each process of the user that has the mark of a development run. The shell
# that calls this function and its parent processes are not in the list.
dev_processes() {
    local file process parent ancestors=" "
    process="$$"
    while [[ -n "${process}" && "${process}" != "0" ]]; do
        ancestors+="${process} "
        # The parent id is the fourth field of the stat file, after the name in brackets.
        parent="$(sed -n 's/^.*) [A-Za-z] \([0-9]*\) .*$/\1/p' "/proc/${process}/stat" 2>/dev/null || true)"
        process="${parent}"
    done
    for file in $(grep -lzxF -- "${dev_mark}" /proc/[0-9]*/environ 2>/dev/null || true); do
        process="${file#/proc/}"
        process="${process%/environ}"
        if [[ "${ancestors}" != *" ${process} "* ]]; then
            echo "${process}"
        fi
    done
}

# Prints the ids of the processes of development runs whose command line has the pattern
# (extended regular expression, as pgrep -f), or whose name is the text (as pgrep -x).
# Usage: dev_processes_matching -f PATTERN | dev_processes_matching -x NAME
dev_processes_matching() {
    local process matches
    matches=" $(pgrep "$1" "$2" 2>/dev/null | tr '\n' ' ')"
    for process in $(dev_processes); do
        if [[ "${matches}" == *" ${process} "* ]]; then
            echo "${process}"
        fi
    done
}

# The data directory of a development run, as one text for a compare: the absolute path with no
# link and no "/." (the same rule as the check of the prefix in tools/dev-run.sh).
dev_run_key() {
    realpath -m -- "$1"
}

# Prints the data directory of the development run of a process (dev_run_key), or "unknown".
# tools/dev-run.sh gives each process of a run SOULWARDEN_DEV_RUN_DIR (from stage 1.8f). A process of a
# run of an earlier script has only SOULWARDEN_DATA_DIR: the launcher gives the game the Windows form
# of its client folder (Z:\...\client), the other processes have the data directory.
dev_run_directory() {
    local line run="" data=""
    while IFS= read -r line; do
        case "${line}" in
        SOULWARDEN_DEV_RUN_DIR=*) run="${line#*=}" ;;
        SOULWARDEN_DATA_DIR=*) data="${line#*=}" ;;
        esac
    done < <({ tr '\0' '\n' <"/proc/$1/environ"; } 2>/dev/null)
    if [[ -z "${run}" ]]; then
        run="${data}"
        if [[ "${run}" == Z:\\* ]]; then
            run="${run#Z:}"
            run="${run//\\//}"
            run="${run%/client}"
        fi
    fi
    if [[ "${run}" != /* ]]; then
        echo "unknown"
        return
    fi
    dev_run_key "${run}"
}

# Prints "PROCESS DIRECTORY" for each process of a development run (dev_processes, dev_run_directory).
dev_processes_with_directory() {
    local process
    for process in $(dev_processes); do
        echo "${process} $(dev_run_directory "${process}")"
    done
}

# Prints the ids of the processes of the development runs of one data directory. A process whose
# directory is not known is not in the list.
dev_processes_of() {
    local key process directory
    key="$(dev_run_key "$1")"
    while read -r process directory; do
        if [[ -n "${process}" && "${directory}" == "${key}" ]]; then
            echo "${process}"
        fi
    done < <(dev_processes_with_directory)
}
