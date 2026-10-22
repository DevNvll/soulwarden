# Project Soulwarden roadmap

This document lists the systems that Project Soulwarden must have, and the sequence of the work. Each
system goes through the same three steps: research, implementation, test. The status
columns show the condition on 2026-10-13 for the client build 1.400.6031.6004151.

The system list comes from the client, not from memory of the game:

- The player slice (`slices/player.dynamicslice`) has 159 component types with a server
  facet. Each one is a function that the original server did.
- The client sends 26 message types in the first minutes in the world. Only the input report
  (type 1589) has a handler (`~/Games/soulwarden/work/research/client-messages.md`).
- The open items of the [protocol notes](PROTOCOL_REVERSE_ENGINEERING_NOTES.md), section 5.

## 1. The pattern for each system

### 1.1 Step R: research (read-only, many agents at the same time)

- Inputs: the client image (`tools/nw`, `tools/gh`, `tools/st`, `tools/ht`), the game data
  (slices, data tables, user interface scripts), and captures of our own runs.
- Output: one file `~/Games/soulwarden/work/research/TOPIC.md`. Each statement has an evidence
  mark: `[S]` static proof, `[D]` game data, `[C]` capture, `[G]` guess. The file has a
  minimal proposal (the bytes that the backend sends), the first live checks, and the
  unknowns.
- A second agent then does the important parts again with a different method and corrects the
  file.
- Gate R: the file has the status "ready with live checks".
- Research is for reverse engineering only (decision of the user, 2026-10-13): the client
  image, the game data, and the captures. Engineering work has no research step: the Go
  port, the repository layout, the libraries, and the build tools. The implementer of the
  stage makes these decisions.

Limit of all research: no capture of an original server exists. The client talks only to the
backend. Logic that was only in the server (creature behavior, loot rolls, timers) comes from
the game data, or it is a decision of this project. The research must say which one applies.

### 1.2 Step I: implementation (one writer)

- Before the start: a copy of the tree (`~/Games/soulwarden/work/trees/baseline-STAGE`) for the
  code review. No backup of the database: this is a development environment, and data
  loss is not a problem.
- First the live checks of the research, then the code.
- Each stage gives: the state and its persistence in SQLite (with a migration), the wire
  contract in the protocol notes with its evidence level, a tool in `tools/gamedata` when a
  table of the game is necessary, a scenario script in `tools/scenarios`, and a command in the
  tool window (system 1.7) to set the state.
- Gate I: the build has no new warning, the tests of the backend pass, the documents agree
  with the code, and the implementer has a screenshot for each visible criterion. After
  the Go port: `go vet` finds nothing, and the tests pass with the race detector on.

### 1.3 Step T: test

- Each criterion has an id. A criterion with a visible result has a screenshot in
  `~/Games/soulwarden/work/evidence/STAGE/`. A criterion with no visible result has a log line.
- Decision of the user (2026-10-13): a stage tests only what it changes. It has no
  regression criterion, and no second agent does its criteria again. A test uses the
  fastest path to the state that it needs: the development data directory with a
  character that is there already, not a new character creation.
- The implementer tests its criteria. The orchestrator reads the result file, looks at
  the screenshots, and runs the test program that needs no game.
- One code review reads the changes of the stage on tree copies, at the same time as the
  next stage. It also compares the changes with the rules of section 1.6. Its findings go
  to the thread of the next stage.
- The regression of all earlier systems and one independent acceptance check run at the
  end of each milestone (for milestone 1: stage 1.9).
- Gate T: all criteria of the stage have evidence, the test program passes, the privacy
  scan finds nothing, and no game process stays.

### 1.4 Rules that apply to all steps

- The safety contract and the privacy rules of [AGENTS.md](../AGENTS.md).
- A test run uses the private display. Test input goes only through `tools/hx`.
- Only one agent at a time starts the game, builds, and edits the tree. Thus the
  implementation stages run in sequence. Research is read-only, thus the research of the next
  milestone runs while a stage is in implementation.
- A system starts step I only when the systems that it depends on have passed gate T.

### 1.5 Sizes

Sizes are estimates. `S`: one stage run. `M`: two or three stage runs. `L`: four or more
stage runs. `XL`: open size; the research splits the system into smaller stages.

### 1.6 Rules for more than one player

Decision of the user (2026-10-12): the project does not implement multiplayer now, but each
system must have a strong foundation for it. Thus these rules apply to each system from
stage 0.6 on, also while only one client is in the world. The design review of step T
compares each stage with them.

- MP-1, world state is not session state. The state of a character (position, vitals, items,
  equipment, progression, quests) is in a world object of the backend, with the character id
  as its key. A session is one observer of the world and the owner of one character.
- MP-2, one path for a state change. A system changes the world state. The replication layer
  then sends the change to each session that observes the entity. No system writes bytes for
  "the client". With one client, the path is the same.
- MP-3, owner view and observer view. The research of each state says which fields only the
  owner gets and which fields each observer gets (the player slice has the sub id 2, the
  remote player slice has the sub id 100). The protocol notes record this for each contract.
- MP-4, no constant ids. The actor id, the entity id, the interest number, and the target ids
  of a player come from the session or from the entity, not from a constant in the code.
- MP-5, the backend has the authority. The backend finds the character of a request from the
  session, not from the body of the request. It checks the owner, the distance, and the
  state. A wrong request gets a failure reply and changes nothing. The backend computes each
  result (quantities, damage, rewards) itself.
- MP-6, one world clock and one world thread. Timers (cooldowns, respawn, world time) use the
  world clock. The HTTP threads and the admin interface give tasks to the world thread.
  After the Go port, one goroutine owns the world state.
- MP-7, persistence. Each row has the character id or the persona id as its owner key. A
  change of more than one row (transfer, trade, craft) is one transaction. A character is in
  the world in one session at most.
- MP-8, position first. The entity table has a position index and an interest range from its
  first version (system 2.4), also with one observer.
- MP-9, tools. A command of the admin interface or of the tool window names a character id.
  Log lines and capture files name the session. New code in the launcher, the harness, and
  the scenario scripts does not assume one game process where a parameter is cheap.
- MP-10, test with two sessions. From stage 0.6a on, each stage has a test with two
  sessions and two characters in the backend, with no game process: an action of one
  session changes what the other session gets only where the system intends it.

### 1.7 Languages and repository layout

Decision of the user (2026-10-13): the backend services are in Go, the hook library stays
as it is (C++), and the repository is a monorepo. The Go port (stages P.1 and P.2, section
4) has no research step. A decision of the user of 2026-10-15 puts it at the end of the
roadmap (section 2.1). Until then the stages continue in the C++ backend, and the layout
below is the layout after the port.

| Path | Content |
| --- | --- |
| `apps/backend/` | Go module with the backend services and the program `soulwarden-backend` |
| `apps/backend-cpp/` | The C++ backend, only as the reference until the gate of stage P.2 |
| `apps/game-hook/` | The hook DLL (C++, CMake). No change. |
| `packages/` | Go code that more than one program uses. Empty at the start. |
| `linux-compat/`, `tools/`, `docs/` | Launcher, scripts and research tools, documents. No change. |
| `go.work`, `package.json`, `pnpm-workspace.yaml`, `turbo.json` | Workspace files of the monorepo |

- Services. Each service is one Go package: the HTTP services (`gateway`, `omni`, `aws`,
  `channel`), the game server (`rep`), the world, the store, and the admin interface. A
  service package does not import a different service package: the services share the
  store and the world through interfaces. One program runs all services now, because the
  launcher starts one backend in the network namespace. The packages permit a split into
  more programs later.
- No change for the other parts. The command line options, the data directory, the database
  schema, the log format, and the capture format of the backend stay. Thus the launcher,
  the hook, and the tools continue to work.
- Equal bytes. For the same input, the Go backend sends the bytes that the C++ backend
  sends. Test vectors that a program makes from the C++ code prove this. JSON replies are
  compared as JSON values.
- Build. `go build -trimpath -buildvcs=false`, so that the program has no path of the
  computer. The products stay in `apps/*/dist`. The module path has no name of the user
  (`soulwarden.internal/backend`). The privacy scan applies to the Go program too.
- Workspace tasks: `pnpm build`, `pnpm build:backend`, `pnpm build:hook`, `pnpm test`,
  `pnpm check`. Each app has a `package.json` whose scripts call the scripts in `tools/`.
- Libraries (the implementer of the stage selects them): a Go library for DTLS 1.2 with
  the cipher of the client (`TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384`), a SQLite driver in
  pure Go, the standard library for HTTP, JSON, and zlib. Risk: the DTLS library must
  complete the handshake with the client. If it does not, the game server keeps Mbed TLS
  through cgo.
- The directory is not a git repository now. The plan does not make one.

## 2. Summary

| Id | System | Depends on | Size | R | I | T |
| --- | --- | --- | --- | --- | --- | --- |
| 0.1 | Start sequence and HTTP services | | | done | done | done |
| 0.2 | Game server transport | 0.1 | | done | done | done |
| 0.3 | World entry of the local player | 0.2 | | done | done | done |
| 0.4 | Movement of the local player (first form) | 0.3 | | done | done | done |
| 0.5 | Development tools and private display | | | done | done | done |
| 0.6a | Session model: tests with two sessions, session ids, session end, world thread | 0.3, 1.2 | M | verified | done | |
| 0.6b | World model: entity table, observer view, id allocator, remote player | 0.6a | M | verified | done | |
| 1.1 | Steam account identity | 0.1 | S | done | done | done |
| 1.1b | The DLC for each player | 1.1, 3.3b | S | verified | done | |
| 1.2 | Character creation and deletion | 1.1 | M | done | done | done |
| 1.3 | Inventory and currency | 1.2, 0.6b | M | verified | done | |
| 1.4 | Equipment and visible gear | 1.3 | M | verified | done | |
| 1.5 | Level, experience, attributes | 1.2, 0.6b | S | verified | done | |
| 1.6 | Weapon mastery, ability bar, trade skills | 1.5 | M | verified | done | |
| 1.7 | Tool window and admin interface | 1.3, 1.5 | M | verified | done | |
| 1.8 | The generic quest engine: each quest from the quest data of the game (before: the original quest state of a new character) | 1.4, 1.6 | M | verified | | |
| 1.9 | Acceptance test from an empty database | 1.1 to 1.8 | S | not necessary | | |
| P.1 | Go port, part 1: monorepo, HTTP services, and store (at the end of the roadmap) | all other stages | M | not necessary | | |
| P.2 | Go port, part 2: game server, world, and all systems of the C++ backend | P.1 | XL | not necessary | | |
| 2.1 | Client message base | 0.3, 0.6b | S | verified (ids), first pass (messages) | | |
| 2.2 | World time and weather | 2.1 | S | verified | done | |
| 2.3 | Exact position and full movement | 0.4, 0.6b | L | verified (position) | 2.3a done | |
| 2.4 | Other entities and area of interest | 2.1, 0.6b | L | verified | | |
| 2.5 | Interaction base | 2.4 | M | verified | done (2.5a, 2.5b) | |
| 2.6 | Death, respawn, camps, fast travel | 2.3, 2.5 | M | verified | | |
| 2.7 | Map, territories, markers | 2.1 | M | verified | | |
| 2.8 | Notices and HUD services | 2.1 | S | verified | | |
| 3.1 | Weapons in the hand and attack actions | 1.4, 2.3 | L | verified | a part in 3.2a (the attack inputs, the weapon keys) | |
| 3.2 | Damage, stamina, and mana model | 3.1, 2.4 | L | verified (`damage.md` command, `hit-reports.md` layouts, `melee-mode.md`); the live checks M1 and M2 of the melee mode passed | 3.2a done | |
| 3.3 | Abilities, cooldowns, status effects, perks | 1.6, 3.2 | L | verified | 3.3a, 3.3b-S1, 3.3b-S2a, 3.3b-S4, 3.3b-S3, 3.3b-S2b, 3.3b-S5, 3.3b-S6, 3.3b-M1, 3.3b-M2 done | |
| 3.4 | Creatures: spawn, behavior, navigation | 2.4, 3.2 | XL | verified (`creatures.md`, `enemies-start-area.md`) | 3.4a done, 3.4c done, 3.4b done, 3.4d done, 3.4e done, 3.4f-A done, 3.4f-B done, 3.4f-C done, 3.4f-D done | |
| 3.5 | Loot and rewards | 1.3, 3.4 | M | verified | 3.5a done, 3.5b.1 done, 3.5b.2 done | |
| 3.6 | Gathering and fishing | 1.3, 1.6, 2.5 | M | verified | 3.6a done | |
| 3.7 | Crafting, refining, repair, salvage | 1.3, 1.6, 2.5 | M | verified | 3.7a, 3.7b, 3.7c done | |
| 4.1 | Objective progress, completion, rewards | 1.8, 2.5 | L | verified | | |
| 4.2 | Conversations, lore pages, cutscenes | 2.5 | M | verified | | |
| 4.3 | Introduction path of a new character | 4.1, 4.2, 3.4 | L | none | | |
| 4.4 | Achievements, titles, milestones | 2.1 | M | verified | | |
| 4.5 | Factions | 4.1 | M | verified | | |
| 4.6 | Events, season track, leaderboards | 4.1 | M | verified | | |
| 5.1 | Shops and transactions | 1.3, 2.5 | M | verified | | |
| 5.2 | Storage | 1.3, 2.7 | M | verified | | |
| 5.3 | Trading post | 5.2 | L | verified | | |
| 5.4 | Territory standing and town projects | 2.7, 4.1 | M | verified | | |
| 5.5 | Housing | 5.4, 2.6 | L | verified | | |
| 5.6 | Cosmetics: transmog, skins, dyes, emotes | 1.4 | M | verified | | |
| 5.7 | Mounts | 2.3, 1.6 | M | verified | | |
| 5.8 | Music | 1.6, 2.5 | S | first pass | | |
| 6.1 | Second player in the same world | 2.4 | XL | verified (remote player record only) | | |
| 6.2 | Chat | 6.1 | M | verified | | |
| 6.3 | Friends, invitations, groups | 6.1 | L | verified | | |
| 6.4 | Trade between players | 6.1, 1.3 | M | first pass | | |
| 6.5 | Companies | 6.3 | L | first pass | | |
| 6.6 | Player against player: flag, duels | 6.1, 3.2, 4.5 | L | first pass (partial file) | | |
| 7.1 | Expeditions and other instances | 3.4, 3.5 | XL | first pass (verified, but not ready: the level change needs a live check) | | |
| 7.2 | World events and arenas | 3.4, 3.5 | L | first pass | | |
| 7.3 | Wars, invasions, Outpost Rush | 6.5, 6.6, 7.1 | XL | first pass | | |
| 8.1 | Game data tools | | M | continuous | | |
| 8.2 | Regression set with no game process | | M | continuous | | |
| 8.3 | Robustness and long sessions | | M | continuous | | |

`R` values: `none`, `first pass` (one agent, static), `in work` (a research batch runs),
`verified` (second method, gate R passed), `done`.

### 2.1 Sequence of the next stages

Decision of the user (2026-10-13): creature spawn and combat come as early as the
dependencies permit, and the social systems (milestone 6) come last. The table below is the
sequence of the work. It replaces the sequence of the summary table for the next stages. A
stage with a letter is a part of the system with that number.

| Order | Stage | Content | Needs | Status |
| --- | --- | --- | --- | --- |
| 1 | 2.3a | Exact position of a character, id rule of the movement state | 0.6b | done |
| 2 | 3.4a | Creatures spawn: the creatures of the start area stand at their spawn points, with name plate and health bar; spawn and remove commands; health and death flag by command | 2.3a | done |
| 3 | 3.2a | The player hits a creature: the hit path of the client, the damage, the damage number, the death of the creature | 3.4a | done |
| 4 | 3.4c | Creatures in the whole world: the creature file for each region (32,840 spawn points, 503 slices in the supported build), a picture check of each slice, the load of the spawn rule, and a travel command of the tool window | 3.4a | done |
| 5 | 3.4b | A creature fights: it goes to the player, attacks, and goes back at the leash distance; the player can die and come back (the necessary part of 2.6) | 3.2a | done |
| 6 | 3.4d | The path search of a creature: navigation data that a tool makes from the collision shapes of the game copy (the terrain, the solid objects, the water), loaded by the backend; a creature goes around a wall and uses stairs; fights that a player can play | 3.4b | done (the user tests the game before the next stage) |
| 7 | 3.4e | Patrol and wander: a creature walks its patrol route or wanders near its spawn point; the reset makes a creature immune on its way back, and it walks back on its way, not in one jump | 3.4d | done |
| 8 | 3.5a | Loot and experience from a kill; the creature spawns again | 3.4b | done |
| 9 | 3.3a | Abilities: the use of an ability of the bar, its cooldown, its damage and its area, and the main status effects; the great axe and the war hammer first (research file `ability-use.md`, live checks L1 to L8 first) | 3.5a | done |
| 10 | 1.3b | Itemization: the perks, the gems, the gear score, and the rarity of an item instance; the effect of the gear on the attributes, the armor, and the damage; the heartrune; the use of potions and food (research file `itemization.md`) | 3.3a | done |
| 11 | 3.3b | Abilities, the shared parts (research file `abilities-shared.md`): part S1 (the status effects of a player), part S2a (the first spell entity: the vortex of Gravity Well; the speed of a status), part S4 (the forced movement: the pull and the push of a hit, the dash of the caster, the counter of Riposte), part S3 (the projectiles of the basic attacks of the bow and the fire staff, and the check of a ranged hit), and part S2b (the spell entities of three kinds: the Pylon of the ice gauntlet, the trap of the musket, the Beacon of the life staff) | 1.3b | S1, S2a, S4, S3, S2b done |
| 12 | 1.1b | The DLC for each player: each account and each character owns the expansion (Rise of the Angry Earth) and the DLC packs of the client data, with no purchase and no setting: the limits of the expansion (level, gear score, trade skills), the flail tree, the content of the expansion (research file `dlc-entitlements.md`) | 3.3b | done (the ownership, the limits, the flail tree, the amulet of a DLC pack; the content of the expansion keeps its own stages) |
| 13 | 3.3b | Abilities, the rest of the build of the user: part S5 (critical hits and the passive events), part S6 (the heals of an ability), and the great axe and war hammer parts M1 (the basic combos, the charged heavy attacks), M2 (the rest of the abilities and nodes of the build), and M3 (the other abilities of the two weapons) (research files `abilities-shared.md`, `abilities-melee.md`) | 1.1b | S5 done (the critical hits, a hit from behind, the headshot of a projectile, and one evaluator of the passive rules of the data); S6 done (one heal path, the heal base of an ability, Divine Embrace, Sacred Ground, the heal pulses of the Beacon, the outgoing and the received heal parts, the number of the owner and the notice of the healer); M1 done (the basic attacks of the eight melee weapons from the attack graph of the data: the light chain with its ender, the partial and the full charge, the fast sub-hits, one hit of each target for each strike; the level part of the heal base; the corrections of the review of S6); M2 done (the abilities of the build of the user are complete: the twelve abilities of the great axe and the war hammer play the fragments of the data with their pulls, pushes, spins, branches, spells, and hit counts, and each node of the two trees acts or is in the list with its cause: grit, block, stamina; the corrections of the review of M1); M2b done (Wrecking Ball hits again: the development character has the build of the user with Breathing Room, and each creature is a homing target and a camera lock target, dataBits 0x03 of its movement state; the corrections of the review of M2; the five findings of the review of M2b, a part of them on the corrections of M2-R1, R6, R7, and R8, are corrected in stage 2.5a: cases RX1 to RX5); the abilities of the build of the user are complete; M3 open |
| 14 | 2.5 | Interaction base: the use of an object or a person (the prompt, the timed interaction, the checks), with an interface for the later systems (research file `interaction-stage.md`): part 2.5a (the objects of the level data and the checked use, the lore pages as the first result) and part 2.5b (the stations of a settlement, the camps, the timed use) | 3.3b | 2.5a done (the request and the reply of the client, the checks of the data, the interaction data of the whole world, the state of an object, the lore pages in the journal of the character, the commands; the corrections of the review of 3.3b-M2b); 2.5b done (the server children: the stations of each settlement at their real places with the forge of each, the prompt and the crafting screen of the client with the Assembly key, the fee flag, the Assembly reference, and the parent settlement of the data in the StationContext; the camp and powder keg fixtures; the timed use on the world clock with one completion; the ends at the removal of the source, the range, and damage; the service handoff base; the corrections of the review of 2.5a); the camp crafting option of the client and the recipes stay open (2.6, 3.7) |
| 15 | 3.4f | Creatures with ranged attacks and abilities: the archers and the casters of the data shoot and cast (a server projectile with its record for each observer, its hit on a player, a miss behind a wall), the dodge of the player against a projectile, and the stamina model that the dodge needs; project rules where the data is silent (the distance band, the choice of melee or ranged, the repeat time); block, encounter abilities, and summons wait (research file `creature-ranged.md`) | 2.5 | 3.4f-A done (the basic shot of the archers of the data: the ranged attacks in the creature data, the projectile of the server with its record for each observer, its hit on a character at the contact, a miss, a wall; the project rules of the band, the choice, the repeat, and the lead; the corrections of the review of 2.5b); 3.4f-B done (the basic cast of the casters, the stamina model of the server from the game data, the dodge with its payment and its immune window, a shot through a dodge, the shield of Maelstrom on a creature shot in the game, the passive rules of the stamina and of the dodge; the corrections of the review of 3.4f-A); 3.4f-C done (the ground areas of six creature spells: the status area with its statuses on the player and the poison damage, the strike with one hit in its window, the ends at the caster; the area attack in the behavior; the maximum 100 of the stamina on the HUD; the corrections of the review of 3.4f-B); 3.4f-D done (the self heal of the Inquisitor Cleric and the Howl of the Evil Knight hound from the status actions of the data, the one path of a status on a creature with its heal ticks, its DMG part, its haste, its dispel, and its due list; the four area spells at the caster with the place rule of the client and the rotation of the caster; the corrections of the review of 3.4f-C); part E (the summons and the encounter abilities) waits |
| 16 | 3.5b | Chests and loot on the ground (research file `chests-ground-loot.md`); after 2.5, because the client handles both as interactions. Three parts (decision of the user through the orchestrator, 2026-10-16): 3.5b.1 the ordinary chests of the world (supply, provision, alchemy, the ordinary elite chests); 3.5b.2 the personal loot bag of a creature (the AILoot bag: the items of a kill go into it) and the discard of a player; 3.5b.3 the special locks, the keys, and the chests of quests and events, after their predicates exist (after 3.6 and 1.8) | 2.5 | 3.5b.1 done (the open with the time of the data, the roll of the root of a chest with the context of its territories and the container floor, the loot claim of each character in the store with its reset, the LootTracker and the Gathering state of the owner, the unavailable chests with the Locked state; the corrections of the review of 3.4f-D); 3.5b.2 done (the personal loot bag of a kill with a part for each character with the credit, the panel and the takes with the stored instances, the private Discarded bag of a drop, the lifetime of the data, the bags in the store; the mana parts of the threshold abilities; the corrections of the review of 3.5b.1); 3.5b.3 waits |
| 17 | 2.2 | World time and weather (research file `world-time-stage.md`) | 3.5b | done (one day and night cycle of the world with the cycle of the client: the request 4248 and the record 7036 with the ids of each own entity, the phase 4247, the clock correction 335 each 3 s; the store of the position with the stop of the cycle while the backend is stopped; the command of the tool window, of the admin interface, and of the development routes; the 405 creature points with a time constraint of the data; the local weather volumes with the shared clock; the mana prediction message 4289 of the client; the corrections of the review of 3.5b.2) |
| 18 | 3.6 | Gathering and fishing (research file `gathering-stage.md`). Two parts (decision of the user through the orchestrator, 2026-10-16): 3.6a the gathering of the resources of the world (mining, logging, harvesting, skinning); 3.6b fishing | 2.5 | 3.6a done (the natural nodes by their offline id and the capital nodes, the tool of the paperdoll and the rank gate, the work with the speed of the tool, the rank, and the effects, the roll of the loot root with the luck, the trade skill experience, the depletion for each observer and the respawn on the run clock of the world, the skinning of a body; the corrections of the review of 2.2); 3.6b waits (order 27: after 3.3c, decision of the user 2026-10-17) |
| 19 | B | The bow build of the user (build sheet `~/Games/soulwarden/work/research/bow-build.md`; decision of the orchestrator: after user test 2). It brings the bow parts of 3.3c forward, only the parts of this build: B0 the build preset and the gear score limit of the artifacts; B1 the three bow abilities (Penetrating Shot, Explosive Arrow, Rapid Shot); B2 the rules of the Boltcaster and the stacks of a status on a target; B3 the haste chain; B4 the other rules of the build (the list in section 3.3, stage B); B5 later (the rapier abilities, the Bile Bomb heart rune, Rain of Arrows) | 3.6a | B0 done (the preset `bow-boltcaster` with the variants PvE and PvP in one task of the world and one transaction, the tool window group "Build presets", the upgrade limit of an artifact to 800, the achievements of the artifact objectives; the corrections of the review of user test 2); B1 done (Penetrating Shot with its aim, the shot item as its confirm, and the pierce; Explosive Arrow with its explosion entity, Contained Detonation, and the burn of Flame Arrow; Rapid Shot with its volley, Rapid Accuracy, and Final Blow; the windows of a bow fragment; the projectile data of format 2; the corrections of the review of stage B0); U3 done between B1 and B2 (the corrections of the creature combat feel, and the corrections of the review of stage B1); B2 done (the stacks of a status on a creature with the count at the name plate, the ApplicationCooldown of a row on each target, the conditions and the effects of the statuses in the evaluator, the Boltcaster with its charges and its bolt, Electrified, Shirking Shock II, Surprise Attack; the corrections of the review of stage U3); B3 done (the haste chain: Dodge and Weave from the dodge status action of the data, Archer's Speed with OnWeaponSwap and its timer, the time factor of a status with Speedy and Alacrity II, Speedy +0.1 while hasted, Closing In, Evasive Tactics, the end of the haste when the bow leaves the hand; the ability data of format 7; the corrections of the review of stage B2); B4 done (the jewelry stat lines with the key CooldownReduction of the stat multipliers, the threshold abilities of the attributes as rules, Long Range, Finishing Shot, the outgoing time factor of Battle Precision, Enchanted Ward with the attack type of a creature hit, Refreshing, Vital Synergy, the potion perks, the cap of the sources of a row; the item catalog of format 4, the stamina data of format 2, the attack type in the creature data; the corrections of the review of stage B3); B5 waits |
| 20 | 3.7 | Crafting, refining, repair, salvage (research file `crafting-stage.md`). Five runs, each with a result that a player can see (the split of the research): 3.7a the fixed crafts at a station and at a camp; 3.7b refining and the reward progress; 3.7c procedural items, charms, and bonuses; 3.7d salvage; 3.7e repair | 3.6 | 3.7a done (the craft request 2997 with the quantities of the recipe, the batch of decision D5 with one transaction for each job, the result with the cause ItemCreated before the progress 2993, the stop 2999, the empty storage search 7046, the fee of the client path, the station variants of decision D1, the crafting data tool; the corrections of the review of stage B4); 3.7b done (refining of each family, the bonus items of the client chance with decision D2, the trade skill experience of the event, the first-craft bonus with its achievement, the cooldown of a recipe on the run clock with the Crafting state 205, the result of a craft-all batch, the crafting data of format 2: 948 recipes; the correction U4-R1 of the review of stage U4); 3.7c done (the craft gear score of the client with the rank, the materials, the craft bonus of the food, gear, and trophy statuses in state 205 field 1 and the caps; the perks of a craft with the one perk generator and the perk items, G, and the push flag; one gem; the swap and the rarity; the perk items and the Azoth upgrade with the station discount; the Azoth balance in state 911 with its cap and the tool window row; the currency ingredients; the crafting data of format 3: 1,917 recipes; the corrections CR7B-R1 to CR7B-R4 of the review of stage 3.7b); 3.7d and 3.7e wait |
| 21 | 5.1 | Shops (research file `shops-stage.md`) | 2.5 | research |
| 22 | 5.6 | Cosmetics and transmog (research file `cosmetics-stage.md`) | 5.1 | research |
| 23 | 1.8 | The generic quest engine: each quest from the quest data of the game, by the type of its tasks (not one quest at a time); it replaces "the original quest state of a new character" (research files `quest-engine.md`, `quest-engine-3.md`: 1.8a, 1.8c, and 1.8d ready with live checks; 1.8b needs the person base of 5.1). Decision of the user (2026-10-21): "start the quest implementation on the backend": 1.8a now, before 5.1 and 5.6. Five parts: 1.8a saved quests, achievements, the start grant, the HUD pin; 1.8b persons, offers, hand-in, topics, cutscenes, whispers, trigger areas; 1.8c counters, items, quest drops, gathering, crafting, POI and kill predicates; 1.8d rewards, availability, timers, recurrence; 1.8e content closure and acceptance | 5.6 | 1.8a done (the quest data tool with the compiled trees of the client index rule; the quest system with the rules of the containers, the achievement actions, TaskAchievement with the non-persisted achievements, and TaskTimer on the world clock; the quests, the pins, the grants, and the undo rows in the store, schema 15; the start quest of a new character on the Windsward beach, one time, with its archetype bit; the Objectives state and the readiness state after the first 2709 with the task elements in index order; the pins of the client; the lock of a bit with the empty list; decision D2 option C, the backstory templates by command); 1.8c done (the events of the other systems on one path: the kills with the vitals categories and the credit list, the items of the bag and the paperdoll, the equipment, the gathers and the quest objects of Gatherable_Quest, the crafts and the refines, the places from the shapes of the points of interest (new file `poi-data.json`), the screens of 2617, TaskObjective, TaskDie; the quest drops, AwardOnlyOnce (schema 16), the regression of a StayActive task, the abandon 3836, the reset 5600, and the failure; a task index for each character; the corrections of the review of 1.8a; live checks K1, G1, POI1, the abandon and the reset of the journal); 1.8b waits for the person base of 5.1; 1.8d and 1.8e wait |
| 24 | BT | Creature behavior from data: behavior trees in JSON files with a default tree for each creature, and custom trees for bosses and mini bosses (plan `behavior-tree-plan.md`): BT0 research of the AI data of the client, BT1 the engine and the default tree with the behavior of now, BT2 the binding of a tree to a creature, BT3 the boss nodes and the first mini boss (decision of the user 2026-10-17: after the quests) | 1.8 | plan |
| 25 | 3.3c | Abilities of the other weapons: the sword and shield, the rapier, the hatchet, the spear, the greatsword, the flail (melee parts M4 to M9b), the bow, the musket, the blunderbuss (ranged parts R1 to R7), the fire staff, the life staff, the ice gauntlet, the void gauntlet (magic runs M1 to M9); the dagger is not in it: the client cannot use its abilities (research files `abilities-melee.md`, `abilities-ranged.md`, `abilities-magic.md`) | BT | research |
| 26 | 3.6b | Fishing (research file `gathering-stage.md`; decision of the user 2026-10-17: "lets deprioritize fishing") | 3.6a | research |
| 27 | MOD | The modding framework in the form of SKSE and F4SE, first the native UI of the game (LyShine), not the tool window (research file `modding-native-ui.md`): P1 a native screen at run time from the elements of a game canvas, with a Lua bridge of the hook, a mod loader, and the mod routes of the backend; P2 a canvas in a mod pak. Native plugins after P2. Request of the user 2026-10-17; decision of the user 2026-10-17: "deprioritize modding" | 3.6b | research |
| 28 | 1.9 | Acceptance test of milestone 1, with the regression of all stages before it | MOD | open |

Decisions of the user (2026-10-14): the abilities come directly after loot and experience
("prioritize the abilities", "after loot and experience"), and itemization after the
abilities. The quests are one generic engine that reads the quest data of the game, not one
stage for each quest: stage 1.8 is now that engine.

Decisions of the user (2026-10-15): after the abilities and the items are complete ("close
abilities and items"), the interaction base, the world time and weather, gathering,
crafting (3.7), and the shops come, and then the cosmetics and the transmog. The Go port
comes at the end of the roadmap ("put port at the end"). Decisions of the orchestrator in
this order: the chests and the loot on the ground (3.5b) come directly after 2.5, because
they need it; the quest engine (1.8) comes after 5.6, because the user gave it no place
and it uses the events of the systems before it. A research thread for each stage of the
rows 11 to 19 started on 2026-10-15.

Decisions of the user (2026-10-15, later): "i want the full game with dlc. for any player, auto unlock the dlc" (row 12), and "use your best judgement for the rest". Decisions of the orchestrator from that: the build of the user first (rows 11 and 13), then the interaction base and the rest of the order, and the other weapons after the quest engine (row 22); the critical hits and the heals of an ability use approximations from the inputs of the game data, because the formulas were only in the original server (research file `crit-heal-formulas.md`): an independent roll for each hit with the chance of the data, one additive pool of critical bonuses on the multiplier of the weapon, and a heal base that scales with Focus as damage scales with its attribute; each one is a project rule in the document of its system. The user first asked for a pause after row 13 ("when done with abilities stuff, pause the implementation") and then cancelled it ("ignore the pause command and keep working even when you finish abilities"): the work continues with row 14.

Decisions of the user (2026-10-17): the creature behavior trees come after the quest engine ("this should be implemented after quests", row 24); fishing comes later ("lets deprioritize fishing": row 26, after the other weapons); a modding framework in the form of SKSE and F4SE, first the native UI of the game (row 27; "deprioritize modding": after fishing). Decisions of the orchestrator: the bow build of the user (row 19) after user test 2; stage U3, the corrections of the creature combat feel from the research `creature-combat-feel.md` (the homing of a creature attack, the reaction of a side or of the back, the length of a reaction from its hit stun, the chain count of the data, the weight limit of a character, the push scale of a promoted elite, the time of a weapon switch), between B1 and B2 of row 19; 3.6b and the modding framework before 1.9.

Decision of the orchestrator (2026-10-16, from the priority of the user: combat first): part 3.3b-M2b found that no creature makes a ranged attack (the creature data tool keeps only the melee attacks), so stage 3.4f (row 15) comes directly after the interaction base, before the chests.

After stage 1.9, the sequence is the sequence of the summary table, with these changes:
the other parts of the systems 3.1 to 3.7 come before milestone 4, and milestone 6 comes
after milestone 7. The Go port (P.1, P.2) comes after all of them.

Consequence for the Go port: all systems of the roadmap are in C++ first, and the port has
them as reference code. The port is then larger, and the C++ backend stays in the
repository until its end. The user can change this.

## 3. Milestone 0: foundation

Systems 0.1 to 0.5 are complete. The client starts, gets its HTTP replies, connects through
DTLS, registers, and enters the world as the local player. The W key moves the character, and
the backend saves the position. The protocol notes have the contracts. Known limits are in
milestone 2 (systems 2.2 and 2.3).

### 0.6 World and session model for more than one player

This work comes after 1.2 and before 1.3. Each later system adds player state: the state
must go into the correct place from its first version. The work has two stages, 0.6a and
0.6b, because the research found that one stage is too large.

- Research (batch A, verified on 2026-10-12, each file "ready with live checks"):
  `multiplayer-foundation.md` (audit and design), `ids.md` (id rules), `entities.md` (other
  entities and the remote player).
- Condition before stage 0.6a: the transport accepts more than one client, and the database
  has the persona id and the character id as keys. Each of two clients gets its own character
  as the local player, and gets nothing about the other one (a test program with the backend
  sources proved this). The faults that the audit found, and their condition after stage
  0.6a:
  - The registration accepts each character id: an id that the store does not have, a
    character of a different account, and a character that is in a session already.
    Repaired in 0.6b.
  - A session ends only when the secure session closes. When the user leaves the world, the
    session stays. The position of the old session can then replace a newer one at the stop
    of the backend. Repaired in 0.6a.
  - A client that stops with no close message gets more data each minute with no limit.
    Repaired in 0.6a.
  - The actor id, the interest number, and the ids of the self identification are constants.
    Repaired in 0.6b.
  - Captures and log lines do not name the session. Repaired in 0.6a.
  - An exception in the listener thread stops the process. Repaired in 0.6a.
  - A fault that the audit did not find, and that the first live check of 0.6a showed: a
    client that leaves the world to the main menu cannot enter it again in the same game
    process, because each session sends the same level information version and the same
    client context instance. Repaired in 0.6a (protocol notes 3.5).

#### 0.6a Session model

- First item, tools only: a development run and the play of the user are separate. A
  development run uses its own Proton prefix, and `tools/dev-stop.sh` stops only the
  processes of development runs. Thus an agent test does not stop a game that the user
  started from Steam, and the start from Steam does not stop an agent test.
- Implementation: the build has a library, the backend program, and a test program
  (`tools/test-soulwarden-backend.sh`). The test runs two sessions with no game process and
  compares the bytes of one session with a stored file. A world clock replaces the direct
  clock calls. Each session has an id, its own capture folder, and a mark in each log line.
  A session ends on a carrier close, after 30 s with no datagram, and after 30 s with no
  registration, and each end saves the position. The world object starts with its clock, its
  task queue, and its timers; an exception in one tick does not stop the process.
- First live checks (one client): what the client shows for a refused registration, for the
  message "rejected" (type 1630), and for the message "kicked" (type 271); what the client
  sends when the user leaves the world.
- Test: the test program passes; no byte for one client changes (the stored file proves it);
  leave the world, enter again, stop: the row has the last position; a game process that is
  stopped for 40 s gives the end cause "idle"; all criteria of 1.1 and 1.2 pass again
  (screenshots).
- Result (implementation complete on 2026-10-13; the independent check is the next step):
  - Tools: a development run has the Proton prefix `~/Games/soulwarden/proton-dev/1063730` and
    the mark `SOULWARDEN_DEV_RUN=1` in each process. `tools/dev-stop.sh` stops only processes
    with the mark.
  - Build: library `soulwarden_backend_core`, program `soulwarden-backend`, test program
    `soulwarden-backend-tests` (`tools/test-soulwarden-backend.sh`, 18 cases). Case T2 compares the
    datagrams for one session with `apps/backend/tests/golden/one_session.hex`, which has
    the bytes of the backend from before the stage.
  - Seams: `world::WorldClock` in place of each direct clock call of the game server, and
    `service::DatagramTransport` between `RepServer` and the DTLS listener.
  - Sessions: session id, capture folder `captures/rep/sNNNN`, log mark ` [sN characterid]`,
    capture limit and log limit for each session.
  - World object (`apps/backend/src/world`): clock, task queue (`post`, `call`), timers
    (`after`). `RepServer::on_tick` calls `World::tick`. With `--no-rep-listener` the world
    has its own thread. An exception in a task, a timer, a session tick, or a message
    handler goes to the log and ends at most one session.
  - Session end causes: carrier close, secure session closed, idle (30 s), no registration
    (30 s), error, backend stop. Each end saves the position one time, and the next tick
    erases the session. Only the newest session of a character writes its position row.
  - Each session has its own level information version and client context instance: the
    user can leave the world and enter it again.
  - Results of the live checks for stage 0.6b (protocol notes 3.6): registration result
    -160 gives a popup about the login ticket; "rejected" (type 1630, reason 2) gives the
    popup "Player is already connected"; "kicked" (type 271, reason 1) gives the popup
    "Your account has connected from another game session". In each case the client closes
    the connection itself and does not try again. A DTLS close notice of the server makes
    the client do a new DTLS handshake: do not send it.
  - Not done in 0.6a, by the scope of the stage: the entity table, the encoders, the
    observer, the refusal of a registration, the kick, the login ticket, the id allocator
    (0.6b); the wait for the HTTP threads at the stop of the backend (necessary when the
    first HTTP route calls the world).
  - Evidence: `~/Games/soulwarden/work/evidence/session-model/implement/` (`RESULT.md`).

#### 0.6b World model

- Implementation: the world object owns the entity table (entity id, actor id, character id,
  owner session, position, component states with a change mark). `RepSession` is an observer:
  it has only transport state and a table of the entities that it knows. One encoder for
  each component state makes the owner view or the observer view. The registration refuses a
  character that the account does not own. A character is in one session at most: the newer
  session wins, and the old one gets the message "kicked". The ids of an entity come from an
  allocator (rule of `ids.md`: the first byte of an actor id is even, and no two instances
  get equal entity ids). The entity table has a position index and two interest distances.
- First live checks: a player actor id that is not the present constant (the proof of the id
  rule), then the record of a remote player (checks L1 to L18 of `entities.md`).
- Test: the test program has the cases T1 to T10 of the research; world entry and movement
  work with an allocated actor id (screenshot); the live client sees a second character of
  the database as a remote player that stands at a set position, and the entity goes away
  after a stop record (screenshots). If the remote player does not show after the live
  checks, the stage records the results in the protocol notes, and this item moves to 2.4.
- Not in these stages: a second game client, the movement of a remote player for an
  observer, chat, groups. These stay in milestone 6.
- Result (implementation complete on 2026-10-13):
  - Tools: the development data directory `~/Games/soulwarden/work/dev-data` is the default of
    a development run and of each development tool (`tools/dev-env.sh` has the default).
    A start of the user keeps `~/.local/share/soulwarden`.
  - World: the entity table (`apps/backend/src/world`), with one entity for each character
    that is in the world, the id allocator, the position index, and the position save. A
    session is an observer with an interest table. One encoder for each of the five
    component states makes the owner view or the observer view
    (`apps/backend/src/server/rep/states`).
  - Sessions: the world refuses a character that the store does not have and a character
    of a different account (registration result -160). A character is in one session at
    most: the newer session wins, and the older one gets the message "kicked" (reason 1).
    The delete route removes the entity of a character that is in the world first (the
    session gets "kicked" with reason 2), in one task of the world. The stop of the backend
    waits for the HTTP connection threads.
  - Ids: each entity, each session, and the player manager have an actor id from the
    allocator (`serial * 2`, a tag of 4 bytes, kind, the tag again; the tag is `"SOUL"` now). Live proof: world entry and movement
    with the new ids, and each target of 55,211 client messages in three sessions agrees
    with the id rule (protocol notes 4.2). The backend checks the route and the target of
    each client message with the ids of the player slice (`tools/gamedata/slice_ids.sh`).
    The stored bytes of test case T2 changed only in the ids (145 bytes of one datagram).
  - Remote player: the live client shows a second character of the database as a remote
    player with a body and a name tag at a set position (`tools/world spawn`), and a stop
    record removes it. Difference from the research: the record of the research gave no
    body. The movement state of an observer also needs the action of layer 0
    (`slayerSequenceId` and `slayerScriptLayers`; protocol notes 4.9 has the result of each
    live check). The client does not move a remote character to the ground.
  - Interest: with `--observe-players` (off for a start of the user, on for a development
    run), a session observes the entities that are 100 m or less from its character and
    stops them at more than 120 m.
  - Tests: `tools/test-soulwarden-backend.sh` has 30 cases (the cases of 0.6a and the cases N1,
    T3, T4, V1, T9, T10, S1, D1, P1, E8, W6, W7).
  - Review of stage 0.6a: its five findings are corrected in this stage (the stop of the
    world thread from one of its tasks, the owner thread id after a stop, the lock of the
    data directory during the removal in `tools/dev-run.sh`, the harness gate in the
    backend program, the identity tool with more than one account).
  - Not done, by the scope of the stage: a second game client, the movement of a remote
    player, the login ticket, an actor id of a character that stays between world entries.
  - Evidence: `~/Games/soulwarden/work/evidence/world-model/implement/` (`RESULT.md`).

## 4. Milestone 1: the character

Goal: the player starts the game with their Steam account, makes a character, enters the
world, has items, wears and holds equipment, has a level and skills, changes all of this in a
tool window, and a new character starts with the original first quest.

### 1.1 Steam account identity (complete)

- Result: the Steam id selects the account row. Stub authentication. Accounts are in SQLite.
- Evidence: `~/Games/soulwarden/work/evidence/steam-account/`, protocol notes 2.1 and 3.4.

### 1.1b The DLC for each player (done)

- Decision of the user (2026-10-15): "i want the full game with dlc. for any player, auto unlock the dlc". Research file
  `dlc-entitlements.md`. Evidence `~/Games/soulwarden/work/evidence/dlc/implement/`, protocol notes 4.29, document
  `docs/systems/dlc-and-entitlements.md`.
- The live checks came first (run `dlc-l1a` with the backend of stage 3.3b-S2b and `tools/world fragment`, run `dlc-l1d`
  with the routes and no state): the client needs the achievement `Expansion_2023_Unlock` of the character (slot 66, type
  3210); the entitlement bit of the expansion (slot 19, type 3133) alone and the entitlement routes alone open nothing that
  a player sees. With the achievement the level window at 60 shows 96,451 XP to level 61, the flail tree has its button
  "Assign", the trade skill limit is 250, and Riding has no lock. The level limit of the client is 70 (the probe of
  `0x3CED4B0`); the level override of the client is 0. The installed-DLC request of Steam (`0x647AB30`) and the DLC
  enumeration did not run.
- Result: each account owns the 19 entitlements of the data (the expansion and the nine Steam roots of the DLC packs with
  the entitlements that they give; rows written at the login, the sync, and the world entry; the routes of the entitlement
  service with the owner check of the token), each character the achievement of the expansion (before its first record),
  and the two owner states. The limits of the owned expansion: the level 70 (a level 60 character keeps its level and its
  experience), the rank of each skill with an expansion value (the trade skills and Riding 250), the equip gear score 800;
  FoodCONT5 works at level 65. Each character gets one AmuletIsabellaT1 (the reward of a DLC pack) one time. The
  progression tables have the format 2 (the expansions, the entitlements, the achievements), schema version 8 (the tables
  `entitlements`, `achievements`, `entitlement_rewards`). The stage also corrected the twelve findings of the review of
  stage 3.3b-S2b (cases RK1 to RK12; each case fails on the tree of stage 3.3b-S2b): the mute of a development run through a
  link, the wall before a near hit, the Beacon on a dead target, two Beacons with one status row, a status row shorter than
  the Beacon, a friend that leaves, the record of an attached Beacon, the vitals row of a summon, the status rows of an area
  at the press, a spell shot straight down, and two tools that keep the old file for a source that is not complete. Test
  cases DL1 to DL10 (DL2: two accounts and two characters of one account), RK1 to RK12.
- Open: the content behind the expansion (the region Elysian Wilds and its story, expeditions, mounts, recipes, quests:
  their own stages, DL12 of the research), the use of the rewards of the DLC packs other than the amulet (skins, emotes,
  crests, the housing item, the title, the art book: their systems), the item level of a gear score in the backend (the
  client checks it).

### 1.2 Character creation and deletion (complete)

- Result: the four steps of the creation, the name check with the name rule of the client,
  the stored creation data, the world entry with the selected body, a second character, the
  body of each character in the menu, and the deletion work. Three independent live checks
  passed all 12 criteria (2026-10-13), with two fix rounds between them.
- Evidence: `~/Games/soulwarden/work/evidence/character-creation/` (`check-2` is the last
  check), protocol notes 2.2.
- Open: the saved position is an estimate from the camera. With the camera farther than
  4 m it can be 4 m from the character, and a walk of less than 2 m from the spawn is not
  saved. Stage 2.3a removes the cause.
- Left for later systems: clothing in the world (1.4), the start position and the first quest
  (1.8).

### 1.3 Inventory and currency

- Client evidence: `Container` (slot 61, state type 1755), `Currency` (slot 60, type 3786),
  `ItemManagement`, `Inventories`, `ItemDrop`. Messages: use 2917, drop 1838, salvage 1733 and
  6188, lock 2549, transfer 2801 and 2942.
- Research: `inventory.md` (verified). First live checks: the item index list of the client
  (46,632 entries from 43 tables), the transmog flag, the size limit of one bundle.
- Implementation: item instances in SQLite; the container state; move, split, stack, drop,
  and lock; coin; the item catalog tool (`tools/gamedata`); the items of the archetype for a
  new character (done in stage 1.4).
- Test: the bag window shows the given items with the correct icon, name, and quantity; a
  move and a drop stay after a restart; the coin number is correct.
- Result (implementation complete on 2026-10-13):
  - Item catalog: `tools/gamedata/items.sh` makes `item-catalog.json` (46,631 items of 45
    tables, each with its index on the wire). The list of the live client (46,632 entries,
    read from its memory with `tools/research/item_index.py`) is equal to the list of the
    tool in each entry. The backend reads the file at its start and gives no item without it.
  - State: the bag is a vector of slots in the entity of the character (`world/item.h`), and
    the inventory system (`world/inventory.h`) is the only code that changes it: give,
    remove, move, swap, stack, split, lock, drop, and the coin. Each change is one
    transaction on the tables `items` and `currencies` (schema version 5), with the
    character id as the owner key. A request that is not correct changes nothing.
  - Wire: encoders for the container state (slot 61, type 1755) and the currency state (slot
    60, type 3786), for the owner only. The first record has the full vector; after that a
    session gets one journal entry for each slot that changed. A session cuts a record
    between two fragments so that each bundle is one carrier message: a bag of 28 items
    went to the live client in three bundles.
  - Client requests: drop (type 1838) and lock (type 2549), each one with a live capture.
    Difference from the plan: the client has no request to move, to swap, to stack, or to
    split an item inside the bag (its bag window sorts the items itself). These four
    operations are in the inventory system and in the development commands only.
  - Development commands: `tools/world items`, `give`, `take`, `coin`, `move`, `split`,
    `lock` (routes `/v1/dev/items`), for a character that is in the world or not.
  - Level: the bag window of the client needs the level of the player. Without it the
    script of each item cell stops with an error (no tooltip, no action menu, no equipment
    panel). Thus the backend sends the progression state (slot 70, type 899) to the owner
    with level 1 for each character. System 1.5 adds the level system to this encoder.
  - Harness: the command `drag` (a pointer drag), and `tools/research/script_errors.py`
    (the script errors of the client from a trace).
  - Review of stage 0.6b: its four findings are corrected in this stage (the stop of the
    HTTP listener joins each connection thread; a world task that did not start in its time
    limit is cancelled; the world refuses a position that is not finite or outside its
    limits; a session cuts a record at a fragment border and sends no bundle above the limit
    of the client).
  - Tests: `tools/test-soulwarden-backend.sh` has 44 cases (the 30 cases of stage 0.6b and the
    cases B1 to B9, H1, D2, P2, F1, T11). The stored bytes of case T2 changed only in the
    first record: three fragments more (43 bytes).
  - Not done, by the scope of the stage: use, salvage, repair, and transfer requests; an
    entity for an item on the ground (a drop removes the item); perks and dyes of an item;
    the weight limit as a rule of the server; the items of the archetype for a new character
    (the creation data has them; they need the equipment of system 1.4).
  - Evidence: `~/Games/soulwarden/work/evidence/inventory/implement/` (`RESULT.md`).

### 1.4 Equipment and visible gear

- Client evidence: `Paperdoll` (slot 62, type 3183). Messages: equip 3895, unequip 3896,
  failure 3626.
- Research: `equipment.md` (verified). First live checks: a fragment with only the chest
  item, the target ids.
- Implementation: equipment slots in SQLite; equip and unequip between bag and paperdoll;
  the look of the items on the body in the world and in the menu; a weapon in each weapon
  slot; gear score.
- Test: the character wears the items in the world (front view) and in the equipment window;
  the state stays after a restart; a wrong equip request gives the failure message, not a
  disconnection.
- Result (implementation complete on 2026-10-13):
  - State: the equipment is a map from the slot number of the paperdoll to a slot in the
    entity of the character (`world::Equipment`). An equipped item is a row of the table
    `items` with the container 2: no new table and no new schema version. The inventory
    system (`world/inventory.h`) has equip, unequip, and the swap of two slots; a move
    between the bag and the paperdoll is one transaction, and the item keeps its instance
    id. The slot of an equip must take the class of the item (the table of the client check,
    `world/item.cpp`).
  - Wire: the encoder of the paperdoll state (slot 62, type 3183) with the two views. The
    owner gets `isLocalPlayer` and each item with its slot and its durability. An observer
    gets `visiblePaperdollSlots` and `visibleItemVisualData` for the slots with a look (armor
    0 to 4, weapons 24 to 29), and the two maps. Difference from the research: the lists of
    a fragment are changes of slots, not a full state. Thus a fragment has only the slots
    that changed, and the research field `visibleItemVisualData` ("not decoded") has its
    form now (protocol notes 4.11).
  - Client requests: equip (type 3895), unequip (type 3896), and the swap of two slots (type
    3900), each one with a live capture, and the failure reply (type 3626), which the live
    client accepts with no disconnection (a run with the research switch
    `refuse-equipment=1`).
  - Visible gear: armor shows on the own character and on a remote character; a weapon hangs
    on the hip (at the world entry, on the inventory screen, and for an observer) or is in
    the hand (the client of the owner after an equip); the HUD shows the active weapon. The
    backend has no draw and no sheathe: each weapon has its sheathe bit, and the first
    weapon slot with an item is the active one.
  - A new character: the create route gives it the items of its archetype that have a
    quantity in the backstory table of the game (four armor pieces and one sword), in the
    transaction that inserts the character. `tools/gamedata/items.sh` writes the lists of
    the 329 backstories into the item catalog.
  - Development commands: `tools/world equip`, `unequip`, `equipment` (routes
    `/v1/dev/items/equip`, `/v1/dev/items/unequip`); `tools/world items` lists the equipment.
  - Review of stage 1.3: its five findings are corrected in this stage (a drop request does
    not remove a locked item; the catalog tool refuses an input that is not complete; a
    delete whose write failed is not "character not found"; the quantity limit of a lock
    request; a spawn offset that is not a number).
  - Tests: `tools/test-soulwarden-backend.sh` has 57 cases (the 44 cases of stage 1.3 and the
    cases G1 to G8, L1, D3, P3, B10, B11). The stored bytes of case T2 changed only in the
    first record: the paperdoll fragment of a character with no equipment (6 bytes).
  - Not done, by the scope of the stage: the draw, the sheathe, and the change of the active
    weapon as actions; the attack actions; the level and attribute rules of an item and the
    attribute numbers of the panel (1.5); perks, durability loss, repair, dyes, skins,
    loadouts; the equip load as a movement rule; the ammunition slots; the body of the
    character in the main menu after a change of equipment (stage U4 added it: the menu
    document with the armor of the paperdoll); the head armor and the weapons
    of an archetype, which the game data gives with the quantity 0 at the start (systems 1.8
    and 4.3).
  - Evidence: `~/Games/soulwarden/work/evidence/equipment/implement/` (`RESULT.md`).

### 1.5 Level, experience, attributes

- Client evidence: `Progression`, `ProgressionPoint`, `Attribute`.
- Research: `progression.md` (verified). Rules: the level on the wire is the number on the
  screen minus 1; an attribute is 5 plus the spent points; the client computes the free
  points.
- Implementation: level, experience, and attribute points in SQLite; the requests to spend
  and to reset points; health from the level and the constitution. Stage 1.3 added the
  encoder of the progression state (`server/rep/states/progression_state.cpp`) with the
  constant level 1 for the owner, because the bag window of the client needs a level: this
  stage gives it the stored values and decides the view of an observer.
- Test: the HUD and the character window show the set level and attributes; spent points stay
  after a restart.
- Result (implementation complete on 2026-10-13, in one work with stage 1.6):
  - Game data: `tools/gamedata/progression.sh` makes `progression-tables.json` (the level
    table with 70 levels, the health of the constitution, the base vitals of a player, and
    the tables of stage 1.6). The largest level of a player is 60: the client has no
    expansion with this backend (it shows "Rise of the Angry Earth needed to advance" for a
    higher level). With no file the backend sends the stored values and refuses a command
    that needs a table.
  - State: the level, the experience, and the points that a character spent on each
    attribute are in its entity and in the table `character_progress` (schema version 6).
    The progress system (`world/progress.h`) is the only code that changes them: set the
    level, give experience with the level gain at the limit, set an attribute, spend, and
    reset. The free points and the maximum health are computed (health: 856, plus 50 for
    each level above the first, plus the health of the constitution table; the sum is a rule
    of this project, the three values are game data).
  - Wire: the progression state (slot 70, type 899) has the stored level and experience,
    and an observer gets the level (live: the name tag of a remote player shows "30
    Remote"). New: the attribute state (slot 27, type 129), for the owner only. The bytes
    are the bytes of the examples of the research.
  - Client requests: spend (type 1502) and reset (type 2409), each one with a live capture.
    The client has no reply message: the attribute state is the answer, also after a
    refusal. The live client shows the level notice of its own for a larger level.
  - The level of a character is in its menu document too: the character list of the main
    menu shows it.
  - Development commands: `tools/world progress`, `level`, `xp`, `attribute` (routes
    `/v1/dev/progress`), for a character that is in the world or not.
  - Review of stage 1.4: its one finding is corrected. The create route makes a character
    only for a backstory of the character creation (the member `creationBackstories` of the
    item catalog: the seven archetypes and the default backstory of the client), and it
    gives the reply 400 for a different one (test case G9; the two other cases of the
    review are G10 and G11).
  - Tests: `tools/test-soulwarden-backend.sh` has 75 cases (the 57 cases of stage 1.4 and the
    cases G9 to G11, A1 to A8, K1 to K7). The stored bytes of case T2 changed only in the
    first record: three fragments more (101 bytes: the attribute state and the two list
    states of the skills with no entry).
  - Not done, by the scope of the stage: a source of experience in the game (a kill, a
    quest, gathering: these systems call the experience function later); the bonus of an
    attribute from equipment and food; the effects of an attribute other than the health;
    the bonus level and the rested experience; the levels 61 to 70 (the expansion); the
    tool window (1.7).
  - Evidence: `~/Games/soulwarden/work/evidence/progression/implement/` (`RESULT.md`).

### 1.6 Weapon mastery, ability bar, trade skills

- Client evidence: `CategoricalProgression` (slot 65, type 911), slot 67 (type 3681), the
  ability bar (slot 46, type 185). Messages 1112, 2857, 2701, 186, 2858.
- Research: `skills.md` (verified). Trade skills: maximum 200, or 250 with the expansion.
- Implementation: mastery level and points for each weapon; the ability tree selections; the
  three ability buttons; the level of each trade skill.
- Test: the mastery window and the trade skill window show the set values; a selected
  ability is on the bar; the values stay after a restart.
- Result (implementation complete on 2026-10-13, in one work with stage 1.5):
  - State: the rank and the experience of each weapon mastery and trade skill, the nodes of
    each weapon tree, and the three abilities of each bar are in the entity of the character
    (`world::SkillSet`) and in the tables `progressions`, `ability_nodes`, and `ability_bars`
    (schema version 6). The skill system (`world/skills.h`) is the only code that changes
    them. A weapon has one mastery point for each rank (a rule of this project). The game
    data file of stage 1.5 has the rank tables (16 weapons, 20 trade skills) and the 611
    nodes of the weapon trees with their conditions.
  - Wire: encoders for the three states, for the owner only: the categorical progression
    state (slot 65, type 911), the progression point state (slot 67, type 3681), and the
    ability state (slot 46, type 185), which has one element for each of the 29 ability
    tables of the client. Difference from the research: the client makes its own list of 29
    empty bars at its world entry (live: a memory read), thus a character with no ability on
    a bar gets no ability state in its first record. The sequence of the 29 tables is not in
    the game files: it is a constant of the client build, read from the live client in two
    runs (`tools/research/ability_tables.py` compares it).
  - Client requests: a mastery point (type 1112), an ability for a place of the bar (type
    2701), the end of a selection (type 2857), and the tree reset (type 186) with its
    answer (type 2858), each one live. The backend has the conditions of a node of the
    client (the node that it needs, a point in the row before it, 10 points for the last
    row). A reset with a cost (rank 10 or more: the currency "Azoth") is refused.
  - The live client shows the mastery level and the points in the mastery window, the nodes
    in the tree, the abilities on the bar of the HUD for the weapon in the hand, the notice
    for a new mastery level, and the trade skill levels in the trade skill window (largest
    level 200: no expansion).
  - Development commands: `tools/world mastery`, `tradeskill`, `skillxp`, and `progress`
    lists the weapons with their nodes and bars and the trade skills.
  - Not done, by the scope of the stage: the use of an ability and its cooldown (3.1 to
    3.3); the effects of a passive node; experience from combat and from gathering (the
    systems call the experience function later); the currency "Azoth"; the territory
    standing, the faction reputation, and the season track, which are entries of the same
    state; the trade skill levels above 200 and the riding skill (the expansion).
  - Evidence: `~/Games/soulwarden/work/evidence/progression/implement/` (`RESULT.md`).

### 1.7 Tool window and admin interface

- Research: `overlay.md` (verified). The client draws with Direct3D 12. The window is Dear
  ImGui in the hook DLL. Key F8. The backend has admin routes on loopback with a token that
  the launcher makes for each start.
- Implementation: the window; the routes `GET /admin/v1/state`, `POST /admin/v1/commands`,
  `GET /admin/v1/catalog/items`; commands to give and remove items, set the coin, set the
  level and the attributes, set mastery and trade skills. Each later system adds its
  commands.
- Test: F8 shows the window in the game; each command changes the game state in the same
  session (screenshot before and after); the game input works again when the window is
  closed; a request with no token gets no data.
- Result (implementation complete on 2026-10-13):
  - The window. The hook DLL has Dear ImGui 1.92.6 (`apps/game-hook/vendor/imgui`) with its
    Direct3D 12 part and the key table of its Win32 part. The hook is at the present call
    site of the client (image address `0x8D445C`, the submission thread of the renderer),
    not in the `Present` function: with NVIDIA Streamline, which is active with Proton on
    the test computer, that function is a function of `sl.interposer.dll`. The window draws
    with the native device and the command queue of the game. The key F8 shows and hides it
    in each start (`SOULWARDEN_TOOL_WINDOW_KEY` selects a different key, `SOULWARDEN_TOOL_WINDOW=0`
    turns the tool window off). A failure of a part of its start turns it off for that start
    with one log line, and the game continues.
  - The input. A detour of the root window procedure keeps the pointer, the keys, and the
    raw input packets from the game while the window shows, and puts them in a queue for the
    present thread. The game never gets F8. The pointer guard holds `SetCursorPos` and
    `ClipCursor` of the game.
  - The admin interface: `GET /admin/v1/state`, `POST /admin/v1/commands`, `GET
    /admin/v1/catalog/items` (protocol notes 6.4). The launcher makes a random token for
    each start and gives it to the backend and to the hook in their environments. The token
    is for the account of the Steam id of the start: a command names a character id, and a
    character of a different account gives the reply 404. The commands: give and remove an
    item, set the coin, set the level, give experience, set an attribute, set a weapon
    mastery level, set a trade skill level. The admin routes and the development routes
    have one layer (`server/http/character_commands.h`).
  - The window has a character selector (for an account with more than one character) and
    the tabs "Items" (catalog search, quantity, give; the bag with a remove action),
    "Character" (level, experience, coin, attributes), and "Skills" (weapon mastery, trade
    skills). The last line shows the result or the error text of each command.
  - Differences from the research (`overlay.md`): the hook point is the call site and not a
    detour of `Present`; no hook of the swap chain creation is necessary (the first present
    call makes the renderer); the swap chain of the present call gives 2 buffers and the
    record 4, and the flags are `0x840`; a command names a character id and the token names
    the account (the research had "the session in the world"); the item search uses the
    item catalog of stage 1.3 (ids, types, classes), not a new catalog file with name texts.
  - Corrections of the code review of the stages 1.5 and 1.6 (six findings), and two faults
    that the live checks of this stage found: the client replaces its tree node list with
    the list of a point state fragment, thus the point state is one fragment for each size;
    and the client closes its connection for a datagram above 1,038 bytes, thus the carrier
    has that limit (it had 1,200).
  - Not done, by the scope of the stage: commands for systems that the backend does not have,
    a tool window for the main menu, saved window settings, name texts of the items in the
    search. Not checked: HDR, frame generation, a picture size other than 1256x660 (the
    larger scales of the layout were checked with a forced scale).
  - Evidence: `~/Games/soulwarden/work/evidence/overlay/implement/` (`RESULT.md`).

### 1.8 Original quest state of a new character

The generic quest engine of row 23 replaces this system. Its part 1.8a (2026-10-21) does what this section asked: a new character
enters the world on the Windsward beach (the position of the start quest), with `06_getoffbeach` active, tracked, and pinned on the HUD
with its task line ("Talk to Aelstrom on the beach"); its quest state is in SQLite. The progress of the beach quest (the trigger, the
cutscene, the hand-in at Aelstrom) is part 1.8b ([Quests](systems/quests.md)).

- Client evidence: `Objectives` (slot 22, type 3857), `ObjectiveInteractor`.
- Research: `quests.md` (batch B, verified), `quest-engine.md`, `quest-engine-3.md`, `quest-display-18a.md`. The HUD pins a quest only
  when the seasons code of the client is ready: the state of slot 73 (type 5606) after the first 2709 (live check L1 of stage 1.8a).
  Decision of the user: the beach quest (`06_getoffbeach`) first, not the introduction (`ftue_v2`).

### 1.9 Acceptance test from an empty database

One scenario script goes from an empty data directory to a character with items, equipment,
level, skills, and the first quest, with a screenshot of each step. An independent agent
compares each system of milestone 1 with its criteria.

### Go port: stages P.1 and P.2

Decision of the user (2026-10-15): the Go port comes at the end of the roadmap (section
2.1). The text below is from the time when the port came after milestone 1: at the time of
the port, the C++ backend has all systems of the roadmap, and P.2 ports each of them.

Decision of the user (2026-10-13): the Go port starts when milestone 1 is complete, and it
has no research step. A later decision of the same day puts the first combat stages before
the end of milestone 1 (section 2.1): the port then has these stages as reference code too. The implementer reads the C++ code and writes the Go code. At that
time the C++ backend has the session model, the world model, and all systems of milestone 1,
each with its tests and its verified wire contracts. It is the reference for the port.

- P.1, part 1: the workspace files (section 1.7). The C++ backend moves to
  `apps/backend-cpp`. The Go module gets the store (the same schema and the same
  migrations), the game data tables, and the HTTP services with all routes.
  Test: the Go tests pass with the race detector on. The replies are equal to the replies of
  the C++ backend for the recorded requests. A copy of the live database opens with no
  change. Live, with the Go backend and no game server: the main menu, the creation steps to
  the stored character, and the deletion work (screenshots).
- P.2, part 2: the game server (DTLS, the carrier, the frames, the registration, the
  sessions, the world object, the encoders of all states of milestone 1, the handlers of all
  client requests of milestone 1, the admin interface). The first live check is the DTLS
  handshake of the client with the Go library.
  Test: the bytes for one session are equal to the bytes of the C++ backend with the same
  clock. The tests with two sessions pass. The acceptance test of milestone 1 (1.9) passes
  again with the Go backend (screenshots).
- After the gate of P.2, `apps/backend-cpp` and its library copies (Mbed TLS, SQLite, the
  JSON library) go out of the repository. A copy stays in the tree copies outside the
  repository.
- The systems of milestone 2 and later were to be in Go from their first version. With the
  decision of 2026-10-15 they are in C++ first, and P.2 ports them.

## 5. Milestone 2: the world base

Goal: the world has a clock, the position is exact, entities other than the player exist, and
the player can interact, die, and travel. All later milestones need this one.

### 2.1 Client message base

- Research: `client-messages.md` (first pass). To verify: the target id rule (entity id of
  the slice XOR a constant, plus the component id) on a run with a different actor id.
- Implementation: a handler table for client messages; a table of target ids for each
  component; a list of known types that need no action, so that the log shows only new types;
  the client settings (types 2515, 2709, 4272) in SQLite.
- Test: a session of 10 minutes has no log line for an unknown type; the correction for the
  debug spawn request (type 1665) stops its error line.

### 2.2 World time and weather

- Client evidence: `Time`, `PlayerTime`. Request 4248, reply 7036.
- Research: `world-time.md` (batch B, verified). It replaces these first notes: the reply
  layout is in `client-messages.md` 5.1; the clock of the anchor value is a guess; how the
  client gets the weather.
- Implementation: a world clock in the backend; the reply; a command to set the time.
- Test: screenshots at noon and at midnight of the same place; the time continues after a
  restart.
- Stage 2.2 (done; protocol notes 4.43 and 4.5; result file `~/Games/soulwarden/work/evidence/world-time/implement/RESULT.md`;
  [World time and weather](systems/world-time-and-weather.md)). Live checks L0 to L2 first (`tools/world message` with records of
  the research): a record with a nonzero offset gives noon and midnight in the client, the hour moves at 26 and 44 game seconds in
  one real second, and a travel and a respawn keep the record while a new world entry (a menu login, a creation, a second session)
  asks again. The world has one cycle (the defaults of the client: 1,800 s of day, 900 s of night, dawn 06:00, dusk 19:00; decision
  D1), a new world at noon (D2), and the store of its position each 60 s and at an orderly stop, with no time while the backend is
  stopped (D3, schema 11). The session answers a valid 4248 of the own entity with 7036 (an anchor one cycle back and an offset; the
  ids of the own entity), sends a new record at a command and each cycle, the phase 4247 at each phase change (the gameplay edges 60
  s after dawn and before dusk, D4), and the clock correction 335 each 3 s (the interval of the server sender of the image). The
  tool window has the tab "World" (the admin command `set_world_time`), and `tools/world time` reads and sets the time. The creature
  data tool reads the TimeOfDayConstraintComponent of a spawner (format 4: 405 points with a mask, each with the despawn flag; one
  random encounter with the two cooldown flags): a point makes its creature only in its phases, and its creature leaves at the end
  of them with no kill and no reward (D6). The weather stays local data of the level (live: the rain of Weaver's Fen, a snowstorm of
  Great Cleave, the Dunwood keep interior with its own hour 0). The open point of 3.5b.2 about the mana meter: the client predicts
  the regeneration on a second copy that only the message 4289 sets; the session sends it with each later mana state, and the meter
  fills (live). Test cases WT1 to WT9 (WT6: a world of 33,856 points and 50 players, the tick of a phase edge below 50 ms), MN2, and
  RLB1 to RLB10 (the findings LB-R1 to LB-R9 of the review of stage 3.5b.2 and the open point of a kill item that the catalog does
  not have, each one failing on the tree of 3.5b.2). Not in this stage: the weather of an event, the activation of world events,
  gathering and fishing with their time rules (3.6), the pause of the time in an instance (7.1), and the PlayerTime component.

### 2.3 Exact position and full movement

- Client evidence: `ActionList` (message 1589), `Traversal`, `WaterLevel`, `Unstuck`,
  `PlayerPositionTracking`, `WorldBoundTracker`, `PlayerInputFilters`.
- Research: `position.md` (batch A, verified, "ready with live checks") and `movement.md`
  (first pass). The input report has a field for the exact position of the character. The
  client fills it only when one flag bit of the entry is set (the flag "within a plot" of the
  housing component). Two ways give the field: the backend sends the housing flag (side
  effects in the client: interior camera, no compass, housing HUD), or the hook sets the bit
  in the function that fills the entry (no side effect). Decision: the hook way, if its
  first live check passes. The research also has a rule for the state id that survives the
  36 minutes, and the speeds of the action lists (walk 1.65, run 3.25, sprint 5.5 m/s).
  Still open: the slow movement with the S key, and the action states for jump, dodge,
  crouch, swim, and climb.
- First stage (2.3a, size S, after 1.4): the position field, the check of each position
  (sequence number, speed limit, height range), and the id rule. **Done** (2026-10-13).
  Result file: `~/Games/soulwarden/work/evidence/exact-position/implement/RESULT.md`.
  - The hook way has live proof. The game hook changes 6 bytes of the client (image address
    `0x2A242C1`): each input entry has flag bit 15, and each input report after the loading
    screen has the position of the character. The camera, the compass, and the HUD do not
    change. `SOULWARDEN_EXACT_POSITION=0` turns the change off. The backend sends no housing
    fragment.
  - The backend reads the field (`server/rep/client_input.cpp`), and the world checks each
    report and keeps the position in the entity (`World::report_position`,
    `world/position_report.h`): a newer input number, finite numbers, the two limit values
    of the height, a peak speed between two reports (20 m/s; 20 m/s up and 120 m/s down for
    the height), and a speed over a time window of 1 s (8 m/s plus 2 m). The limits come
    from one live session with a fall of 150 m, sprint, dodge, and jump.
    A refused report changes nothing. The values of the client for the check are in the
    session. The camera estimate is the fallback for a client with no position field.
  - The position of the backend is 0.001 m or less from the position in the client memory
    in the horizontal plane, and less than one height step (0.017 m) in the height: at the
    spawn, after a walk, after a jump, and against a wall. The saved position is the exact
    value, and the next session starts there (the spawn height stays 0.5 m above the row).
  - The heartbeat state of the movement component has the id of the rule
    (`movement_state_id`): 0x8000 or 0x0000, 0x4000 or more from the newest input number of
    the client. The wrap of the input number was tested with a write of the number in the
    client memory.
  - Protocol notes 4.6 and 4.7 have the contract with live proof.
- What stays for system 2.3 (next stages):
  - A session of 40 minutes with no position correction (the wrap test of stage 2.3a
    replaced it).
  - The answer to a refused position: the client gets no correction now. A later stage can
    send a teleport to the last accepted position (rule MP-5). A teleport command of the
    server needs the same rule for the first reports after it (the 5 m rule of the session
    start).
  - The movement of a remote player for an observer: an observer gets the position of an
    entity one time, with its record. The exact position is now in the entity, thus this
    is a job of the replication layer (system 2.4 and 6.1).
  - The action states for jump, dodge, crouch, swim, and climb, and the slow movement with
    the S key (`position.md` sections 6 and 7).
  - A movement simulation, a check against the collision data of the world (a position in a
    wall or below the ground), and a larger speed limit for a mount (system 5.7).
  - The layouts of the three lists and of the block of mask bit 8 of an input entry: with
    one of them in an entry, the reader gives no position for that report, and the session
    uses the camera estimate after 1 s (protocol notes 4.7).
- Implementation: the exact position in the backend; an id rule that survives 36 minutes;
  the missing action states.
- Test: the saved position agrees with the client position within 0.2 m near a wall; a
  session of 40 minutes has no position correction; screenshots of jump, dodge, swim, and
  climb.

### 2.4 Other entities and area of interest

- Client evidence: `Aoi`, `PlayerAOI`, `Spawner`, `PrefabSpawner`, `DetectableObject`. From
  stage 0.6b on, the backend has the entity table, the position index, the interest table
  of a session with two distances, the stop of an entity, and the record of a remote player
  that stands (protocol notes 4.9). `tools/world` puts a character into the world as a test
  entity.
- Research: `entities.md` and `ids.md` (batch A, verified, "ready with live checks"). A new
  entity starts with a record that has the metadata state. The client has 65,534 places for
  entities. A stop record or the message `ReplicationControl` (type 9) removes an entity.
  For a player that is not local, the backend sends the same slice, and the client changes
  it to the remote player slice. The remote player slice has the same slots as the player
  slice and no facet for the bag, the coin, the attributes, the mastery, the abilities, and
  the objectives: these states are for the owner only. One bundle must stay below 1,024
  bytes. Stage 0.6b used the remote player as the first test entity: its live checks found
  that an observer needs the action of the character in the movement state, and that the
  client does not move a remote character to the ground. Still open: the live checks L9,
  L10, L11, L13, and L16 of `entities.md` (the `ReplicationControl` form, a sequence gap, a
  creature, an odd id, a stop while the slice loads), creatures and nodes (a first proposal
  exists for one creature), and which objects the client makes itself from the level data.
- Implementation: an entity table in the backend with a position index; spawn and removal
  for the observer; a command to spawn a test entity.
- Test: a test entity (for example a storage chest or a creature that does not move) shows
  in the world at the given position, and goes away when the player moves far from it.

### 2.5 Interaction base

- Client evidence: `Interactor`, `InteractorUI`, `Interact`, `UIActionManager`,
  `CameraLock`.
- Research (`interaction.md`, `interaction-stage.md`, verified): the interaction request and the reply; the prompt on the
  screen; timed interactions; the stations and doors of a settlement.
- Implementation: an interaction service that other systems register with (storage, shops,
  crafting stations, gathering, conversations).
- Test: the prompt shows at a station, and the E key opens its window.
- Stage 2.5a (done, 2026-10-16; [Interactions](systems/interactions.md), protocol notes 4.35; result:
  `~/Games/soulwarden/work/evidence/interaction/implement/RESULT.md`): the interaction block of the input report is read; the
  interaction system checks each request against the interaction data of the game (`tools/gamedata/interactions.sh`: 43,225
  objects of the level data with the ids of the client) and the session replies 3743; 2473 with its reference ends the committed
  interaction; the state of an object (Enabled, HasInteractors) goes to each session near it as the record of the level object of
  the client, and the Interactor state of a character to its owner and its observers. The first result: a lore page (E Read, T Add
  To Journal) keeps its achievement in the database, and the journal of the character has the page after a restart. The
  conversations and the animations of the client are accepted; each other option is refused until its stage. The commands list,
  enable, and cancel. The corrections of the code review of stage 3.3b-M2b (findings M2b-R1 to M2b-R5, cases RX1 to RX5).
- Stage 2.5b (done, 2026-10-16; [Interactions](systems/interactions.md), protocol notes 4.36; result:
  `~/Games/soulwarden/work/evidence/stations/implement/RESULT.md`): the interaction data has the server children of the placements
  (format 2: 425 children of 93 slice templates; 176 stations, 249 camps) and three fixtures. The world makes the station children
  near the characters (150 m; removal at 200 m) as object entities with actors of the allocator; the record (metadata and
  PositionInTheWorld, the smallest record of live check L4) shows the forge of each station (a template child), its prompt, and its
  crafting screen. A use of a station or a camp has the StationContext (the Assembly key, the fee flag, the Assembly reference, the
  parent settlement: the values of the client). A timed use (Trigger of the powder keg, 5 s) completes one time on the world clock
  after a check; a release, a cancel, damage, a travel, or the death before the end gives no completion. A use ends at the removal
  of its source, at the loss of the range, and at damage for the animations and the timed options (3746); a conversation that the
  server ends frees the input (the client sends 6705). The service grant of a source stays after a conversation and is checked for
  each later operation (5.1, 5.6). The corrections of the code review of stage 2.5a (findings INT-R1 to INT-R6, cases IR1 to IR5).
  Open: the crafting option of a camp in the client (2.6), the person children of a client spawner (their ids), the recipes (3.7).

### 2.6 Death, respawn, camps, fast travel

- Client evidence: `IncapacitatedCharacter`, `Camping`, `PlayerHome`, `ServerTransfer`;
  the death and teleport notices (types 3950, 3954); the teleport counter of the movement
  state.
- Research (`death-travel.md`, verified): the death sequence; the respawn selection; the camp; the inn check-in;
  fast travel between shrines and settlements.
- Implementation: death and respawn with no enemy (a command sets the health to 0); the
  respawn points in SQLite; teleport.
- Stage 3.4c did the teleport (protocol notes 4.16): `World::travel` moves a character with
  no new login. The client takes the position of a movement state with a new teleport
  number, and the two messages 3951 (the loading screen) and 3954 go with it. The travel
  places of the game data (`tools/gamedata/places.sh`) are the spirit shrines and the inns
  of each territory. Open: the fast travel of the game (the interaction of a shrine, the
  map, the costs) and the recall to an inn.
- Stage 3.4b did the death and the respawn (protocol notes 4.18): the health 0 is the
  death (the dead bit of the vitals state and the death notice, type 3950), the client
  shows its death screen and then its respawn screen, and its respawn request (type 1938)
  makes the character live again with its full health at the nearest travel place (a
  travel). The health and the death are not in SQLite: each world entry starts with the
  full health. Open: the respawn points of the game (the camps, the inns after a check-in,
  the houses: the list of the respawn screen has only the row "Random Location"), the
  cost of a death, and the "death's door" condition.
- Test: screenshots of the death screen, of the respawn at a camp, and of the arrival after
  fast travel.

### 2.7 Map, territories, markers

- Client evidence: `TerritoryDetector` (types 2297, 6416), `Waypoints`, `Marker`,
  `Trackable`, `HubLocalCache`.
- Research (`map.md`, verified): the discovery state; the territory data on the map; waypoints; the points
  of interest.
- Implementation: discovery in SQLite; the replies to the detector messages; waypoints.
- Test: the map shows a newly discovered area after the player goes into it, and the area
  stays discovered after a restart.

### 2.8 Notices and HUD services

- Client evidence: `NotificationService`, `EventNotification`, `HUD`, `CombatText`; types
  2617 and 5929.
- Implementation: a function to send a notice to the client, for use by all systems.
- Test: a command shows a notice on the screen.

## 6. Milestone 3: combat and creatures

Goal: the player fights creatures, gets loot and experience, gathers resources, and crafts
items. This is the largest milestone. System 3.4 has the highest risk of the roadmap.

### 3.1 Weapons in the hand and attack actions

- Client evidence: `ActionList`, `WeaponAccuracy`, `Charge`, `Homing`,
  `AttackHeightRetarget`, `CameraLockTarget`.
- Research (`combat-actions.md`, verified): the action states of each weapon type; draw and sheathe; light and heavy
  attack; block; ammunition; what the server must confirm for each action.
- Stage 3.2a did a part of this system (protocol notes 4.15): the backend reads the attack
  inputs of the client (the bits of the left pointer button) and the weapon keys from each
  input entry. The client starts each melee attack itself and plays it with no message of
  the server. The weapon in the hand of a character, for the damage, is the weapon slot of
  the last weapon key. Open: the sheathe bits and the active bits of the paperdoll state for
  an observer, the action of a remote player, the kind of each attack from the action lists
  (light, heavy, charged, the last attack of a chain), ranged weapons, and the block.
- Test: screenshots of an attack with a sword, a bow, and a staff; stamina goes down with a
  dodge.

### 3.2 Damage, stamina, and mana model

- Client evidence: `DamageReceiver`, `HitVolume`, `DefenseCollision`, `CriticalHits`,
  `CombatStatus`, `ReactionTracking`, `StatMultiplierTable`, `Grit`, `Stamina` (slot 42),
  `Mana` (slot 47), `Vitals` (slot 41).
- Research: `damage.md`, `hit-reports.md`, and `melee-mode.md` (verified). The client starts
  each attack itself. Its input report has the data of a projectile. It reports a melee
  contact only with the client setting `javelin.enable-client-authoritative-melee`, which is
  off by default: the backend turns it on with a remote configuration document (stage
  3.2a). Then: the damage formula and its tables; regeneration; the combat state.
- Implementation: the damage service; regeneration; a target dummy for tests.
- Stage 3.2a (done; protocol notes 2.3, 4.7, and 4.15; evidence
  `~/Games/soulwarden/work/evidence/hit/implement/`). The two live checks of the melee mode
  passed with no change of the client code: the backend answers one remote configuration
  request of the client with a document that turns the setting on, and the client then puts
  each melee contact into the third list of its input entry. The backend reads each entry of
  an input report with its three lists. A contact is a claim: the combat system of the world
  (`world/combat.h`) checks the target, the reference, the interest of the session, the life
  of the creature, the attack input, the weapon, the distance, and the claim rate (project
  rules), computes the damage in one function from the combat tables of the game
  (`tools/gamedata/combat.sh`: weapon, gear score, damage row, attributes, level, and the
  mitigation, the weakness, and the absorption of the creature; a project rule), lowers the
  health through the creature system, and gives the attacker the damage number (message type
  2071). The creature plays its reaction, and it dies at the health 0. Live: a sword, a great
  axe, a hatchet, a war hammer, and a spear; a Grey Wolf, a rabbit, and a Risen. Not in this
  stage: ranged weapons and magic, abilities, critical hits, blocks, status effects, stamina,
  regeneration, the combat state, and damage to a player. For stage 3.4b: the client of a
  player sends no report when a creature attacks it (the server must test that contact), and
  a sequence of movement states with a new position each 200 ms makes a creature walk with
  moving legs.
- Test: a hit on a test target shows a damage number and lowers its health bar.

### 3.3 Abilities, cooldowns, status effects, perks

- Client evidence: `Ability`, `AbilityInstanceTracking`, `CooldownTimers`, `StatusEffects`,
  `PerkCondition`, `Magic`, `PointsAccumulator`, `SpellTargetIndicatorManager`.
- Research (`abilities-effects.md`, verified): ability activation and its confirmation; cooldown state; status effect
  state and its tables; perks of items; consumables (food and potions, message 2917).
- Test: an ability starts, its button shows the cooldown, a potion gives health, a food
  effect shows as an icon.
- Stage 3.3a (done, protocol notes 4.22, document `docs/systems/abilities.md`; evidence
  `~/Games/soulwarden/work/evidence/abilities/implement/`): the six abilities of the great axe
  (Charge, Maelstrom, Gravity Well) and of the war hammer (Wrecking Ball, Shockwave, Path of
  Destiny) of the build of the user. The live checks L1 to L8 came first: the three keys of
  the bar set bits of the action words of the input report; the client plays an ability with
  no answer of the server, and it has no message for a start or a refusal; the client reports
  the melee contacts of an ability as the contacts of an attack (Maelstrom two, Charge,
  Wrecking Ball, and the swing of Gravity Well one), Gravity Well also one shot, and Shockwave
  and Path of Destiny nothing; the cooldown state (slot 45, type 2932) has one map for each
  place of the bar with the server time in microseconds; the icon of a status effect at the
  name plate of a creature comes only from the compact map of its status state (type 4236),
  with the position of the CRC of the status id in the list of all status rows; the action
  `Stun` and the reaction fragments of a slice play the stun and the knockdown; the client
  predicts no cost. A new tool (`tools/gamedata/abilities.sh`, file `ability-data.json`, 0.4
  MB) writes the cooldowns, the status effects, and the spells of the game data; the creature
  tool writes the reaction fragments of each slice. The world takes a press of a key (the
  character from the session, the weapon in the hand, its bar, the node, the cooldown, the
  lock, no other cast; a refused press changes nothing), starts the cooldown of the data on
  the world clock (a value of the character; the owner gets the countdown on the HUD), gives
  the hits of the melee phases through the contact check and `Combat::strike`, the hits of
  the areas through the position index and `Navigation::sees` (one hit of a creature for each
  phase), and the stun, root, slow, weaken, knockdown, and stagger of the data to the
  creatures through the creature system; the behavior obeys them, a creature on its way back
  takes none, and each observer sees the icon and the action. The nodes of the build select
  the rows, spells, and effects (Storm's Reach, Gravity, No Reprieve, Unyielding, Crowded
  Well, Breathing Room, Meteoric Crater, Frailty, Seismic Waves, Stimulated Reduction,
  Aftershock). The cost is 0. Test cases AB1 to AB12; AB10: 100 Shockwaves near 40 creatures
  in a world of 4,040 creatures in 0.12 s (limit 5 s). Live: a video of each ability with its
  animation and its damage numbers, the countdown and a press in it, a stunned, a slowed, and
  a rooted wolf beside a wolf with no effect, Shockwave and Path of Destiny on two wolves, and
  a kill by Charge with its rewards. The stage also corrected the eleven findings of the
  review of stage 3.5a (the reward of a kill as one change of the store, the checks of the
  loot file in the backend and in the tool, the scope at the end of a spawn lead, a jump of a
  creature that cannot walk, the hold at a new nearest place, the respawn after a command
  removal, the credit rule of the reference, the counts of the roll command, and a large
  drop). Not in this stage: the abilities of the other weapons, a spell entity (the vortex
  shows no picture), the pull of Maelstrom and of the vortex, the effects on the caster and
  the passive nodes (fortify, grit, empower, critical hits, heal), a refusal message, and the
  perks of an item that change an ability (stage 1.3b: protocol notes 4.22 "Not known, not
  done").
- Stage 1.3b, itemization (done, protocol notes 4.23, documents
  `docs/systems/items-and-equipment.md`, `gear-and-perks.md`, `consumables-and-heart-rune.md`;
  evidence `~/Games/soulwarden/work/evidence/itemization/implement/`). The live checks came
  first: the perk list of the client (1,822 entries, equal to the list of the new tool
  `tools/gamedata/perks.sh` after a second start), the perks, the gem, the gear score, and the
  rarity of an item in the tooltip, the perk swap of the client, the attribute bonus map, the
  charge state of the heart rune (slot 48, type 5257) and its key (bit 450), the quick slot
  keys (bits 404, 117, 140, 513), the use message of the bag (2917), and the countdown of a
  consumable (the item map of the cooldown state). The backend stores five perk places for
  each item (schema 7), sends their indexes, applies the swap table of the game at a give, and
  rolls the perks of a loot item with the procedure of the client. The gear system gives the
  passive values of the perks and the effects (attribute bonus, health, armor, absorption, the
  DMG parts, the equip load in whole tenths), the gem conversion (two damage numbers), and the
  perks of a hit with the conditions of the data (Sundering Shockwave, Insatiable Gravity Well
  with its burst, Refreshing Move, the void damage over time and the chain). The heart rune
  charges from hits and makes one explosion of 5 m 3 s after its key. A quick slot key or the
  message of the bag uses a potion, a food, or a dust: one item, its effect, its cooldown and
  the shared cooldown. The cost of an ability comes from the game data (I14): the six abilities
  of the build have none. The cooldown of the client after a reset of the server was the cause
  of the "first press after a weapon change" of stage 3.3a (I8). The paperdoll of a first
  record in two parts lost the jewelry and the quick slots in the client: each list group now
  stays in one part. The character of the user got the perks and the gems of the build. Test
  cases IZ1 to IZ11 (IZ11: 50 characters with 2,000 hits, their perks, chains, ticks, and 150
  explosions in a world of 4,400 creatures in 0.07 s, limit 5 s). The stage also corrected the
  thirteen findings of the review of stage 3.3a (cases RA1 to RA13). Not in this stage:
  crafting, repair, salvage, the socket action, the status state of a player (no icon of a
  food), the perks that need grit, stamina, a critical hit, a dodge, or a player target, the
  other heart runes, and the perk biasing of a roll.
- Stage 3.3b-S1, the status effects of a player (done, protocol notes 4.24, document
  `docs/systems/status-effects.md`; evidence `~/Games/soulwarden/work/evidence/abilities-status/implement/`).
  The live checks L1 and L2 came first: an entry of the full map of the status state of the
  owner (slot 44, type 4236) alone gives the HUD icon, the countdown, the stacks, the visual
  effect, and the armor of the character sheet; a tray entry gives a second icon; the end of
  an entry is a time of the synced wall clock of the client, which needs the wall clock pair of
  the Player state (group 1, fields 6 and 7); an observer sees the icon at the name plate from
  the compact map. A new status system (`world/statuses.h`) is the one owner of the statuses of
  a character: a food, a potion, a dust, the status of the heart rune, the status of an affix of
  the gear, and the caster effects of the ability rows (Safety Measures, Greed, Hammer Time) come
  through it, with the rules of the data (stacks, the end rules, ApplicationCooldown, ticks,
  StopOnHitCount, RemoveOnDeath, OnEndStatusEffect, the limits of the status categories). Each
  value counts one time in the gear summary. The explosion of the heart rune comes from the
  chain of the data (the end status Rune_Detonate_CastAoE). The owner gets the full map and the
  clock pair; each observer gets the public statuses. The ability data tool writes format 3
  (the status categories and 497 caster rows). The stage also corrected the sixteen findings of
  the review of stage 1.3b (cases RI1 to RI14: the gem of the weapon instance, a fixed perk
  after its swap, a swap duplicate, the rune at a death or a leave, the level of a consumable,
  the entity of message 2917, the own cooldown of an item, the countdown of a new item, an
  early refusal, a file with no status row, the end of the client cooldown, a large paperdoll
  group, the links of the perk data, and the checks of the perk tool) and the blank comparator
  of AbilityData (case RI15: LessThanOrEqual, the static proof of the parser; Executioner, its
  critical node, and Leaping Strike). Test cases ST1 to ST11 (ST7: two sessions; ST11: the load
  of 200 characters with 1,000 statuses in a world of 4,000 creatures, with a time limit). Live:
  the icons and the countdowns of a food, a potion, a dust, the rune, and a fortify of Safety
  Measures, their expiry, the CON of a food and the armor of a dust in the sheet, and the icon
  of a public status of a remote player. Open: the client does not apply the speed of a status
  to its run (a walk of 2 s is the same with a haste of 0.2 or 0.4); the server accepts the
  faster positions (closed in stage 3.3b-S2a: the speed table). Not in this stage: the parts S2 to S6, the statuses after a restart of the
  backend (a decision of the user), and the rows with OnHitTaken or NumAroundMe.
- Stage 3.3b-S2a, the first spell entity and the speed of a status (done, protocol notes 4.25 and
  4.24, document `docs/systems/spells.md`; evidence
  `~/Games/soulwarden/work/evidence/abilities-spell/implement/`). The live check L4 came first,
  with a probe entity (a development command): the smallest record that shows the vortex is the
  metadata of the slice and the Spell state (type 2912) with the key of the row and the start
  position; the field masks are chained; the client runs the delayed events of the slice from
  the init, a delayed event state (type 3139) gives a late observer the correct age, a completed
  activation plays the burst again, and the stop removes the vortex at once (the destroy event
  of the slice does not). A new spell system (`world/spells.h`) makes the vortex of Gravity
  Well an entity of the world (the kind `spell`, its id from the allocator, its caster, place,
  row, and world clock times): the vortex and the burst run at its place at the times of the
  slice (3.0 s, the end at 3.5 s), one time each; the leave of the caster ends it; each session
  near it (50 m) gets its record with the age, and the stop. A new tool
  (`tools/gamedata/spells.sh`) writes `spell-data.json` (1,565 spells, 934 slices). The speed of
  a status (criterion S2a-I10, research `status-speed.md`): the speed table of the player (slot
  43, type 1525) is what the run of the client reads; its encoder sends the multipliers of the
  statuses, and the position check accepts a faster run only after the session sent the table.
  Live walks: a haste x1.43, the heart rune x1.2, the slow of the vortex x0.40, the removal and
  the expiry, the first record of a new entry, and no table of a different character. The
  stage also corrected the thirteen findings of the review of stage 3.3b-S1 (cases RS1 to RS13;
  each case fails on the tree of stage 3.3b-S1). Test cases SP1 to SP9 (SP6: two sessions; SP7:
  the load of 5,000 far spells and 10 near ones, with a time limit) and SV1 to SV3 (SV3: two
  sessions). Live: Gravity Well on three wolves with the damage numbers, a cancel at the leave
  of the caster, a late observer, a leave and a return. Open (decisions of the user): the data
  has no EndOnCasterDeath for the vortex (the vortex stays and bursts after the death of its
  caster; the brief asked for a cancel). Not in this stage: the pull (S4), the beacon, the
  pylon, and the traps (S2b), the projectiles (S3), the Ownership and TemporaryAffiliation states
  of a spell.
- Stage 3.3b-S4, the forced movement (done, protocol notes 4.26, document
  `docs/systems/forced-movement.md`; evidence `~/Games/soulwarden/work/evidence/abilities-motion/implement/`). The
  live checks came first. L7-A: the client does not move a creature of the server by the impact of a hit (it plays the
  reaction, and the name plate stays). L7-B and L7-C: the client moves its own character in the dash of Charge (13.77 m
  in 1 s, 14.73 m with the swing, a step of 0.602 m at most), and the position check of a run refused 7 reports of one
  dash. L7-D and L8: the numbers of the action layer of the client (rest 30/1080, the dash 7/59, the swing 7/2312, the
  Riposte loop 7/3324); the counter action Ability_Rapier_Riposte_Out has the mark ServerStarted (client code
  0x2A66E50), and its numbers are not known. The pull and the push of the client (`ReactionMotion`, client code
  0x48A0D30 and 0x488B8F0: a travel with a decay, or a constant speed to or from the point of the damage) are the rule
  of the server: a new part of the creature system (`world/forced_motion.h`) moves a creature by the impact of the
  damage row of a hit (the new member `impacts` of `combat-tables.json`, 234 rows) in steps of 50 ms on the line of the
  navigation (`Navigation::slide`: a wall or a drop stops it), not a creature on its way back; the behavior waits and
  then searches its path from the new place. The pull of Maelstrom (1.5 m, 2 m), the pull of the vortex (7 m/s to the
  center, the child entity VortexPull of the slice), its burst, Path of Destiny, Shockwave, Charge, and the basic
  attacks push or pull with the values of the data. The dash of the caster: the server gives no second displacement,
  and the position check accepts the dash in the envelope of the accepted cast (14.73 m x 1.3, 18.07 m/s x 1.3); the
  observers get the state `motion` with the place, the rotation, and the actions of the dash. Riposte: the window
  Rapier_Riposte_Timer at the press; a creature hit in the window starts the counter of the server (one hit of
  1H_Rapier_Riposte_Stun at 0.433 s, its stun, its push). The stage also corrected the five findings of the review of
  stage 3.3b-S2a (cases RG1 to RG5; each case fails on the tree of stage 3.3b-S2a): an old haste allowance ends one
  window after its change, a file with no speed limits gives the limits of the client (1.5), the spell loader refuses
  a delayed slot with no event, the ability tool requires the rule columns of a caster row and the ABS columns of each
  damage type, and the spell tool refuses a delay that is not a 32-bit float. Test cases FM1 to FM8 (FM7: two
  sessions; FM8: the load of 200 movements in 4,200 creatures, with a time limit), CM1 and CM2 (CM2: the observer of a
  dash of the server), RC1 and RC2 (RC2: two sessions), RG1 to RG5. Live: Maelstrom on two wolves, Gravity Well on a group (the pull, then the burst), the push of
  Path of Destiny, a pull that a pillar stops, the dash of Charge (own and observed), and the counter of Riposte. Open:
  the client animation of the counter (the numbers of the action Ability_Rapier_Riposte_Out are not known; the rapier
  part), the action Pushable and the PushBackScale of an elite, the part Z of an impact, the push of a character. Not in
  this stage: the critical hits and passive events (S5), the heals (S6), the projectiles (S3), the attached and
  persistent spells (S2b), the other abilities of the rapier and the other weapons.
- Stage 3.3b-S3, the projectiles of a player and the check of a ranged hit (done, protocol notes 4.27, document
  `docs/systems/projectiles.md`; evidence `~/Games/soulwarden/work/evidence/abilities-projectiles/implement/`). The live
  checks came first (L1, L2, L5 with the backend of stage 3.3b-S4). A basic shot of the bow or the fire staff is one item
  of the second list of the input entry: the origin, the direction, the charge (0, or 100 for the charged arrow at a hold
  of 15 steps), the ammunition key (DefaultArrow: the ammunition-free setting of the client is on), the attack (`basic`,
  `basic_heavy`), and the row (0 for a magic shot). The client reports no later hit (46 bow shots, 6 fire balls: no item and
  no message after a hit on a wolf, a wall, or nothing), and the item has no number of its own; a creature in the first 2 m
  is the receiver of the item itself (the hit near the muzzle). Thus the server follows each projectile: a new world system
  (`world/projectiles.h`) accepts a shot with the rules of the data (the profile of the weapon, the attack, the row and the
  charge of a shot rule, the hold of a heavy shot, the ammunition, the origin, the direction), keeps one ledger entry for
  each press (the key: the character, the session, the input sequence, the press), flies the projectile on the world clock
  with the speed, the acceleration (after its delay distance), the range, and the life time of the data, and gives the
  first creature whose capsule (the capsule of its character, a new member of the creature data) the segment touches one
  hit through `Combat::strike` (the damage modifier 1.2 of the ammunition); a wall or the ground (`Navigation::line_stop`)
  stops it with no hit. The new tool `tools/gamedata/projectiles.sh` writes `projectile-data.json` (323 attacks, 85 slices,
  105 ammunition rows, 261 weapons, 58 shots of basic attacks). The stage also corrected the two findings of the review of
  stage 3.3b-S4 (cases RM1, RM2; each case fails on the tree of stage 3.3b-S4): one Charge gives its extra distance one
  time (a budget from the place of the press), and the counter of Riposte needs the rapier in the hand. Test cases PJ1 to
  PJ11 (PJ9: two sessions; PJ10: the load of 100 shots in 4,040 creatures, with a time limit), RM1, RM2. Live: a bow shot on
  a wolf (the hit 128 ms after the launch, 270.6 damage), a charged shot (406.0), two fast shots (two hits), a miss, a shot
  into the sky (its range), a shot that the yard wall stops (the wolf behind it keeps its health), light fire balls
  (135.8), and the shots of a second character seen by the first one (the health bar, no number). Open: the observers see
  no projectile (no projectile entity, type 16, and no message 327), the use of `m_extraSpeed`, the precedence of the
  predicted acceleration of a profile, the heavy fire ball of a client of the harness (the hold bit), the grouped spread of
  the blunderbuss (2719, 2570), headshots and the fall of the damage with the distance (S5). Not in this stage: the
  abilities of the ranged and magic weapons (their own parts), the critical hits (S5), the heals (S6), the attached and
  persistent spells (S2b).
- Stage 3.3b-S2b, the spell entities of three kinds (done, protocol notes 4.28, document `docs/systems/spells.md`;
  evidence `~/Games/soulwarden/work/evidence/abilities-spells2/implement/`). The live checks came first (run `s2b-l1` with
  the probe entity): the trap slice has no Spell component and shows only with its PositionInTheWorld state (type 13);
  field 6 of the Spell state (the attachment) makes the client move the Beacon with its target; the Vitals state of the
  Pylon gives its name plate and its health bar; an event of the init whose target the server triggered, sent completed,
  plays the death chain of the Pylon and the activation of the trap; the press of the Pylon gives no item, the trap and the
  Beacon give a shot item 8 and 4 input steps after the press. The spell system has roles: a summon (the Pylon: the spawn
  at 0.6 s 2 m in front of the caster, the payment of 15 mana at the spawn, an indefinite cooldown while it lives and the
  cooldown of 10 s after it, the health of its vitals row, the shots of its AutoSpell each 2 s through the projectile path,
  a creature that a shot hurts takes it as its target and bites it, the death chain at the health 0 and at 15 s), a trap
  (the toss through the projectile path, the arm time 0.1 s, one trigger: one hit and the root on each creature in 1.1 m,
  the end at the timeline or at 30.5 s), and an aura (the Beacon: the hit on a creature, the area on the entity that the
  projectile hit, the statuses of its row on a friend in 3 m, off at the exit and at the end, the end at 10 s or at the
  leave of its target or caster; the pulses of its status each 1 s with no heal). The spell data has the format 2 (the
  values of the Vitals, AutoSpell, detection volume, offense, detection event, and timeline components; 1,703 spells,
  1,058 slices), and the projectile data has the 33 projectile spells of the abilities. The stage also corrected the
  eight findings of the review of stage 3.3b-S3 (cases RJ1 to RJ8; each case fails on the tree of stage 3.3b-S3): the
  near hit on the line of the launch, one shot for each press, the weapon of the press, the ground after a creature on a
  steep shot, the hold on the world clock, no hit through a wall by the tolerance, a projectile tool that keeps the old
  file for a source that is not complete, and no number that is not a finite float. Test cases SB1 to SB10 (SB7: two
  sessions; SB8: the load of 3,003 spells and a far crowd of 4,000 creatures, with a time limit), RJ1 to RJ8. Live: the
  Pylon on a wolf (the shots each 2 s, the wolf bites the Pylon, the bar goes down, the end chain at 15 s), the trap on a
  wolf (54 damage, the root of 3 s, the removal 0.508 s later) and a trap with no creature (the end at 30.5 s), the
  Beacon on a second character that moves, on a wolf that runs, the statuses of the caster in it and their end, and the
  Beacon of the second character seen by the first one. Open (decisions of the user): the friendly relation of two players
  (a group of one now; a friend only in a fixture of the tests), the overload rule (a press with less mana than the cost
  of the spawn is refused before the payment), the affiliation of a summon (the data applies none at the start; with no
  TemporaryAffiliation state the own Pylon has a red bar, a made-up entry gives a blue bar in a live check; the values of
  the original server are not known). Open: the heal amount of a pulse (S6), the message `Pylon_Spawn` (the HUD shows no
  countdown while the Pylon lives, and the client plays the animation of a refused second press), the picture of a
  projectile of the server for an observer, the Ownership state. Not in this stage:
  the critical hits (S5), the heal base (S6), the groups, the interaction option of the trap (2.5), the Pylon spells of
  the nodes, the other abilities of the weapons.
- Stage 3.3b-S5, the critical hits and the passive events (done, protocol notes 4.30, documents
  `docs/systems/combat-and-health.md` and `docs/systems/passive-rules.md`; evidence
  `~/Games/soulwarden/work/evidence/abilities-crit/implement/`). The live check came first (L6, run `crit-l6` with the
  backend of stage 1.1b and `tools/world message`): the client shows the special damage number (orange, larger) for the
  flag Crit, or for Headshot with Ranged and CanCrit; Headshot alone, Ranged alone, and IsFromStatusEffect alone give the
  normal number. The rules (decisions of the user of 2026-10-15): one independent roll for each hit with the generator of
  the world and the chance of the data (the weapon, the fixed nodes, the rules of the hit, the statuses); the multiplier
  of the weapon (or DmgCoefCrit of the row) plus one additive pool of the bonuses; a hit from behind at the critical angle
  of the slice of the creature (the wolf: 135 degrees) and a projectile on the head volume of the slice are critical with
  no roll, because the data makes them critical. The tools write the new data: `criticals` and `hitRules` of
  `combat-tables.json`, the angles and the head volume of 374 slices of `creatures.json` (the bone of the HitVolume in the
  bind pose of the skeleton of the character), and `ability-data.json` format 4 (the 2,025 rows of the ability tables of
  the players, the 52 spell selections of the action lists). One evaluator of the passive rules (`world/passives.h`)
  reads the cells of each learned node and each perk row: the events OnHit, OnCrit, OnKill, OnHitTaken, OnHeadShot, and
  OnHitBehind, the conditions with the comparator of the client (the blank comparator is LessThanOrEqual, client code
  0x43B6390; an equal band of 0.001), the hit counts of one attack, the entities near a character (the tracking spell of
  the rule), and the effects (a damage part, the critical parts, the armor penetration, a status on the caster or on the
  target, a heal of a part of the damage, a cooldown change, a fixed stat); the gear system has no condition check of its
  own now. The plans of Shockwave, Path of Destiny, Gravity Well, and the Beacon take their spell from the action data.
  `tools/world passives NAME` and `--table WEAPON` list each rule with what the backend does with it: the great axe 18
  of 38 nodes applied, the war hammer 22 of 38; the rest is listed with the cause (no plan of its ability yet, an action
  of the client, a block, grit). The stage also corrected the six findings of the review of stage 1.1b (cases RD1 to RD6;
  each case fails on the tree of stage 1.1b): the mute of a development run opens and writes each settings file through
  one checked descriptor (F1), a Vitals component needs its row (F2), the progression tool checks the rank limits (F3),
  the projectile tool requires the shots of the abilities (F4), and the progression loader reads the format first and
  checks the children of a DLC root (F5, F6). Test cases CH1 to CH10 (CH2: a fixed generator; CH10: two sessions), PS1 to PS7 (PS2: the
  limits of Executioner 50, its critical node 30, and Leaping Strike 30; PS5: two sessions; PS6: the load of 50
  characters in 4,200 creatures, with a time limit), RD1 to RD6; 420 cases pass, and the stored bytes of T2 did not
  change. Live (runs `crit-live1` and `crit-live2` with the backend of the stage): critical numbers on a wolf with a
  status of critical chance (667, 718, 597, and Shockwave 343 and 340), a hit from behind (603; the same hit from the
  front 502), a headshot of the bow (367; a shot on the body 283), the statuses of Mauler's Fury and Greed in the HUD,
  and the exhaust of Exhaustive Attacks at the name plate of the wolf (5 s). Open: the roll and the composition of the
  original server (a pity rule is not known); the head in the pose of an animation (the backend uses the bind pose: a
  shot at the chest of a wolf that looks at the shooter can be a headshot); a hit from behind and a headshot on a player.
  Not in this stage: the heals of an ability (S6), the special rules of the other weapons (Berserk, the channels, the
  stacks of the rapier and the spear, the stances of the greatsword), grit, blocks, and the abilities of the great axe
  and the war hammer that have no plan yet (M2, M3).
- Stage 3.3b-S6, the heals of an ability and of a friend (done, protocol notes 4.31, documents
  `docs/systems/combat-and-health.md`, `abilities.md`, `status-effects.md`, `spells.md`, `passive-rules.md`; evidence
  `~/Games/soulwarden/work/evidence/abilities-heal/implement/`). The live checks came first (run `heal-l3` with the backend
  of stage 3.3b-S5, `tools/world fragment` and `tools/world message`): bit 1 of `healthChangeFlags` in the owner view
  gives the green heal number of the owner (the new health less the health of the client), the observer view shows no
  number, and message 4298 with the id of the entity Stats of the friend gives the number of the healer at the feet of
  the friend (L3); the weapon damage of the attribute window has the base damage of the weapon and the Focus curve of the
  damage rule, and a level part (L9). The rules: one heal path (`Health::heal`) for each heal (an ability, a potion, a
  food, the gear, a life steal) with the real gain after the cap, one number for the owner, and one notice for a healer
  that is a different character; the heal base of an ability (a decision of the user of 2026-10-15): the base damage of
  the weapon at its gear score times the Focus scaling of the damage rule; the amount of a status of an ability: the base
  times HealthModifierDamageBased, the potency, the stacks, and the potency factor (EffectPotencyMods), times the outgoing
  factor of the healer (HealScalingValueMultiplier of its statuses and of its rules with OnHealed: Divine Blessing; the
  equip load) times the received factor of the target (HealMod, and ConsumableHealMod, NonConsumableHealMod, or
  DmgPctToHealthHealMod of its kind, with the limits of the categories), multiplied (a decision of the user). Divine
  Embrace (a direct heal at 1.0 s, its cost with the heal, Privilege), Sacred Ground (the area of its self spell at the
  caster, the heal each 1 s for 12 s, Holy Ground, Anointed), and the heal pulses of the Beacon. A live run showed that
  the key of Divine Embrace and Sacred Ground starts only the aim of the client: the backend casts at the confirm of the
  same session (the self cast input LCTRL, action bit 430, or a click), and the key with the self cast input held casts at
  once (the action list of the data). The stage also corrected the twelve findings of the review of stage 3.3b-S5 (cases
  RV1 to RV12; each case fails on the tree of stage 3.3b-S5): the mute of a development run uses only descriptors after
  the first open of the prefix (F1), an explicit CanCrit 0 is a row with no critical hit (F2, `combat-tables.json` format
  2), the CritDamage of a rule with OnCrit counts (F3), Crowded Well counts the upgraded vortex (F4), a node change
  refreshes the gear values (F5), a rule with OnHitTaken checks its conditions (F6), a projectile that stops at the body is
  not a headshot from behind (F7), a hit count gates the damage values (F8), a tracking spell of the shape Box is not
  applied (F9; the great axe now 17 of 38 nodes applied), the tools and the loader require the target status of a rule
  (F10), a Pylon with no Vitals component needs its row (F11), and the creature tool stops for a cut angle field (F12).
  Test cases HE1 to HE11 (HE2, HE8, HE11: two sessions; HE9: the load of 100 healers in 4,000 creatures and 400 far
  characters, with a time limit: 0.27 s), RV1 to RV12; 443 cases pass, and the stored bytes of T2 did not change. Live
  (runs `heal-live1` and `heal-live2` with the backend of the stage): a self Divine Embrace with LCTRL ("+412"), Sacred
  Ground ("+55" each second to the full health), the Beacon on the caster (10 pulses of "+55"), and Divine Embrace on a
  friend of the test fixture with a click ("+412" at the feet of the friend for the healer, and the health bar under its
  name). Open: the point of the click of an aim (the nearest friend in range and in sight; Sacred Ground at the caster),
  the level part of the heal base (L9; a decision for the user), the client reader of ShowUiDamageNumbersOnHeal, the
  groups (a friend only in a fixture), an effect after a heal. Not in this stage: the other abilities of the life staff,
  the heals of the other weapons, the groups.
- Stage 3.3b-M1, the basic attacks of the melee weapons (done, protocol notes 4.32 and 4.15, documents
  `docs/systems/combat-and-health.md`, `abilities.md`, `spells.md`, `projectiles.md`, `docs/development/game-data.md`,
  `live-runs.md`, `build-and-test.md`; evidence `~/Games/soulwarden/work/evidence/abilities-combos/implement/`). The live checks
  came first (runs `m1-i1` to `m1-i1d` with the backend of stage 3.3b-S6, the captures, and the action layer of the client): the
  client plays the attack graph of its action lists and animation databases: the clock of a fragment that starts at the input step
  s is (k - s + 1) / 30 at the step k; a light swing starts at the release step, a partial heavy at the release step after 0.333
  s, the full charge by itself at 0.667 s (0.5 s for the flail); a buffered window takes a press and starts the next fragment at
  its end, a press before it is lost; an instant window starts it at the first step with the press or the hold (the repeat of a
  long hold); a fragment ends at the end of its animation (the length of its CAF file). Each contact came in the window of the
  damage key of its fragment (7 weapons; the spear gave no contact). The tool `tools/gamedata/weapon-actions.sh` writes
  `weapon-actions.json` (12 tag sets of melee weapons, 119 fragments, 275 transitions, 89 strikes); `combat-tables.json` needs no
  change (it has each damage row). The combat system follows the sequence of each character (the character id is the key, rule
  MP-1) with the input steps of its session, finds the fragment and the strike of each contact, gives the row of the strike (with
  the node conditions of its clips: the pull of the great axe), and counts each target one time for each strike (a ledger of the
  last 4 visits); a weapon change, an accepted ability, a travel, a death, a reconnect, and a dodge in a buffer window or a later
  cancel window end it (a dodge in the cancel window from the start of a swing does not: live check). The claim limit is 64 in 1
  s (was 12), and a strike hurts 32 creatures at most. With no file the rule of stage 3.2a stays. The heal base has the level
  part of the damage rule (a decision of the user of 2026-10-15, case HE12). The stage corrected the nine findings of the review
  of stage 3.3b-S6 (cases RW1 to RW8 fail on the tree of stage 3.3b-S6; HE9 with no area heal passes there and fails now): a
  development run is silent through the startup setting `-sys_audio_disable 1`, and no tool edits a file of a prefix (S6-R1);
  the confirm of an aim needs the ability of the aim on its place of the bar and the lock of the session now, and a change of
  the bar, of the nodes, or a travel ends the aim (S6-R2, S6-R8); a target at its full health takes no direct heal and no cost
  (S6-R3); an area heal needs the sight to the friend (S6-R4); the aura that stays gives its heal source (S6-R5); a head contact
  must come within the hit tolerance after the body (S6-R6); each heal with a number is its own owner state (S6-R7); HE9 checks
  the periodic heals (S6-R9). Test cases MB1 to MB9 (MB5: two sessions; MB6: a replay, a wrap, a flood, a reconnect; MB7: the
  load of 60 swings on 8 wolves in 4,008 creatures: 0.06 s), RW1 to RW9, HE12; 462 cases pass, and the stored bytes of T2 did
  not change. Live (runs `m1-proof` and `m1-proof2` with the backend of the stage): the light chain, the partial and the full
  charge of the great axe and the war hammer, one chain of the sword, the rapier (the second sub-hit), the hatchet (the ender),
  the greatsword, and the flail, each hit with the row of the fragment that the client played, and a dodge. Open: the spear
  gives no contact in this client (5 distances, two wolf levels); the stamina has no model (the payments of the reachable
  graphs are 0, but the stance payment of the greatsword); the stances of the greatsword and the oils. Not in this stage: the
  abilities of the great axe and the war hammer (M2, M3), the abilities and the special rules of the other weapons, the block.
- Stage 3.3b-M2, the other abilities and the nodes of the great axe and the war hammer (done, protocol notes 4.33 and 4.22,
  documents `docs/systems/abilities.md`, `passive-rules.md`, `spells.md`, `projectiles.md`, `forced-movement.md`,
  `combat-and-health.md`, `status-effects.md`, `docs/development/game-data.md`; evidence
  `~/Games/soulwarden/work/evidence/abilities-gawh/implement/`). The live checks came first (run `m2-i1` with the backend of stage
  3.3b-M1): each ability gives one contact for a target in the window of each damage key of its fragment, the pull of Reap gives
  none, and the client leaves the spin of Unending Winds at 1.933 s when it has no count of the hits. The tools write the abilities
  of each weapon (`weapon-actions.json` format 2: the action of each ability, its fragment and its selections by the nodes and the
  perks, its spells, its start windows, and the conditions of its transitions; 71 abilities, 208 fragments), the status of each
  damage row (`combat-tables.json` format 3), the AreaStatusEffectComponents of the slices (`spell-data.json` format 3), and the
  status actions of the action lists (`ability-data.json` format 5). The abilities of the two trees play the fragment of their
  action in the sequence of the character: the strikes of the fragment are the hits (the reach of the shape of the row), the
  press starts the fragment at once or at the window of the fragment that plays (else the client does not play it), the
  transitions inside an ability follow the nodes, the perks, the attack input, the block press (bit 484), a collision that a
  contact shows, and the hits of the cast. The server gives the pull of Reap (it tests the shapes of the strike), the status of a
  damage row (the root of Gravity), the hit counts of the ability state (slot 46, fields 1 and 2: the seven spins of Unending
  Winds), the spells of an action, a fragment, or a rule (the shield of Maelstrom, the ground of Mighty Gavel, the fortify of
  Power Cleaner), the area statuses of the upgraded vortex (Unyielding), the status action of Guarded Sprint, the box of Blood
  Lust in front of the look, and the rows of Execute and Armor Breaker. The list of the passive rules: the great axe 33 of 38
  nodes, the war hammer 34 of 38 (the rest needs grit, a block, or stamina). The stage corrected the eight findings of the review
  of stage 3.3b-M1 (cases RB1 to RB8 fail on the tree of stage 3.3b-M1): the due time of a visit on the world clock (M1-R1), a
  step with no hold is a release (M1-R2), the first hit commits the fragment of a visit (M1-R3), the steps of a gap (M1-R4), the
  end of a visit and a strike after the end of its fragment (M1-R5), the weapon actions tool stops for incomplete sources (M1-R6),
  a friend with no gain is no heal target (M1-R7), the heal numbers stay 2 s (M1-R8). Test cases GH1 to GH19 (GH10, GH12,
  GH15, GH19: two sessions; GH16: 20 casts in 4,001 creatures with a time limit), RB1 to RB8; 489 cases pass, and the stored
  bytes of T2 did not change. Live (runs `m2-live1` to `m2-live5` with the backend of the stage): videos of Reap and its pull,
  Execute, the seven spins of Whirlwind, Armor Breaker, Mighty Gavel, Clear Out and its push and fortify, Charge with its
  uppercut and its swing, the shield of Maelstrom, and Shockwave and Path of Destiny as before. Open: no creature shoots (the
  removal of a projectile by the shield), Wrecking Ball gave no contact at the test place (also with the backend of stage
  3.3b-M1), the picture of the ground of Mighty Gavel is not clear in the stills. Not in this stage: the abilities of the other
  weapons, the stamina model, grit, and the block.
- Stage 3.3b-M2b (done; protocol notes 4.34; evidence `~/Games/soulwarden/work/evidence/abilities-wrecking-ball/implement/`):
  the contact of Wrecking Ball. A search over the backends of the stages 3.3a, 1.3b, 3.3b-S1, 3.3b-S4, and 3.3b-M2 (each built from
  its tree, with its game data and a copy of the development data) showed that no stage of the backend lost the contact. Part M2
  took Breathing Room away from the development character; with no Breathing Room the client tests only the base clip of the
  strike (a capsule at the joint of the weapon), which seldom meets a creature. The development character has the 19 war hammer
  nodes of the build of the user again. A second cause, in the backend: the movement state of a creature had `dataBits` 0, and
  the client takes the homing flag and the camera lock flag of an entity from bits 1 and 0 of that field (client code and a live
  trace): the Homing clips of the abilities found no target. Each movement state of a creature now has `dataBits` 0x03: the leap
  of Wrecking Ball and the move of Mighty Gavel turn to a creature at the side. Test cases GH20 (Wrecking Ball in the fragment
  of the data, two sessions) and GH21 (the target bits, two sessions; it fails on the tree of stage 3.3b-M2), CR2 (the stored
  bytes of a creature record). Live: videos of Wrecking Ball with its damage number at the test place of part M2, at the place
  of the video of stage 3.3a, with a wolf at 35 degrees to the side, and at Lakeshore Shrine in Brightwood. Open: the base clip
  of Wrecking Ball (no Breathing Room) seldom gives a contact; no creature shoots (the creature data tool keeps only the melee
  damage events), thus the removal of a projectile by the shield of Maelstrom has no live proof. The stage corrected the ten
  findings of the review of stage 3.3b-M2 (cases RH1 to RH11 fail on the tree of stage 3.3b-M2): the items of a fragment wait for
  their due time on the world clock (M2-R1), the strike of a contact comes from its time window (M2-R2), the end of an aura with no
  ClearOnEnd leaves its rows (M2-R3), the pull of Reap tests each target on its axis (the move of the caster since the press, the
  turn of the homing) and takes 32 creatures at most (M2-R4, M2-R10), the spells of a buffered action come at its real start
  (M2-R5), a refused later press ends the strikes of a cast (M2-R6), the weapon actions tool stops for incomplete ability sources
  (M2-R7), a fragment with a missing part refuses the press (M2-R8), the loader refuses a status action with no row (M2-R9).
- Stage B: the bow build of the user (build sheet `~/Games/soulwarden/work/research/bow-build.md`, section 5; decision of the
  orchestrator: after user test 2, before 3.6b). The parts of stage 3.3c for this build only, in this sequence:
  - B0 (done; document `docs/systems/build-presets.md`; evidence `~/Games/soulwarden/work/evidence/bow-build/b0/implement/`): the
    build preset and the gear score limit of the artifacts. The preset `bow-boltcaster` (project data, `world/build_presets.cpp`)
    with the variants `pve` (the default) and `pvp` gives a character the whole build in one task of the world and one SQLite
    transaction: the 14 items of the paperdoll with their gear scores and perks, the consumables of the bag and of the quick slots,
    level 70, the attributes, the bow and rapier masteries with their 19 nodes each, the bars, and the achievement of the objective
    of each artifact (the client shows the perks 2 to 5 of an artifact only with it: live check U1). The old equipment goes into the
    bag; a refused preset changes nothing. The tool window has the group "Build presets" in the tab Character; the admin interface has
    `apply_preset` and the catalog of the presets; `tools/world preset`. The item catalog has the format 3 (`maxCraftUpgradeGS`, the
    artifact objectives): a give takes the gear score up to the upgrade limit of its item (800 for an artifact), not above the
    MaxEquipGS of the expansion. The list of the passive rules names Finishing Shot and Refreshing as not applied. The stage also
    corrected the six findings of the code review of user test 2 (cases RT1 to RT6 fail on the tree of user test 2): the path after a
    short push (U2-R1), a Magic hit of power 0 (U2-R2), the end of the reaction limit (U2-R3), a partial reaction table (U2-R4), the
    hit stun of a wander (U2-R5), the region files of a held manifest (U2-R6). Test cases BP1 to BP9 (BP5: two sessions).
  - B1 (done; document `docs/systems/abilities.md`, section "The bow abilities"; evidence
    `~/Games/soulwarden/work/evidence/bow-build/b1/implement/`): the three bow abilities. The projectile data has the format 2: the
    ability actions of `playeractions_ability_bow` and `player_bow.adb` (the clips, the spell casts with their node conditions, the
    windows to a different ability). The shot items of the client are the shots of a cast (a volley: one shot for each clip or cast, at
    its time). Penetrating Shot: the key is the aim of the client, and the shot item is its confirm (the cooldown starts there); the
    arrow passes through creatures in the order of the line, each one time, to the 50 m of its profile, and a wall stops it; Blood Soaked
    Arrow adds 0.1 for each earlier hit (to 0.5; the member `hitRates` of the combat tables). Explosive Arrow: the direct row, then the
    explosion of the slice at the collision (2.5 m; 1.25 m and the row 1.7 with Contained Detonation); Flame Arrow gives the burn only
    from the direct row (its ticks through the creature status, its icon at the name plate). Rapid Shot: three arrows at 0.333 s,
    0.867 s, 1.733 s; Final Blow on the third; Rapid Accuracy when the three arrows of one cast hit one target (decision D11). A key of a
    different ability in a bow fragment waits for its window. The stage also corrected the six findings of the code review of stage B0
    (cases RQ1, RQ2, BP10 to BP12 fail on the tree of stage B0; B0-R6 with live pictures): the path after a push of 0.9 m (B0-R1), the
    Riposte window after a preset (B0-R2), the artifact objectives of the catalog (B0-R3 to B0-R5), the form of the tool window after a
    second apply (B0-R6). Test cases BW1 to BW10 (BW9: two sessions and two casters; BW10: the load).
  - U3 (done; between B1 and B2, a decision of the orchestrator; research `~/Games/soulwarden/work/research/creature-combat-feel.md`;
    documents `docs/systems/creatures.md`, `creature-behavior.md`, `forced-movement.md`, `items-and-equipment.md`,
    `combat-and-health.md`; evidence `~/Games/soulwarden/work/evidence/combat-feel/u3/implement/`): the corrections of the creature
    combat feel. The homing of a creature attack (Q1): the server moves the creature in the Homing clips of its attack with the time
    rule of the client (the client does not move a remote creature: live checks LC-Q1-1, LC-Q1-2; no root motion). The reaction of
    a side or the back (Q2): the first row of the reaction table for the power and the angle, AlignToDirection. The length of a
    reaction with the clip HitStun and the chain of the data with its three resets (Q3; decisions of the orchestrator). The weight
    of the bag (Q4): the limit of the data, no cast and no dodge above it, no attack from rest above 1.5 x; the item catalog has the
    weight of the client (the WeightOverride of a weapon or an armor item; live check of the inventory screen). The push scales of a
    promoted elite and the additive reaction in its own scope (Q5; live check LC-Q5-2: the client plays it over a run; its push does
    not hold the behavior). The weapon switch in a swing and the press after CanTransition (Q6; live checks SW-L1 to SW-L3, CT-L1). The
    stage also corrected the nine findings of the code review of stage B1 (cases BRV1 to BRV7, BRV9 fail on the tree of stage B1;
    B1-R8 with the test form of the tool checks): no shot of a finished or cancelled cast (B1-R1), a lost first item of Rapid Shot
    (B1-R2), the death ends the aim of Penetrating Shot (B1-R3), a refreshed burn (B1-R4), a new instance of the same rapier (B1-R5), a
    clip with no launchable slice (B1-R6), the radius of the offense of the explosion slice (B1-R7), the projectile tool stops for an
    incomplete bow action (B1-R8), the origin of a bow shot behind a low wall (B1-R9). Test cases CF1 to CF13 (CF1,
    CF2, CF5: two sessions; CF9: the load of 200 homings in a world of 4,200 creatures).
  - B2 (done; documents `docs/systems/creatures.md`, `status-effects.md`, `passive-rules.md`, protocol notes 4.47; evidence
    `~/Games/soulwarden/work/evidence/bow-build/b2/implement/`): the rules of the Boltcaster and the stacks of a status on a target. A
    creature effect has stacks to the StackMax of its row, with the count in the compact status map (live check L1: the name plate
    shows "x2", "x3"); the end of a renewal follows RefreshDuration and DurationMax (one function for a character and a creature); the
    ApplicationCooldown of a row is on the target; a running removal status refuses its rows; a row with no duration is one damage now
    (the bolt; live check L2: its compact entry plays the strike). The evaluator has the conditions DamageTypes, TargetStatusEffect
    (the stacks of each source, decision D10), StatusEffect of the attacker, and the effects RemoveTargetStatusEffectsList and
    RemoveStatusEffects; the helper rows of a node and the rows that a status equips are rules. The Boltcaster: Electrified, the
    charges and the bolt (live checks B2-1, B2-2), Shirking Shock II (B2-3), Surprise Attack with 0.2 of the data (B2-4, decision of
    the orchestrator). A charm in a place replaces the fixed perk of the definition (BC10). The stage also corrected the nine findings
    of the code review of stage U3 (cases UR1 to UR9 fail on the tree of stage U3; U3-R9 is a document correction; U3-R7 and U3-R8 with
    live videos): the root in a homing (U3-R1), a push in a homing (U3-R2), the weight verdict of a kept press (U3-R3) and of a bow press
    (U3-R4), a kept press of a bow (U3-R5), the overlapping homing clips (U3-R6), a dodge in a bow action (U3-R7: the dodge marks of the
    projectile data of format 3), a weapon key in a bow action (U3-R8: its switch marks). Test cases BC1 to BC11 (BC6: two sessions;
    BC7: two attackers; BC9: the load).
  - B3 (done; documents `docs/systems/passive-rules.md`, `status-effects.md`, `stamina-and-dodge.md`, protocol notes 4.48; evidence
    `~/Games/soulwarden/work/evidence/bow-build/b3/implement/`): the haste chain. The ability data of format 7 has the dodge status actions
    and the tag removal actions of the action lists. The dodge action of the backend (a buffered next dodge continues it; it ends at the
    mark CanTransition of its last dodge) gives Dodge and Weave its haste at the end (stamina 0.99 or more at the start, the bow in the hand
    at the start and at the end, no new haste while one runs); the haste ends when the bow leaves the hand. The key of a weapon is the event
    OnWeaponSwap: Archer's Speed with its timer (OnStackStatusEffect of the status row). The time factor of a status on the character (the
    sum of the client getter 0x68308E0: Speedy 0.5 and Alacrity II 0.094 x 1.274; `time x (1 + sum)`, an agent decision): 3.24 s and 8.1 s.
    The condition StatusEffectCategories (Speedy +0.1 while hasted), Duration as a mark (Closing In), the rules after a dodge (Evasive
    Tactics, the cooldown change of the perks). Live checks L1 (before the code: the icon, its countdown, the speed table 1.07) and B3-1
    to B3-4; the client predicts no haste of Dodge and Weave. The stage also corrected the six findings of the code review of stage B2
    (cases BV1 to BV5 fail on the tree of stage B2; B2-R6 is the correction of case BC7): the next ability after a cancelled bow action
    (B2-R1), an effect after its end (B2-R2), the row of a perk rend in the hit snapshot (B2-R3), the removal window of a perk status
    (B2-R4), the weight gate of a press that a switch holds (B2-R5). Test cases HS1 to HS10 (HS7: two sessions; HS8: the load).
  - B4 (done; documents `docs/systems/passive-rules.md`, `gear-and-perks.md`, `consumables-and-heart-rune.md`, `stamina-and-dodge.md`,
    protocol notes 4.49; evidence `~/Games/soulwarden/work/evidence/bow-build/b4/implement/`): the other rules of the build. The jewelry
    stat lines (item catalog format 4; client 0x3636A50: gear score x scale x TrinketBonus x 0.01) join the parts of the perks; the cooldown
    line goes to the client as the key 0x4F of the stat multiplier state and joins Refreshing in one sum (live: the tooltip and the HUD
    19.7 s for Explosive Arrow). The threshold abilities of the attributes are rules of the character. Long Range (DistFromDefender from the
    place of the attacker at the hit), Finishing Shot (a target condition with no event is a rule of each hit), the outgoing time factor
    (Battle Precision, Dex_Bonus_150_1, the outgoing part of Alacrity II; an agent decision with the form of the client getter 0x68308E0),
    Enchanted Ward (the attack type of the damage row of a creature attack, in the creature data), Refreshing (a fixed cooldown rule),
    Vital Synergy (the perk script 0x238DEF0 of the client), the potion perks (the ConsumableHealMod of the perk rows with Divine,
    decision D9; the cooldown factor of OnUsedConsumable with a positive value and CooldownId; the Toast perks with IsConsumableIds).
    The maximum health of the preset is 5,330 (live sheet); the 10,676 of the video has no source in this client and its data (open). The
    rapier number of the sheet (1,824, the video 1,012) is a display formula of the client with the flag UseWeaponAttributeScaling of the
    gem (static proof), not the damage of the server. Live checks L1 (before the code) and B4-1 to B4-6. The stage also corrected the four
    findings of the code review of stage B3 (cases BY1 to BY3 fail on the tree of stage B3; B3-R4 is the correction of case HS7, proved on
    a tree with no haste allowance): an attack after CanAttackCancel ends the dodge action (B3-R1, stamina data format 2), the cap of the
    sources of a row (B3-R2), the check of an OnStackStatusEffect link (B3-R3). Test cases BR1 to BR14 (BR12: two sessions; BR13: the load).
  - B5, later: the rapier abilities (3.3c), the Bile Bomb heart rune, Rain of Arrows, Hardcore Parkour.

### 3.4 Creatures: spawn, behavior, navigation

- Client evidence: `Spawner`, `SpawnerReverseDetection`, `AITargetable`, `Pathing`,
  `NavMeshDebug`, `LootTracker`. The behavior ran in the server.
- Research (`creatures.md`, verified): the spawner data and the vitals tables; the slices of the creatures;
  which behavior data the game files have (behavior trees, ability tables); the navigation
  data; the movement state of a creature on the wire.
- Implementation in steps, each one a stage: (a) creatures at their spawn points that do not
  move; (b) health, damage taken, death, respawn timer; (c) aggression and attacks with no
  path search; (d) movement with the navigation data; (e) abilities of each creature family.
- Stage 3.4a (done; protocol notes 4.14; evidence
  `~/Games/soulwarden/work/evidence/creatures/implement/`). The tool
  `tools/gamedata/creatures.sh` writes the creature data: 1,051 usual spawn points of 121
  kinds with 70 slices in 800 m around the start place, and the action table of each slice. A
  creature is an entity of the world with no character and no session. The world makes the
  creature of a spawn point when a player is 150 m or less from it and removes it at 200 m
  (project rule). A session observes a creature at the radius of its slice and sends its
  record: the metadata state, the movement state, the vitals state, and the marker state.
  Live results: the body, the idle animation, the name, the level, and the health bar show
  with no action field in the record; a health change needs bit 0 of `healthChangeFlags`;
  the dead mark gives the death effect, and the body falls only with the death action as a
  scope record of the movement state. The index of an action in that record is the position
  of the action in the action lists of the grid file, with only the actions that the alias
  map of the grid names (live tables of three kinds: 50, 62, and 19 actions, equal to the
  tables of the tool). Commands: `tools/world creatures`, `kinds`, `creature spawn`,
  `remove`, `health`, `kill`, `reset`, and the tab "Creatures" of the tool window. Not in this
  stage: movement, perception, attacks, damage from a player, loot, and the respawn timer
  (its place is `Creatures::respawn_at`). For stage 3.2a: the client sends only its input
  reports for a sword attack on a creature (no hit message). The code review of stage 3.4a
  had 7 findings, which stage 3.2a corrected (the respawn of a point whose body is near a
  player, the reference character of an admin creature command, the check of the creature
  data file, the records of a creature, the animation sources of the tool, the totals of
  the height check, and the health command with 100 percent).
- Stage 3.4c (done; protocol notes 4.14 and 4.16; evidence
  `~/Games/soulwarden/work/evidence/world-spawn/implement/`). The creature data has the whole
  world: 32,840 spawn points of 3,848 kinds with 503 slices in 25 regions (a file of 8.8 MB,
  loaded in 0.2 s; the backend needs 58 MB with it). An entry with no category row is a
  spawn point (the client shows the name of its vitals row). A picture tour
  (`tools/research/creature_tour.py`) made one creature of each of the 553 slices in the
  live client: 476 show a body and a name plate with no action, 27 show them after the
  spawn action of the slice (`m_spawnAction`, which the backend starts at the creation of
  the creature), and 50 do not show (1,541 spawn points stay out; the largest groups are
  the sulfur lizard, the corruption swarmer, the chameleon, and the meatbags). The spawn
  rule looks only at the grid cells near each player: 200 scans with 50 players need 25 ms
  to 31 ms, against more than 1 s for a scan of all points (test case WS2, with a time
  limit). The travel command moves a character to one of 161 places (spirit shrines and
  inns) or to a position: `tools/world travel`, the admin command `travel`, and the tab
  "Travel" of the tool window. Live: travels to shrines in Brimstone Sands, Great Cleave,
  Monarch's Bluffs, Weaver's Fen, and Everfall, with the creatures of each place. The
  stage also corrected the six findings of the code review of stage 3.2a (the combat tables
  loader, the time of the height check for reports that come late, the combat data tool,
  an input entry that ends after its position field, the exact path of the remote
  configuration document, and the hit limits of a character over sessions). Not in this
  stage: the 50 slices, the entries of events and encounters, and the entries with no
  placement. For stage 3.4b: a movement of a creature must change its place in the position
  index; the leave rule of the spawn points uses the place of the creature, and the enter
  rule uses the place of its spawn point, thus a creature that goes 50 m or more from its
  point needs a change of the rule; a slice with a spawn action has that action in its
  movement state.
- Stage 3.4b (done; protocol notes 4.17 and 4.18; evidence
  `~/Games/soulwarden/work/evidence/fight/implement/`). A creature fights. The game files give
  the values of a creature and no rule of its behavior, thus the behavior is a design of
  this project, the same for each family (`world/behavior.h`): a creature at rest gets a
  character as its target when the character is in its vision (the distance and the angle
  of its slice) or hurts it; it goes to the target with the run speed of its slice; in the
  reach of its next attack it starts the attack (an action of the server), and the world
  tests each contact of the attack at its time on the world clock; it goes back to its
  home when the target is dead or gone, or at the end of its leash, and it has its full
  health there. The creature data tool gives the behavior data of each slice (the vision,
  the leash, the speeds, the animation tags of its weapons, and the attacks with their
  contact times and damage rows, from the animation databases and the damage tables):
  2,989 of the 3,848 kinds attack (24,228 of the 32,840 spawn points), and 859 kinds stand.
  A new tool makes the terrain heights from the height maps of the game copy
  (`tools/gamedata/heights.sh`, 49 region files, 136 MB), and the backend reads one tile of
  64 m when it needs it. A creature walks on the terrain: a creature whose spawn point is
  more than 1.0 m from the terrain height (6,893 spawn points of kinds that fight: a
  floor, a bridge, a cave) does not leave its place and attacks in its reach. A character
  takes the damage of one rule (`Combat::creature_damage`: the level and the kind of the
  creature, the row of the attack, the level difference, the armor), sees the damage mark
  and the damage number, dies at the health 0, and comes back at the nearest travel place
  (section 2.6). The spawn point rule uses the distance of the spawn point for its two
  distances, and the behavior needs time only for the creatures with a target (test case
  FT9: 1,000 ticks with 50 fights need 3.6 ms). Switches: `SOULWARDEN_CREATURE_BEHAVIOR=0`,
  the command `set_creatures_ignore` (`tools/world ignore`, the tool window), and the
  command `heal`. Live: a Grey Wolf, a Risen, a miner, an Ancient Guardian Reaver (a slice
  with a spawn action and with weapon tags), a Siltboot Deckhand in Monarch's Bluffs, and a
  rabbit (no attack). The stage also corrected the seven findings of the code review of
  stage 3.4c (the camera estimate after a travel, the time of the height check, the
  teleport number of more than one travel in one tick, the spawn action of a creature
  that dies in its reaction, a travel whose position write failed, the travel places tool
  with a wrong position, and a fragment number outside the range). Not in this stage: path
  search and walls, the line of sight, patrols, group aggression, ranged attacks, magic,
  abilities of a creature, blocks, dodges, critical hits, status effects, regeneration,
  loot, experience, and the respawn timer of a creature. For stage 3.4d: the two functions
  of `world/navigation.h` (`ground_height` with a position that has a height, and
  `next_step`) are the only place where the behavior gets a place; the terrain tolerance
  of 1.0 m (`gamedata::kTerrainTolerance`) and the member `ground` of a spawn point decide
  which creatures walk. For stage 3.5a: the death of a creature is in `Creatures::hurt`
  (with the attacker), the place of the respawn timer is `Creatures::respawn_at`, and a
  creature has its full health only at its home.
- User test 2 of stage 3.6a (done; result file
  `~/Games/soulwarden/work/evidence/user-test-2/implement/RESULT.md`; diagnosis `~/Games/soulwarden/work/research/user-test-2.md`):
  an ability starts only from the step of its press (a press before a window of the fragment that plays is refused; the server of
  the release started it later and ended the swing that the client played on, and its contact was refused with "no strike", 4.33);
  the end of a creature action, a walk, and a stun give the idle record of the slice (the client kept the last pose and ended its own
  idle at each state, 4.14); a timed action goes out again in each state while it plays (one arrow gave three reactions, 4.14); the
  reaction to a hit follows the damage row and the action Reaction of the action lists (the power, the length of its fragment, the
  hit stun, the creature types, the chain limit; no reaction for a damage over time, 4.14); the push lasts the reaction (0.4 s for
  a row with NoReaction, nothing with no reaction) and the creature goes on with its path after it (4.26); the creature data tool
  reads the lengths of the reaction fragments and of the attacks from the animation files; one log line for each refused claim with
  no strike and each refused press (32 at most). The seven findings of the code review of stage 3.6a (gathering) are corrected. Not
  in this stage: the homing of a creature attack (the client moves the body, the server keeps the place), the reaction of a side or
  the back, the weight rule of the abilities, the block press in the charge (the client played its light swing: no fault found).
- User test 1 of stage 3.4d (done; result file
  `~/Games/soulwarden/work/evidence/user-test-1/implement/RESULT.md`): the locomotion action
  of a creature and the rule of its scope fields (the creatures slid in the pose of an
  attack, protocol notes 4.14), the answer to the end of a cutscene and the end of each lock
  of the input at a travel and a respawn (a conversation locked the character, 4.20), the
  mana state (the blue bar, 4.5), the creatures of a travel that wait for the client, and the
  11 findings of the code review of stage 3.4d (4.19).
- Stage 3.4d (done; protocol notes 4.19; result file
  `~/Games/soulwarden/work/evidence/path-search/implement/RESULT.md`). The game copy has no
  navigation mesh: the original one was only on the server. A tool
  (`tools/gamedata/navigation.sh`) makes the navigation data of the whole world from the
  collision shapes of the game copy, the terrain after the cookies of the placements (a rule
  of the client code that changes the height map near some placements), the water, and the
  navigation marks of the level designers: 49 regions, 1.2 GB, cells of 0.25 m with more
  than one floor, the sight above each floor, 43 minutes with 8 processes. The backend reads
  only the tiles near its players with a thread of its own, finds a path with A* in a limit
  of steps for each tick, and stops the vision and the attack of a creature at a wall.
  23,021 of the 24,228 spawn points of kinds that fight walk (6,893 did not walk in stage
  3.4b). The gates of a settlement are closed for creatures by the marks of the level
  designers. The stage also made the fights playable (the shelter of 10 s after a travel,
  the health that comes back out of a fight, light and heavy attacks with a pause, one hit
  for the two hit volumes of one swing, the crowd), and it corrected the nine findings of
  the code review of stage 3.4b. Not in this stage: patrols, jump links, swimming and flying
  creatures, objects that move, a second agent size, loot. Open: a path with more than
  60,000 search steps is "no way"; the creatures of one fight stand in a queue on a narrow
  way; doors and gates of spawners that are off at the start are not in the data. For stage
  3.5a: a dead creature stays at its place on its floor; the place of the respawn timer is
  `Creatures::respawn_at`; a spawn point whose creature stands on no free floor (1,207 of
  kinds that fight) needs no path.
- Stage 3.4e (done; protocol notes 4.14, 4.17, and 4.19; result file
  `~/Games/soulwarden/work/evidence/patrol/implement/RESULT.md`). A creature patrols and
  wanders. The creature data tool reads the patrol of each spawner (`m_patrolPathEntity`,
  the components `AIPatrol` and `AIWaypoint`: the waypoints in world places, the wait range
  and the task of each waypoint, the policy) and the wander values of each slice (format 3,
  12.4 MB): 19,814 of the 32,840 spawn points have a route, and 465 kinds wander. A creature
  roams only while a character is in the leave distance of its interest (120 m at most): it
  walks with the walk action `Move` and the ambient speed of its slice, waits at each
  waypoint, and follows the policy (a loop, back and forth, at random); a wander takes random
  places of its range from the generator of the world. The leash is measured from the anchor
  (the place where the creature took its target), the way back ends there, and the patrol
  goes on. On the way back a creature is immune (no damage, no target by a hit, the vitals
  state has the immune mark), it has its full health at the anchor, and with no path back it
  walks its trail back; it jumps only when no session observes it. A creature whose target
  has no way to it goes to the place nearest to it and waits there 12 s. Test case PT9: 1,000
  ticks with 1,500 creatures that roam near 50 characters need 76.5 ms. Live: the start wolf
  walks its four waypoints; a fight from a patrol to the leash and back; a wolf on its trail
  back; the no-way wait at the Tomb of the Old Warrior; the patrols of Mosswater Bourg in
  Everfall (9.0 kB each second of creature states for one session with 64 creatures that
  roam). The stage also corrected the nine findings of the review of the corrections of user
  test 1 (the sight over a cell edge, the death with no death action, the action after a
  first spawn record, the travel fallback with no camera, the tile of a goal, the cache of the
  collision tool, a step with no locomotion action, the floor limit of the tile writer, and
  the spawn timing text). Not in this stage: group aggression, a creature that calls others,
  flee, the tasks of the waypoints that are not an action of the action table (24,872 of
  24,895), swimming and flying. Open: the client shows no immune text; two travels had no
  effect in the client. For stage 3.5a: the death and the anchor are in `world/behavior.cpp`
  (`died`, `take_target`); a creature that comes again at its spawn point starts its patrol
  at the first waypoint.
- Stage 3.4f-A (done; protocol notes 4.37; result file
  `~/Games/soulwarden/work/evidence/creature-ranged/implement/RESULT.md`). The first creature
  that shoots. The creature data tool keeps the ranged attacks of each slice (225 attacks of 95
  slices, 459 releases, with the attack index, the ammunition, the projectile slice, and the
  damage row of each release), the 69 projectile slices, and the capsule of a player; the
  backend uses the basic shot FireBow of 20 archer slices, so 3,034 of the 3,848 kinds attack
  (814 do not). A shot is an entity of its own with the Projectile state (type 16: the launch
  form of 51 bytes and the collision form, live checks L1 and L2), and it flies on the world
  clock with the speed, the acceleration, the range, and the life time of the data. The first
  character on its segment takes one damage result of the creature rule; a wall or the ground
  stops it with no damage. The project rules of the behavior (research `creature-ranged.md`
  Q3): the melee attack wins in its reach, the shot in 20 m with a free line, a walk to 18 m
  from farther, a walk back to 14 m from nearer than 8 m one time in 6 s, the next shot 3 s
  after the start of the last one, and the lead of the data with the velocity of the target.
  Test cases CRG1 to CRG6 and CRG9 (1,000 ticks with 50 archers and 200 flights near 50
  characters need about 15 ms; the same work with 840 and with 32,840 spawn points). Live: a
  Varangian Archer of the start area hits the character (65 damage at the contact), misses a
  character that steps to the side, stops at a brick wall, and a second character sees the
  shots at a different character; the arcane arrow of a skeleton archer and the shot of a
  Dryad archer (0.65 s) fly too. The stage also corrected the eight findings of the review of
  stage 2.5b (cases RY1 to RY8). Not in this part: the casters, the dodge, and the stamina
  (part B), the areas and the hostile statuses (part C), the heals and the buffs of a creature
  (part D), the block, the summons, and the abilities of an encounter. Open: the impact effect
  at a character, the miss distribution of the data, a live check in daylight.
- Stage 3.4f-B (done; protocol notes 4.38; result file
  `~/Games/soulwarden/work/evidence/creature-ranged-b/implement/RESULT.md`). The casters, the
  dodge, and the stamina. A slice with the cast `Cast_One_Projectile` and no bow shot casts it
  as its basic shot (10 slices of 88 kinds; 3,043 of the 3,848 kinds attack, 805 do not): the
  Skeleton Mage casts its slow ball (index 125, no ammunition, 20 m/s) 0.5 s after the start of
  its action, in a band of 6 m to 16 m, each 2.5 s (a decision of an agent with an option of the
  research). A new tool (`tools/gamedata/stamina.sh`, `stamina-data.json`) writes the stamina of
  the player slice, the delays, the dodge of each load class with its cost, and the times of the
  dodge fragments. The server keeps the stamina of each character with the rules of the client
  (live checks LS1 to LS9: the client predicts its own stamina and does not take the Stamina
  state; one input bit for each press, also a refused one): one path for each change, the
  regeneration of 33.33 in a second after 1 s, no debt, winded and exhausted at 0. A dodge pays
  45 at 0.1 s, and a creature hit in its first 0.5 s does nothing; a creature shot flies on
  through it. The passive rules of the stamina and of the dodge apply (MyStaminaPercent with the
  real stamina, Stamina Recovery, Mauler's Stamina as the hit-taken action of the data, Shirking
  Fortification with OnHitTakenWhileInvulnerable, the tag Dodging of Guarded Sprint); grit and
  the block stay out. A creature that walks into its band walks on to the hold distance (the
  rule of the research that the code of 3.4f-A did not keep). Test cases SM1 to SM5, CRB1, CRG9
  (25 archers and 25 mages) to CRG13, and RR1 to RR7 (the seven findings of the review of stage
  3.4f-A, each one failing on the tree of 3.4f-A). Live (daylight): a Celestial Mage casts and
  hits; a dodge lets its ball and two arrows of a Varangian Archer pass with no damage; the
  shield of Maelstrom takes an arrow; three dodges empty the stamina, a fourth press is refused,
  and the bar is full 4.4 s later. Not in this part: the areas and the hostile statuses (part C),
  the heals and the buffs of a creature (part D), the block, grit, the summons, and the abilities
  of an encounter. Open: the maximum 95 of the client of the development character (closed in
  stage 3.4f-C), the red cloud after an avoided ball.
- Stage 3.4f-C (done; protocol notes 4.39; result file
  `~/Games/soulwarden/work/evidence/creature-areas/implement/RESULT.md`). The ground areas of the
  creatures and the hostile statuses. Live check L8 first proved the records: the mushroom of the
  Dryad Shaman needs only the metadata and the PositionInTheWorld state (the tell, the cloud at 1 s,
  the end of the client at 26 s), the lightning strike of the Undead Shaman needs the Spell state
  with the caster (the omen, the bolt at 0.9 s); a late observer gets a completed activation of a
  lasting area and the hold of a burst. The spell data has format 4 (the shapes and the times of
  the detection volumes, the delays of the area statuses), and the creature data has the area
  attacks of the spells whose record has a live check (6 spells, 9 slices, 70 kinds, 255 spawn
  points; 3,059 of the 3,848 kinds attack). A status area gives its statuses 0.5 s after the entry
  and again each reapply delay (client code of the server facet), takes them at the exit and at
  its end, and has no line test. The poison ticks take a part of the maximum health and a part of
  the core damage of the creature (the producer of the client code; the sum, the potency, the
  stacks, and no armor are project rules), not in the immune window of a dodge. A strike gives each
  character in its sphere one hit of its damage row in its window (the dodge decides it); a wall
  only with CheckObstructions. The death of the caster ends an area with EndOnCasterDeath, its
  leave ends each area. The behavior casts an area before the shot and the melee attack: in the
  CastDistance with a free line, 10 s after the last one (5 s for a creature with only areas, which
  walks to 2 m inside the CastDistance), after the pause of the last attack; the place at the
  target with the lead and the angle limits of the clip. The first record of the owner has the
  Stamina state: the client default 95 of the server copy no longer comes into the HUD (live trace
  and memory read). Test cases CRA1 to CRA9 (CRA9: 100 casting creatures and 200 areas near 50
  characters with the same count of looks for 840 and 32,840 spawn points), SM6, and RZ1 to RZ6
  (the six findings of the review of stage 3.4f-B, each one failing on the tree of 3.4f-B). Live:
  three mushrooms with the poison icon and ticks of 242.3, the end of the poison at the exit; a
  lightning strike with one hit of 121.9, a strike that ends before its bolt at the death of the
  shaman; a natural fight of a Dryad Shaman with its two area attacks in turn and shots between;
  the stamina bar of 100. Not in this part: the heals and the buffs of a creature (part D), the
  summons and the encounters (part E), the block, grit, and the area spells with a new rule (the
  self-destructs, the shockwave at the caster, the shield of the officers, the spells with
  CastDistance 0).
- Stage 3.4f-D (done; protocol notes 4.40; result file
  `~/Games/soulwarden/work/evidence/creature-support/implement/RESULT.md`). The heals and the buffs of the creatures, and the areas at
  the caster. Live checks D-L1 to D-L5 first (with the backend of stage 3.4f-C): the vitals state of a creature moves the bar of its name
  plate up, the compact status map shows the icons of `AI_InqCleric_Rekindle` and `AI_Evil_Knight_Wolf_Haste`, the probes of the four
  strikes at the caster show their pictures, `AI_Weaken` takes 18.6 percent from a hit of the character and `AI_Snare_6sec` halves its
  run (11.03 m to 5.31 m in 2 s), and a late observer of the mage area sees the cloud. The game data gives the triggers of the actions and
  the recipient: the action list of the Cleric puts `AI_InqCleric_Rekindle` on itself at the mark of `InqCleric_Rekindle_Self` (0.633 s;
  8 s, 4.5 percent each 1 s), the action list of the wolves puts `AI_Evil_Knight_Wolf_Haste` on the Evil Knight hound at the mark of its
  `Howl` (1.2 s; 10 s, DMG 0.1, speed 0.2); the creature data has these status actions (the list `statusActions` with their live checks)
  and the low-health part of the Vitals data of each slice (0.15 for the Cleric). A status on a creature has one path: its effect with
  its row (the DMG part of the type of a hit, the strongest haste, the icon), the heal of its ticks through the one heal path of a
  creature, the OnTickStatusEffect, the dispel of the data (RemoveStatusEffectCategories), the end at its time and at the death; a due
  list of the ends and the ticks. The behavior starts a status action before its other attacks: the heal at the low-health part, the
  buff when the creature does not have it, at most each 20 s (project rules: the behavior tree is not in the game files). The place of an
  AI cast follows the client code (0x679DBB0): with no ranged attack data in the row the spell is at the caster (the plague nova, the
  corrupted blast, the anchor slam, and the shockwave with the rotation of the caster in the Spell state); the Radius of the row is its
  distance. 10 area spells (177 kinds, 539 spawn points), 3,101 of the 3,848 kinds attack. Test cases CRH1 to CRH6 and CRH9 (two
  sessions; CRH9: 100 creatures that heal and howl and 1,000 long effects with the same work as no far effect, less than 3 s) and RAR1 to
  RAR5 and RAR8 (the findings AR-R1 to AR-R5 and AR-R8 of the review of stage 3.4f-C, each one failing on the tree of 3.4f-C; AR-R6 and
  AR-R7 in the tool checks). Live: a Sentinel Cleric at 12 percent heals itself 8 times 819.1 with its icons; a Vorongard Hound howls,
  bites for 32.9 with the buff and 29.9 after it, and howls again 20.9 s later; a dispel takes the icon; a Plagued ghost casts its nova
  at itself (526.8 damage), a Vorongard Crusher its shockwave. Not in this part: the heal of a different creature (`InqCleric_Rekindle`
  with its beam), the other status actions of the data, the self-destructs, the shield of the officers, the summons and the encounters
  (part E), the block, and grit.
- Test: screenshots of a creature at its spawn point, of a fight, and of its death; the
  health of the player goes down when the creature hits.
- Risk: no data for the behavior. Then the behavior is a design of this project. Decision
  of the user: simple rules for each creature family first (section 12).

### 3.5 Loot and rewards

- Client evidence: `LootTracker`, `ItemDrop`, the loot tables of the game data.
- Research (`loot.md`, verified): the loot tables and their conditions; the loot window; world containers;
  experience and weapon mastery experience for a kill.
- Test: a dead creature gives items to the bag, and the experience bar moves.
- Stage 3.5a (done, protocol notes 4.21): the rewards of a kill and the respawn of a creature.
  A new tool (`tools/gamedata/loot.sh`, file `loot-tables.json`, 1.8 MB, 10 s) writes the
  experience and the coin of a kill of each level, the factors of the 99 creature types, the
  level differences, the credit data, the loot data of the 8,779 vitals ids, the 108 loot
  tables and the 85 loot buckets that a creature can reach, and the gear score limits of each
  level; the combat tables have the weapon mastery of each weapon. A kill by a hit of a
  character gives each character with the credit (1 percent of the health or more, the last
  hit 120 s or less before the death, in the world, alive, not ignored, 200 m or less, 10
  characters at most) the experience of the calculation of the client (through
  `Progress::add_experience`: a level up), the same amount to the mastery of the weapon in
  its hand (through the skill system), the coin with its chance, and the loot: the loot
  table of the creature, rolled with the rules of the roller of the client and the generator
  of the world, with the gear score of each new item. The items and the coin go into the bag
  in one transaction of the inventory system with the cause "ItemGathered": the client shows
  its loot notice, "+N XP", "+N Coin", and "+N Mastery". An item with no free slot is not
  given (no item on the ground in this stage). A spawn point makes its creature again after
  the respawn time of its spawner (360 s for most points), when a player is 150 m or less
  from it. A loot switch of a character (the tool window, `tools/world loot`) makes each kill
  drop loot and coin. Test case LT7: 591 kills with their rolls near 50 players need 36 ms
  to 48 ms (limit 1 s), 10,000 rolls 173 ms to 215 ms (limit 2 s). Live: kills of a Grey Wolf and of a Risen in the start area, of a
  bobcat of level 61 in Brimstone Sands (an item, the mastery, the coin), a level up by a
  kill, and the bobcat at its spawn point 360 s after its death. The stage also corrected the
  nine findings of the review of stage 3.4e (an anchor and a waypoint on a different floor,
  the walk in the lead of a first spawn record, the start of the no-way wait, a patrol source
  that is not complete, a number that is not finite in the creature tool, the commands on an
  immune creature, a task of a waypoint for ever after a fight, and the text of the kinds with
  no attack). Not in this stage: the perks, the gems, and the rarity of an instance (stage
  1.3b), an item on the ground, chests, gathering, territory standing, the loot of a quest,
  and the loot of a group. For stage 1.3b: protocol notes 4.21 ("Not known, not done") lists
  what the roll of a new instance must add.
- Stage 3.5b.1 (done; protocol notes 4.41; result file `~/Games/soulwarden/work/evidence/chests/implement/RESULT.md`;
  [Chests](systems/chests.md)). The ordinary chests of the world. Live checks L1 to L4, L8, and L9 first (a probe backend and
  `tools/world fragment`): E sends GatherOption of the chest object of the interaction data; the Gathering state (type 81, slot 36)
  with Success, the time, and the speed 1 runs the ring of the prompt, and Done ends the use of the client; the LootTracker state (type
  982, slot 33) with Looted removes the prompt, Locked and NotOnRequiredQuest hide it, and the client does not end a claim at its
  reset time. A new tool (`tools/gamedata/chests.sh`, `chest-data.json`, 3.6 MB) writes the 5,519 chest placements of the main
  world with their variation rows, gatherable rows, loot roots, territories (loot tags, content level), and states (5,149 ordinary,
  370 unavailable with their reasons); `loot-tables.json` format 2 has the chest roots, the loot limits, and the container floors;
  `interactions.py` keeps a condition of a bound only when it has a value (each chest had an empty one). The server opens an ordinary
  chest with the BaseGatherTime of the data (2.5 s, 5 s), checks the use again at the end, rolls the root of the chest (the level of
  the character, the content level and the loot tags of its territories, the container floor of the gear score), and writes the items
  and the Looted claim in one transaction; the claim resets at the time of the gatherable row (the elite chests 82,800 s) or at 60
  minutes, on the wall clock (a restart keeps it). A full bag keeps the roll in a Rolled claim (no item lost, no new roll; a project
  rule, decision of the user). Each character has its own claims; the map of a character has its claims and the unavailable chests
  near it (192 m), and the store keeps each active claim also above 60. Test cases LC1 to LC9 (LC8: 100,000 far chest definitions and
  50 characters with the same work as 1,000, less than 3 s) and RSU1 to RSU7 (the findings SU-R1 to SU-R7 of the review of stage
  3.4f-D, each one failing on the tree of 3.4f-D: the tick cadence of a heal again, a due heal tick in a late step, format 5 of the
  spell data with the parents of a volume, a broken join of a listed area spell in the creature tool, the next press after the echo
  of a server end, the self action against a Pylon, no poison tick after the life of its area). Live: a supply cache opens in 2.5 s
  with "+8 Obsidian Sandpaper" and "+12 Wireweave" and loses its prompt; the looted elite chest refuses a second open also after a
  restart; a second account opens the same chest; the reset of the override gives the prompt back; a quest chest with the Locked
  entry has no prompt and no light of the loot. Not in this part: the personal loot bag (3.5b.2), the locks, keys, quests, and
  events (3.5b.3), the loot limits, HWMMult and LootBiasing of the roller, and the umbral currency and the territory standing of the
  elite event.
- Stage 3.5b.2 (done; protocol notes 4.42 and 4.5; result file `~/Games/soulwarden/work/evidence/loot-bags/implement/RESULT.md`;
  [Loot bags](systems/loot-bags.md)). The personal loot bag of a kill (AILoot) and the discard of a player (Discarded). Live checks
  L5 to L7 first (a probe backend and `tools/world fragment`): the record of the slice `instanced_loot_container` shows only with a
  LootTracker entry of the character; the panel needs the selected Container (slot 76) after the request OpenStorageOption; the item
  list under the prompt is the key of the bag in the GlobalStorage map (slot 77); 2939 and 2942 name the Container of the player, not
  the bag; the client does not end a bag at its time (the server removes the record); 1838 and 826 are the drops. `interactions.py`
  format 3 has the personal loot container with the times of its slice (180 s, 1.25 s after the last item). The server puts the items
  of each character with the credit of a kill into its own part of one bag at the creature (the experience, the mastery, and the coin
  at the kill; decision of the user); only the eligible characters observe the bag; a pickup (E, T and the panel, 2939, 2942) moves
  the stored item instances in one transaction, a full bag takes what fits; a drop puts the items into a private Discarded bag at the
  feet; the bags and their instances stay in the store (schema 10) over a restart with the time on the wall clock. The mana (a task of
  the orchestrator): the MaxMana and ManaRate parts of the threshold abilities of the attributes, the perks, and the effects; the
  states send the current value with the parts and the base maximum (the client takes the base from the first mana state: live
  check). Test cases GL1 to GL7 (GL7: 2,000 and 20,000 far bags and 50 characters with the same work, less than 2 s), MN1, and RCH1 to
  RCH6 (the findings CH-R1 to CH-R8 of the review of stage 3.5b.1, each one failing on the tree of 3.5b.1: a delayed report after a
  cancel, the release after the open time, an item that the catalog does not have, a repeated actor, a missing metadata record, a
  missing territory row, a record of a different row, a missing container floor). Live: a kill gives a bag with its beam, the panel
  "LOOT", Take All, and the item in the inventory; a take of one item; the end of a bag at 180 s; the drop and the recovery of bound
  goggles with the same instance; the mana bar goes away at full mana, shows 85 of 110 after Divine Embrace, and goes away again.
  Not in this part: the shared bag of a death, the permissions of a group, the trade between players, the locks, keys, quests, and
  events (3.5b.3).

### 3.6 Gathering and fishing

- Client evidence: `Gathering`, `Fishing`, the gatherable tables.
- Research (`gathering.md`, verified): the nodes in the world (entities of 2.4); the gathering sequence; tools;
  yields; the respawn timer; trade skill experience.
- Test: screenshots before and after a tree is cut, with the wood in the bag and the skill
  experience higher.
- Stage 3.6a (done; protocol notes 4.44; result file `~/Games/soulwarden/work/evidence/gathering/implement/RESULT.md`;
  [Gathering](systems/gathering.md)). The gathering of the resources of the world: mining, logging, harvesting, skinning. Live checks
  L2 to L8 first (a probe backend and `tools/world fragment`): the client gives each record of a distribution file an offline id (G
  from the region and the record counter; not the packed position id of the research), and its request of E is GatherOption with that
  G; the GatherableController state (type 12, slot 3) Depleted hides a node and Healthy brings it back, and the client restores no node
  by itself; the Gathering state (type 81) runs the ring for amount / speed, Done ends the use, Failure cancels it; the tool must be in
  its paperdoll slot (33 to 36); the "Gather Speed" of a tool is MinGatherEFF to MaxGatherEFF over its gear score range. A new tool
  (`tools/gamedata/gathering.sh`, `gathering-data.json` and one file for each region, 40 MB) writes the 295 gatherable rows, the
  ranks, the events, the 230 tools, the 127 gathering slices, the 33,071 capital nodes, the skinning of 117 creature slices, and the
  2,450,322 natural nodes of 49 regions with their heights; `loot-tables.json` has the gathering roots. The server resolves a natural
  node and a creature body through a resolver of the interaction base (the capacity of a bound does not apply to action 14), checks the
  row, the tool, and the rank, runs the work (the speed: the tool times 1 plus the rank part and the EFF parts, a decision of an
  agent), rolls the loot root with the luck of the rank and the ROL parts, and writes the items, the experience, and the depleted node
  in one transaction; each observer gets the depletion; the respawn is one draw of the bounds of the data on the run clock of the world
  (schema 13: the run clock and the table `gathering_nodes`; a restart keeps the remaining time, the downtime does not count). A full
  bag keeps the roll for the next gather of the node; of two characters at one node the first completion wins (a decision of an
  agent); a skinned body stays depleted until it goes. Test cases GAT1 to GAT10 (GAT9: 2.5 million placements, 50 characters, 1,000
  updates, one region file read, the same work with 100 times more far placements, less than 5 s) and RWT1 to RWT5 (the findings
  WT-R1 to WT-R5 of the review of stage 2.2, each one failing on the tree of 2.2: the whole quantity of a kept roll at a restart, the
  fields of a stored stack beyond 500, the protection columns of the item catalog tool, the format of the catalog, the end of a time
  text). Live: a young tree falls ("+44 Logging", "+9 Green Wood"), also at its second depletion; a boulder with the pick gives 14
  stone; a hemp plant dissolves ("+215 Harvesting", "+26 Fiber"); the skinning of a rabbit ("+2 Rawhide", "+1 Game Meat", 32
  Skinning); the boulder with no pick says "No equipped Pick"; the tree comes back at its respawn; the depleted nodes stay after a
  restart. Not in this part: fishing (3.6b), the durability loss (3.7), the gather action for an observer (type 11), the yield
  multiplier, and the nodes of quests and events.

### 3.7 Crafting, refining, repair, salvage

- Client evidence: `Crafting`, `ItemRepair`, `Camping`. Salvage messages 1733 and 6188.
- Research (`crafting.md`, verified; `crafting-stage.md`, ready with live checks): the recipe tables; the craft request and its result;
  stations and their tiers; the gear score roll; repair parts. The research splits the stage into five runs (3.7a to 3.7e).
- Test: an item crafted at a station is in the bag; the materials are gone; a repair sets
  the durability to its maximum.
- 3.7a done (2026-10-17, [Crafting](systems/crafting.md), protocol notes 4.50): the fixed crafts. Live checks L1 (the empty state 205 is
  applied and changes nothing; the station prompt opens the recipe screen), L2 (the screen sends 7044; the empty 7046 with its UUID reaches
  the result handler), L3 (the request 2997 of the knife: the recipe CRC, the count, the fee 1, the gear score 100, two Item entries of
  quantity 1; the item with the cause ItemCreated and 2993 1 of 1 show the result screen; the stop form ends the crafting of the client, and
  its screen shows "Crafting attempt failed." after its own 10 s), L10 (the craft animation and the result screen), L10b (the count 3 in one
  request; the summary "3 ITEMS CRAFTED"; 2999 after each final progress; the server cancel closes the screen); L9 (the craft option of a camp)
  stays open: the client hides it. The tool `tools/gamedata/crafting.sh` writes `crafting-data.json` (8,294 recipes, 63 station rows, 617
  ingredient categories; 6 recipes with no cause of this run: the flint tools, the fishing pole, the wood sword). The server checks the
  station of the use (the Assembly key and its row), the recipe and its causes, the rank, the count, the entries (the item or a category
  member for each ingredient), the plan of the whole batch (the unlocked bag and quick slots, one cost for two ingredients of one item, the
  whole output in the bag), and the fee of the client path; the batch (decision D5, option A) completes its first job 1.0 s after the request
  and the next jobs in the next ticks, each job one transaction; a stop keeps the completed jobs; a late change of the bag stops the batch
  before the next cost. Decision D1 (option B): the 9 station children of a key with no row (Smelting2, Carpentry2, Masonry2, Tanning2,
  Weaving2) use the `_t2` variant of their family (key ending in 3), with one log line. Test cases CRAFT1, CRAFT2, CRAFT-MP, CRAFT-LOAD
  (20,000 placements and 100 more characters: the same visits, 10,000 validations and 200 crafts in less than 2 s), CRAFT-D1, CRAFT-CMD,
  CRAFT-TOOL, and BZ1 to BZ5 (the findings B4-R1 to B4-R5 of the review of stage B4, each one failing on the tree of stage B4). Live: the
  flint skinning knife (gear score 100) from Flint and Green Wood at the Workshop of Oxboro, in the bag after a restart; the summary of a
  batch of 3; the refusal of a job whose Flint went away; the Woodshop Tier 3 of D1.
- 3.7b done (2026-10-17, [Crafting](systems/crafting.md), protocol notes 4.51): refining and the reward progress. Live checks L4 (the
  experience of the screen 44 x n + 88 with the first-craft bonus, the bit FC_IngotT2 of the achievement state removes it, the result screen
  of a craft-all recipe counts the items at the first update with the cause ItemCreated) and L6 (state 205: the end of a cooldown on the
  synced wall clock in nanoseconds, the client ends a window itself; the gear score bonus map keyed by the TradeskillType, Engineering 6)
  before the code. One recipe of each refining family (iron ingot, timber, coarse leather, linen, stone block); the bonus chance of the
  client (the recipe, the tier lists, the material points, the RollBonus of the rank, the ROL effects) and decision D2 option A (e =
  OutputQty x chance: floor(e) and one draw of the rest); the trade skill experience of the event (220 for the first batch of 3 iron
  ingots, then 44) with the first-craft achievement in the transaction; the cooldown (Asmodeum "23h 59m per 9 items", the end of a window,
  a restart on the run clock); the crafting data of format 2 (948 recipes with no cause); the correction U4-R1 (an output with a gear
  score range). Open: the territory standing of a craft event (no territory system), the currency ingredients (the Azoth balance, 3.7c).
- 3.7c done (2026-10-17, [Crafting](systems/crafting.md), protocol notes 4.52): procedural items, charms, and bonuses. Live checks L5 (the
  knife of the workshop: 205 at Engineering 0, 255 at 50, 210 with Fae Iron; the client adds field 1 of state 205 to the cap 50 and not
  the statuses of the character; the perk item CRC and the upgrade byte of 2997; the Azoth levels 0, 8, 16, 24, 33, 41 from state 911),
  L5b (the forge: the great axe 600, the bytes of both push values, the gem pins keep the ordinary gem row with push true; the bag
  "500 / 2,500") and L5c (the entry of a currency ingredient: type Item, item 0) before the code. The craft gear score (the base, the rank
  for the tier, the materials, the status bonus to 50, the override, the MaxCraftGS of the item or of the expansion; the claim of the
  request); the craft bonus of the MaxGS columns of the statuses in field 1 of state 205; the perks of a procedural craft with the one
  generator of the loot and the perk items, G, and PerkItemsBucketPush (the research mapping), one gem, the swap, the rarity; the Azoth of
  a job (trunc(PerkCost x G), the station discount); the Azoth balance (Azoth_Currency, cap 2,500, state 911, the command `set_currency`
  and the tool window row); the currency ingredients; no extra units for a recipe that is not craft-all and not refining; the crafting data
  of format 3 (1,917 recipes with no cause, 837 procedural); the corrections CR7B-R1 (the experience of a batch from its raw total), CR7B-R2
  (the harness gate of the seed and the cooldown commands), CR7B-R3 (one bit of a first-craft achievement), CR7B-R4 (a sentence of D2).
  Test cases CRAFTC1 to CRAFTC6, CRAFTC-DATA, CRAFTC-MP, CRAFTC-LOAD (20,000 more buckets and 100 more characters: the same visits), CRAFTC-CMD,
  CR7B1 to CR7B3. Live: the knife 205 and 210 (a charm), the knife with G 1 (8 Azoth), the Orichalcum Great Axe 600 with the charm and G 4
  (33 Azoth): LEGENDARY with five perks, the seeded preview equal, the same tooltip after a restart; the food, trophy, and gear bonuses
  (600, 617, 602); the Tempered Cast for 250 Azoth. Open: no Azoth source of the world (only the tool window), no house trophy (no houses),
  the statuses (the food) end at a restart.
- 3.7d and 3.7e wait. The plan after 3.7c: 3.7d salvage (1733, 6188, the loot of RepairRecipe, the parts of decision D3 option A: a uniform
  fraction from 0.1 to 0.45 for each salvaged unit; the Azoth and the perk items of a salvaged procedural item); 3.7e repair (3003, 3007,
  2079, the kits, the coin and the parts; the durability of a crafted item).

## 7. Milestone 4: quests and story

### 4.1 Objective progress, completion, rewards

- Research: `quest-progress.md` (batch D, verified) and `quests.md` section 6: the task types and what completes each
  one (kill, gather, go to a place, interact, talk); rewards; the journal.
- Implementation: a task engine in the backend that gets events from the systems of
  milestones 2 and 3.
- Test: screenshots of a quest from its start to its reward.

### 4.2 Conversations, lore pages, cutscenes

- Client evidence: `Conversation`, `LoreReader`, `PlayerCutscene`, `Timeline`,
  `TimelineController`, `DiegeticObjectivePins`.
- Research: `conversations.md` (batch D, verified). The conversation window opens in the
  client with no message of the server.
- Test: a conversation with a quest giver opens and gives a quest; a lore page goes into the
  journal.

### 4.3 Introduction path of a new character

- Client evidence: `FtueMetric`, `PlayerTutorials`, the level `ftue_v2`, the member
  `SkipFtue` of the create request.
- Depends on creatures and quest progress. Until then a new character starts after the
  introduction (system 1.8).

### 4.4 Achievements, titles, milestones

- Client evidence: `Achievement`, `MetaAchievement` (3092 with reply 1679, 843 with reply
  3802), titles (4143 with reply 4147), milestone windows (5929).
- Research: `achievements.md` (batch E part 2, verified).

### 4.5 Factions

- Client evidence: `Faction`. Research (`factions.md`, verified): the faction choice,
  missions, reputation, the shop.

### 4.6 Events, season track, leaderboards

- Client evidence: `GameEvent`, `RewardTrack`, `SeasonsRewardsTrackedStat`, `Leaderboard`.
  Low priority, but the seasons states of the slots 72 and 73 are necessary for the quest
  HUD of stage 1.8.
- Research: `seasons.md` (batch E part 2, verified). The state of slot 73 alone opens the
  seasons gate of the quest pin. The level of
  the season track is a row of the categorical progression state (slot 65). The entry
  "Activities" of the menu bar opens the group finder.

## 8. Milestone 5: economy and settlements

| Id | Client evidence | Main research questions | Test (screenshot) |
| --- | --- | --- | --- |
| 5.1 Shops | `Transaction`, `CurrencyConversion`, `EconomyTracker` | Shop tables, the buy and sell requests, fees and taxes | An item bought from a shop is in the bag, and the coin is lower |
| 5.2 Storage | `GlobalStorage` (387, state 2938), `ContainerTransfer`, slot 76 | The storage of each settlement, transfer between storages | An item put in a storage is there after a restart |
| 5.3 Trading post | `Contracts` (2556, reply 2278) | Order format, search, fill; who makes the offers in a world with one player | A sell order shows in the list, and a buy fills it |
| 5.4 Standing and projects | `CategoricalProgression`, `Contribution`, `ContributionTracker`, `TerritoryInteractor` | Standing experience and its bonus cards, the town board | The standing bar of a territory moves |
| 5.5 Housing | `PlayerHousing`, `PlayerHome`, `Builder` | Buy a house, place furniture, the house as a travel point | A placed table is in the house after a restart |
| 5.6 Cosmetics | `Transmog`, `ItemSkinning`, `Entitlement`, `EmoteController`, `MaterialOverride` | Skins and their entitlements, dyes, emotes | A skin changes the look of an item, an emote plays |
| 5.7 Mounts | `Mount` (6268, 6271) | The mount state, call and dismount, riding skill | The character rides a mount |
| 5.8 Music | `MusicalPerformancePlayer`, `MusicalPerformanceIndicator` | The performance sequence | The performance window opens with an instrument |

Research files (2026-10-13): `shops.md`, `storage.md`, `territories.md`, `housing.md`,
`cosmetics.md`, `mounts.md`, and `trading-post.md` are verified. `music.md` is a first pass.

## 9. Milestone 6: more than one player

All systems of this milestone need a second client. Until now each test has one client.
Decision of the user: one player until milestone 4 is complete, with the foundation in place
from the stages 0.6a and 0.6b (rules of section 1.6). The research part of 6.1 (the remote
player slice) ran early, in batch A, because the entity model of system 2.4 depends on it.
The safety contract permits only loopback: a second client must run on the same computer in
the same network namespace.

| Id | Client evidence | Main research questions |
| --- | --- | --- |
| 6.1 Second player | `slices/remoteplayer.dynamicslice`, `PlayerDataBroadcast`, `PlayerNameTag` | Two clients on one computer (two accounts, graphics memory), the state of a remote player, movement for an observer |
| 6.2 Chat | `Chat`, `WhisperPlayer` | Is chat a game server message or a separate service; channels |
| 6.3 Friends and groups | `Social`, `Groups` (5463, reply 5458), `PlayerGenericInvite` | Invitations, the group state, shared markers |
| 6.4 Player trade | `P2PTrade`, `PlayerTrade` | The trade sequence and its locks |
| 6.5 Companies | `Guilds` | Ranks, treasury, the company services |
| 6.6 Player against player | `Faction`, `PlayerArena`, `TemporaryAffiliation` | The flag, duels, arenas |

Research files (2026-10-13): `chat.md` (verified: text chat is a message of the game
server, types 1291 and 293, with no separate service) and `social.md` (verified: the group
data is an entity of its own with its own slice). `player-trade.md` and `companies.md` are
a first pass. `pvp.md` is a partial first pass. Decision of the user (2026-10-13): the
social systems have the lowest priority. They get no new research, no verification, and
no stage until the user asks.

## 10. Milestone 7: instances and large activities

| Id | Client evidence | Remark |
| --- | --- | --- |
| 7.1 Expeditions | `ServerTransfer`, `InstancedSlayerScript`, `SlayerScriptForwarder`, `GameModeParticipant`, `PositionalTicketing` | A second world in the same backend, scripts of the encounters |
| 7.2 World events and arenas | `GameEvent`, `PlayerArena` | Breaches, world bosses, trials |
| 7.3 Wars, invasions, Outpost Rush | `PlayerTurret`, `GameModeParticipant` | Needs many players: last, and only if the user wants it |

Research files (2026-10-13): `expeditions.md` is verified with the status "not ready": the
change of the level in one connection has static proof only, and a live check must decide
it. `world-events.md` and `wars.md` are a first pass.

## 11. Continuous work

- 8.1 Game data tools: one tool set in `tools/gamedata` for all tables (items, vitals,
  spawners, loot, recipes, objectives). The products stay in `~/.local/share/soulwarden/gamedata`.
- 8.2 Regression set with no game process: a test program with two sessions and with
  captures of own runs, so that a stage finds a break before a live run. It starts in
  stage 0.6a. The Go port changes it to Go tests (`go test -race`), with test vectors that
  a program makes from the C++ code.
- 8.3 Robustness: no request of a client can stop the backend process (finding of stage
  1.2); sessions of 40 minutes and more; a quiet log.
- The tool window gets the commands of each new system (give item, set level, spawn
  creature, teleport, set time, complete objective).
- Play from Steam: `tools/steam-shortcut.py add` puts the launcher in the Steam library as a
  non-Steam game. From stage 0.6a on, this entry and a development run are separate: a
  development run has its own Proton prefix, and `tools/dev-stop.sh` stops only development
  runs. They share the game copy and the products in `apps/*/dist`. An instance name for the
  lock file and the client folder, for two games with one backend, is later work
  (`multiplayer-foundation.md`, section 3).

Not planned, because the function needs a service on the internet or is against the safety
contract: voice chat (type 1643), the Twitch functions (type 2776), the store with real
money, telemetry, cross-play, and each connection to a public game service.

## 12. Research batches and decisions

Research is reverse engineering, and it comes first: the batches run one after the other,
and they do not wait for the implementation stages. Each batch is one research run with
five topics at most, then one verification run.

| Batch | Topics | Starts with |
| --- | --- | --- |
| A (complete) | 0.6 foundation audit and design, 2.4 entities with the remote player (research part of 6.1), 2.1 ids, 2.3 exact position | The fix round of stage 1.2 |
| B (complete) | 1.8 quest state, 2.2 time and weather, 2.5 interaction, 2.6 death and travel, 2.7 map | 2026-10-13 |
| C (complete) | 3.1 attack actions, 3.2 damage model, 3.3 abilities and effects, 3.4 creatures, 3.5 loot, 3.6 gathering, 3.7 crafting | 2026-10-13. The combat part of 3.2 is not ready: see batch D. |
| D (complete) | 2.8 notices, 3.2 hit reports of the client, 4.1 quest progress, 4.2 conversations, 5.1 shops, 5.2 storage | 2026-10-13, one thread for each topic. The hit reports are not ready: live check L10 decides. |
| E part 1 (complete) | 4.5 factions, 5.4 territories, 5.5 housing, 5.6 cosmetics, 5.7 mounts | 2026-10-13 |
| E part 2 (complete) | 4.4 achievements, 4.6 seasons, 5.3 trading post, 6.2 chat, 6.3 groups, 7.1 expeditions | 2026-10-13. 7.1 is not ready: the level change needs a live check. |
| E part 3 (first pass complete) | 5.8 music, 6.4 player trade, 6.5 companies, 6.6 player against player, 7.2 world events, 7.3 wars | 2026-10-13. No verification now: these systems are late in the sequence. |
| F (complete) | Priority research for the combat stages: the melee hit mode of the client (`melee-mode.md`, verified: a remote configuration reply of the backend turns the mode on), the enemies of the start area (`enemies-start-area.md`, verified; stage 3.4a gave the live proof of its records and corrected its action index rule) | 2026-10-13 |

Each topic is one thread, and each research file gets one verification thread with a
different method. The research files are in `~/Games/soulwarden/work/research/`.

Fault of the tool `tools/st` (found by four verifiers on 2026-10-13): it gives a wrong group
for a state type whose constructor the decompiler shows with omitted arguments, and it
omits the state type 3210 and one field. It printed no warning. The audit
(`state-layout-audit.md`) found a wrong layout for 27 of the 130 state types, and no wrong
byte example in a research file. The correction is in the tree from 2026-10-13
(`tools/statetype.py`, `tools/state_disassembly.py`): the tool reads the raw code of a
constructor and refuses a layout that it cannot prove. The states that the backend sends
have live proof.

Decisions of the user (2026-10-12):

1. Creature behavior (3.4) when the game files have no behavior data: simple rules for each
   creature family first. A copy of the original behavior is later work.
2. More than one player: one player until milestone 4 is complete. But the foundation for
   more than one player is important now: the rules of section 1.6 apply to each system, and
   the stages 0.6a and 0.6b come before the systems that add player state.
3. Start of a new character: the beach quest first (1.8). The introduction level `ftue_v2`
   comes later (4.3).
4. The tool window is on in each start, with the key F8.

Decision of the user (2026-10-13):

5. The backend services are in Go. The hook library stays as it is. The repository is a
   monorepo (section 1.7). The Go port starts when milestone 1 is complete, and it has no
   research step: only implementation. The tool window stays in the hook DLL; its admin
   interface is a service of the backend.
6. Priority: creature spawn and combat as early as possible, and the social systems last
   (section 2.1 has the sequence).
7. The path search of a creature (stage 3.4d) comes directly after stage 3.4b, before loot
   (2026-10-13). The game copy has no navigation data: the original mesh was only on the
   server. A tool makes the data from the collision shapes of the game copy (research file
   `navigation.md`).

8. Patrol and wander (stage 3.4e) come before loot (2026-10-14): the user asked for a
   creature that patrols an area, follows and attacks a player that it sees, and runs back
   to its spawn point at its leash. The itemization (stage 1.3b) comes after loot, when its
   research has its second check: the user wants a full character build with the perks,
   the gems, and the heartrune of its items.

9. The order after the itemization (2026-10-15): the abilities of all weapons (3.3b), the
   interaction base (2.5), chests and loot on the ground (3.5b), world time and weather
   (2.2), gathering (3.6), crafting (3.7), shops (5.1), cosmetics and transmog (5.6), the
   generic quest engine (1.8), and the acceptance test (1.9). The Go port comes at the end
   of the roadmap (section 2.1).