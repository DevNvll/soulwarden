# Analysis of the client

Read this document before static analysis (Ghidra, the image queries) or live analysis (trace points,
probes, memory reads) of the client. The results go into the protocol notes.

## Start here

| Task | Start here |
| --- | --- |
| Static analysis | `tools/nw` (image queries), `tools/gh` (Ghidra MCP), `tools/ht` (message types) |
| Live analysis | `SOULWARDEN_TRACE`, `SOULWARDEN_PROBE`, `tools/hx`, `tools/shot`, `tools/research/player_position.py` |
| A screen of the client that is not complete (script errors) | `tools/research/script_errors.py`, protocol notes 4.10 |

## Commands

```sh
tools/ghidra/start-mcp.sh &          # Ghidra MCP server on 127.0.0.1:8765 (needs ~1 minute)
```

- `tools/research/script_errors.py` prints the errors of the user interface scripts of the
  client (run the game with `SOULWARDEN_TRACE=4A65DA0:512`; the game log has no line for such an
  error). Use it when a screen of the client is not complete after a new state.
- `SOULWARDEN_TRACE="RVA[:bytes][s],..."` logs the arguments of a function at entry (`s` adds the
  return addresses on the stack; the first 400 calls of each point). `SOULWARDEN_PROBE="RVA[:bytes],..."`
  logs all registers at any instruction start (the first 3 hits, then one of each 120).
  Output: `SOULWARDEN_DATA_DIR/client/logs/hook.log` (a development run:
  `~/Games/soulwarden/work/dev-data/client/logs/hook.log`).

## Traps

- One run with `SOULWARDEN_PROBE=2A2480D` (together with the trace points `6769980` and
  `3587E40`) ended 30 s after its start with a crash file of the client, before the main
  menu. The same run with no probe (trace points `6769980`, `486E170`, `3587E40`) was
  correct. The cause is not known: do not use a probe in the code of the input entry
  (`2A236C0` to `2A2493C`) without a check of that start.
- An error in a user interface script of the client gives no line in the game log, and the
  screen is then not complete with no other sign. Example of stage 1.3: with no level of the
  player (progression state, slot 70), each item cell of the bag was a wide white cell with
  no tooltip and no action menu, and the inventory screen had no equipment panel. The cause
  was one compare of a value that the client did not have. When a new state shows on the
  screen but a part of the screen is not correct, run with `SOULWARDEN_TRACE=4A65DA0:512` and
  read `tools/research/script_errors.py`: the call stack names the script function. A state
  that is not there yet can be necessary for the screen of a different system.
- Ghidra does not accept a project path with a folder that starts with a dot.
- The decompiler shows some field names as addresses, and some state types register fields
  inline. `tools/st` handles both; check its output against a live trace of the field readers
  when a state is not applied. The decompiler also omits arguments of some constructor
  calls: before 2026-10-13 the tool then gave a wrong group for a field, with no warning
  (27 of the 130 state types). The tool now reads the raw code of the constructor
  (`tools/state_disassembly.py`) and refuses a layout that it cannot prove (the types 41 and
  4896). The audit of all state types is `~/Games/soulwarden/work/research/state-layout-audit.md`.
- The main thread of the client can stay in an endless loop with the loading screen on.
  `gdb -p PID -batch -ex 'info registers rip'` gives the address (subtract 0x140000000).
- A watch of an access violation with gdb (stage QG9c): Proton gives the client its own SIGSEGV for each page guard. Use `handle all
  nostop noprint pass` and then `handle SIGSEGV stop print pass`, and continue (`c`) at each stop that is not in the code range of the
  fault. Only read: the watch writes nothing to the memory of the client. A register of the faulting function can hold a pointer that is
  not the object of the fault (0x4871FDA: rsi is not the facet); find the object by its vtable in the registers and in the stack. The
  script of stage QG9c: `~/Games/soulwarden/work/evidence/quests/qg9c/implement/scripts/fault_watch.py`.
