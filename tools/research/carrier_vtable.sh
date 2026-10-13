#!/usr/bin/env bash
# Research: read the live carrier vtable of the client while the game server session is open.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${repository_directory}/tools/dev-env.sh"
data_dir="${dev_data_dir}"
python_binary="${SOULWARDEN_PYTHON:-${HOME}/Games/soulwarden/venv/bin/python}"
SOULWARDEN_TRACE="6b3c250:16" "${repository_directory}/tools/scenarios/play.sh" "${1:-vt}" >/dev/null
for ((attempt = 0; attempt < 120; ++attempt)); do
    grep -q "carrier: connection established" "${data_dir}/logs/backend.log" && break
    sleep 0.25
done
sleep 1
transport="$(grep -E 'trace: 0x6B3C250' "${data_dir}/client/logs/hook.log" | head -1 | sed -E 's/.*rcx=([0-9a-f]+).*/\1/')"
carrier="$("${python_binary}" -I "${repository_directory}/tools/research/peek.py" "${transport}" 96 | sed -n 10p | awk '{print $2}')"
vtable="$("${python_binary}" -I "${repository_directory}/tools/research/peek.py" "${carrier}" 8 | head -1 | awk '{print $2}')"
echo "transport ${transport} carrier ${carrier} vtable ${vtable}"
"${python_binary}" -I "${repository_directory}/tools/research/peek.py" "${vtable}" 200
