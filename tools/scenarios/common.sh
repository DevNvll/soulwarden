#!/usr/bin/env bash
# Common parts of the scenario scripts. A scenario reads this file with "source".
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${repository_directory}/tools/dev-env.sh"
data_dir="${dev_data_dir}"
backend_log="${data_dir}/logs/backend.log"
# A scenario is a development run: its game log is in the Proton prefix of development runs.
game_log="${dev_compat_dir}/pfx/drive_c/users/steamuser/AppData/Local/AGS/New World/Game.log"

# Waits until the backend log has a line with the pattern. Status 1 after the limit (seconds).
wait_for_log() {
    local pattern="$1" limit="$2" attempt
    for ((attempt = 0; attempt < limit * 2; ++attempt)); do
        if grep -q "${pattern}" "${backend_log}" 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    echo "scenario: no '${pattern}' in the backend log after ${limit} s" >&2
    return 1
}

# Prints the number of lines of the backend log that have the text.
log_count() {
    grep -cF "$1" "${backend_log}" 2>/dev/null || true
}

# Waits until the backend log has more than COUNT lines with the text. Status 1 after the
# limit (seconds).
wait_for_log_count() {
    local text="$1" count="$2" limit="$3" attempt
    for ((attempt = 0; attempt < limit * 2; ++attempt)); do
        if (( $(log_count "${text}") > count )); then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

# Waits until the game log has a line with the text. Status 1 after the limit (seconds).
wait_for_game_log() {
    local text="$1" limit="$2" attempt
    for ((attempt = 0; attempt < limit * 2; ++attempt)); do
        if grep -qaF "${text}" "${game_log}" 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}
