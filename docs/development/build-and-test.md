# Build and test

Read this document before a build, a change of the test program, or a change of the products in
`apps/*/dist`.

## Start here

| Task | Start here |
| --- | --- |
| Test the backend with no game process | `tools/test-soulwarden-backend.sh`, `apps/backend/tests` |

## Rules

- `tools/test-soulwarden-backend.sh` builds the test program (`soulwarden-backend-tests`, native
  target only) and runs it. The program runs the real game server with made-up clients, a manual
  clock, a transport with no socket, and a database in memory. Its output lists each case.
  `--list` prints each case with its subject, and `T1 E3` runs only these cases. Some cases run
  the check of a Python tool with `python3` (`--list` names the tool). `--write-golden` writes the
  stored file of T2 again: do this only for an intended change of the wire bytes. Cases D2 and H1
  wait some seconds.
- Dear ImGui (version 1.92.6) is in `apps/game-hook/vendor/imgui`, with no change of its
  files. The CMake file compiles each `.cpp` file below `apps/game-hook/vendor`: do not add a
  file of the library that the hook does not use (the demo file). The JSON library of the
  backend is also in `apps/game-hook/vendor/nlohmann`. The OpenSimplex noise code
  (`tools/gamedata/vendor/open-simplex-noise`, public domain, with its license file, no
  change of its files) is a part of the navigation tool only: the client makes the noise of
  the terrain rule with this code, and the tool needs equal numbers. It goes into no product.
- The products in `apps/*/dist` are also what the Steam library entry of the user starts. A
  hook DLL that stops the game stops the game of the user too. Build a hook that is not
  tested to a path outside `dist` (`SOULWARDEN_GAME_HOOK_OUTPUT`), start it with `SOULWARDEN_HOOK_DLL`,
  and build into `dist` only after a live run. The backend of the build folder
  (`~/Games/soulwarden/work/build/native-backend/apps/backend/soulwarden-backend`, made by
  `cmake --build ... --target soulwarden_backend` and by `tools/build-soulwarden-backend.sh`)
  starts with `SOULWARDEN_BACKEND_EXE`. `tools/test-soulwarden-backend.sh` builds only the
  test program: after a change, build the backend target before a live run, or the run has
  the old backend (stage 3.4d lost one live run this way; compare the time of the file).

## Commands

```sh
tools/build-soulwarden-backend.sh
tools/test-soulwarden-backend.sh          # tests of the backend with no game process (status 0: all pass)
tools/build-soulwarden-game-hook.sh
SOULWARDEN_GAME_HOOK_OUTPUT=~/Games/soulwarden/work/hook-test/steam_api64.dll tools/build-soulwarden-game-hook.sh   # a hook for a test: not into dist
```

## Traps

- Case FT9 has a time limit of 1 s for 1,000 ticks with 50 fights (3.6 ms in one run).
- `log::Context` keeps a pointer to its text: give it a string in a variable, not a
  temporary (a log line of the rewards had bytes of free memory at its end before this
  correction).
- A test host with a creature data set whose kind or spawn point has a level with no row
  gives no creature data (`CreatureData::set` returns false): the loader refuses data that
  do not fit together.
- In a test, do not compare a height of a constant with a height of the backend with `==`:
  the compiler computes a constant with a fused multiply and add, which gives a value one
  float step from the value at run time (`same_height` in `tests/position_tests.cpp`).
- The measured values of case WS2 (the times of the scans) change from run to run by 30
  percent. The limit of the case is a ratio (one tenth of a scan of all points), and
  400 ms.
- Case HT11 runs `tools/gamedata/check_combat_tool.py` with `python3` (`SOULWARDEN_PYTHON`
  selects a different one): with no Python 3 the case fails. The check makes its tables in
  memory: no file of the game. The same for case TV9 and
  `tools/gamedata/check_places_tool.py`, and for the cases RG4 (`check_ability_tool.py`) and RG5
  (`check_spell_tool.py`): the case passes when the lines of the named cases of the check start
  with `ok`.
- In a test, a loop over a member of a temporary (`for (x : test_progression().nodes)`) reads
  memory that is free: keep the object in a variable.
- The backend is in the network namespace of the launcher: a request from the host does not
  get to it. To test a route with `curl`, start `apps/backend/dist/soulwarden-backend` alone with a
  copy of a database (`--data-dir`, `--http-port 32299`, `--rep-address 127.0.0.1:32298`,
  `--no-rep-listener`) and send the service host in the header `X-Soulwarden-Host`. `nsenter -t
  BACKEND_PID -U -n --preserve-credentials COMMAND` runs a command in the namespace of a live
  run (`tools/world` does this). A backend that you start directly needs `SOULWARDEN_HARNESS=1` in
  its environment for `--dev-commands` and `--research-session`: without it the backend
  prints a message and stops with status 2.
- A request member with a wrong type, or a handler that stops with an exception, does not
  stop the backend: the reply has status 400, and the backend log has a line `http: METHOD
  PATH failed`.
- Generated files can have absolute paths of the computer, and a part of such a path can be
  equal to a name of the user (compile commands of CMake, `__FILE__` text of Mbed TLS in the
  backend, bytecode caches of Python). Thus the build directories are outside the repository,
  the top `CMakeLists.txt` sets `-ffile-prefix-map`, and each Python tool that imports a
  module of `tools` sets `sys.dont_write_bytecode`. A work script that imports a module of
  `tools` must run with `python -B`, or a `__pycache__` directory comes back. `.gitignore`
  names `build/`, `dist/`, and `__pycache__/`. After a build, a privacy scan of all files of
  the repository directory must find nothing (`strings -a apps/backend/dist/soulwarden-backend |
  grep -c /home/` gives 0).
