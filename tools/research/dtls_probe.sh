#!/usr/bin/env bash
# Research probe: put an OpenSSL DTLS server on the game server port to see if the client
# completes the handshake with a local certificate, and record its first clear records.
# Usage: tools/research/dtls_probe.sh RUN_LABEL
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${repository_directory}/tools/dev-env.sh"
data_dir="${dev_data_dir}"
work_dir="${SOULWARDEN_WORK_DIR:-${HOME}/Games/soulwarden/work}/dtls"
label="${1:-dtls}"
backend_log="${data_dir}/logs/backend.log"

wait_for_log() {
    for ((attempt = 0; attempt < $2 * 2; ++attempt)); do
        grep -q "$1" "${backend_log}" 2>/dev/null && return 0
        sleep 0.5
    done
    echo "probe: no '$1' in the backend log" >&2
    return 1
}

SOULWARDEN_NO_REP_LISTENER=1 "${repository_directory}/tools/dev-run.sh" "${label}" >/dev/null
wait_for_log 'backend: ready' 60
backend_pid="$(pgrep -x soulwarden-backend | head -1)"
(tail -f /dev/null | nsenter -t "${backend_pid}" -U -n --preserve-credentials \
    openssl s_server -dtls1_2 -accept 127.0.0.1:32210 -cert "${work_dir}/cert.pem" -key "${work_dir}/key.pem" \
    -cipher ECDHE-RSA-AES256-GCM-SHA384 -msg -state -debug >"${work_dir}/s_server-${label}.log" 2>&1) &
wait_for_log 'gateway.omni_credentials' 120
sleep 14
"${repository_directory}/tools/hx" 'key 0x20' >/dev/null
wait_for_log 'gateway.get_login_info' 60
sleep 5
"${repository_directory}/tools/hx" 'click 1089 572' >/dev/null
echo "probe: Play pressed"
