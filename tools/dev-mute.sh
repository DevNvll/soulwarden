#!/usr/bin/env bash
# Mute a development run: set the master volume (m_masterVolume) of each account to 0 in the
# settings files of a development Proton prefix. tools/dev-run.sh calls it before each start, while
# no game of the prefix runs (the game would write the old value back).
# Usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX
#
# The settings of the user must not change. A check of a path and a later use of a path is never
# safe (code reviews of stages 1.1b and 3.3b-S5, finding F1 of each: a folder moved and a link put at
# its old path between the two steps). Thus tools/dev_mute.py uses no path text after its first open:
# it compares the device and inode of the open development prefix with the folders of the user, opens
# each folder and each settings file from the descriptor before it with no follow of a link, refuses
# a file with more than one link or the identity of a settings file of the user, and writes through
# the descriptor of the file. It writes nothing and says so on the error output when a check fails.
# Status 0 in each case: a start with no mute is not a fault. tools/check_dev_mute.py checks this
# with folders of a test, never with the prefix of the user.
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX" >&2
    exit 2
fi
exec python3 -B "$(dirname -- "${BASH_SOURCE[0]}")/dev_mute.py" "$1" "$2"
