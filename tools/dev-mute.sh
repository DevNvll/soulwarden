#!/usr/bin/env bash
# Mute a development run: set the master volume (m_masterVolume) of each account to 0 in the
# settings files of a development Proton prefix. tools/dev-run.sh calls it before each start, while
# no game of the prefix runs (the game would write the old value back).
# Usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX
#
# The settings of the user must not change. A check of a path and a later write by that path is
# never safe (code review of stage 1.1b, finding F1: a folder moved and a link put at its old path
# between the check and the write). Thus tools/dev_mute.py opens each folder and each settings
# file one time with no follow of a link, checks the open descriptor (its real path in the
# development prefix and not in the prefix of the user, one link, not a file of the user), and
# writes through the same descriptor. It writes nothing and says so on the error output when a
# path is the prefix of the user, a path in it, or a link. Status 0 in each case: a start with no
# mute is not a fault. tools/check_dev_mute.py checks this with folders of a test, never with the
# prefix of the user.
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX" >&2
    exit 2
fi
exec python3 -B "$(dirname -- "${BASH_SOURCE[0]}")/dev_mute.py" "$1" "$2"
