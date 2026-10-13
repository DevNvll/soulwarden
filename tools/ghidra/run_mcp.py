#!/usr/bin/env python3
"""Start pyghidra-mcp on the analyzed client project.

The stock server decompiles every function at startup to build a semantic code index.
The client image has 423,000 functions, so this launcher turns that index off. All other
tools (decompile, cross-references, symbols, strings, bytes, disassembly) stay available.
"""

import sys

import pyghidra_mcp.indexing_mixin as indexing


def _skip_code_index(self, program_info) -> None:
    return None


indexing.IndexingMixin._init_chroma_code_collection_for_program = _skip_code_index

from pyghidra_mcp.server import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
