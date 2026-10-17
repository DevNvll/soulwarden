#!/usr/bin/env bash
# Releases of Soulwarden for the game of the user: a fixed copy of the launcher of the commit, the
# products of apps/*/dist, and the game data folder. The Steam library entry starts the current
# release. tools/release.py has the rules (tools/release.sh --help).
# Usage: tools/release.sh [install [--allow-dirty] | status | activate VERSION | rollback]
set -euo pipefail
exec python3 -B "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/release.py" "$@"
