# Build and test

Read this document before a build, a change of the test program, a change of the products in
`apps/*/dist`, or a release for the game of the user.

## Start here

| Task | Start here |
| --- | --- |
| Test the backend with no game process | `tools/test-soulwarden-backend.sh`, `apps/backend/tests` |
| Make a release for the game of the user, go back to an earlier release | `tools/release.sh`, `tools/check_release.py` |

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
- The products in `apps/*/dist` go into the next release for the user (section "Releases"), and
  the launcher of the working copy starts them. A hook DLL that stops the game stops the game of
  the user after the next release. Build a hook that is not
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

## Releases

The Steam library entry of the user starts the current release, not the working copy. A release
is a fixed copy of a state that a live run tested. `tools/release.sh` makes it. Then a stage can
change the working copy, `apps/*/dist`, and the game data folder, and the game of the user does
not change.

```text
~/Games/soulwarden/releases/                the release root (SOULWARDEN_RELEASE_ROOT)
  current -> 2026-10-16-8b4abb8             the current release: the Steam library entry starts it
  2026-10-16-8b4abb8/
    VERSION                                 the version name
    release.json                            the commit, its subject, the install time, the SHA-256 of each part
    linux-compat/run-soulwarden.sh          the launcher of the commit
    apps/backend/dist/soulwarden-backend    a copy of the backend of dist
    apps/game-hook/dist/steam_api64.dll     a copy of the hook of dist
    gamedata/                               a copy of ~/.local/share/soulwarden/gamedata (SOULWARDEN_GAMEDATA_DIR)
```

- `tools/release.sh` (`install`) makes a release of the commit of the working copy, and makes it
  current. It refuses when a tracked file differs from the commit: a release must be a commit.
  `--allow-dirty` is only for a test: the version name then ends with `dirty`, and the launcher
  comes from the working copy.
- The command builds nothing. It copies the products of `apps/*/dist` and the game data folder as
  they are. Thus make a release only after the live run of the commit, with the products of that
  run in `dist`.
- The launcher of a release is the file of the commit. It needs no other file of the repository.
  When the launcher gets such a file, add the file to `LAUNCHER_FILES` in `tools/release.py`:
  `tools/check_release.py` finds each file that the launcher names and a release does not have.
- The layout keeps the paths of the repository: the launcher of a release finds its products with
  no change. When `SOULWARDEN_GAMEDATA_DIR` is not set and the folder `<release>/gamedata` exists,
  the launcher uses that folder. The working copy has no such folder: a development run uses
  `~/.local/share/soulwarden/gamedata`.
- The launcher uses its physical folder (`pwd -P`). A game that starts through the link `current`
  keeps the files of its release for the whole run.
- The version name is the date and the short commit (`2026-10-16-8b4abb8`; `-2` for a different
  release of the same commit on the same day). When an earlier release has the same commit, the
  same products, and the same game data, `install` makes no copy: it makes that release current.
- On btrfs the copies are reflinks: an unchanged file uses no new space on the disk. On a different
  file system the command makes normal copies, and it stops before a copy leaves less than 1 GiB
  free. The files of a release are read only.
- The lock rule: while the game of the user runs, its launcher holds `run.lock` in the data
  directory of the user (`~/.local/share/soulwarden`, `SOULWARDEN_DATA_DIR`). Then the command does
  not change `current`, and it removes no release. `install` puts the new release in place, prints
  one line with the command that activates it later, and gives status 3. `activate` and `rollback`
  change nothing and give status 3. The command opens the lock file read only.
- The command changes `current` atomically (a new link, then a rename). Then it removes each release
  that is not one of the 3 newest releases and that is not current.
- `rollback` makes the release before the current one current (the order of the installs).
  `activate VERSION` makes a different release current.
- `tools/steam-shortcut.py add` makes the Steam library entry for the launcher of the current
  release (`--launcher release`) or of the working copy (`--launcher working-copy`). With no option,
  it selects the release when one exists. The entry names the link `current`: `activate` and
  `rollback` need no change of the entry. Steam must be closed for a change of the entry.
- `add` replaces the Soulwarden entry, and `remove` removes it. The tool finds the entry by the
  two launcher paths, and it also finds an older entry of the tool: a launcher that is a link to
  the launcher of the working copy (an older file name), or the name "New World: Aeternum (NAME)"
  with the launcher `linux-compat/run-NAME.sh` (an older project name, or a different checkout).
  One entry stays. It keeps the last play time, and the library images of the old entry get the id
  of the new entry (an image of the image cache of Steam replaces each image that it has). Do not
  write an older project name into the repository: the rule finds it by the form.

```sh
tools/release.sh                            # install: a release of the commit, and it becomes current
tools/release.sh status                     # the current release (*) and the other releases
tools/release.sh activate 2026-10-16-8b4abb8
tools/release.sh rollback                   # the release before the current one
cat ~/Games/soulwarden/releases/current/VERSION
tools/steam-shortcut.py add                 # with Steam closed: the entry starts the current release
python3 -B tools/check_release.py           # the check of the release command and of the Steam entry (temporary folders)
python3 -B tools/check_release.py --work ~/Games/soulwarden/work --backend apps/backend/dist/soulwarden-backend
                                            # the same on btrfs (reflinks), and a start of the backend of a release
```

## Traps

- The proof that a new case fails on the tree before a stage (stage B0): copy the earlier tree into a work folder, add the new test
  files and their registration, and build the test program of the copy with `-DSOULWARDEN_B0_BASELINE` in `CXXFLAGS` (a work build
  root, not the build folder of the repository). In `tests/build_preset_tests.cpp` and `tests/review_user_test_2_tests.cpp` this mark
  removes the parts that need the new code (the build presets, the catalog format 3, the count `pushedOffPath`); the cases of a new
  feature with no code before (BP3 to BP8) are not in such a build. The helpers of the creature cases of user test 2 (an idle wolf, a
  passive creature on a wander) are in `tests/support/idle_fixture.h` (cases UX1 to UX9 and RT1 to RT6).
- Stage B1 uses the mark `SOULWARDEN_B1_BASELINE` (proof on a copy of the tree of stage B0): in `tests/support/bow_fixture.h` and
  `tests/bow_tests.cpp` it removes the ability actions, the clip shots, and the hit rates, and the counts of the stage are 0, so that
  each case BW1 to BW10 runs and fails on the behavior of the old tree (the press of a bow ability is refused there: no rule). The
  cases of the review of stage B0 are in `tests/review_b0_tests.cpp` (RQ1, RQ2) and `tests/build_preset_tests.cpp` (BP10 to BP12); they
  need no mark. Do not change the baseline copy of the orchestrator: copy it again for the proof (`cp -a --reflink=auto`), and build
  in a work folder of `SOULWARDEN_BUILD_ROOT`.
- Stage B2 uses the mark `SOULWARDEN_B2_BASELINE` (proof on a copy of the tree of stage U3): in `tests/support/boltcaster_fixture.h`,
  `tests/boltcaster_tests.cpp`, and `tests/review_u3_tests.cpp` it removes the stacks of a creature effect, the counts of the stage, the
  marks of the projectile data of format 3, so that each case BC1 to BC11 and UR1 to UR9 runs and fails on the behavior of the old
  tree. Copy the new test files and their registration in `tests/cases.cpp` into the copy.
- Stage B3 uses the mark `SOULWARDEN_B3_BASELINE` (proof on a copy of the tree of stage B2): in `tests/support/haste_fixture.h` and
  `tests/haste_tests.cpp` it removes the dodge status actions, the tag removal actions, the fixed time of a command, the time factor call,
  and the lists of format 7, and the counts of the stage are 0, so that each case HS1 to HS10 runs and fails on the behavior of the old tree.
  The cases of the code review of stage B2 (`tests/review_b2_tests.cpp`, BV1 to BV5) need no mark. The correction of case BC7 (finding
  B2-R6) is proved with a copy of the tree whose ApplicationCooldown of a creature is broken: the corrected BC7 fails, the old one passes.
- Stage B4 uses the mark `SOULWARDEN_B4_BASELINE` (proof on a copy of the tree of stage B3): in `tests/support/bow_rules_fixture.h` and
  `tests/bow_rules_tests.cpp` it removes the trinket rows of the catalog, the mark CanAttackCancel of the stamina data, the counts of the
  stage, and the call of the outgoing factor, so that the cases BR1 to BR14 and BY1 to BY3 (`tests/review_b3_tests.cpp`, the code review of
  stage B3) run on the old tree: each one fails there except BR11 (Desert Sunrise worked from stage B3; the stage adds only the test and
  the note of the list, which BR14 tests). Build the copy with `-DCMAKE_CXX_FLAGS=-DSOULWARDEN_B4_BASELINE`. The correction of case HS7
  (finding B3-R4) is proved with a copy of the tree of stage B4 whose position check has no haste allowance (`PositionTrack::judge`
  with `kWindowSpeed` only): the corrected HS7 fails (11 refused reports with the haste), the old one passes.
- Stage U4 uses the mark `SOULWARDEN_U4_BASELINE` (proof on a copy of the tree of stage 3.7a): in `tests/menu_equipment_tests.cpp` it
  gives the names of the stage (`state::MenuDocument`, `make_menu_document`, `world::menu_equipment`) the behavior of the old tree (the
  stored creation data with the level of the row, no entry, no appearance column, no count of the item queries), so that each case ME1 to
  ME8 runs and fails there. The cases of the code review of stage 3.7a (`tests/review_37a_tests.cpp`, CRV1 to CRV4) need no mark: each
  one fails on the old tree (CRV3 there: the tool check has no case `incomplete`). Copy the two test files and their registration in
  `tests/cases.cpp` into the copy, and build it with `-DCMAKE_CXX_FLAGS=-DSOULWARDEN_U4_BASELINE`.
- The Windows headers of the MinGW build define `near` and `far` as macros: a member or a variable with such a name in a file that a
  route includes after them does not compile (`SOULWARDEN_BACKEND_TARGET=windows`; stage 3.5b.1: `ChestsView::nearby`). Build the
  Windows target before a commit of a new header.
- A new member of a struct that the tests fill by position (`SpellVolume`) needs a default initializer (`{}`), else each such fixture
  gives the warning `-Wmissing-field-initializers`.
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
- A release needs a commit: in the main checkout, a stage with changes that are not a commit blocks
  `tools/release.sh`. Make the release after the commit of the stage.
- A launcher that starts in the same moment as a change of `current` (some microseconds) stops with
  "another game process uses this data directory": the release command holds the lock of the
  launcher for the rename. Start the game again.
- `/tmp` is tmpfs, which makes no reflink: `tools/check_release.py` with no `--work` tests the normal
  copy. With `--work ~/Games/soulwarden/work` (btrfs) it tests the reflinks. The check never opens
  the real Steam directory: its Steam directory and its `HOME` are temporary folders.
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
- Case RK1 runs `tools/check_dev_sound.py` (the silence of a development run: the startup setting, and no tool that edits
  a settings file; since the code review of stage 3.3b-S6), RK11 `check_projectile_tool.py`, RK12 `check_spell_tool.py`, and
  DL9 `tools/gamedata/check_progression_tool.py` (stage 1.1b): with no Python 3 these cases fail. `check_dev_sound.py TREE`
  checks a different tree (the proof of finding S6-R1 on the tree of stage 3.3b-S6: status 1).
- Stage 3.3b-S5 adds the cases CH1 to CH10 (`tests/crit_tests.cpp`: the critical hits), PS1 to PS7
  (`tests/passive_tests.cpp`: the evaluator of the passive rules), and RD1 to RD6 (`tests/review_dlc_tests.cpp`: the
  corrections of the code review of stage 1.1b). The passive fixture (`tests/support/passive_fixture.h`) has rows of the
  ability tables of the game copy (the rows that each case names, with their statuses and spells: made-up ids only, no
  file of the game), a combat file with critical values, a progression file with the trees of the great axe and the war
  hammer, and a wolf slice with its angles and its head volume. Case CH9 runs `tools/gamedata/check_hit_facts_tool.py`,
  RD1 `tools/check_dev_sound.py`, RD2 `check_spell_tool.py`, RD3 `check_progression_tool.py`, and RD4
  `check_projectile_tool.py`. A test of a roll sets the seed of the generator of the world (`World::set_random_seed`):
  the numbers of a seed are fixed (splitmix64), and the case computes the first number itself.
- Stage 3.3b-S6 adds the cases HE1 to HE11 (`tests/heal_tests.cpp`: the heals) and RV1 to RV12
  (`tests/review_crit_tests.cpp`: the corrections of the code review of stage 3.3b-S5). The heal fixture
  (`tests/support/heal_fixture.h`, `HealShop`) has the life staff with ScalingFocus and the Focus table, Divine Embrace,
  Sacred Ground, the Beacon, the heal statuses of the data, and two sessions. A heal press of a client needs its confirm
  (`HealShop::cast`: the key, then the self cast input or a click). The RV cases use only the interfaces of the tree of
  stage 3.3b-S5, so that the same file proves each finding on that tree. RV1 runs `tools/check_dev_sound.py`, RV10
  `check_ability_tool.py`, and RV12 `check_hit_facts_tool.py`.
- Stage 3.3b-M2 adds the cases GH1 to GH19 (`tests/gawh_tests.cpp`: the abilities of the great axe and the war hammer) and RB1
  to RB8 (`tests/review_combos_tests.cpp`: the corrections of the code review of stage 3.3b-M1; each one fails on the tree of
  stage 3.3b-M1). The attack fixture (`tests/support/attack_fixture.h`, namespace `attack_fixture`) has the world of the cases
  MB and RB: the axe and the rapier graphs of the data with made-up ids, the wolf, and a client of one report for each step.
  The fixture of the abilities (`tests/support/gawh_fixture.h`, `GawhShop`) is the shop with the axe and the hammer graphs of
  format 2 (the form and the times of the fragments of the game data), the rows of the six abilities with their impacts and
  row statuses, the slices of the spells of an action, and `GawhShop::press`, `step`, `contact_at`. The state decoder of the
  tests reads the fields 1 and 2 of slot 46 (`progress_wire::ability_value_size`); the bundle decoder needs this to read a
  record of a character with a hit count.
- Stage 2.5a adds the cases IT1 to IT11 (`tests/interaction_tests.cpp`: the interaction base) and RX1 to RX5
  (`tests/review_wrecking_tests.cpp`: the corrections of the code review of stage 3.3b-M2b, findings M2b-R1 to M2b-R5; each one fails
  on the tree of stage 3.3b-M2b and passes with the correction; RX4 runs `check_weapon_actions_tool.py`). The fixture of the IT cases
  is in its file (`Shop`: made-up objects of the form of `interaction-data.json`, two players near them, a block of the client for an
  option, `ServerHost(options, database)` for a database file). The input builder of the tests writes a full interaction block
  (`InteractionBlock`, `InputEntry::interaction`), and the state decoder knows the states 3752 and 2930. IT10 runs
  `tools/gamedata/check_interactions_tool.py`. IT5 adds a trigger to a database file (a refused row); IT8 has a time limit of 300 ms
  for 400,000 lookups and of 2 s for 300 reports with a catalog of 90,008 objects.
- Stage 2.2 adds the cases WT1 to WT9 (`tests/world_time_tests.cpp`: the world time: the cycle and the wire with the vector of
  the research and the float model of the client, two sessions and a third one with the deliveries and the clock correction, the
  requests that change nothing, the store with a database file and a refused write (a trigger), the creature points with a time
  constraint with two observers, the load at the size of the world with 50 players (limit 50 ms for the tick of a phase edge and of
  a command), the creature file of format 4, the cases phase-* of `check_creature_tool.py` (WT8), and the admin and development
  commands) and MN2 (`tests/mana_tests.cpp`: the mana prediction message 4289). The fixture `TimeHost` of WT2 to WT5 passes time in
  steps of 1 s (`pass`): a larger step of the manual clock ends each session (the idle limit of 30 s). The interaction fixture
  (`Shop`) takes the ids of the player slice for MN2.
- Stage 2.2 adds the cases RLB1 to RLB10 (`tests/review_loot_bags_tests.cpp`: the corrections of the code review of stage 3.5b.2,
  findings LB-R1 to LB-R9, and the open point of 3.5b.2, an item of a kill that the item catalog does not have; each one fails on the
  tree of stage 3.5b.2 and passes with the correction, with only the interfaces of that tree). RLB1 runs the new check
  `tools/gamedata/check_item_catalog_tool.py`: it writes a catalog of made-up rows into the work folder, and the case loads that file.
  RLB7 runs the review cases of `check_progression_tool.py`. RLB4, RLB8, RLB9, and RLB10 write database files into the work folder
  (RLB4 and RLB8 change them with SQL: a row of a 501st stack, and the tables of schema 10 for the migration). RLB8 has a load case with
  100 due ends and 20,000 far bags and a limit of 1 s, written in the case before it runs; RLB5 makes 30 bags of 200 items. B11 now
  checks the schema version 12 (version 11: the table `world_time`; version 12: the bag index and `emptied_at`).
- Stage 3.5b.2 adds the cases GL1 to GL7 (`tests/loot_bag_tests.cpp`: the personal loot bags of a kill and of a discard: the bag and
  its states, the panel and the takes, two sessions and a known reference of a different character, the discard and its recovery, the
  lifetime on a synthetic clock at its boundaries, a refused write and a restart, and the load with 2,000 and 20,000 far bags and a limit
  of 2 s, written in the case before it runs), MN1 (`tests/mana_tests.cpp`: the MaxMana and ManaRate parts of the threshold abilities and
  the base maximum of the states), and RCH1 to RCH6 (`tests/review_chests_tests.cpp`: the corrections of the code review of stage
  3.5b.1, findings CH-R1 to CH-R8; each one fails on the tree of stage 3.5b.1 and passes with the correction; RCH6 runs the review cases
  of `check_chests_tool.py`). The fixture of the GL cases is in `tests/support/loot_bag_fixture.h` (namespace `tests::bags`: a made-up
  bag slice and template, items, a creature kind, and a loot table; `BagShop`). The state decoder knows the states 2938 and 2027.
- Stage 3.5b.1 adds the cases LC1 to LC9 (`tests/chest_tests.cpp`: the chest file and the loot tables of format 2, the open and its
  ends, the context of the roll, the claim in the store and its resets on a synthetic clock with the elite value 82,800 s of the data, two
  characters on one chest, a refused write and a full bag, the gates, the load, and the check of the chest tool) and RSU1 to RSU7
  (`tests/review_support_tests.cpp`: the corrections of the code review of stage 3.4f-D, findings SU-R1 to SU-R7; each one fails on the
  tree of stage 3.4f-D and passes with the correction; RSU4 runs the cases `review-support-*` of `check_creature_tool.py`). The fixture of
  the LC cases is in `tests/support/chest_fixture.h` (namespace `tests::chests`: made-up chests, gatherables, and loot tables of the form
  of the game data; `ChestShop`). The state decoder knows the states 81 and 982. LC8 has 100,000 far chest definitions and 50 characters
  with a limit of 3 s, written in the case before it runs; LC9 runs `tools/gamedata/check_chests_tool.py`. A test host gets chest data
  with `ServerHost::with_chests`.
- Stage 3.4f-D adds the cases CRH1 to CRH6 and CRH9 (`tests/creature_support_tests.cpp`: two Clerics that heal themselves with two
  sessions, the cap and a duplicate and the rules of the behavior, the buff of the hound and its hit, the expiry, the dispel, the death
  of the source, a knockdown before the mark, the areas at the caster with the rotation of the caster, the loader of the status actions,
  the load) and RAR1 to RAR5 and RAR8 (`tests/review_areas_tests.cpp`: the corrections of the code review of stage 3.4f-C, findings AR-R1
  to AR-R5 and AR-R8; each one fails on the tree of stage 3.4f-C and passes with the correction; AR-R6 and AR-R7 are cases of
  `check_spell_tool.py` and `check_creature_tool.py`). The fixture `Support` is in `tests/support/support_fixture.h` (namespace
  `tests::support`: the cleric, the hound, the ghost, and the knight slices with their rows of the game data). CRH9 has 50 characters, 50
  Clerics that heal, 50 hounds that howl, 1,000 creatures with a long effect, and 32,840 far spawn points: 1000 ticks in less than 3 s,
  with the same due entries and looks as 840 points and no far effect. The area fixture rows have the ranged attack data of the game rows
  (the place rule of stage 3.4f-D).
- Stage 3.4f-C adds the cases CRA1 to CRA9 (`tests/ranged_c_tests.cpp`: the mushroom and its records, the poison and its ticks, the
  strike, the ends at the caster and the walls, two characters and two areas, the behavior, the loaders, the dodge, the load), SM6
  (`tests/ranged_b_tests.cpp`: the Stamina state of the first record), and RZ1 to RZ6 (`tests/review_casters_tests.cpp`: the
  corrections of the code review of stage 3.4f-B, findings CS-R1 to CS-R6; each one fails on the tree of stage 3.4f-B and passes with
  the correction). The fixture `Areas` is in `tests/support/area_fixture.h` (namespace `tests::area`: the shaman, the striker, the
  thrower with a shot, the mushroom and the strike slices, the poison row). CRA9 has 50 characters, 50 shamans, 50 strikers, and 200
  areas or more: 1000 ticks in less than 3 s, with the same count of steps and looks for 840 and 32,840 spawn points. T1, G5, and T2
  have the Stamina state in the first record (`one_session.hex` written again after the live trace of the stage). The case
  `replace-before-pay` of `check_stamina_tool.py` (SM1) checks finding CS-R5.
- Stage 3.4f-B adds the cases SM1 to SM5, CRB1, and CRG10 to CRG13 (`tests/ranged_b_tests.cpp`: the stamina data and path, the
  statuses and the passive rules of the stamina, the tag Dodging, the caster, two attackers and two targets with a late observer, the
  dodge, no immunity for a different character, the shield before a later target and a wall; SM1 runs `check_stamina_tool.py`) and RR1
  to RR7 (`tests/review_ranged_tests.cpp`: the corrections of the code review of stage 3.4f-A, findings CR-R1 to CR-R7; each one fails
  on the tree of stage 3.4f-A and passes with the correction; RR5 and RR6 run the review cases of `check_creature_tool.py`). The
  fixture `Range` moved to `tests/support/range_fixture.h` (namespace `tests::range`, with the test mage `mage_slice` and
  `set_battle`); the stamina values of the retail data are in `tests/support/stamina_fixture.h` (`set_stamina`). `ServerHost` has the
  stamina data (`stamina`), and the state decoder knows the Stamina state (type 4297). CRG9 has 25 archers and 25 mages, with the
  same limit. The check of the ability tool expects format 6 (cases RA12, RS8, RS13, RG4).
- Stage 3.4f-A adds the cases CRG1 to CRG6 and CRG9 (`tests/ranged_tests.cpp`: the archer; its fixture `Range` is the shop with the
  archer slice, its basic shot, the arrow slice, and the capsule of the player slice; CRG1 runs `check_creature_tool.py` and reads its
  ranged cases; CRG9 has a time limit of 3 s for 1,000 ticks with 50 archers and 200 flights) and RY1 to RY8
  (`tests/review_stations_tests.cpp`: the corrections of the code review of stage 2.5b, findings ST-R1 to ST-R8; each one fails on the
  tree of stage 2.5b and passes with the correction; RY6 to RY8 run `check_interactions_tool.py --review-stations`). The fixture of the
  SC cases moved to `tests/support/station_fixture.h` (namespace `tests::stations`). The state decoder knows the Projectile state
  (type 16). Case RP5 counts its seven patrol cases of `check_creature_tool.py` by name (the check has the ranged cases too). Case SC5
  has the corrected range rule of the service grant (finding ST-R3: the walk ends the grant, `noGrant`).
- Stage 2.5b adds the cases SC1 to SC9 (`tests/station_tests.cpp`: the server children, the StationContext, the timed use, the ends
  of a use, the service handoff, two sessions, the load, the loader of format 2, the fixture commands) and IR1 to IR5
  (`tests/review_interaction_tests.cpp`: the corrections of the code review of stage 2.5a, findings INT-R1 to INT-R6; each one fails
  on the tree of stage 2.5a and passes with the correction; IR5 runs `check_interactions_tool.py --review`). The fixture of the IT
  cases moved to `tests/support/interaction_fixture.h` (namespace `tests::interaction`) for the three files. The state decoder knows
  the states 1594 and 3217. SC7 has a time limit of 3 s for 1,000 steps with 60,000 far server children and of 2 s for 150 timed
  uses.
- Stage 3.3b-M2b adds the cases GH20 (Wrecking Ball in the fragment of the data) and GH21 (the target bits of a creature) to
  `tests/gawh_tests.cpp`; GH21 and the stored bytes of CR2 fail on the tree of stage 3.3b-M2. The hammer graph of `GawhShop` has
  the fragment of Wrecking Ball with its two clips (the base capsule and the sphere of Breathing Room) and the rule row of Safety
  Measures. The cases RH1 to RH11 (`tests/review_gawh_tests.cpp`: the corrections of the code review of stage 3.3b-M2, findings
  M2-R1 to M2-R10) fail on the tree of stage 3.3b-M2 and pass with the corrections; RH7 runs `check_weapon_actions_tool.py` (case
  "incomplete abilities"). GH15 has its wolves in the pull shapes of their casters (finding M2-R4).
- The silence of a development run is the startup setting `-sys_audio_disable 1` of the game (`tools/dev-env.sh`,
  `dev_sound_arguments`; code review of stage 3.3b-S6, finding S6-R1). The file mute of the stages 1.1b to 3.3b-S6
  (`tools/dev_mute.py`, `tools/dev-mute.sh`, `tools/check_dev_mute.py`) is gone: four reviews found a swap of folders that sent
  its write to a settings file of the user. Do not add a tool that edits a file of a Proton prefix.
- Stage 3.3b-M1 adds the cases MB1 to MB9 (`tests/attack_sequence_tests.cpp`: the basic attacks of the melee weapons, with the
  made-up graphs of an axe and a rapier in the form of the data; MB5 two sessions; MB6 a replay, a wrap of the input number, a
  flood, a reconnect; MB7 the load with a time limit; MB9 runs `tools/gamedata/check_weapon_actions_tool.py`), RW1 to RW9
  (`tests/review_heal_tests.cpp`: the corrections of the code review of stage 3.3b-S6, from the proof cases of the reviewer; RW1
  runs `tools/check_dev_sound.py`; RW9 is a control), and HE12 (the level part of the heal base). HE9 counts the periodic heals
  apart (S6-R9). The proof that RW1 to RW8 fail on the tree of stage 3.3b-S6 used a copy of that tree with only the case file
  added (evidence of the stage, `review-proof/`). The claim limit of a character is 64 in 1 s: case HT5 uses the constant.
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
