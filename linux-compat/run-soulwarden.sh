#!/usr/bin/env bash
# Start the local backend and the New World client in an isolated network namespace.
# The namespace has loopback only. The game cannot reach a public service.
set -euo pipefail

launcher_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_dir="$(cd -- "${launcher_dir}/.." && pwd)"

game_dir="${SOULWARDEN_GAME_DIR:-${HOME}/Games/soulwarden/game}"
compat_dir="${SOULWARDEN_COMPAT_DIR:-${HOME}/Games/soulwarden/proton/1063730}"
steam_dir="${SOULWARDEN_STEAM_DIR:-${HOME}/.local/share/Steam}"
proton_dir="${SOULWARDEN_PROTON_DIR:-${steam_dir}/steamapps/common/Proton - Experimental}"
original_dir="${SOULWARDEN_ORIGINAL_DIR:-${HOME}/Games/soulwarden/original}"
data_dir="${SOULWARDEN_DATA_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/soulwarden}"
# Tables that a tool makes from the game copy (tools/gamedata). They are not player data: a
# private data directory of a test run uses the same tables.
gamedata_dir="${SOULWARDEN_GAMEDATA_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/soulwarden/gamedata}"
client_data_dir="${data_dir}/client"
hook_dll="${SOULWARDEN_HOOK_DLL:-${repository_dir}/apps/game-hook/dist/steam_api64.dll}"
backend_executable="${SOULWARDEN_BACKEND_EXE:-${repository_dir}/apps/backend/dist/soulwarden-backend}"
backend_http_host="${SOULWARDEN_BACKEND_HTTP_HOST:-127.0.0.1}"
backend_http_port="${SOULWARDEN_BACKEND_HTTP_PORT:-32200}"
backend_health_url="http://${backend_http_host}:${backend_http_port}/healthz"

if [[ ! -x "${proton_dir}/proton" ]]; then
    echo "Soulwarden: Proton was not found at ${proton_dir}." >&2
    exit 1
fi
if [[ ! -f "${game_dir}/Bin64/NewWorld.exe" ]]; then
    echo "Soulwarden: NewWorld.exe was not found in ${game_dir}/Bin64." >&2
    exit 1
fi
if [[ ! -f "${hook_dll}" ]]; then
    echo "Soulwarden: the hook DLL was not found at ${hook_dll}. Run tools/build-soulwarden-game-hook.sh." >&2
    exit 1
fi

# Wine creates sockets in the wineserver process. A wineserver that remains from an earlier
# run is in the earlier network namespace, where no backend listens. Stop it and its helper
# processes before the new namespace starts. Only processes of this prefix are stopped.
prefix_wineservers() {
    # All processes of this prefix count: Wine helper processes keep display connections open
    # after their server stops.
    local process prefix
    for process in $(pgrep -u "$(id -u)" 2>/dev/null || true); do
        prefix="$({ tr '\0' '\n' <"/proc/${process}/environ"; } 2>/dev/null | sed -n 's/^WINEPREFIX=//p' | head -1)"
        if [[ -n "${prefix}" && "${prefix%/}" == "${compat_dir}/pfx" ]]; then
            echo "${process}"
        fi
    done
}

stop_stale_wineserver() {
    local process
    for process in $(prefix_wineservers); do
        kill "${process}" >/dev/null 2>&1 || true
    done
    for ((attempt = 0; attempt < 100; ++attempt)); do
        [[ -z "$(prefix_wineservers)" ]] && return 0
        sleep 0.1
    done
    for process in $(prefix_wineservers); do
        kill -9 "${process}" >/dev/null 2>&1 || true
    done
    sleep 0.3
    [[ -z "$(prefix_wineservers)" ]]
}

if [[ "${SOULWARDEN_NET_ISOLATED:-0}" != "1" ]]; then
    if ! stop_stale_wineserver; then
        echo "Soulwarden: an earlier Wine server for this prefix did not stop." >&2
        exit 1
    fi
    for tool in unshare ip flock curl; do
        if ! command -v "${tool}" >/dev/null 2>&1; then
            echo "Soulwarden: ${tool} was not found. The game cannot start safely." >&2
            exit 1
        fi
    done
    exec unshare --user --map-root-user --net bash -c '
        set -euo pipefail
        ip link set lo up
        export SOULWARDEN_NET_ISOLATED=1
        exec "$@"
    ' bash "$0" "$@"
fi

mkdir -p "${compat_dir}" "${original_dir}" "${client_data_dir}/logs" "${data_dir}/logs"

exec 9>"${data_dir}/run.lock"
if ! flock -n 9; then
    echo "Soulwarden: another game process uses this data directory." >&2
    exit 1
fi

if [[ ! -f "${original_dir}/steam_api64.dll" ]]; then
    install -m 644 "${game_dir}/Bin64/steam_api64.dll" "${original_dir}/steam_api64.dll"
fi
install -m 644 "${hook_dll}" "${game_dir}/Bin64/steam_api64.dll"

windows_path() {
    printf 'Z:%s' "${1//\//\\}"
}

# Prints "Steam id<TAB>persona name" of the account that logged in to Steam last on this
# computer. The source is config/loginusers.vdf of the Steam installation: the block with
# MostRecent = 1, else the block with the largest Timestamp. Only this file is read, and only
# these two values leave this function. Key names are compared without case.
steam_account() {
    local login_file="${steam_dir}/config/loginusers.vdf"
    [[ -r "${login_file}" ]] || return 0
    awk '
        { text = text $0 "\n" }
        END {
            depth = 0; pending = 0; total = length(text)
            for (i = 1; i <= total; ++i) {
                c = substr(text, i, 1)
                if (c == "{") { ++depth; pending = 0; continue }
                if (c == "}") { --depth; pending = 0; continue }
                if (c != "\"") continue
                value = ""
                for (++i; i <= total; ++i) {
                    c = substr(text, i, 1)
                    if (c == "\\") { c = substr(text, ++i, 1); if (c == "n" || c == "t") c = " " }
                    else if (c == "\"") break
                    value = value c
                }
                if (depth == 1) { id = value; order[++count] = id; continue }
                if (depth != 2) continue
                if (!pending) { key = tolower(value); pending = 1; continue }
                field[id, key] = value; pending = 0
            }
            best = ""
            for (n = 1; n <= count; ++n) {
                id = order[n]
                if (id !~ /^[0-9]+$/ || length(id) != 17) continue
                if (field[id, "mostrecent"] == "1") { best = id; break }
                if (best == "" || field[id, "timestamp"] + 0 > field[best, "timestamp"] + 0) best = id
            }
            if (best != "") printf "%s\t%s\n", best, field[best, "personaname"]
        }
    ' "${login_file}" 2>/dev/null || true
}

# A Steam id of an individual account in the public universe: 17 decimal digits, and the high
# 32 bits are 0x01100001.
is_steam_id() {
    [[ "$1" =~ ^[0-9]{17}$ ]] && (( (10#$1 >> 32) == 0x01100001 ))
}

# Sets SOULWARDEN_STEAM_ID and SOULWARDEN_PERSONA_NAME for the game hook. The hook gives them to the
# game and to the backend as the player identity. No Steam credential is read.
#   SOULWARDEN_STEAM_ID not set   account of the Steam installation (SOULWARDEN_STEAM_DIR)
#   SOULWARDEN_STEAM_ID=<id>      that Steam id, with SOULWARDEN_PERSONA_NAME as its name
#   SOULWARDEN_STEAM_ID=local     fallback identity of the hook
# With no Steam account on this computer the two variables stay unset, and the hook uses its
# fallback identity. Messages show only the last 4 digits of a Steam id.
resolve_steam_identity() {
    local steam_id="${SOULWARDEN_STEAM_ID:-}" persona_name="${SOULWARDEN_PERSONA_NAME:-}" account="" source="SOULWARDEN_STEAM_ID"
    unset SOULWARDEN_STEAM_ID SOULWARDEN_PERSONA_NAME
    if [[ "${steam_id}" == "local" ]]; then
        echo "Soulwarden: SOULWARDEN_STEAM_ID=local. The game uses the fallback identity." >&2
        return 0
    fi
    if [[ -z "${steam_id}" ]]; then
        source="Steam installation"
        account="$(steam_account)"
        steam_id="${account%%$'\t'*}"
        if [[ -z "${persona_name}" && "${account}" == *$'\t'* ]]; then
            persona_name="${account#*$'\t'}"
        fi
        if ! is_steam_id "${steam_id}"; then
            echo "Soulwarden: no Steam account was found in ${steam_dir}. The game uses the fallback identity." >&2
            return 0
        fi
    elif ! is_steam_id "${steam_id}"; then
        echo "Soulwarden: SOULWARDEN_STEAM_ID must be the Steam id of an individual account (17 digits) or \"local\"." >&2
        exit 1
    fi
    export SOULWARDEN_STEAM_ID="${steam_id}"
    export SOULWARDEN_PERSONA_NAME="${persona_name:-Soulwarden}"
    echo "Soulwarden: Steam id ...${steam_id: -4} (${source})." >&2
}

backend_process=""
cleanup() {
    if [[ -n "${backend_process}" ]] && kill -0 "${backend_process}" >/dev/null 2>&1; then
        curl --silent --max-time 3 --request POST \
            "http://${backend_http_host}:${backend_http_port}/v1/shutdown" >/dev/null 2>&1 || true
        for ((attempt = 0; attempt < 50; ++attempt)); do
            kill -0 "${backend_process}" >/dev/null 2>&1 || break
            sleep 0.1
        done
        kill "${backend_process}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

# The identity comes before the backend: the admin interface of the backend is for the account
# of the game of this start.
resolve_steam_identity

# The tool window of the game hook (key F8) sends its commands to the admin interface of the
# backend. SOULWARDEN_TOOL_WINDOW=0 turns the two off. The token of the admin interface is a new
# random text for each start: only the backend and the game of this start get it, in their
# environments. It is not an option of a command line, which other users of the computer can
# read, and no log has it.
tool_window="${SOULWARDEN_TOOL_WINDOW:-1}"
admin_token=""
if [[ "${tool_window}" != "0" ]]; then
    admin_token="$(head -c 24 /dev/urandom | od -An -v -tx1 | tr -d ' \n')"
    if [[ "${#admin_token}" != 48 ]]; then
        echo "Soulwarden: no random token for the admin interface. The tool window gets no data." >&2
        admin_token=""
    fi
fi

if [[ "${SOULWARDEN_NO_BACKEND:-0}" != "1" ]]; then
    if [[ ! -x "${backend_executable}" ]]; then
        echo "Soulwarden: the backend was not found at ${backend_executable}. Run tools/build-soulwarden-backend.sh." >&2
        exit 1
    fi
    backend_arguments=(--data-dir "${data_dir}" --gamedata-dir "${gamedata_dir}"
        --http-host "${backend_http_host}" --http-port "${backend_http_port}")
    if [[ -n "${SOULWARDEN_SEED_CHARACTER:-}" ]]; then
        backend_arguments+=(--seed-character "${SOULWARDEN_SEED_CHARACTER}")
    fi
    if [[ -n "${SOULWARDEN_REP_ADDRESS:-}" ]]; then
        backend_arguments+=(--rep-address "${SOULWARDEN_REP_ADDRESS}")
    fi
    if [[ "${SOULWARDEN_NO_REP_LISTENER:-0}" == "1" ]]; then
        backend_arguments+=(--no-rep-listener)
    fi
    if [[ "${SOULWARDEN_CAPTURE:-0}" == "1" ]]; then
        backend_arguments+=(--capture)
    fi
    # Each session observes the characters of other sessions near its character, and its
    # client gets them as remote players. Off by default: a client gets only its own character.
    if [[ "${SOULWARDEN_OBSERVE_PLAYERS:-0}" == "1" ]]; then
        backend_arguments+=(--observe-players)
    fi
    # The melee contact report of the client: the backend gives the client one remote
    # configuration document, and the client then reports each melee contact that it finds
    # in its input report. SOULWARDEN_MELEE_REPORT=0 turns it off: the client gets no document,
    # and a melee attack of a player has no effect.
    if [[ "${SOULWARDEN_MELEE_REPORT:-1}" == "0" ]]; then
        backend_arguments+=(--no-melee-report)
    fi
    # The behavior of the creatures: a creature gets a target, goes to it, and attacks.
    # SOULWARDEN_CREATURE_BEHAVIOR=0 turns it off for a start: the creatures stand at their
    # spawn points.
    if [[ "${SOULWARDEN_CREATURE_BEHAVIOR:-1}" == "0" ]]; then
        backend_arguments+=(--no-creature-behavior)
    fi
    # Development commands for the world (tools/world). They are diagnostics: only a run with
    # the harness gate on gets them, and the backend refuses the option without the gate.
    if [[ "${SOULWARDEN_DEV_COMMANDS:-0}" == "1" ]]; then
        if [[ "${SOULWARDEN_HARNESS:-0}" == "1" ]]; then
            backend_arguments+=(--dev-commands)
        else
            echo "Soulwarden: SOULWARDEN_DEV_COMMANDS has no effect without SOULWARDEN_HARNESS=1." >&2
        fi
    fi
    # Research switch of the game server for live checks of the client (for example
    # "kick=1" or "registration-result=-160 kick-delay=5": one backend option for each word).
    # It is not product behavior: only a run with the harness gate on gets it.
    if [[ -n "${SOULWARDEN_RESEARCH_SESSION:-}" ]]; then
        if [[ "${SOULWARDEN_HARNESS:-0}" == "1" ]]; then
            for research_option in ${SOULWARDEN_RESEARCH_SESSION}; do
                backend_arguments+=(--research-session "${research_option}")
            done
        else
            echo "Soulwarden: SOULWARDEN_RESEARCH_SESSION has no effect without SOULWARDEN_HARNESS=1." >&2
        fi
    fi
    SOULWARDEN_ADMIN_TOKEN="${admin_token}" SOULWARDEN_ADMIN_STEAM_ID="${SOULWARDEN_STEAM_ID:-}" \
        "${backend_executable}" "${backend_arguments[@]}" \
        >>"${data_dir}/logs/backend-launch.log" 2>&1 9>&- &
    backend_process=$!
    ready=0
    for ((attempt = 0; attempt < 100; ++attempt)); do
        if curl --silent --fail --max-time 1 "${backend_health_url}" >/dev/null 2>&1; then
            ready=1
            break
        fi
        if ! kill -0 "${backend_process}" >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
    if [[ "${ready}" != "1" ]]; then
        echo "Soulwarden: the backend is not ready at ${backend_health_url}." >&2
        tail -20 "${data_dir}/logs/backend-launch.log" >&2 || true
        exit 1
    fi
fi

export STEAM_COMPAT_APP_ID=1063730
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${steam_dir}"
export STEAM_COMPAT_DATA_PATH="${compat_dir}"
export STEAM_COMPAT_INSTALL_PATH="${game_dir}"
export SteamAppId=1063730
export SteamGameId=1063730
# winebrowser is off: a link in the game must not open a page in the browser of the user. The
# browser is outside the network namespace, so that page would come from a public service.
export WINEDLLOVERRIDES="steam_api64=n;winebrowser.exe=d${SOULWARDEN_EXTRA_DLL_OVERRIDES:+;${SOULWARDEN_EXTRA_DLL_OVERRIDES}}"
export SOULWARDEN_DATA_DIR="$(windows_path "${client_data_dir}")"
export SOULWARDEN_BACKEND_HTTP_HOST="${backend_http_host}"
export SOULWARDEN_BACKEND_HTTP_PORT="${backend_http_port}"
export SOULWARDEN_HARNESS="${SOULWARDEN_HARNESS:-0}"
export SOULWARDEN_TRACE="${SOULWARDEN_TRACE:-}"
export SOULWARDEN_PROBE="${SOULWARDEN_PROBE:-}"
export SOULWARDEN_TOOL_WINDOW="${tool_window}"
export SOULWARDEN_ADMIN_TOKEN="${admin_token}"
if [[ -n "${SOULWARDEN_TOOL_WINDOW_KEY:-}" ]]; then
    export SOULWARDEN_TOOL_WINDOW_KEY
fi
# A test of a start in which the tool window cannot start. It is diagnostics: the hook reads
# it only with the harness gate.
if [[ -n "${SOULWARDEN_TOOL_WINDOW_FAULT:-}" && "${SOULWARDEN_HARNESS}" == "1" ]]; then
    export SOULWARDEN_TOOL_WINDOW_FAULT
else
    unset SOULWARDEN_TOOL_WINDOW_FAULT
fi
# The position report of the game hook: the client sends the exact position of its character
# in each input report. SOULWARDEN_EXACT_POSITION=0 turns it off: the hook then does not change the
# client, and the backend estimates the position from the camera.
export SOULWARDEN_EXACT_POSITION="${SOULWARDEN_EXACT_POSITION:-1}"
# A test of a start in which the hook finds client code that is not the code of the supported
# build. It is diagnostics: the hook reads it only with the harness gate.
if [[ -n "${SOULWARDEN_EXACT_POSITION_FAULT:-}" && "${SOULWARDEN_HARNESS}" == "1" ]]; then
    export SOULWARDEN_EXACT_POSITION_FAULT
else
    unset SOULWARDEN_EXACT_POSITION_FAULT
fi
export PROTON_LOG_DIR="${client_data_dir}/logs"
if [[ -n "${SOULWARDEN_WINEDEBUG:-}" ]]; then
    export PROTON_LOG=1
    export WINEDEBUG="${SOULWARDEN_WINEDEBUG}"
fi

cd "${game_dir}/Bin64"
set +e
"${proton_dir}/proton" run "${game_dir}/Bin64/NewWorld.exe" "$@" 9>&-
game_status=$?
set -e
exit "${game_status}"
