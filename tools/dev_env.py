"""Common values of the development tools for a Python tool.

tools/dev-env.sh is the only place of each default. This module asks that file, so that a
Python tool and a shell tool use the same data directory.

A tool that imports this module must set `sys.dont_write_bytecode = True` first: a bytecode
cache in the repository has paths of the computer.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

DEV_ENV = Path(__file__).resolve().parent / "dev-env.sh"


def data_dir() -> Path:
    """@return The data directory of the development tools: SOULWARDEN_DATA_DIR, or the default of dev-env.sh."""
    value = os.environ.get("SOULWARDEN_DATA_DIR")
    if value:
        return Path(value)
    result = subprocess.run(["bash", "-c", 'source "$1" && printf %s "${dev_data_dir}"', "bash", str(DEV_ENV)],
                            capture_output=True, text=True, check=True)
    return Path(result.stdout)
