#!/usr/bin/env bash
# Mute a development run: set the master volume (m_masterVolume) of each account to 0 in the
# settings files of a development Proton prefix. tools/dev-run.sh calls it before each start, while
# no game of the prefix runs (the game would write the old value back).
# Usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX
#
# The settings of the user must not change (code review of stage 3.3b-S2b, finding F1). A path
# text can name the prefix of the user through a link, a "/." at its end, or a savedata folder
# that is a link. Thus the script compares the resolved paths: it writes nothing when the
# development prefix, its settings folder, or a settings file resolves to the prefix of the user
# or to a path in it, and it says so on the error output. It writes only a file whose resolved
# path is in the resolved development prefix. Status 0 in each case: a start with no mute is not
# a fault. tools/check_dev_mute.py checks this with folders of a test, never with the prefix of
# the user.
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX" >&2
    exit 2
fi
development_prefix="$1"
user_prefix="$2"
# SOULWARDEN_DEV_SOUND=1 keeps the volumes of the prefix.
if [[ "${SOULWARDEN_DEV_SOUND:-0}" == "1" ]]; then
    exit 0
fi
settings_folder="pfx/drive_c/users/steamuser/AppData/Roaming/AGS/New World/savedata"

# The prefix of the user can be absent: -m resolves the parts that are there.
user_resolved="$(realpath -m -- "${user_prefix}")"
# True when the resolved path $1 is the folder $2 or a path in it.
inside() {
    [[ "$1" == "$2" || "$1" == "$2/"* ]]
}
refuse() {
    echo "dev-mute: $1 is the prefix of the user or a path in it (${user_resolved}): no settings file changed," \
        "the run has the volume of the game" >&2
    exit 0
}

if ! development_resolved="$(realpath -e -- "${development_prefix}" 2>/dev/null)"; then
    exit 0 # No prefix yet: Proton makes it at the first start, with the volume of the game.
fi
if inside "${development_resolved}" "${user_resolved}"; then
    refuse "the development prefix ${development_prefix}"
fi
settings_directory="${development_prefix%/}/${settings_folder}"
if ! directory_resolved="$(realpath -e -- "${settings_directory}" 2>/dev/null)"; then
    exit 0
fi
if inside "${directory_resolved}" "${user_resolved}"; then
    refuse "the settings folder of the development prefix"
fi
# Each file is checked before the first write: one file of the user stops all writes.
targets=()
for settings_file in "${settings_directory}"/usersettings_*.javsave; do
    [[ -f "${settings_file}" ]] || continue
    file_resolved="$(realpath -e -- "${settings_file}")"
    if inside "${file_resolved}" "${user_resolved}"; then
        refuse "the settings file ${settings_file##*/}"
    fi
    if ! inside "${file_resolved}" "${development_resolved}"; then
        echo "dev-mute: the settings file ${settings_file##*/} is outside the development prefix: it did not change" >&2
        continue
    fi
    targets+=("${file_resolved}")
done
for target in "${targets[@]}"; do
    sed -i -E 's/(field="m_masterVolume" value=")[0-9]+(")/\10\2/' "${target}"
done
