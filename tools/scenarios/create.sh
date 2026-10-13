#!/usr/bin/env bash
# Scenario: start the game, create a character in the client, and enter the world with it.
# The scenario uses the first empty row of the character list of the main menu, goes through
# the four steps of the creation (archetype, appearance, name, world), and waits until the
# client is in the world with the new character. All input goes through the harness.
# Usage: tools/scenarios/create.sh RUN_LABEL NAME
# NAME has 3 to 15 characters, no space, and 6 digits at most, and no character of any account
# has it.
# Status 0: the new character is in the world and the loading screen is gone. Status 1: a step
# failed (the message names it).
# The character stays in the database of the data directory (the development data directory,
# or SOULWARDEN_DATA_DIR). The scenario starts no game when SOULWARDEN_DATA_DIR is the data directory of
# the user: the database is then the player data of the user. SOULWARDEN_CREATE_LIVE=1 permits it.
# Optional variables:
#   SOULWARDEN_CREATE_ARCHETYPE=1..7   archetype button, counted by rows (default: the first one)
#   SOULWARDEN_CREATE_APPEARANCE='click 210 166;click 363 166;click 210 258;click 559 279'
#                                 harness commands for the appearance step, with ";" between
#                                 them (default: the appearance that the client shows)
#   SOULWARDEN_SCENARIO_SHOTS=1        a screenshot of each step: tools/shot RUN_LABEL-STEP
#   SOULWARDEN_CREATE_LIVE=1           permit a run on the data directory of the user
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
label="${1:-create}"
name="${2:-}"
archetype="${SOULWARDEN_CREATE_ARCHETYPE:-}"
hx="${repository_directory}/tools/hx"

# Positions in window coordinates (window of 1256x660).
character_row_x=170      # Rows of the character list: the first row, and the distance to
character_row_y=399      # the next row.
character_row_step=55
world_type_button='628 342'      # Panel "Standard" of the popup "Choose world type".
confirm_button='714 397'         # "Yes" of the popup "Create character".
archetype_x=138                  # Archetype buttons: 4 in the first row, 3 in the second.
archetype_y=171
archetype_step=72
continue_button='1010 589'       # "Continue" of the archetype, appearance, and name steps.
name_field='270 330'
world_continue_button='935 456'  # "Continue" of the world step.
# Seconds: from the load of the creation level to the intro film, the hold time of the key
# that skips the film, and from the skip to the buttons of the archetype step (a camera
# sequence plays first, and the buttons come when it stops).
intro_wait=15
skip_hold_ms=3000
archetype_wait=25
# Largest number of digits in a name: the client refuses a name with more and sends no request
# (name pattern of the client, protocol notes 2.2.2).
name_digits_limit=6
# Seconds from the first camera position of the client to a screen with no loading picture.
world_fade_wait=3

fail() {
    echo "scenario: $*" >&2
    exit 1
}

# Takes a screenshot of a step when SOULWARDEN_SCENARIO_SHOTS is 1.
shot() {
    if [[ "${SOULWARDEN_SCENARIO_SHOTS:-0}" == "1" ]]; then
        "${repository_directory}/tools/shot" "${label}-$1" >/dev/null || true
    fi
}

if [[ ! "${name}" =~ ^[A-Za-z][A-Za-z0-9\'.-]{2,14}$ ]]; then
    fail "give a character name with 3 to 15 letters or digits (usage: create.sh RUN_LABEL NAME)"
fi
name_digits="${name//[^0-9]/}"
if (( ${#name_digits} > name_digits_limit )); then
    fail "the name ${name} has ${#name_digits} digits: the client accepts ${name_digits_limit} digits at most"
fi
if [[ -n "${archetype}" && ! "${archetype}" =~ ^[1-7]$ ]]; then
    fail "SOULWARDEN_CREATE_ARCHETYPE must be a number from 1 to 7"
fi
if [[ "${data_dir%/}" == "${user_data_dir%/}" && "${SOULWARDEN_CREATE_LIVE:-0}" != "1" ]]; then
    fail "SOULWARDEN_DATA_DIR is the data directory of the user: the character would stay in the database of the user" \
        "(${data_dir}). Use a different data directory, or set SOULWARDEN_CREATE_LIVE=1 to permit this database."
fi

"${repository_directory}/tools/scenarios/menu.sh" "${label}" >/dev/null || fail "the game did not get to the main menu"
# The last line of this form is the character list that the menu shows.
characters="$(grep -F 'character(s)' "${backend_log}" | tail -1 | sed -n 's/.* has \([0-9]*\) character(s).*/\1/p' || true)"
[[ -n "${characters}" ]] || fail "the backend log has no character list of the account"
(( characters < 4 )) || fail "the account has ${characters} characters: the menu has no empty row"

# Main menu: the first empty row, then the two popups.
sleep 5
shot 1-menu
"${hx}" "click ${character_row_x} $((character_row_y + characters * character_row_step))" 'wait 2500' \
    "click ${world_type_button}" 'wait 2500' "click ${confirm_button}" >/dev/null
wait_for_game_log 'Loading level FTUE_V2_CC' 30 || fail "the creation level did not load (no click on the empty row?)"

# Intro film, then the camera sequence of the archetype step.
sleep "${intro_wait}"
"${hx}" "key 0x1B ${skip_hold_ms}" >/dev/null
sleep "${archetype_wait}"

# Step 1, archetype.
if [[ -n "${archetype}" ]]; then
    index=$((archetype - 1))
    "${hx}" "click $((archetype_x + index % 4 * archetype_step)) $((archetype_y + index / 4 * archetype_step))" \
        'wait 3000' >/dev/null
fi
shot 2-archetype
"${hx}" "click ${continue_button}" 'wait 6000' >/dev/null

# Step 2, appearance.
if [[ -n "${SOULWARDEN_CREATE_APPEARANCE:-}" ]]; then
    IFS=';' read -r -a appearance_commands <<<"${SOULWARDEN_CREATE_APPEARANCE}"
    for command in "${appearance_commands[@]}"; do
        "${hx}" "${command}" 'wait 2500' >/dev/null
    done
fi
shot 3-appearance
"${hx}" "click ${continue_button}" 'wait 5000' >/dev/null

# Step 3, name. The client asks the backend 2 seconds after the last key.
checks="$(log_count 'gateway.validate_character')"
"${hx}" "click ${name_field}" 'wait 300' "text ${name}" >/dev/null
wait_for_log_count 'gateway.validate_character' "${checks}" 20 \
    || fail "no name check request: the name step did not show (intro film not skipped?)"
"${hx}" 'wait 1000' >/dev/null
shot 4-name
if ! grep -F 'gateway.validate_character' "${backend_log}" | tail -1 | grep -qF -- '-> 200 '; then
    fail "the name ${name} is not available"
fi

# Step 4, world. The client loads the world list again, and its button is on 1 second later.
lists="$(log_count 'gateway.get_login_info')"
"${hx}" 'wait 1500' "click ${continue_button}" >/dev/null
wait_for_log_count 'gateway.get_login_info' "${lists}" 20 || fail "the world step did not show"
"${hx}" 'wait 4000' >/dev/null
shot 5-world
"${hx}" "click ${world_continue_button}" >/dev/null
wait_for_log_count 'gateway.create_character' 0 20 || fail "the client sent no create request"
if ! grep -F 'gateway.create_character' "${backend_log}" | tail -1 | grep -qF -- '-> 200 '; then
    grep -F 'gateway.create_character' "${backend_log}" | tail -1 \
        | sed -n 's/.* -> \([0-9]*\) .*/scenario: the reply to the create request has status \1/p' >&2 || true
    grep -F 'gateway: no character' "${backend_log}" | tail -1 | sed 's/^[^ ]* /scenario: backend log: /' >&2 || true
    fail "the backend did not create the character ${name}"
fi

# The client plays an end sequence and then logs in with the new character.
wait_for_log_count 'gateway.queue_login' 0 40 || fail "the client did not log in with the new character"
wait_for_game_log 'to new state InGame' 90 || fail "the client did not get to the world (no InGame in the game log)"
# The loading screen stays for some seconds after InGame. The client sends the position of its
# camera only when the loading screen ends: the backend log then has the first input line.
wait_for_log 'rep: input [0-9]* camera ' 90 2>/dev/null \
    || fail "the loading screen did not end (no camera position from the client)"
sleep "${world_fade_wait}"
shot 6-in-world
echo "scenario: character ${name} is in the world"
