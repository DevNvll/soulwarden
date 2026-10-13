#!/usr/bin/env bash
# Scenario: start the game, pass the title screen, and press Play for the first character.
# An account with no character gets the seed character Soulwarden (SOULWARDEN_SEED_CHARACTER). When a
# different account has that name, the account gets no character and the scenario stops with
# status 1: set SOULWARDEN_SEED_CHARACTER to a name that is not in use.
# Usage: tools/scenarios/play.sh RUN_LABEL
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
label="${1:-play}"
# Seconds from the Play button to the login queue request of the client.
play_limit=30

SOULWARDEN_SEED_CHARACTER="${SOULWARDEN_SEED_CHARACTER:-Soulwarden}" "${repository_directory}/tools/scenarios/menu.sh" "${label}" >/dev/null
# The last line of this form is the character list that the menu shows.
if grep -F 'character(s)' "${backend_log}" | tail -1 | grep -qF 'has 0 character(s)'; then
    echo "scenario: the account has no character. The Play button of the menu is off." >&2
    grep -F 'omni: no seed character' "${backend_log}" | tail -1 | sed 's/^[^ ]* /scenario: backend log: /' >&2
    exit 1
fi
sleep 5
"${repository_directory}/tools/hx" 'click 1089 572' >/dev/null
wait_for_log 'gateway.queue_login' "${play_limit}"
echo "scenario: Play pressed"
