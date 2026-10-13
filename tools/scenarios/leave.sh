#!/usr/bin/env bash
# Scenario: leave the world to the main menu with the Game Menu of the client. The game must
# be in the world (tools/scenarios/play.sh or create.sh started it, and the backend log has a
# line "rep: input N camera"). This script starts no game.
# Usage: tools/scenarios/leave.sh
# Status 0: the backend log has the end of the session of the client, and the client asked for
# the character list again (the main menu is back). Status 1: the client did not leave the
# world in three tries, or the main menu did not come back.
# The Escape key opens and closes the Game Menu, and the first Escape of a world session does
# nothing. The script cannot see if the menu is open. Thus each try presses Escape one time
# and then clicks the gear, "Exit to Main Menu", and "Yes": when the menu was not open, the
# clicks have no effect, and the next try has the other state of the menu.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
hx="${repository_directory}/tools/hx"

# Positions in window coordinates (window of 1256x660).
gear_button='972 23'          # Gear in the bar that the Escape key shows.
exit_button='474 515'         # "Exit to Main Menu" of the Game Menu.
confirm_button='714 397'      # "Yes" of the popup "Exit game".

ends="$(log_count 'rep: session ended')"
lists="$(log_count 'gateway.get_login_info')"
for ((try = 1; try <= 3; ++try)); do
    "${hx}" 'key 0x1B' 'wait 1200' "click ${gear_button}" 'wait 1200' "click ${exit_button}" 'wait 1200' \
        "click ${confirm_button}" >/dev/null
    if wait_for_log_count 'rep: session ended' "${ends}" 6; then
        # The client loads the main menu and asks for the character list again (12 s to 15 s
        # after the end of the session). The Play button is on some seconds later.
        if ! wait_for_log_count 'gateway.get_login_info' "${lists}" 60; then
            echo "scenario: the client left the world, but the main menu did not come back" >&2
            exit 1
        fi
        sleep 5
        echo "scenario: main menu after the world"
        exit 0
    fi
done
echo "scenario: the client did not leave the world" >&2
exit 1
