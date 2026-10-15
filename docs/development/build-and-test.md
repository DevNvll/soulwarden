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
  `tools/gamedata/check_places_tool.py`, and for the cases RG4 (`check_ability_tool.py`), RG5
  (`check_spell_tool.py`), and PJ11 (`check_projectile_tool.py`): the case passes when the lines of the named cases of
  the check start with `ok`.
- The motion fixture of the tests (`tests/support/motion_fixture.h`: the shop with impacts, the rapier, and the nodes of
  three weapons) is common to the cases of stage 3.3b-S4 (FM, CM, RC) and the review cases RM1 and RM2.
- The projectile fixture (`tests/support/projectile_fixture.h`: the shop with a bow, a fire staff, made-up projectile
  data, and the wolf with a capsule) is common to the cases PJ, RJ (the review of stage 3.3b-S3; RJ7 runs
  `check_projectile_tool.py`), and SB (stage 3.3b-S2b: `tests/spell_kind_tests.cpp` adds the ice gauntlet, the musket, the
  life staff, and the slices of the Pylon, the trap, and the Beacon).
- The spell kind fixture (`tests/support/spell_kind_fixture.h`: the Pylon, the trap, and the Beacon on the projectile
  fixture) is common to the cases SB (stage 3.3b-S2b) and RK (the corrections of its code review, stage 1.1b,
  `tests/review_spells2_tests.cpp`).
- Case RK1 runs `tools/check_dev_mute.py` (the mute of `tools/dev-mute.sh` against made-up prefixes in a temporary folder),
  RK11 `check_projectile_tool.py`, RK12 `check_spell_tool.py`, and DL9 `tools/gamedata/check_progression_tool.py`
  (stage 1.1b): with no Python 3 these cases fail. `check_dev_mute.py --script FILE` runs the check against a different mute
  script (the proof on the tree of stage 3.3b-S2b used the block of its `tools/dev-run.sh`).
- Stage 3.3b-S5 adds the cases CH1 to CH10 (`tests/crit_tests.cpp`: the critical hits), PS1 to PS7
  (`tests/passive_tests.cpp`: the evaluator of the passive rules), and RD1 to RD6 (`tests/review_dlc_tests.cpp`: the
  corrections of the code review of stage 1.1b). The passive fixture (`tests/support/passive_fixture.h`) has rows of the
  ability tables of the game copy (the rows that each case names, with their statuses and spells: made-up ids only, no
  file of the game), a combat file with critical values, a progression file with the trees of the great axe and the war
  hammer, and a wolf slice with its angles and its head volume. Case CH9 runs `tools/gamedata/check_hit_facts_tool.py`,
  RD1 `tools/check_dev_mute.py`, RD2 `check_spell_tool.py`, RD3 `check_progression_tool.py`, and RD4
  `check_projectile_tool.py`. A test of a roll sets the seed of the generator of the world (`World::set_random_seed`):
  the numbers of a seed are fixed (splitmix64), and the case computes the first number itself.
- Stage 3.3b-S6 adds the cases HE1 to HE11 (`tests/heal_tests.cpp`: the heals) and RV1 to RV12
  (`tests/review_crit_tests.cpp`: the corrections of the code review of stage 3.3b-S5). The heal fixture
  (`tests/support/heal_fixture.h`, `HealShop`) has the life staff with ScalingFocus and the Focus table, Divine Embrace,
  Sacred Ground, the Beacon, the heal statuses of the data, and two sessions. A heal press of a client needs its confirm
  (`HealShop::cast`: the key, then the self cast input or a click). The RV cases use only the interfaces of the tree of
  stage 3.3b-S5, so that the same file proves each finding on that tree. RV1 runs `tools/check_dev_mute.py`, RV10
  `check_ability_tool.py`, and RV12 `check_hit_facts_tool.py`.
- The mute of a development run is `tools/dev_mute.py` (code review of stage 1.1b, finding F1; code review of stage
  3.3b-S5, finding F1): it opens the development prefix one time (O_DIRECTORY), compares its identity with the prefix of
  the user and each folder above the settings files, opens each part below it from its parent descriptor with no link
  (O_NOFOLLOW), refuses a settings file with more than one link or with the identity of a file of the user, and writes
  through the same descriptor (`pwrite`, `ftruncate`). No path text is used after the first open.
  `python3 -B tools/check_dev_mute.py` (status 0) tests it with made-up folders, also a swap of the folder at three
  moments, a swap of the prefix before the open (the probe RCM1 of the reviewer), and two hard links; never run it against
  a real prefix.
- The state decoder of the tests knows the entitlement state (3133) and the achievement state (3210) of stage 1.1b: a case
  with expansion data has them in the first record of a character (cases DL2, DL10). The stored bytes of T2 have no
  expansion data: the change of stage 1.1b does not change them.
- In a test, a JSON initializer with a list of pairs (`{"spells", {{"Name", 0}}}`) is an object for `nlohmann::json`, not a
  list of lists: write `json::array({json::array({"Name", 0})})`.
- The state decoder of the tests (`tests/support/state_decoder.h`) must know each state type of a record: a type that it
  does not know drops the whole record (stage 3.3b-S2b: the PositionInTheWorld state of the trap).
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
