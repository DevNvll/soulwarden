# Encounters

Read this document before a change of the encounters of the quests: the recipes of the game data, the instance of a placement, its
stages, its members and its start object, its participants, the credit of the success, the fail and the reset, and the respawn. The
wire contracts are in the protocol notes (section 4.62). The quest events are in [Quests](quests.md), the creatures in
[Creatures](creatures.md), the gather of a start object in [Gathering](gathering.md) and [Interactions](interactions.md).

## Start here

| Task | Start here |
| --- | --- |
| The runner: the placements, the instances, the stages, the timers, the participants, the credit, the ends | `apps/backend/src/world/encounters.h` (`Encounters`, `encounter_rules`, `EncounterPlacement`, `EncounterInstance`), `world/encounters.cpp`, test cases QG5C2B to QG5C2G (`tests/encounter_tests.cpp`) |
| The entity of the root and its states | `world/entity.h` (`EntityKind::encounter`, `EncounterBody`, `EncounterStageState`, `EncounterTaskState`, `Entity::encounterLinks`), `world/world.h` (`make_encounter`, `remove_encounter`) |
| The wire: the metadata, the place, the manager state 6786, the stage states 2133, the root Objectives 3857, the player Objectives field 5, the VariationData state 14 of a start object | Protocol notes 4.62, `server/rep/states/encounter_states.cpp`, `server/rep/states/objectives_state.cpp` (`encode_encounter_objectives_state`, field 5), `server/rep/states/state_codecs.cpp` (`object_codecs`), `tests/support/state_decoder.h` |
| The members: a creature of a stage and its aggro, a start object | `world/creatures.h` (`spawn_member`, `remove_member`, `member_died`), `world/behavior.h` (`Behavior::provoke`), `world/gathering.h` (`NodeKind::encounter`, `encounter_object_made`), `world/interactions.h` (`spawn_container`) |
| The quest side: the event outcome, the pins of a linked root, the manifest | `world/quests.h` (`QuestEventResult`, `QuestEventOutcome`, `QuestEvent::callerRetries`, `client_tracked`, `encounter_unlinked`), `world/quest_support.cpp`, test case QG5C2H |
| The recipe data (`encounter-data.json`) and its tool | `gamedata/encounter_data.h` (`EncounterData`, `EncounterRecipe`, `EncounterStage`, `EncounterMember`), `tools/gamedata/encounters.sh` (`encounters.py`, `encounter_recipes.py`, `check_encounters_tool.py`), [Game data](../development/game-data.md), test cases QG5C2A, QG5C2I |
| The member kinds, the start object templates and rows | `creatures.json` (`members`, `CreatureData::member_kind`), `interaction-data.json` (`encounterObjects`, `InteractionData::encounter_object`), `gathering-data.json` (`encounterObjects`, `GatheringData::encounter_object`) |
| The commands | `server/http/encounter_commands.cpp`, `server/http/character_commands.h`, `tools/world encounter`, the admin state member `encounters`, `GET /admin/v1/catalog/encounters` |
| The load | Test case QG5C2G (4,001 placements, a time limit) |
| The corrections of the code review of stage QG5c1 (QG5C1R-R1 to R5) | Test cases QGR5C1A to QGR5C1C (`tests/review_qg5c1_tests.cpp`), protocol notes 4.62 (R5), the README (R4) |

## Rules

The source of each rule is in brackets: [data] the game data, [client ADDR] the client code (build 1.400.6031.6004151, image-relative
addresses), [live] a live check of stage QG5c1 or QG5c2, [agent] a decision of the agent of the stage (a choice where the data does not
decide), [user] a decision of the user.

### The recipes

- A recipe is an encounter slice of a placement of the main world (an area, a point, or a prefab spawner of a capital layer) whose events
  a quest task names: the success event of the manager or the game event of the gatherable row of a start object [data]. The tool reads
  403 encounters; 67 are recipes of a quest, and 336 are not [data].
- The key of a recipe is `PLACEMENT_UUID/SPAWNER_ENTITY`; its world place is the placement matrix times the matrix of the spawner entity
  [data]. A spawn location is the placement matrix times the spawner times the location entity [data].
- The stage order is the order of `m_stages` of the manager (a breadth walk of the child stages); a stage that no walk reaches is listed
  as `unreachable` and does not run [data].
- The tool refuses a recipe with its cause (`refused`), and the runner refuses a recipe whose member has no kind, no template, or no row
  in the other data files [agent: the scope of stage QG5c2]. The runner takes a recipe when: the manager starts it by itself
  (`m_autoActivate`), it has no reset event, the detector is a sphere with a radius, each objective is a timed wave
  (`InvasionWaveObjective` with a time limit and `succeedOnTimeout`) or a destroyed objective (`SliceDestroyedObjective`), no stage has a
  failure condition, a background activation, a child stage, or an action other than `EntityEventAction` and `KillSpawnsAction`, each
  member is a creature member or a start object of its own destroyed stage, and the recipe has a creature member [agent].
- A member that is not a creature and not a start object, in a stage that tracks a different slice, is a visual of the stage (the smoke
  burst of the lost pylons): the backend does not make it, and the recipe stays [agent]. A tracked slice of an alias counts for the
  creature member whose alias has that slice [data].
- Kept out, each with its stage [agent: the scope of the brief of stage QG5c2]: the ambushes (a proximity start object, a manager that
  waits for an event), the summoned opponents, the interaction sequences (no creature member), the defenses (QG5c3); the summoned
  opponents with phases (BT3), the scheduled and seasonal events (the Darkness, the seasons), the arenas, and the instances (later). The
  boat reinforcements of the open world are world events (later). The table `LATER` of `encounters.py` names them; the support manifest
  names the cause of each event that only a refused recipe gives.
- The counts of the supported build: 67 recipes, 16 with no cause of the tool, 14 taken by the runner (2 more need a member kind that the
  creature tool leaves out), 53 refused [data].

### The instance and its stages

- The world makes the instance of a taken placement when a character is 150 m or less from it, and removes it when no character is 200 m
  or less from it (a dormant end: the placement is free again at once) [agent: the distances of the spawn points]. The placements are in
  a grid of 64 m cells: a scan looks only at the cells near each character, each 500 ms of the world clock [agent: MP-8].
- Each instance has a generation. Each due entry, member, start object, and credit keeps the generation of its instance; an entry of an
  old generation changes nothing [agent].
- The instance has a root entity: the metadata (the asset and the sub id of the recipe slice, an actor of the id allocator), the place
  (PositionInTheWorld at the place slot), the manager status (6786 at the manager slot), the root Objectives (3857 at the objectives
  slot), and one stage state 2133 for each stage with a status [live: QG5c1 I1, QG5c2 L2]. Each session near the root observes it; the
  views are the same (the root is public) [agent].
- The manager starts the first stage after `m_autoActivateDelayMS` [data]. A stage starts with the status 2, makes its spawns at their
  delays on the world clock, and ends with the status 3 (success) or 4 (failure) [data; client 0x26F64A0]. A timed stage ends at the end
  of its time [data]. A destroyed stage ends at `m_requiredCount` destroyed spawns: the deaths of its members and the gather of its start
  object (its tracked slice, else each spawn of the stage) [data]. `KillSpawnsAction` at the end of a stage removes its members [data].
- The sequence of the stage states increases by one for each change; the history keeps 10 entries (the reader of the client takes at most
  10) [client 0x26F7550]. The backend does not send 1278: the state 2133 carries the same status [agent].
- The root Objectives has one active element with `isPoiObjective` 1 and the task elements of the generated tree: the leaf of the user-wave
  stage k (from 0) has the index 2k+1, its wrapper 2k+2, the outer container 2n+1 (n user waves); the progress of a kill leaf is the N of
  "N / M" [client 0x4465159, 0x272E52C; live QG5c2 L2: "1 / 2"]. The taskCRC is 0 (its generated value is not found; no compare gates the
  progress) [client 0x3A43FD7].
- The last stage ends the encounter: the manager status 3, the credit, the removal of the root, the members, and the objects
  (`m_destroySpawnsOnDestroyed` of the root spawner) [data]. The parent makes a new instance after a time from its respawn range (a uniform
  draw from the minimum to the maximum) [data: the area spawner; agent: the draw; live QG5c2 L4: 11.8 s for the Perseus ruin].

### The members

- A creature member is a kind of `creatures.json` with no spawn point (`member`: the member slice), made through the creature system with
  the encounter mark (the placement and the generation) [agent]. Its death counts for the destroyed stage of its generation, and the
  creature system queues the event (no entity removal inside a hit) [agent].
- The members attack: at their spawn each member gets the nearest participant that lives as its target (`Behavior::provoke`), after the
  aggro delay of the manager (`m_overrideAggroDelay` 1 and `m_aggroDelayOverride` 0 for the Obelisk: at once) [data; live QG5c2 L2: with no
  provoke a member at 3 m did not see the character behind it]. A member whose target dies goes back to its anchor and gets its full
  health (the rule of the way back) [live QG5c2 L4].
- A start object is a server child of the interaction template of its slice (`encounterObjects` of `interaction-data.json`) with the node
  state Healthy and the row of `gathering-data.json` (`NodeKind::encounter`) [agent]. Its GatherOption is a timed gather of the row (the
  Obelisk device 3 s, the blight offering 4 s); at the completion the gatherer gets the game event of the row, the object goes, and the
  stage counts it [data; live QG5c2 L4].
- A start object has the VariationData state (type 14) at the slot of its VariationData component: the u16 row index of its variant in
  its variation table (the file row order; 733 for `04A_Side02_02_EncounterStart_01`, 205 for `1434_0104_EncStart_BlightRanger`). Without
  it the client clears `m_selectedVariant` at the activation, shows the mesh, and shows no prompt [client 0x42D0300, 0x42CF180, 0x42D0720;
  live QG5c2 L3 and L4: with it "E OFFER BLIGHT" and "E ANCIENT DEVICE", and a GatherOption request].

### The participants and the HUD

- A participant is a character in the detector sphere of the root (`m_Radius`, 3D) while the instance runs; a character that leaves the
  sphere stays a participant for `m_gracePeriodSec` of the tracker [data]. The participants of an instance are the characters near its
  root (the position index), never a scan of the world [agent: MP-8].
- Each participant has the G of the root in the field 5 of its player Objectives state (one encoder: `objectives_state.cpp`); the client
  then shows the encounter at the top of the tracked list with the teal diamond icon [live QG5c1 I2]. The link goes at the end of the
  instance or when the character is no participant [agent]. A session stores no encounter state: the links are in the entity of the
  character (`Entity::encounterLinks`) [agent: MP-1].
- The quest system takes the root instance of a linked encounter in the pinned list of the client (message 1458), and it does not store it
  [live QG5c2 L2: with no such rule a quest grant took the encounter off the HUD].
- A fight stage with no participant fails the encounter: the end of the instance, and a new instance after the respawn time of the
  parent, as after a success [agent].

### The credit

- At the success the runner takes a snapshot of the participants and the generation, and gives each one the event of the recipe one time
  through `Quests::event` (gameEvent) [agent]. The outcome (`QuestEventResult`) tells credited, no match, repeated, failed, and store
  failed [agent]. A store failure keeps the recipient in the ledger: it tries again after 2 s, at most 5 times; a payout is never marked
  paid before the store took it [agent]. A recipient that is not in the world at the time of the payout gets nothing [agent].
- The event of a start object goes only to its gatherer [data: the gatherable row; live QG5c2 L4].

## Commands

- `tools/world encounter CHARACTER [METERS]` (route `GET /v1/dev/encounters`; the admin state member `encounters`, 200 m): the instances
  near a character with their stage, the progress, the participants, the members, the links of the character, and the counts.
- `tools/world encounter recipes` (route `GET /v1/dev/encounters/recipes`; admin `GET /admin/v1/catalog/encounters`): each recipe with its
  key, its root, its pattern, its quest events, and its causes.
- `tools/world encounter reset CHARACTER KEY` (route `POST /v1/dev/encounters/reset`): a development aid; the instance of a placement ends,
  and the placement is free again at once.
- `tools/world encounter use CHARACTER` (route `POST /v1/dev/encounters/use`): a development aid; the start object 10 m or less from the
  character counts as gathered by it. It is not in the admin routes: it counts a gather that the client did not do [agent].

## Traps

- The navigation data has a false wall at the top of the Perseus ruin (8732 4250 181): some creature hits miss and some bow shots stop
  at it [live QG5c2 L2]. The kills of the live check L2 used `tools/world creature kill`; in L4 one skeleton died from bow shots.
- The client sometimes ends the gather of a start object at once: about 110 ms after the accepted request it sends 2473, sends the
  request again, and ends that one too; a later press of E completes the gather. Seen at the Obelisk device in L4 (its task complete, and
  30 s after a new instance of the quest the gather completed) and in L5 (the task active, 10 s after a new instance; 50 s later the gather
  completed). The cause is not known: the tag query of the client (near 0x35D77C0), the state of the quest in the client, or the movement
  just before the press [live QG5c2 L4, L5]. Press E again after some seconds.
- A world creature near a start object (the Blight Fiends of Edengrove at 3.6 m) stops the gather with its damage, as in the game (the
  option ends at damage) [live QG5c2 L4].
- The HUD shows the encounter only while the character is a participant: a picture of the HUD from outside the sphere shows nothing.
- A start object with no VariationData state shows its mesh but no prompt (see the rules). A server child of an other system with a
  variation (a quest object of stage QG5a) can have the same fault.
- The creatures of the encounters have no spawn point: `CreatureData::member_kind` gives their kind; a spawn point loop does not find
  them. The support manifest adds the member kinds of the taken recipes to the kill sources.
- The admin and development views read the instances near a character: an instance far from each character is not in the world (it is
  dormant).
