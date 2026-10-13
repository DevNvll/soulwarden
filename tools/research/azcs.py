#!/usr/bin/env python3
"""Decode an engine compressed stream ("AZCS" header) to its plain content.

Game data files that are stored without pak compression can still have this header:
`"AZCS" compressor:u32 size:u64` (big-endian), then the compressor data. Compressor
0x73887D3A is zlib: `seekPoints:u32`, the zlib stream, then the seek point table.

Usage: azcs.py INPUT [OUTPUT]     (without OUTPUT: write to standard output)
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

ZLIB = 0x73887D3A


def decode(data: bytes) -> bytes:
    """@return The plain content. Data without the header is returned as it is."""
    if data[:4] != b"AZCS":
        return data
    compressor, size = struct.unpack(">IQ", data[4:16])
    if compressor != ZLIB:
        raise ValueError(f"compressor 0x{compressor:08X} is not supported")
    plain = zlib.decompressobj().decompress(data[20:])
    if len(plain) != size:
        raise ValueError(f"size {len(plain)} is not the header size {size}")
    return plain


def main() -> None:
    plain = decode(Path(sys.argv[1]).read_bytes())
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_bytes(plain)
    else:
        sys.stdout.buffer.write(plain)


if __name__ == "__main__":
    main()
