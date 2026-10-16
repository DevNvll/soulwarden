# Project Soulwarden

Project Soulwarden is an offline server for the last retail New World client (Steam app 1063730,
client build 1.400.6031.6004151). It has two products.

| Product | Source | Output |
| --- | --- | --- |
| Game hook. It replaces `steam_api64.dll`, sends the traffic of the client to the backend, and draws the tool window. | `apps/game-hook` | `apps/game-hook/dist/steam_api64.dll` |
| Backend. The C++ service with the game server, the HTTP routes, and the SQLite database. | `apps/backend` | `apps/backend/dist/soulwarden-backend` |

The hook DLL does not include or start the backend. The launcher (`linux-compat/run-soulwarden.sh`)
starts the game with Proton in a network namespace with loopback only.

This file has the project rules and the common workflows. The details of each system are in the
documents of the section "Documents". Before you change a system, read its document.

## Rules

### Safety and privacy

- All game traffic stays on the local computer. The backend listens only on loopback. The
  launcher runs the game in a network namespace with loopback only. Do not weaken this.
- Privacy: the Steam id, the account name, and the persona name of the user do not go into a
  file of the repository. A log line of the hook or of the backend shows only the last 4
  digits of a Steam id (`masked_steam_id` in the backend). This applies to generated files
  too: the CMake build directories are in `~/Games/soulwarden/work/build` (`SOULWARDEN_BUILD_ROOT`), the
  products have no absolute path (`-ffile-prefix-map`), and the Python tools write no
  bytecode cache.
- The game log, the HTTP captures, the game server captures, and the database of a run have
  the full Steam id and the persona name of the user. Do not copy text from them into the
  repository, a document, or a report. Mask a Steam id to its last 4 digits.
- Keep SQLite, logs, captures, game data, and analysis files outside the repository:
  `~/.local/share/soulwarden` (runtime data) and `~/Games/soulwarden` (game copy, Proton prefix, Ghidra
  project, Python environment, work files).
- Keep automation and diagnostics behind the `SOULWARDEN_HARNESS=1` process gate. The backend
  refuses the options `--research-session` and `--dev-commands` when its own process does
  not have `SOULWARDEN_HARNESS=1`, before it starts a listener.
- The launcher reads only the Steam id and the persona name of the Steam installation. Never open
  a Steam credential.

### The game of the user and the development runs

- A development run (`tools/dev-run.sh`, `tools/scenarios`) has its own Proton prefix, its own data
  directory, and the mark `SOULWARDEN_DEV_RUN=1`. A start of the user (`linux-compat/run-soulwarden.sh`,
  the Steam library entry) uses `~/Games/soulwarden/proton/1063730` and `~/.local/share/soulwarden`.
  Never use the prefix or the data directory of the user in a run. Stop only development runs
  (`tools/dev-stop.sh`).
- Send test input only through `tools/hx`. Do not use a host input tool (`xdotool`,
  `ydotool`, `wtype`, `hyprctl dispatch`, `omarchy-agent-desktop key` or `enter`).
- A development run shows the game in gamescope on the agent desktop. Do not turn this off
  (`SOULWARDEN_PRIVATE_DISPLAY=0`) while the user uses the computer: the input of the harness then
  gets to the other applications of the user.
- The products in `apps/*/dist` and the game data folder `~/.local/share/soulwarden/gamedata` are
  also what the user starts. Build a hook that is not tested to a path outside `dist`. Give a game
  data tool an output path for a test.

### The backend

- Foundation for more than one player: the project has one client in the world now, but each
  change of the backend obeys the rules MP-1 to MP-10 of the roadmap (section 1.6). In short:
  the state of a character is in a world object with the character id as its key, not in a
  session object; one path sends a state change to each session that observes the entity; no
  constant id for a player; the backend finds the character of a request from the session;
  timers use the world clock.
- One thread owns the world. A different thread (an HTTP route) gives the world a task
  (`World::post`, `World::call`) and reads no session. The world and each session read the time
  only from the world clock (`world/world_clock.h`).
- The state of a character or of a creature is in its entity (`world/entity.h`), and only its
  system changes it (the inventory system the items, the creature system the creatures, the
  health system the health). A system that stores a change writes all its rows in one
  transaction, then changes the entity and marks the component state (`World::touch`). A request
  that is not correct changes nothing.
- A session is an observer. One encoder for each component state (`server/rep/states`) makes the
  bytes for the owner view and for the observer view. No other code writes the bytes of a state.
- The cost of a rule depends on the players and on what is near them, not on the size of the
  world. Do not scan each spawn point or each creature of the world. The cases of the load (WS2,
  FT9, NV10, PT9, LT7) have time limits.
- A table that comes from the game files is game data. A tool in `tools/gamedata` makes it from
  the game copy, and the backend must start and work with a default when the file is not there.
  Numbers of a wire contract are code constants.
- A character belongs to the account of the request token (persona id). A route that
  creates, deletes, or logs in a character must compare the owner with that account.
- Vendored code (Dear ImGui, the JSON library, the OpenSimplex noise) has no change of its files.

### Tests and documents

- `tools/test-soulwarden-backend.sh` must give status 0 after each change of the backend. Each
  stage adds a case with two sessions (rule MP-10). The stored bytes of case T2
  (`apps/backend/tests/golden/one_session.hex`) change only for a change of the wire bytes
  that is intended and that a live run proves (`--write-golden`). The tests have only
  made-up ids: no capture file and no id of a real account goes into the repository.
- A stage tests only what it changes. The regression of the earlier systems runs at the end
  of each milestone (roadmap section 1.3).
- A successful build does not prove a feature. Restart the game and check the game log and a
  screenshot.
- Write each wire contract in the protocol notes with its evidence level and the client code
  address. Addresses are image-relative and apply to one client build.
- Write a new rule or a new trap into the document of its system, in its section "Rules" or
  "Traps". Change this file only for a rule of the whole project or for a common workflow.
- The documents use short sentences and simple words (ASD-STE100 Simplified Technical English).
  Write new text in the same style.

## Common workflows

### Build and test the backend

```sh
tools/build-soulwarden-backend.sh          # the backend: apps/backend/dist/soulwarden-backend
tools/test-soulwarden-backend.sh           # the test program; status 0: each case passed
tools/test-soulwarden-backend.sh --list    # each case with its subject
tools/test-soulwarden-backend.sh FT9 RP1   # only these cases
```

`tools/test-soulwarden-backend.sh` builds only the test program. Build the backend before a live
run, or the run has the old backend.

### Build the hook

```sh
SOULWARDEN_GAME_HOOK_OUTPUT=~/Games/soulwarden/work/hook-test/steam_api64.dll tools/build-soulwarden-game-hook.sh
tools/build-soulwarden-game-hook.sh        # into apps/game-hook/dist: only after a live run of the test build
```

A development run starts the test build with `SOULWARDEN_HOOK_DLL`, and a different backend with
`SOULWARDEN_BACKEND_EXE`.

### A live test in the game

```sh
tools/scenarios/play.sh RUN_LABEL          # start a development run and enter the world
tools/world ignore Soulwarden on           # the creatures do not attack (a test that is not a fight)
tools/hx 'key 0x77'                        # input for the game (F8: the tool window)
gamescopectl screenshot FILE.png           # a picture of the game, and of nothing else
tools/world list                           # the entities, the load, the behavior, the rewards
tools/dev-stop.sh                          # stop the development runs
tools/dev-stop.sh --check                  # status 0: no process of a development run is alive
```

- Show each result that a person can see with a picture. Show a movement or an animation with a
  video (a loop of `gamescopectl screenshot`, then `ffmpeg`). A still picture does not show if a
  creature walks or slides.
- Wait for the first line `rep: input N camera` of the backend log before a picture or an input.
  The loading screen stays for some seconds after the world entry.
- The backend log of a development run is `~/Games/soulwarden/work/dev-data/logs/backend.log`.
  `tools/dev-run.sh` removes the logs and the captures at each start. Copy what you need first.

### Add a feature to the backend

1. Read the document of the system and the sections of the protocol notes that it names.
2. Prove a new message or state in the live client before you write its code (`tools/world
   fragment`, `tools/world message`). Then write its encoder in `server/rep/states` (protocol
   notes section 4, `tools/st NAME_REGEX`).
3. Put a command for a character into `server/http/character_commands.h`, and give it to the admin
   routes and to the development routes. A route has no logic of its own for a character.
4. Add the test cases, with two sessions for the change of a stage (rule MP-10). Run the tests.
5. Write the rules and the traps into the document of the system, the wire contract into the
   protocol notes, and a change that a user sees into the README and `docs/REFERENCE.md`.

### Before a commit

- `tools/test-soulwarden-backend.sh` gives status 0.
- No file of the repository has a Steam id, an account name, a persona name, a home folder path,
  or text from a game log, a capture, or a database.
  `strings -a apps/backend/dist/soulwarden-backend | grep -c /home/` gives 0.
- Run each Python tool with `python3 -B`, so that no `__pycache__` folder comes back.

## Documents

| Task | Read |
| --- | --- |
| The plan, the stage sequence, the rules MP-1 to MP-10 | [Roadmap](docs/ROADMAP.md) |
| Wire contracts and client code addresses | [Protocol notes](docs/PROTOCOL_REVERSE_ENGINEERING_NOTES.md) |
| What a user can do, the variables, the architecture | [README](README.md), [Reference](docs/REFERENCE.md) |
| Start the game for a test: the scenarios, the harness, pictures, logs, captures | [Development runs and live tests](docs/development/live-runs.md) |
| The build, the test program, the products in `dist` | [Build and test](docs/development/build-and-test.md) |
| The game data files and their tools | [Game data](docs/development/game-data.md) |
| Static and live analysis of the client | [Analysis of the client](docs/development/client-analysis.md) |
| The world object, the entity table, sessions, component states, bundles | [World and sessions](docs/systems/world-and-sessions.md) |
| The position of a character and the position report of the client | [Position](docs/systems/position.md) |
| The bag, the coin, the equipment, the perks of an item instance | [Items and equipment](docs/systems/items-and-equipment.md) |
| What the equipment and the effects give a character, the perks of a hit, the gem conversion | [Gear and perks](docs/systems/gear-and-perks.md) |
| The use of a potion or a food, the effects on a character, the heart rune | [Consumables and the heart rune](docs/systems/consumables-and-heart-rune.md) |
| The status effects of a player: the one owner, the stacks and ends, the values, the HUD icons, the name plate of an observer, the speed table | [Status effects](docs/systems/status-effects.md) |
| The spell entities: the vortex of Gravity Well, the Pylon, the trap, the Beacon, their records, their delayed events, their ends, the probe | [Spell entities](docs/systems/spells.md) |
| The pull and the push of a hit on a creature, the dash of a caster and its observers, the counter that the server starts | [Forced movement](docs/systems/forced-movement.md) |
| The shot of a ranged or magic basic attack, its flight on the server, its hit on a creature or a wall, the hit near the muzzle | [Projectiles](docs/systems/projectiles.md) |
| The level, the attributes, the weapon masteries, the ability bars, the trade skills | [Progression and skills](docs/systems/progression-and-skills.md) |
| Creatures: the spawn points, the record for a session, the actions, the creature data, the heals and the buffs of a creature (its statuses, its status actions) | [Creatures](docs/systems/creatures.md) |
| The behavior of a creature: the fight, the way back, patrol and wander | [Creature behavior](docs/systems/creature-behavior.md) |
| The navigation data, the path search, the terrain heights | [Navigation](docs/systems/navigation.md) |
| Melee attacks, the damage, the critical hits (the roll, a hit from behind, a headshot), the health and the death of a character, the heals (the one heal path, the heal base, the heal number), the mana | [Combat and health](docs/systems/combat-and-health.md) |
| The stamina of a player and the dodge: the one stamina path, the regeneration, the empty stamina, the dodge input and its checks, the immune window, the Stamina state | [Stamina and dodge](docs/systems/stamina-and-dodge.md) |
| The abilities of a player: the press, the aim and its confirm, the cast, the cooldown, the hits, the heals of the life staff, the effects on a creature | [Abilities](docs/systems/abilities.md) |
| The passive rules of the weapon trees and the perks: the one evaluator, its events, conditions, and effects, the list of the rules that it does not apply | [Passive rules](docs/systems/passive-rules.md) |
| The rewards of a kill and the respawn of a creature | [Rewards](docs/systems/rewards.md) |
| Travel, the travel places, a conversation and the lock of the input | [Travel and conversations](docs/systems/travel-and-conversations.md) |
| The use of an object of the world: the request and the reply, the checks, the interaction data, the state of an object, the lore pages and the journal, the stations of a settlement and the server children, the timed use, the ends of a use, the service handoff, a handler of a later stage | [Interactions](docs/systems/interactions.md) |
| The tool window (F8) and the admin interface | [Tool window and admin](docs/systems/tool-window-and-admin.md) |
| Player identity, accounts, the character creation, the main menu | [Identity and characters](docs/systems/identity-and-characters.md) |
| The DLC for each player: the entitlements of an account, the achievements of a character, the expansion limits, the rewards of the DLC packs, the entitlement routes | [DLC and entitlements](docs/systems/dlc-and-entitlements.md) |
