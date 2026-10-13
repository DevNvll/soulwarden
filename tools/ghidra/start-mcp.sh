#!/usr/bin/env bash
# Start the Ghidra MCP server for the analyzed New World client project.
set -euo pipefail

tool_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python_binary="${SOULWARDEN_PYTHON:-${HOME}/Games/soulwarden/venv/bin/python}"
project_file="${SOULWARDEN_GHIDRA_PROJECT:-${HOME}/Games/soulwarden/ghidra/soulwarden.gpr}"
port="${SOULWARDEN_GHIDRA_MCP_PORT:-8765}"

export GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-/opt/ghidra}"
cd "$(dirname -- "${project_file}")"
exec "${python_binary}" "${tool_directory}/run_mcp.py" \
    --transport streamable-http --host 127.0.0.1 --port "${port}" \
    --project-path "${project_file}" "$@"
