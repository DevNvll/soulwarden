# Instances and spaces

Read this document before a change of the spaces of the world, the instances (the story instances of the quests and the expeditions),
the move of a character between two spaces, the level change of a session in one connection, or the entrance, the membership, and the
leave of a game mode. The wire contracts are in the protocol notes (section 4.66, section 4.67 for the entrance and the membership, and
section 3.5 for the account rule). The encounter of a mode is in [Encounters](encounters.md). The world, the entity table, and the interest of a session are in
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
| The entrance of a game mode, the result 3094, the exit portal, the empty timer (stage QG9a) | `world/instances.cpp` (`EntranceHandler`, `ExitHandler`, `Instances::entrance_result`, `enter_mode`, `in_mode`, `start_content`, `member_gone`, `content_succeeded`, `dungeon_result`), `gamedata/interaction_data.h` (`DungeonEntrance`, kinds `dungeonEntrance` and `dungeonExit`, `instance_object`), `server/rep/instance_messages.h`, test cases SI1, SI6, SI7 (`tests/story_instance_tests.cpp`) |
| The GameModeParticipant state, the leave 3309 and 1980, TaskGameMode (stage QG9a) | `world/entity.h` (`GameModeMembership`), `Instances::membership_changed`, `server/rep/states/game_mode_state.cpp`, `RepSession::handle_game_mode_request`, `world/quests.cpp` (`task_type::kGameMode`, `mode_changed`), test cases SI2, SI3, SI6 |
| The corrections of the code review of stage DG1 (the paths that cross two spaces) | `tests/review_dg1_tests.cpp` (cases DG1R1 to DG1R8), the section "The paths across two spaces" below |
| Stage DG2: the level data of an own level (its set, its read at the first space of the level, the spawn points of each space, the navigation of each level) | the section "The level data of an own level (stage DG2)" below, `gamedata/level_data.h` (`LevelData`, `LevelFolder`, `LevelSource`), `world/levels.h` (`Levels`), `World::navigation(space)`, `world/creatures.h` (`SpawnSet`, `level_space_started`, `data_of`), `tools/gamedata/level.sh` (`level_data.py`, `check_level_tool.py`), test cases LV1 to LV5 (`tests/level_tests.cpp`) |
| Stage DG2, part 5: a level object of the main world level in a story instance (the statue of Taurus) | `world/interactions.cpp` (`check`), test case LV6 (`tests/story_level_object_tests.cpp`) |
| Stage DG2: the corrections of the code review of stage QG9d (the floor wait of a spawn, a quest object after a reset) | `world/encounters.cpp` (`spawn`, the end rule), `world/instances.cpp` (`look_quest_objects`), `tests/review_qg9d_tests.cpp` (cases QG9DR1 to QG9DR3) |
| Stage QG9d: the quest objects of a mode root, the exit portal of a mode root that the client makes | `world/instances.cpp` (`InstanceQuestObject`, `look_quest_objects`, `quest_object`, `start_content`), `world/interactions.cpp` (`check`), `gamedata/interaction_data.h` (`InteractionObject::exitMode`), `tools/gamedata/interactions.py` (`exit_children`, `mode_root_slices`), `tools/gamedata/gathering.py` (`instance_object_form`, `instance_capital`), test cases QG9D2, QG9D3 (`tests/story_mode_qg9d_tests.cpp`), the section "The story instances of stage QG9d" below |
| The other story instances (stage QG9b), the corrections of the code review of stage QG9a | the section "The other story instances" below, `world/instances.cpp` (`exit_pending`, `m_unrecovered`, the `committed` call of `enter_mode`), `world/quests.cpp` (`predicate`, `mode_task_active`), `world/abilities.cpp` (`space_left`), `tests/review_qg9a_tests.cpp` (cases QG9AR1 to QG9AR6, QG9ARC), `tests/story_mode_tests.cpp` (SY1 to SY5; SY5: the setting of QuestMedusa, `encounter_rules::kQuestMedusaRuns`, `World::set_quest_medusa`) |

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
  in an instance observes none of them [agent: the objects of an instance level are stage DG2]. Stage QG9a: a server child that the
  instance system makes in the space of an instance (the exit portal of a mode) is an object of that space only [agent].

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
- An instance with no content ends at once when its last member goes [agent]. Stage QG9a: an instance with content (the encounter of its
  mode or its exit portal) waits for TimerThresholdDefault of its mode (600 s [data]) with no member; a former member that enters the mode
  again in that time joins it (its timer stops), unless its content succeeded; then the entry makes a new instance [agent]. At the end of
  the time the instance ends with its content (the encounter system ends the runs of the space, then the portal goes).
- A member that leaves the world with its entity (the end of its session, a removal, a delete, a backend stop that ends the sessions) does
  the exit rows; its next world entry is at its return place in the main world [orchestrator: decision D9; live: a leave to the main menu
  from Amrine, then Play]. A newer session that takes the entity of the character (a session replaced) keeps it in the instance [agent].
- A backend stop ends each instance. The member rows of a stop with no exit (a crash) go at the first tick or the first world entry of the
  next start: in one transaction each character of a row gets its return place, and the row goes [agent; live: the backend of the run was
  stopped with `kill -9` in Amrine; the next start wrote "instance 3 (DungeonAmrine) of an earlier start ended with the backend stop", and
  the login was at the return place].
- A respawn in an instance is at the start of its map, in its space [agent: the checkpoints (respawn type 10) are stage DG3].
- An instance of a different level has the content of the data set of its level (stage DG2, the section below); with no set it has no
  content. A creature of a command near a character in an instance is refused (409) [agent].
- A map whose level is the main world (the story instances in `newworld_vitaeeterna`, for example QuestMedea) is an instance with its own
  space and a level change to the same level text [agent; live: stage QG9a]. Its space uses the terrain, the navigation, and the collision
  data of the main world (`World::main_level`) [agent: the instance has only its own content].

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
- the interaction request (a use of an object, a gather, a station, a person): in an instance only a server child of the space of the
  character (the exit portal; stage QG9a), and (stage QG9d) the exit portal of the level data of the mode of the instance of the character;
- the LootTracker of the chests, the bags of the LootTracker and of the storage map, the discard of an item (refused in an instance: a bag
  is an object of the main world), the proximity quest objects, the place tasks and the trigger tasks of the quests, the location rule of
  a drop, and the territory of a character (`World::territory_of`: 0 in an instance): only the main world.

The full list of the scans and the reason of each one is in the result of stage DG1
(`~/Games/soulwarden/work/evidence/dungeons/dg1/implement/RESULT.md`, criterion I4).

### The story instances of the main world level (stage QG9a)

The first one is QuestMedea (quest 06A_MSQ_21, the boss Medea in Thornhall). The sources: the static read E of stage QG9a
(`~/Games/soulwarden/work/evidence/quests/qg9a/implement/static/E/ANSWERS.md`) and the live runs L1 to L5 of stage QG9a.

- The entrance is a level object of the kind `dungeonEntrance` (interaction data format 6, member `entrance`): the mode of its
  DungeonEntranceComponent (`m_gameMode`; never the text of the option: the Medea entrance offers EnterQuestCaerDun), the solo flag, the
  slot and the facet target of the component, and its exit point [data; client 0x2FF5720]. OpenDungeonMenu (action 71) is accepted: the
  client opens its window itself. The Enter option (action 72) runs the checks of `Instances::entrance_result` and sends 3094 [client
  0x2FFB4F0; live].
- The checks of the entry, in this order: the map of the mode is there (one map, of that mode); the character is in the main world and in
  no instance (else 11, GroupAlreadyInDungeon); its level is RequiredLevel of the mode or more; the backend runs the content of the mode
  (`Encounters::runs_mode`: each instance recipe of the mode is taken by the runner). A refusal is 10 (EntranceActivationFailure) and
  changes nothing [agent: the client has no value for "content that the server does not run"]. The required item of the Enter option of
  the data has the count 0: no item [data; live].
- The success: 3094 with 0, and the entry in the next tick of the world (`World::after`), so that the reply 3743 of the request goes first
  [agent; live: a move inside the request ends its committed use]. The client sends no 946 before the level change, and the backend needs
  none [client: E1c; live].
- A new instance of a mode starts its content after the move of the character (the participants are in the space): the encounter of each
  instance recipe of the mode waits in the space for a character in its detector (`Encounters::start_space`), and the exit portal of the
  mode (the server children of the mode root slice, `instanceObjects` of the encounter data, a template of the kind `dungeonExit`) is made
  in the space at its place of the data [agent: the mode script that spawns it is not in the client]. A recipe of a mode never runs at its
  placement in the main world [agent].
- The membership of a character is the instance of its space (`Instances::in_mode`). It is in the entity for the encoder
  (`Entity::gameModeMembership`: the reserved actor of the instance, its G, the CRCs of the mode and of the map, the entry time) and goes at
  the leave; the GameModeParticipant state of the owner shows it (protocol notes 4.67) [client 0x32BA3B0, 0x32B78C0; live]. A session
  stores no instance state.
- TaskGameMode (task type 54) is done while the character is in the mode of its GameModeID, and never for a task with
  GameModeMustBeMutated 1 (the backend has no mutation) [client 0x3AD5CC0]. A kill, an event, or an item task with GameModeID counts only
  in that mode (the matched tasks) [client 0x3AD5CC0; live: the kill task of Medea]. The quest system looks again at the tasks of a
  character at each change of its membership (the entry, the leave, a new session) [agent].
- The leave: the exit portal (ExitDungeon, action 74; only a portal of the instance of the character, in its space), 3309 with the key of
  the membership of the character, and 1980 with the leave event 9BB080D0 [client 0x3273BB0, 0x26FEC60; live: the portal]. Each one is
  the exit of an instance (its rows, the move to the return place), in the next tick. 3309 with an other key changes nothing [agent].
- A death in the instance: the respawn is at the start of the map in its space (the rule of stage DG1) [agent; live]. The encounter of the
  mode resets when its participants are dead or away for its grace time, and starts again at the next character in its detector
  (the rule of the encounters) [data: the grace time of the recipe; live].
- The success of the content: when each encounter of the mode in the space succeeded (`Instances::content_succeeded`). The instance keeps
  its members; a later entry after its success makes a new instance [agent].
- The cost: each rule reads only the instances, their spaces, and the recipes of their modes (MP-8). The empty timers are in the tick of
  the instances [agent].

### The other story instances of the main world level (stage QG9b)

Stage QG9b runs the solo modes of the main world level after QuestMedea. The encounter rules of these modes are in
[Encounters](encounters.md) (section "The instance recipes (stage QG9b)"). The sources: the static read F and the live runs of stage QG9b
(`~/Games/soulwarden/work/evidence/quests/qg9b/implement/`).

| Mode | Runs | The bosses and their declared behavior | Known limits (the actions of the boss trees that the client does not have) |
| --- | --- | --- | --- |
| QuestWolf (the Sorrowthorn trial) | Yes | the werewolf lord (part 2, melee attacks of the data) and its minions | the channel phase and the summons of the tree |
| QuestCaerDun | Yes | Brother Umberto (melee attacks of the data) | the drain locations; the heartgems of the mode script |
| QuestMedusa | Yes (stage QG9c: the setting below is on) | Medusa (declared cast `Attack_Poison_Spit_Single`), the boar, reindeer, and corvid minions | Gorgons Gaze, the maze, the tail attacks, the summons; the boar and the reindeer minions have no attack in the data |
| QuestSeraphim (4 recipes) | Yes | the blind, mute, and heartless seraphs (declared cast `Attack_WingSweep_L`), the breathless seraph | the braziers; the breathless seraph has no attack in the data |
| QuestHeartforge | Yes | the Heartforge Guardian (melee attacks, the cast Cast_One_Projectile, and the area attack of the data) | the flames of the pillars are gathers of the Azoth Staff |
| QuestMammoth | Yes | Mahantaram (melee attacks of the data) and the root caster | the root spells of the caster |
| QuestApophis | Yes | Apophis (declared cast `Attack_Projectile_Shot`), the Terra Myrmidons, the shaman minion (declared cast `Attack_Shard_Spike`) | the burrow phases pass at once (event rule) |
| QuestYonas (3 recipes) | Yes | the hallway, the Isabella intro, Yonas (declared cast `Combat_Ritual`) | Magic1, Magic2, Time_Bomb, DeathsDoor pass at once (event rule) |
| QuestEmpressZhou | No | | `Phase_2` tracks pedestals that no stage makes; a background activation of `BeforeIntro` |
| QuestVarlaam | No | | a defense of the device of the mode root |
| QuestGawain, QuestMorgaine | No (stage BT3) | | the corrupted idol and the altar of Gawain; the pedestals and the third phase of Morgaine |

- The setting of the mode QuestMedusa (`encounter_rules::kQuestMedusaRuns` in `world/encounters.h`, the one place of its value; the
  world option `questMedusaRuns` has it, and a test sets it with `World::set_quest_medusa`): on from stage QG9c (the cause of the stop of
  the client at the kill of Medusa by a hit is corrected: [Creatures](creatures.md), "The death of a slice with no reaction action"). Off:
  the runner refuses the recipe of QuestMedusa with the reason "the mode QuestMedusa is off by its setting
  (encounter_rules::kQuestMedusaRuns)", the entrance refuses the entry (3094 with 10, no move, no row), and the support manifest counts the
  rows of the mode as not supported with that reason. On: the rules of this section [orchestrator; case SY5].
- The bosses with no melee attack and no basic cast of the data get a declared cast (`gamedata::kDeclaredCasts`;
  [Creatures](creatures.md)) [agent].
- The entry: the checks of stage QG9a; the 3094 with 0 goes after the commit of the rows of the entry, before the move. A store that refuses
  the rows sends 10 (EntranceActivationFailure) and moves nothing; a refusal of the checks sends its result at once [agent; code review of
  stage QG9a, finding QG9AR-R6; case QG9AR6].
- The recovery of the instance rows at the start of the backend: a member row that the recovery cannot remove keeps its character out of
  the world (`Instances::exit_pending`) until a recovery succeeds; the newer position of the character is not overwritten [agent; finding
  QG9AR-R1; case QG9AR1].
- A task with a GameModeID counts only in its mode, also a task of a predicate (a level, a place, an item count): outside the mode its value
  does not grow [client 0x3AD5CC0; finding QG9AR-R3; case QG9AR3]. The world entry looks again at the mode tasks of a restored quest (a
  StayActive TaskGameMode is false outside its mode) [finding QG9AR-R2; case QG9AR2].
- A trigger area task (TaskTriggerArea) with a GameModeID counts in an instance of the main world level of that mode (the trigger volumes
  of the main level: `09A_M11_TalkToMammothAmbush`, `09A_M12_EnterArena`) [data; live: both done in their instances].
- A move to a different space ends the delayed work of the casts of the character, its dashes, and its motion marks
  (`Abilities::space_left`) [agent; finding QG9AR-R5; cases QG9AR5, QG9AR5B].
- An achievement conditional of an entrance can end with a line end (QuestApophis: `16_ORI_Necropolis_Unlock || (...)` and CR LF): the
  reader takes a line end as a blank [data; live: the client opens the window; case SY4].
- Live (stage QG9b, QuestMedusa with the mode on, before its setting): QuestMedusa, QuestMammoth, and QuestApophis from the entrance to
  the kill of the boss and the exit portal, with the
  quest task after the success (`09A_M12_DefeatMedusa`, `09A_M11_DefeatMammoth`, `1624_0801_d_02_02_Kill_1_Apophis`) [live].

### The live checks of stage QG9c

The evidence: `~/Games/soulwarden/work/evidence/quests/qg9c/implement/` (the videos, the pictures, the backend logs). The quest chains were
made short with `tools/world quest grant` and the development aid `tools/world quest advance` (stage QG9c).

| Mode | Entry | Boss killed by hits | Success of the content | Quest task after the success | Exit |
| --- | --- | --- | --- | --- | --- |
| QuestMedusa | yes | yes, three times (in an attack, in a chain of hits, idle) | yes | `09A_M12_DefeatMedusa` done ("Unpetrify Taurus") | `instance return` |
| QuestWolf | yes | yes (Lady Asena; with the floor rule of [Encounters](encounters.md)) | yes (`17_MSQ_AsenaFight2_GlobalSuccess`) | `Quest_18018_Task_18` done ("Talk to Cecilia") | the exit portal |
| QuestCaerDun | yes | yes (Umberto) | yes (the final seal: `tools/world encounter use`) | stays at `99B_MSQ_10_..._Interact_PlaceHeartgem` (the heartgems of the mode script) | the exit portal |
| QuestSeraphim | yes | yes (the mute seraph) | yes | each task of `Quest_28003` done | `instance return` (the open problem of the portal below) |
| QuestHeartforge | yes | yes (the guardian, after the four flames with the Azoth Staff) | yes | `99A_MSQ_15_01D_KillNamed` and the cutscene tasks done | `instance return` (a white screen after the cutscene until the exit) |
| QuestYonas | yes | no (the open problem below) | the hallway and the Isabella intro yes; the boss no | `ApproachFogwall` (the pressure plate, finding QG9BR-R4) done; `Kill_1_CorruptedYonas` active | `instance return` |

- Stage QG9c, open then: Yonas stands 43 m from the center of the detector sphere of his recipe. Stage QG9d: the axes rule of the engine
  ([Encounters](encounters.md)) does not move that center (the root of the slice has the world turn 0); the cause is the leash of Yonas
  and his boss tree (open, input of stage BT3; [Encounters](encounters.md), "QuestYonas").
- Stage QG9c, open then: the seraph portal of the level data and the Caer Dun seal. Stage QG9d: corrected or explained in the section
  below.

### The story instances of stage QG9d

The sources: the static reads AXES and AZOTH and the live runs P1 and P2 of stage QG9d
(`~/Games/soulwarden/work/evidence/quests/qg9d/implement/`).

- The quest objects of a mode root (QuestCaerDun: `PlaceHeartgem`, `SmashHeartgem`): an object of the mode root slice (`instanceObjects` of
  the encounter data) with a template of the kind gathering, a row of the gathering data (`encounterObjects` of `gathering-data.json`),
  and a game event that a task with the GameModeID of the mode names [data: the spawners of `caerdunscript` with m_spawnOnInitialize false,
  the rows `99B_MSQ_10_PlaceHeartgem` (Handcrafting, 3 s) and `99B_MSQ_10_SmashHeartgem` (Mining with a pick, 35 s), the tasks of
  `99B_MSQ_10_cleansefort`]. The instance makes each one (a server child of its space) while a member has an active task of the mode that
  names its event, and it goes after its gather; the gather has the rules of its row and gives its event to the gatherer
  (`Instances::look_quest_objects`, `quest_object`, `quest_object_gathered`) [agent: the mode script that spawns them is not in the
  client]. The encounters of the space wait until each quest object is gathered or a member has an active task that names a recipe of
  the space [agent from the quest data: the heartgem tasks come before the kill of Umberto]. A character with no such task never gets the
  objects, and the encounters of its instance wait [agent]. Count `questObjectsMade`, `questObjectsGathered` of `tools/world instance
  list`. Case QG9D2. Stage DG2 (code review of stage QG9d, finding QG9DR-R3): the gather ends the use of the object, not its task: when the
  object is gone and a member has its task active again (a reset of the quest), the instance makes the object again (count
  `questObjectsAgain`) [review; agent]. Case QG9DR3.
- The exit portal of a mode root that the client makes: the spawner `Portal` of the seraph mode root has m_spawnOnInitialize true, so the
  client makes the portal from the level data with the id of a child (client 0x6160B60, the rule of a person child) [data; live: the
  actor 67c3245614c36008 of the refused request of stage QG9c is this id]. The interactions tool writes it as a level object of the kind
  dungeonExit with the member `mode` (`exit_children`); the backend takes its ExitDungeon from a member of an instance of that mode (also
  in the space of the instance), and the instance system makes no server child for a portal of such a spawner [agent]. The modes that run:
  QuestSeraphim has this portal (QuestVarlaam too, a mode that does not run); the portals of QuestMedea, QuestWolf, QuestCaerDun,
  QuestMedusa, QuestHeartforge, QuestMammoth, QuestApophis, and QuestYonas have no spawn at the init and stay server children [data:
  `logs/exit-portals.txt` of the evidence]. Case QG9D3.
- The seal of Caer Dun shows "No equipped Azoth Staff Tier 1" when the rank of the trade skill AzothStaff is below the
  RequiredTradeskillLevel of its row (1); the prompt has one text for a missing tool and a low rank [client: the gate 0x35C26E0 (the rank
  compare 0x35C2941), the rank 0x3CB6330 from the CategoricalProgression state 911; script `gatheringtooladditionalinfo`; static read
  AZOTH]. The main story gives the rank 1 at `99A_MSQ_11_ForgeStaff_success` (before Caer Dun) [data]; the test character of stage QG9c had
  skipped that hand-in. No change of the backend [live: the seal with the rank 1].
- The white screen after the end cutscene of QuestHeartforge is the cinematic video `FTUE_Rit`: the stage `Wave_04 - TriggerCinematic`
  starts the IGC and the entity `Delay` (22 s), which starts `02_Trigger_IGC_Spawner_Bink` (PlayCinematicVideoComponent) [data]. In a
  development run (`-sys_audio_disable 1`) the play function returns before it opens the video (protocol notes 4.58): the screen stays
  white, and no 6606 or 6542 comes [live: the trace of 0x2CC7E70 with the entry `FTUE_Rit` and of 0x6506CB0, no 0x2EF9A00]. No task
  waits for this cinematic, and no server message ends the screen; the game of the user (with sound) plays it. No change of the backend.

| Mode (stage QG9d) | Check | Result |
| --- | --- | --- |
| QuestCaerDun | entry, the two heartgems, Umberto by hits, the seal (rank 1) | the success; next task `99B_MSQ_10_d_05_02_TriggerArea_UndercroftExit` ("Return to the surface") |
| QuestSeraphim | ExitDungeon at the portal of the level data | the leave to the return place |
| QuestMedea | the detector at the new center (axes) | starts in the new sphere only |
| QuestHeartforge | the end cutscene, the trace of the cinematic | the white screen is `FTUE_Rit` with no sound |
| QuestYonas | the boss stage | open (BT3) |

### The level data of an own level (stage DG2)

An own level is a level of a map row that is not the level of the main world: Amrine (`nw_dungeon_windsward_00`), the other expeditions,
the solo story levels with a level of their own, the arenas. The sources: research dungeons.md (sections 7 and 8) and the evidence of stage
DG2 (`~/Games/soulwarden/work/evidence/dungeons/dg2/implement/`).

- The data of an own level is a set in the folder `levels/LEVEL` of the game data folder: the manifest `level.json` (format 1: the level, its
  terrain folder, its maps, its parts), `creatures.json` (the spawn points of the layers of its terrain project, with their kinds and their
  slices: the form of the creature data of the main world), `navigation/` and `heights/` (its one region), and `interaction-data.json` (the
  objects of its capitals). `tools/gamedata/level.sh LEVEL` makes it; the level and its terrain folder come from the map row of the data
  (`CoatlicueName`), not from a constant [data; agent: the form of the set].
- The world reads a set when the first space of its level starts (`Levels::space_started`, from the instance system before the move of the
  character), in the world thread, one time for each backend process: the data stays until the stop of the backend, and each later instance
  of the level uses it [agent: a level set is small (Amrine: 2.2 MB on the disk, 8 ms, +2.7 MB in memory); the load case LV5 has a set of
  2,000 points]. A level with no folder, a set with no manifest, and a manifest of a different format (an old file) or of a different level
  give an instance with no content (the rule of stage DG1) and one log line. A part that its reader refuses has no data; the other parts
  stay [agent].
- Each instance of a level has the states of its own spawn points (`Creatures::SpawnSet` of its space): the spawn point rule makes the
  creature of a point of the level set in the space of the instance when a character of that space is near it (the distances of the main
  world). Two instances of one level have two creatures of one point; a character of the main world at the same coordinates makes and sees
  none of them (rule MP-8; case LV2). The end of an instance removes its creatures (`World::clear_space`) and then its states [agent].
- A creature of a level has the kind and the slice of its level set: each index of its data (the projectile of a ranged release, the route of
  its spawn point) is an index of that set (`Creatures::data_of`, `spawn_point_of`). The level table and the damage rules are the tables of
  the creature data of the backend (the same table of the game in each file): with no creature data of the backend the points of a level make
  no creature [agent].
- Each own level with navigation data or terrain heights has its own navigation (`Levels`, one `Navigation` with its loader thread, its tiles,
  and its paths). `World::navigation(space)` gives the navigation of the level of a space: the main world and a space of the main world
  level have the navigation of the main world; a space of an own level has the navigation of its set, or a navigation with no data (no
  ground, no wall). Each place of a creature, each path, each line test, each slide, and each floor of a spawn uses the navigation of the
  space of its subject [agent; case LV3: the floor of the level (30 m) and of the main world (22 m) at the same coordinates].
- The interaction objects of a level set are read (the part, its count), and the interaction system does not use them yet: a request in an
  own level gets the rules of stage DG1 (an unknown object) [agent: the interaction system has one catalog of the objects, the options, and
  the achievements, and the states of its objects by index; a second catalog is a change of each handler: stage DG3, with the exit portal
  stone of Amrine]. The chests of the layers of a level: the chest tool writes none for Amrine (its chests are objects of its mode root,
  stage DG5).
- The encounters of an own level (the encounter entries of its layers, the recipes of its mode root) are not in the set: stage DG5
  (expeditions) and QG9e (the solo story levels) [orchestrator scope].

The levels of the map table and their sets (the tool of stage DG2 on each own level): the evidence of stage DG2 (`logs/all-levels.tsv`).
No solo story level with a level of its own has a usual spawn point: each creature of such a level is a member of an encounter of its mode
root (the encounter entries of its layers) [data].

### The level objects of the main world level in a story instance (stage DG2, part 5)

- A level object of the main world data is there in an instance of a mode of the main world level when one bound of it has the InteractTag
  of an active task of the character with the GameModeID of the mode of the instance (`Quests::mode_task_active` with the key of the tag)
  [data: the task `09A_M12_SaveTaurus` (GameModeID QuestMedusa, InteractTag `IO_09A_M12_TaurusStatue`) names the statue of Taurus, a
  capital of the main world level (a quest gatherable, InteractOnlyOnQuest) in the arena of the mode; agent: the rule; live: the prompt
  "Free Taurus with the tincture", the gather of 2 s, the task done, the next task `09A_M12_TalkToTaurus`]. Each other level object of the main
  world stays unknown in an instance (case LV6).
- The inner gate of the Mammoth trial (`DungeonEntrance_QuestMammoth_Locked_V2`, a coatgen capital at 9464.7 1959.4 65.8) is a teleporter of
  the open world after the trial: the option `InteractTeleporter` (action 82), the conditional `09A_M11_MammothTrialComplete ...`, and a
  `TeleportDestination` entity [data]. No task of the mode names it, the interaction data has no such object (the tool has no kind of a
  teleporter), and the backend has no handler of action 82 in any space: the request stays refused (unknown object). The leave of the trial
  is its exit portal [agent: a teleporter is a later stage].

### The paths across two spaces (the code review of stage DG1)

The review of stage DG1 found eight paths that crossed two spaces (`~/Games/soulwarden/work/evidence/dungeons/dg1/code-review/FINDINGS.md`).
Stage QG9a corrected each one; cases DG1R1 to DG1R8 fail on the tree of stage DG1 [agent]:

- A contact of a creature that waits for its time hits no target in a different space; a cast or a release of a creature ends when its
  target is in a different space (R1).
- A cast of a character keeps its space (`Cast::space`): a timed step of the cast in a different space (a volley, a throw, a summon, a heal,
  an area, a counter) does nothing (R2).
- A move to a different space resets the LootTracker states of the chests and of the bags, and the quest object trackers are only in the
  owner record of the main world (R3).
- The return place of an entry must be a place of a travel, also at the exit (an older row): else the entry or the exit is refused
  (`badReturnPlace`) (R4).
- The exit rows of a departure that the database refused try again at each tick and at each world entry; the character does not enter the
  world while its rows wait (R5).
- The sight and the line tests of the navigation (`sees`, `line_stop`, `line_ready`) take the space: in a space of a different level
  nothing stops them (R6).
- A strike in an instance hits no quest object of the main world (R7).
- The credit of a kill pays no contributor in a different space (R8).

## Commands

```sh
tools/world instance maps [TEXT]                 # the maps of the game modes (game-modes.json)
tools/world instance enter CHARACTER MAP [N]     # a new instance of the map (or the instance N of that map), at the start of the map
tools/world instance return CHARACTER            # back to the return place in the main world
tools/world instance list                        # the instances, their members, their return places, and (stage DG2) the levels read
tools/world path NAME X Y Z                      # stage DG2: the path in the navigation of the space of the character (--space N)
tools/gamedata/level.sh LEVEL [GAMEDATA_DIR]     # stage DG2: the data set of an own level (a level or a map id); --list names the levels
tools/world list                                 # each entity with its space (0: the main world) and the line "instances"
```

The commands are in the development routes only (the harness gate). Stage QG9a: the entrance of the game enters a solo mode of the main
world level whose content the backend runs (QuestMedea; stage QG9b: the modes of the table above); the other modes refuse it (result 10).

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
- Stage QG9a: the client shows the prompt of the Medea entrance only when the camera looks at it. After a travel the camera can look away;
  turn the character to the entrance before the key E (live run L1).
- Stage QG9a: the exit portal needs a hold of the key E (`tools/hx 'key 0x45 900'`); a short press sends nothing.
- Stage QG9a: a travel to the platform of the Medea fight (z 57) can put the character below the floor; the fight then fails and resets.
  Walk in from the start of the map.
- Stage QG9a: the leave button of the escape menu of the client was empty in the live runs (no text, nothing sent). Use the exit portal.
- Stage QG9a: the handlers of the entrances and of the exit portals are handlers of the kind (`Interactions::register_kind_handler`): the
  Enter options of the data have many names (EnterQuestCaerDun, EnterDungeonAmrine, ...). A new handler of one option of these kinds
  comes before the kind handler.
- Stage QG9b, corrected in stage QG9c: the client stopped (an access violation at 0x4871DB6 or 0x4871FDA in the repair 0x4871B60 of the
  action history of the remote entity Medusa) at the death of Medusa after a hit of the player, also by the kill command after a hit. The
  cause and the rule are in [Creatures](creatures.md), "The death of a slice with no reaction action". Stage QG9d: the body stays on the
  ground (the first record of the renewed body lives, and the dead mark follows).
- Stage QG9b: the arena of Medusa is full of statues with collision. The start of the map and the place of the intro are between them,
  and a walk to Medusa stops at a statue. Use `tools/world travel` to open ground near her (8621.5 1421.3 203.7).
- Stage QG9b: the level objects of the main world are not in an instance (the rule above). Stage DG2: the statue of Taurus of
  `09A_M12_SaveTaurus` is there (part 5). The inner side of the gate of the Mammoth trial still shows the prompt of a teleporter of the main
  level (refused: "no object of the data has this reference"). Leave the Mammoth trial by the exit portal of the arena
  (`interactobject_exitportal`, 9521.9 1974.9 62.9).
- Stage DG2: a travel (`tools/world travel`) of the target ends the fight of a creature ("the target went to a different place"). For a live
  fight with a path, let the creature see the character and move the character with keys (`tools/hx 'keydown 0x57'`).
- Stage DG2: the tiles of the navigation of a level are the tiles of its one region `r_+00_+00`: a tile of Amrine near the start is 1.8 MB
  in memory (the mine has many floors), and the search table of each level navigation is 8 MB (one time for each level).
- Stage QG9b: the quest of Apophis (1624_0801_agodawakens) needs the three trigger volumes of the teleport puzzle before the mode
  (`TriggerVol_16_TelePuzzle_Enter`, `_FinalPlat`, `_End` of `trigger-data.json`); a travel to each one completes them.
