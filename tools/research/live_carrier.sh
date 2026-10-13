#!/usr/bin/env bash
# Research: dump objects referenced by the live carrier thread object of the client.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${repository_directory}/tools/dev-env.sh"
data_dir="${dev_data_dir}"
python_binary="${SOULWARDEN_PYTHON:-${HOME}/Games/soulwarden/venv/bin/python}"
SOULWARDEN_TRACE="5DD4A90:8" "${repository_directory}/tools/scenarios/play.sh" "${1:-live}" >/dev/null
for ((attempt = 0; attempt < 120; ++attempt)); do
    grep -q "trace: 0x5DD4A90 #0" "${data_dir}/client/logs/hook.log" && break
    sleep 0.25
done
line="$(grep -E 'trace: 0x5DD4A90 #0' "${data_dir}/client/logs/hook.log" | head -1)"
thread="$(sed -E 's/.*rcx=([0-9a-f]+).*/\1/' <<<"${line}")"
connection="$(sed -E 's/.*rdx=([0-9a-f]+).*/\1/' <<<"${line}")"
echo "carrier thread ${thread}, thread connection ${connection}"
"${python_binary}" -I "${repository_directory}/tools/research/find_carrier.py" "${thread}" "${SOULWARDEN_SCAN_BYTES:-1024}"
echo "--- thread connection"
"${python_binary}" -I "${repository_directory}/tools/research/find_carrier.py" "${connection}" 512
