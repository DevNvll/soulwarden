# Instances and spaces

Read this document before a change of the spaces of the world, the instances (the story instances of the quests and the expeditions),
the move of a character between two spaces, or the level change of a session in one connection. The wire contracts are in the protocol
notes (section 4.66, and section 3.5 for the account rule). The world, the entity table, and the interest of a session are in
[World and sessions](world-and-sessions.md); the position rules in [Position](position.md).

## Start here

| Task | Start here |
| --- | --- |
| The space of an entity, the position index by space, the one path of a move | `apps/backend/src/world/ids.h` (`SpaceId`, `kMainSpace`), `world/entity.h` (`Entity::space`), `world/position_index.h` (`PositionIndex`, `for_each_in_space`), `world/world.h` (`World::move_to_space`, `for_each_near`, `has_space`, `level_of`, `clear_space`, `SpaceChange`, `SessionSink::space_changed`), test cases IN1, IN2 (`tests/instance_tests.cpp`) |
| The instance object, its rows, the recovery of a backend stop | `world/instances.h` (`Instances`, `Instance`, `InstanceMember`, `InstanceError`, `kMainLevel`, `kMainTerrain`), `world/instances.cpp`, `state/store.h` (`InstanceMemberRow`, table `character_instances`, schema 21), test cases IN6, IN7 |
| The level change of a session in one connection, the account rule of the version and the context instance, the 5181 of the client | `server/rep/rep_session.cpp` (`RepSession::space_changed`, `change_level`, `next_level_context`, `handle_level_acknowledgement`, `send_level_information`), `server/rep/level_contexts.h` (`LevelContexts`), `service/rep_server.h` (the table of the game server), test cases IN4, IN5, C1 |
| The game modes (the maps of the instances) and their tool | `gamedata/game_modes.h` (`GameModes`, `GameMap`), `tools/gamedata/game-modes.sh` (`game_modes.py`, `check_game_modes_tool.py`), game data `game-modes.json`, [Game data](../development/game-data.md), test case IN3 |
| The commands | `server/http/instance_commands.cpp`, `server/http/character_commands.h` (`enter_instance`, `return_instance`, `instances_json`, `game_maps_json`), the development routes `/v1/dev/world/instance/enter`, `/v1/dev/world/instance/return`, `/v1/dev/world/instances`, `/v1/dev/world/game-maps`, `tools/world instance` |
| Two sessions (MP-10), the scans of the systems, the load | Test cases IN8, IN9, IN10, IL1 |

## Rules

The source of each rule is in brackets: [client ADDR] the client code (build 1.400.6031.6004151, image-relative addresses), [data] the
game data, [live] a live check (the probe of research dungeons.md section 2, and the live checks of stage DG1), [agent] a decision of the
agent of stage DG1 where the client and the data do not decide, [orchestrator] a pick of the orchestrator that the user can change.

### The spaces

- The instance model is I1 of research dungeons.md section 7: an instance is a space of the one world object, with one thread, one clock,
  and one store; not a second world object [orchestrator: decision D8].
- Each entity has a space (`Entity::space`). The main world is `kMainSpace` (0). Each instance has its own space from the allocator of
  the instance system: 1, 2, ...; a space is not given two times in one backend process [agent].
- The position index has the space in its key: a cell is (space, column, row). `World::for_each_near(space, center, range, visit)` gives
  only the entities of that space, also for a large circle (the walk of the cells stays in the space). An instance level has its own
  coordinates (Amrine: the start 645, 663, 153 [data: TeamTeleportData of the map row]), and a place of the main world can have the same
  numbers: a search with no space would find the entities of the main world there [agent].
- One path moves a character to a different space: `World::move_to_space(character, space, place)` (rule MP-2). Only the instance system
  calls it, after its rows are in the database; the function writes no row. It ends what the character does in the old space (the rules
  of a travel: the basic attack, the aim, the dodge, the committed interaction, the conversation; and its spells and projectiles, which are
  entities of the old space), moves the entity out of the cells of the old space and into the cells of the new one, gives it the next
  teleport number, and marks its movement state. Each session that observes the character compares its interest at once
  (`last_travel`). The session of the owner gets `SessionSink::space_changed` [agent; the steps of the session are [live]].
- An encounter of the old space loses the character as after a travel: the character is out of its detector, and its link goes after the
  grace time of the recipe [agent: the rule of a travel].
- The level of a space: the main world has the level `newworld_vitaeeterna` and the terrain `coatlicue/newworld_vitaeeterna` (the wire
  constants of the world entry, protocol notes 4.1 [live]); an instance has the level and the terrain of its map row: the terrain project
  `CoatlicueName` in lower case with "/", and its last part as the level text (`coatlicue/nw_dungeon_windsward_00`,
  `nw_dungeon_windsward_00`) [data; live: the client loaded Amrine with these texts].
- The end of an instance removes each other entity of its space (a probe of a command; the spells and the projectiles of a member end
  with its move): `World::clear_space`. The space is then free and is not given again [agent].

### The session

- A level change in one connection (`RepSession::change_level`) has the steps of the live probe of research dungeons.md section 2.1, in
  this order [live]: the session forgets each record (the client makes a new client context: `ClientContext::Initialize` 0x171B850 cleans
  the interest table of the old one [client 0x16835B0]), the own entity has the interest number 1 again; the next version and the next
  context instance (the account rule below); the bundle sequence starts again at 0 (the reset 0x6463540 of the client sets the expected
  sequence to 1 [client]); the self identification 1628 with the new instance in its level block; the level information 1635 with the
  level of the space of the entity, the new instance, the reload flag 1, and the new version; the spawn point notice 1617; an empty bundle
  with sequence 1. The next tick sends the full owner record (sequence 2, interest 1, the metadata state first).
- The session does not wait for 5181 (`PlayerComponentServerFacet_OnAckLevelInfoChanged`, body target:u64 entity:u64 value:u8, reader
  0x65917B0 [client]). The client sends it at once, before its load, with the new instance as its byte (live: 11 ms to 26 ms after the
  level information, the bytes 2, 3, 4, 6). The backend writes one log line [live].
- A level change sends no 3746 and no 6709 (the end of the input lock of a travel) before the level information: the world ended the
  interaction and the conversation, and the client gets nothing in its old context (the sequence of the probe) [agent; live: no lock in
  the four changes].
- The session takes the level texts from the space of its entity (`World::level_of`), also at the world entry: a newer session that takes
  the entity of a character in an instance (a session replaced) sends the level of the instance [agent].
- The account rule of the version and the context instance [client 0x6446800: the client does nothing for a version that is its last
  version (0x64468B1), and it makes no context for an instance that is its last instance (0x6446B47); the values stay over a new
  connection, protocol notes 3.5]. The backend cannot see the game process: the game server keeps, for each account (persona id), the
  last version and the last context instance that it gave (`LevelContexts`), and each new level information of that account (a world
  entry, a level change) gets one more version and the next instance (1 to 255, never 0). The first one of an account in a backend
  process has the values of the session id, as before this stage (the stored bytes of case T2) [agent: the key is the account]. The
  trap of the session rule: session 1 moves to instance 2 with a level change, and session 2 of the same game process would start with
  instance 2: the client keeps its context and stays in `WaitingForPlayerSpawn` (research dungeons.md 2.4; case IN5). Live: a second
  connection after three level changes got version 5 and instance 5 and entered the world.
- The quest states (the gate of the first 2709 of stage 1.8a) are not gated again after a level change: the Objectives state comes in the
  full owner record of the new context [agent; live: the tracker and the journal after each load].
- The world entity (slices/globalgde, the LandClaimManager state) is in the interest table of each session in each space (the client got
  it in Amrine with no fault) [agent].

### The interest of a session

- The interest scan of a session uses the space of its own entity: the candidates come from the cells of that space. An entity of a
  different space goes out of the interest table at the next scan (the stop of its interest number): the observer in the main world of a
  character that moved into an instance gets the stop at once, and it keeps its level, its context instance, and its bundle sequence
  (case IN8) [agent; MP-2].
- The objects of the interaction system (the level objects, the server children, the loot bags) are objects of the main world: a session
  in an instance observes none of them [agent: the objects of an instance level are stage DG2].

### The instance object

- An instance is a world object (rule MP-1): its id (1, 2, ... in one backend process), its space, its map row (GameModeMapId) and its mode
  (GameModeId), its level, its start (the first team of TeamTeleportData) and its heading, its capacity, its start time (the world clock),
  and its members by character id with the return place and the entry time of each one. A session stores no instance state.
- The capacity is TeamCapacity of the mode [data]; a mode with the capacity 0 (a solo trial) has one member place [data: IsSoloTrial;
  agent: 0 is one].
- The entry (the development command `instance enter`): the character is in the world and in the main world; the backend has the game
  modes; the map is there (a map id, or a mode with one map); with an instance id, the instance is there, of the same map, with a free
  member place. A refused entry changes nothing (rule MP-5) [agent]. With no instance id each entry makes a new instance [agent].
- The return place is the place of the character at the entry [orchestrator: decision D9]; the mode rows have no place of a return
  (WorldPosToTeleportBackTo is blank in each instance row [data]).
- The entry is one transaction (rule MP-7): the position row of the character gets the return place, and the member row (the instance id,
  the map, the return place, the wall time of the entry). Then the system makes the instance (or adds the member) and calls
  `World::move_to_space` to the start of the map. With no commit nothing changes [agent].
- While a character is in an instance its position row keeps the main world position (the return place): the save rule writes no
  position of an instance space, and a travel in an instance moves the character in its space and writes no row [agent; live: the row of
  the remote character kept its main world place after a travel in Amrine].
- The exit (the development command `instance return`) is one transaction: the position row gets the return place, and the member row
  goes. Then the member goes, and `World::move_to_space` moves the character to its return place in the main world. A refused exit
  changes nothing: the character stays in the instance [agent].
- An instance with no member ends at once [agent: an instance of this stage has no content; the timeout of an empty instance
  (TimerThresholdDefault 600 s [data]) is stage DG3].
- A member that leaves the world with its entity (the end of its session, a removal, a delete, a backend stop that ends the sessions) does
  the exit rows; its next world entry is at its return place in the main world [orchestrator: decision D9; live: a leave to the main menu
  from Amrine, then Play]. A newer session that takes the entity of the character (a session replaced) keeps it in the instance [agent].
- A backend stop ends each instance. The member rows of a stop with no exit (a crash) go at the first tick or the first world entry of the
  next start: in one transaction each character of a row gets its return place, and the row goes [agent; live: the backend of the run was
  stopped with `kill -9` in Amrine; the next start wrote "instance 3 (DungeonAmrine) of an earlier start ended with the backend stop", and
  the login was at the return place].
- A respawn in an instance is at the start of its map, in its space [agent: the checkpoints (respawn type 10) are stage DG3].
- An instance has no content in this stage: no creature, no object, no level data (stage DG2). A creature of a command near a character
  in an instance is refused (409) [agent].
- A map whose level is the main world (the story instances in `newworld_vitaeeterna`, for example QuestCaerDun) is an instance with its own
  space and a level change to the same level text [agent; not checked live: open problem of stage DG1].

### The systems in an instance (the global scans)

Each scan of a system that reads all entities of a kind, or the main world data at the place of each character, tests the space or uses
the index of the space (rule MP-8). A system whose data is the level data of the main world acts only for a character of the main world
until the level data of an instance comes (stage DG2) [agent]:

- the spawn point rule of the creatures (the places of the characters), the placements of the encounters, the server children of the
  interaction system, and the tiles of the navigation that a character needs: only the characters of the main world;
- the vision of a creature, the roam selection, the "observed" rule of a creature, the crowd rule, the target of a creature (a target in a
  different space is lost): the space of the creature or of the character;
- the abilities, the spells, the projectiles, the passive rules, the heart rune, the chain of a perk: the space of the caster, of the
  spell, of the creature, or of the target;
- the interaction request (a use of an object, a gather, a station, a person): refused in an instance (no object there);
- the LootTracker of the chests, the bags of the LootTracker and of the storage map, the discard of an item (refused in an instance: a bag
  is an object of the main world), the proximity quest objects, the place tasks and the trigger tasks of the quests, the location rule of
  a drop, and the territory of a character (`World::territory_of`: 0 in an instance): only the main world.

The full list of the scans and the reason of each one is in the result of stage DG1
(`~/Games/soulwarden/work/evidence/dungeons/dg1/implement/RESULT.md`, criterion I4).

## Commands

```sh
tools/world instance maps [TEXT]                 # the maps of the game modes (game-modes.json)
tools/world instance enter CHARACTER MAP [N]     # a new instance of the map (or the instance N of that map), at the start of the map
tools/world instance return CHARACTER            # back to the return place in the main world
tools/world instance list                        # the instances, their members, and their return places
tools/world list                                 # each entity with its space (0: the main world) and the line "instances"
```

The commands are in the development routes only (the harness gate) until stage DG3 gives the entrance of the game.

## Traps

- More than one gamescope can run on this computer: a picture with no `GAMESCOPE_WAYLAND_DISPLAY` can take the window of a different
  project. Read the display from the environment of the backend of the run (docs/development/live-runs.md).
- The client shows the territory of the main world on its compass in an instance (research dungeons.md, open problem P6), and the buttons
  "Map" of the quests are not in the journal there. The tracker of the quests can show the text of an active task of a place of the main
  world two times in an instance (the task `06_crewmates_02_TaskGoToPOITag_shipwreck1` and the inactive kill task of the same objective
  have one text; live: one line in the main world, two lines in Amrine). The Objectives state is the same state of the entity: this is a
  rule of the client display, not a lost state.
- The client writes `HandleUnexpectedMessage skipping ReceiveStateBundle for bundle message due to clientContext == nullptr` at each load,
  also at the first world entry, and at once after the load `ProcessPendingReliableMsgQueue processing 1 messages from
  m_pendingUnexpectedMsgs`: it keeps the bundle that starts the load and applies it after the load (open problem P2 of research
  dungeons.md; live: six loads, six pairs of lines, the bag, the gear, the coin, the quick slots, and the journal the same).
- A test that changes the level must count the messages of the old context: the bundle sequence of the session starts again at 1, and
  the interest number 1 is the own entity again.
- The account rule changes the values of a second session of an account: case C1 expects instance 2 for the client of session 1 that
  connects again as session 3 (the next instance of its account), not the instance of the session id.
