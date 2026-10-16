#!/usr/bin/env bash
# Common values of the development scripts. A script reads this file with "source".
#
# A development run (tools/dev-run.sh, tools/scenarios) and a game that the user starts
# (Steam library entry, linux-compat/run-soulwarden.sh) are separate:
# - A development run has its own Proton prefix. The launcher stops all processes of its
#   prefix before a start, thus two starts with one prefix stop each other.
# - Each process of a development run has the mark SOULWARDEN_DEV_RUN=1 in its environment.
#   tools/dev-stop.sh stops only the processes that have the mark.

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
