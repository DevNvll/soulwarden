#!/usr/bin/env python3
"""Print the script errors of the live client from the hook log.

The client writes no line to its game log for an error in a user interface script (Lua). A
script that stops with an error leaves its screen incomplete: an element with no tooltip, a
panel that does not show. The script error function of the client is at image RVA 0x4A65DA0;
its fourth argument is the text of the error with the call stack.

Run the game with the trace point `SOULWARDEN_TRACE=4A65DA0:512` (add it to the other trace points
with a comma). The tool reads the trace lines of the hook log and prints each different text
with its count. The trace has the first 400 calls of the function.

Usage: script_errors.py [HOOK_LOG]
Default: the hook log of the development data directory (tools/dev-env.sh), or SOULWARDEN_DATA_DIR.
Read the log before tools/dev-stop.sh: the hook log can be made again at the stop of a run.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_env  # noqa: E402

TRACE_POINT = "trace: 0x4A65DA0"
# The dump lines of one trace entry follow its first line.
ENTRY_LINES = 12


def main() -> int:
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1] in ("-h", "--help")):
        print(__doc__.strip())
        return 0
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else dev_env.data_dir() / "client/logs/hook.log"
    try:
        lines = path.read_text(errors="replace").splitlines()
    except OSError as error:
        sys.exit(f"script-errors: {error}")
    texts: dict[str, list[str]] = {}
    for index, line in enumerate(lines):
        if TRACE_POINT not in line:
            continue
        block = " ".join(lines[index:index + ENTRY_LINES])
        match = re.search(r"r9 ?-> ?([0-9a-f]+)", block)
        if not match:
            continue
        digits = match.group(1)
        data = bytes.fromhex(digits[:len(digits) // 2 * 2])
        text = data.split(b"\0")[0].decode("utf-8", "replace").strip()
        texts.setdefault(text, []).append(line.split()[0])
    if not texts:
        print(f"script-errors: no line of the trace point 0x4A65DA0 in {path.name} (run with SOULWARDEN_TRACE=4A65DA0:512)")
        return 1
    for text, stamps in texts.items():
        print(f"{len(stamps)} time(s), first at {stamps[0]}:")
        for part in text.splitlines():
            if part.strip():
                print(f"    {part.rstrip()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
