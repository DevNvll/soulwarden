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
as it is (C++), and the repository is a monorepo. The Go port starts when milestone 1 is
complete (stages P.1 and P.2, section 4). It has no research step. Until then the stages
continue in the C++ backend, and the layout below is the layout after the port.

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
| 1.2 | Character creation and deletion | 1.1 | M | done | done | done |
| 1.3 | Inventory and currency | 1.2, 0.6b | M | verified | done | |
| 1.4 | Equipment and visible gear | 1.3 | M | verified | done | |
| 1.5 | Level, experience, attributes | 1.2, 0.6b | S | verified | done | |
| 1.6 | Weapon mastery, ability bar, trade skills | 1.5 | M | verified | done | |
| 1.7 | Tool window and admin interface | 1.3, 1.5 | M | verified | done | |
| 1.8 | Original quest state of a new character | 1.4, 1.6 | M | verified | | |
| 1.9 | Acceptance test from an empty database | 1.1 to 1.8 | S | not necessary | | |
| P.1 | Go port, part 1: monorepo, HTTP services, and store | 1.9 | M | not necessary | | |
| P.2 | Go port, part 2: game server, world, and the systems of milestone 1 | P.1 | L | not necessary | | |
| 2.1 | Client message base | 0.3, 0.6b | S | verified (ids), first pass (messages) | | |
| 2.2 | World time and weather | 2.1 | S | verified | | |
| 2.3 | Exact position and full movement | 0.4, 0.6b | L | verified (position) | 2.3a done | |
| 2.4 | Other entities and area of interest | 2.1, 0.6b | L | verified | | |
| 2.5 | Interaction base | 2.4 | M | verified | | |
| 2.6 | Death, respawn, camps, fast travel | 2.3, 2.5 | M | verified | | |
| 2.7 | Map, territories, markers | 2.1 | M | verified | | |
| 2.8 | Notices and HUD services | 2.1 | S | verified | | |
| 3.1 | Weapons in the hand and attack actions | 1.4, 2.3 | L | verified | a part in 3.2a (the attack inputs, the weapon keys) | |
| 3.2 | Damage, stamina, and mana model | 3.1, 2.4 | L | verified (`damage.md` command, `hit-reports.md` layouts, `melee-mode.md`); the live checks M1 and M2 of the melee mode passed | 3.2a done | |
| 3.3 | Abilities, cooldowns, status effects, perks | 1.6, 3.2 | L | verified | | |
| 3.4 | Creatures: spawn, behavior, navigation | 2.4, 3.2 | XL | verified (`creatures.md`, `enemies-start-area.md`) | 3.4a done, 3.4c in work | |
| 3.5 | Loot and rewards | 1.3, 3.4 | M | verified | | |
| 3.6 | Gathering and fishing | 1.3, 1.6, 2.5 | M | verified | | |
| 3.7 | Crafting, refining, repair, salvage | 1.3, 1.6, 2.5 | M | verified | | |
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
| 4 | 3.4c | Creatures in the whole world: the creature file for each region (33,328 spawn points, 541 slices in the supported build), a picture check of each slice, the load of the spawn rule, and a travel command of the tool window | 3.4a | in work |
| 5 | 3.4b | A creature fights: it goes to the player, attacks, and goes back at the leash distance; the player can die and come back (the necessary part of 2.6) | 3.2a | open |
| 6 | 3.5a | Loot and experience from a kill; the creature spawns again | 3.4b | open |
| 7 | 1.8 | Original quest state of a new character | 1.6 | open |
| 8 | 1.9 | Acceptance test of milestone 1, with the regression of all stages before it | 1.8 | open |
| 9 | P.1, P.2 | Go port | 1.9 | open |

After the Go port, the sequence is the sequence of the summary table, with these changes:
the other parts of the systems 3.1 to 3.7 come before milestone 4, and milestone 6 comes
after milestone 7.

Consequence for the Go port: it comes after the first combat stages, not directly after the
character systems. The stages 3.4a to 3.5a are in C++, and the port has them as reference
code. The user can change this.

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
    character in the main menu after a change of equipment; the head armor and the weapons
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

- Client evidence: `Objectives` (slot 22, type 3857), `ObjectiveInteractor`.
- Research: `quests.md` (batch B, verified). The HUD pins a quest only when the seasons code
  of the client is ready: the state of slot 73 (type 5606) must come after the first record
  (`seasons.md`, first pass, verification in work). Decision of the user: the beach quest
  (`06_getoffbeach`) first, not the introduction (`ftue_v2`).
- Implementation: the objective state of a character in SQLite; the first quest active and
  tracked for a new character; the original start position.
- Test: a new character enters the world at the original position, and the HUD shows the
  first quest with its tasks. This system does not include progress (system 4.1).

### 1.9 Acceptance test from an empty database

One scenario script goes from an empty data directory to a character with items, equipment,
level, skills, and the first quest, with a screenshot of each step. An independent agent
compares each system of milestone 1 with its criteria.

### Go port: stages P.1 and P.2

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
- The systems of milestone 2 and later are in Go from their first version.

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
- Research (`interaction.md`, verified): the interaction request and the reply; the prompt on the screen; timed
  interactions; the stations and doors of a settlement.
- Implementation: an interaction service that other systems register with (storage, shops,
  crafting stations, gathering, conversations).
- Test: the prompt shows at a station, and the E key opens its window.

### 2.6 Death, respawn, camps, fast travel

- Client evidence: `IncapacitatedCharacter`, `Camping`, `PlayerHome`, `ServerTransfer`;
  the death and teleport notices (types 3950, 3954); the teleport counter of the movement
  state.
- Research (`death-travel.md`, verified): the death sequence; the respawn selection; the camp; the inn check-in;
  fast travel between shrines and settlements.
- Implementation: death and respawn with no enemy (a command sets the health to 0); the
  respawn points in SQLite; teleport.
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
- Test: screenshots of a creature at its spawn point, of a fight, and of its death; the
  health of the player goes down when the creature hits.
- Risk: no data for the behavior. Then the behavior is a design of this project. Decision
  of the user: simple rules for each creature family first (section 12).

### 3.5 Loot and rewards

- Client evidence: `LootTracker`, `ItemDrop`, the loot tables of the game data.
- Research (`loot.md`, verified): the loot tables and their conditions; the loot window; world containers;
  experience and weapon mastery experience for a kill.
- Test: a dead creature gives items to the bag, and the experience bar moves.

### 3.6 Gathering and fishing

- Client evidence: `Gathering`, `Fishing`, the gatherable tables.
- Research (`gathering.md`, verified): the nodes in the world (entities of 2.4); the gathering sequence; tools;
  yields; the respawn timer; trade skill experience.
- Test: screenshots before and after a tree is cut, with the wood in the bag and the skill
  experience higher.

### 3.7 Crafting, refining, repair, salvage

- Client evidence: `Crafting`, `ItemRepair`, `Camping`. Salvage messages 1733 and 6188.
- Research (`crafting.md`, verified): the recipe tables; the craft request and its result; stations and their
  tiers; the gear score roll; repair parts.
- Test: an item crafted at a station is in the bag; the materials are gone; a repair sets
  the durability to its maximum.

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
