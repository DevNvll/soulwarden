#!/usr/bin/env bash
# Scenario: start the game and go to the main menu. The menu shows the characters of the
# account, or four "Create Character" rows when the account has none.
# Usage: tools/scenarios/menu.sh RUN_LABEL
# A persona that has no settings file in the client gets five first-run screens before the
# menu. The scenario passes them: it sets the narration to Off, keeps the other defaults, and
# presses "I Accept" on the code of conduct screen of the client. Nothing leaves the computer.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
label="${1:-menu}"
# The client writes this line with the file name when the persona has no settings file. A
# line with an empty file name is in the game log of each run.
first_run_line="Failed to find Options file '@savedata@"
# Buttons of the first-run screens in window coordinates (window of 1256x660): narration Off,
# then the button that goes to the next screen for narration, seizure warning, basic settings,
# brightness, and code of conduct. A click at one of these positions does no harm on a
# different screen of the sequence. Narration Off is there two times: the title screen takes
# the first click when it did not take the key.
first_run_clicks=('573 378' '573 378' '629 562' '946 575' '720 546' '812 586' '629 576')

"${repository_directory}/tools/dev-run.sh" "${label}" >/dev/null
wait_for_log 'gateway.omni_credentials' 120
# The title screen ignores a key that comes before it is ready. Press the key again until the
# client asks for the character list.
sleep 10
for ((pass = 0; pass < 12; ++pass)); do
    "${repository_directory}/tools/hx" 'key 0x20' >/dev/null
    if wait_for_log 'gateway.get_login_info' 5 2>/dev/null; then
        break
    fi
    if grep -qF "${first_run_line}" "${game_log}" 2>/dev/null; then
        for position in "${first_run_clicks[@]}"; do
            "${repository_directory}/tools/hx" "click ${position}" >/dev/null
            if wait_for_log 'gateway.get_login_info' 3 2>/dev/null; then
                break
            fi
        done
        # The client asks for the character list some seconds after the last screen.
        if wait_for_log 'gateway.get_login_info' 10 2>/dev/null; then
            break
        fi
    fi
done
wait_for_log 'gateway.get_login_info' 1
echo "scenario: main menu"
