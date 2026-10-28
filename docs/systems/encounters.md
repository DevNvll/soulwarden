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
| The load | Test cases QG5C2G (4,001 placements, a time limit) and QG5C3F (4,001 ambushes with their proximity objects) |
| Stage QG5c3: the child stages, the spawned objective, the proximity objects, the members with no entity, the detector shapes | `world/encounters.cpp` (`start_stage`, `end_stage`, `objective_met`, `count_destroyed`, `look_proximity`, `in_detector`, `spawn_needed`), `gamedata/encounter_data.h` (`EncounterShape`, `EncounterStage::children`, `EncounterMember::radius`, `lifetime`, `interact`, `direct`), test cases QG5C3A to QG5C3G (`tests/encounter_pattern_tests.cpp`, the fixture `tests/support/encounter_fixture.h`) |
| The corrections of the code review of stage QG5c1 (QG5C1R-R1 to R5) | Test cases QGR5C1A to QGR5C1C (`tests/review_qg5c1_tests.cpp`), protocol notes 4.62 (R5), the README (R4) |
| The corrections of the code review of stage QG5c2 (QG5C2R-R1 to R6) | Test cases QGR5C2A to QGR5C2F (`tests/review_qg5c2_tests.cpp`; QGR5C2F runs the case `complete` of `check_encounters_tool.py`) |

## Rules

The source of each rule is in brackets: [data] the game data, [client ADDR] the client code (build 1.400.6031.6004151, image-relative
addresses), [live] a live check of stage QG5c1, QG5c2, or QG5c3, [agent] a decision of the agent of the stage (a choice where the data does
not decide), [user] a decision of the user, [review] a correction of a code review.

### The recipes

- A recipe is an encounter slice of a placement of the main world (an area, a point, or a prefab spawner of a capital layer) whose events
  a quest task names: the success event of the manager or the game event of the gatherable row of a start object [data]. The tool reads
  403 encounters; 67 are recipes of a quest, and 336 are not [data].
- The key of a recipe is `PLACEMENT_UUID/SPAWNER_ENTITY`; its world place is the placement matrix times the matrix of the spawner entity
  [data]. A spawn location is the placement matrix times the spawner times the location entity [data].
- The stage order is the order of `m_stages` of the manager (a breadth walk of the child stages); a stage that no walk reaches is listed
  as `unreachable` and does not run [data].
- The tool refuses a recipe with its cause (`refused`), and the runner refuses a recipe whose member has no kind, no template, or no row
  in the other data files when a stage needs it [agent]. The runner takes a recipe when: the manager starts it by itself
  (`m_autoActivate`); the detector has shapes that it tests; the parent is an area spawner that spawns at its enable; each objective is a
  timed wave (`InvasionWaveObjective` with a time limit and `succeedOnTimeout`), a destroyed objective (`SliceDestroyedObjective`), or a
  spawned objective (`SliceSpawnedObjective`, stage QG5c3); no stage has a failure condition, a background activation, or an action
  other than `EntityEventAction` and `KillSpawnsAction`; each start object is in a destroyed stage that tracks it; the recipe has a
  creature member; and the spawns that the runner makes reach the count of each destroyed or spawned stage with no timed end [agent;
  review QG5C2R-R3].
- A spawn is needed when its stage or a stage above it (the child stage lists) has a destroyed or a spawned objective that tracks its member
  or tracks no slice; a timed stage needs no spawn (it ends at its time) [data: the objective classes; agent: the rule]. A member that no
  stage needs and that the runner cannot make is not made, and the recipe stays [agent]: a visual (the smoke burst of the lost pylons in
  their tracked stages, the cannon storm of Helvan in a timed stage, the bramble walls), a creature member that the client does not show
  (the swarmers of the timed stage of Thorpe and of the subwaves of the Yonas retreat; the count `spawnsSkipped`). A tracked slice of an
  alias counts for the creature member whose alias has that slice [data].
- Stage QG5c3: the parent spawner is an area spawner that spawns at its enable (`m_spawnOnEnable`) [data]. A point or a prefab spawner of
  the data has no spawn at its enable, and each one has a twin spawner of the same encounter (the `EncounterCondition` spawners of the
  Bathory hall and of the rotating bookcase, the prefab spawner of Gazsala): the tool refuses that placement with its cause [data; agent:
  the start of such a spawner is not in the data that the tools read].
- Stage QG5c3: a reset event of the manager (`m_resetEncounterEventId`, `Reset_Encounter` of the stone tablet of Helvan and of Overseer
  Zane) is no cause: no slice or table of the data that the study QG5c0 and this stage read sends it, and the fail of the runner (a fight
  with no participant) is the reset [agent].
- Stage QG5c3: the quest tasks switch some encounters on and off with an achievement (`EncActive_99E_MSQ_07_StaffCave`,
  `EncVol_16_AEAmbush`, `EncActive_99B_MSQ_05_YonasRetreat`), and no slice of the encounter or its parent and no table reads it [data: the
  achievements table and the task tables only]. The runner does not read the achievement; the quest gate of the proximity object
  (`InteractOnlyOnQuest` and its tag) decides who starts an ambush [agent].
- Kept out, each with its stage [agent: the scope of the briefs of stages QG5c2 and QG5c3]: the interaction sequences and the defenses
  (QG5c4), the summoned opponent with phases (BT3), the scheduled and seasonal events (the Darkness, the seasons), the arenas, and the
  instances (later). The boat reinforcements of the open world are world events (later). The table `LATER` of `encounters.py` names them,
  and the table `PATTERNS` gives the label of the ambushes and the summoned opponents; the support manifest names the cause of each event
  that only a refused recipe gives.
- The counts of the supported build (stage QG5c3): 67 recipes, 36 with no cause of the tool, 32 taken by the runner, 35 refused [data].
  By pattern: the kill waves 25 taken and 4 refused; the summoned opponents 3 taken (Thorpe, Templar Invectus, Grivaros) and 5 refused (the
  armored dragon of Gazsala and the hercyne boar: the client does not show their slice; the twin prefab spawner of Gazsala; the two point
  spawners of the Bathory hall); the ambushes 4 taken; the defenses 4, the interaction sequences 14, the Darkness 5, the world events 2, and
  the summoned opponent with phases 1 refused (their stages).
- Stage QG5c3: an area trigger (a proximity object with no Interact component: the 20 m trigger of Grivaros, the rows
  `PressurePlate_Ancient`) makes no recipe a quest recipe: its row is a shared row of the pressure plates, and its event names no
  encounter (one task of the instance `QuestYonas` names it) [data; agent: the selection]. The runner gives the event of its row to each
  character that finds it [data].

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
- Stage QG5c3: the child stages of a stage (`m_stages` of its EncounterComponent) run one after the other in their order: the first one
  starts at the start of its parent (after the spawns of the parent), and each next one at the end of the one before it [data: the order
  of the list; agent: the sequence, from the data: each subwave of the ambushes has a time limit just above its last spawn delay (25 s and
  15 s, 30 s and 25 s), which has a sense only in a sequence; live QG5c3 L2: 30 s each, the warrior of the third subwave at its 4 s]. A
  stage counts the spawns of its child stages (its tracked slice, else each spawn below it) [data: the required counts are the totals of
  the children: 7 pickaxe villagers of the CorrStaff ambush in three subwaves, 10 blight fiends, 3 named ghosts of Torment]. A stage whose
  child stages are not optional (`m_childStagesOptional` 0) ends when it has its count and its last child stage ended; an optional one
  ends at its count [data: the name of the field; agent: the rule]. The end of a stage ends its child stages that run (the status of the
  parent); a failure of a child stage that is not optional fails its parent [agent].
- Stage QG5c3: a spawned objective (`SliceSpawnedObjective`: Wave_02 - Setup - Spawn Helvan) ends when `m_requiredCount` members of its
  tracked slice came [data: the class and its fields; live QG5c3 L3: the stage ends at the spawn of Helvan].
- Stage QG5c3: a destroyed stage with no tracked slice and no spawn of its own or of its child stages counts each death of a creature
  member of its instance while it runs (Wave_06 - Objective: Kill Helvan Boss: Helvan came in an earlier stage) [agent: a stage with no
  spawn has nothing else to count; live QG5c3 L3]. Such a stage needs a time limit with `succeedOnTimeout` (it has 180 s) [review
  QG5C2R-R3].
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

- Stage QG5c3: a direct creature member is a creature slice itself (a CharacterComponent and a Vitals component with `m_rowReference`,
  no AIVariantProvider): its vitals row is `m_rowReference`, its body the slice (the inquisitors of the corrupted tree, the named anubian
  brute of the tomb, the tendrils of Overseer Zane) [data]. A GatherableController of the mining or the skinning of its body is a part of
  the creature [data: the brute has AnubianBrute_Base, the inquisitors Damned_Axeman].
- Stage QG5c3: a proximity object (`m_isProximityGatherable` of its GatherableController: the starts of the ambushes, of the Legion attack,
  and of the cave fight of 09A_S5) is a server child of its template with its VariationData state and its node state Healthy, as a start
  object [data; live QG5c3 L2: the record]. No GatherOption counts it (it is not a start object of the gathering system). At each scan of
  the runner (500 ms), each character in its sphere finds it: an accepted position (no camera estimate, no cutscene, no cinematic), a
  character that lives, the 3D distance to its place at most its radius, and for an `InteractOnlyOnQuest` row an Active task with its tag
  [the rule of the proximity quest objects of stage QG5a: data and client 0x8261FC0; agent: the scan of the runner, the look of each
  character in the sphere at the find]. Each finder gets the game event and the tag of its row (one plan of the quest system each:
  `Quests::object_destroyed`); then the object goes and its stage counts it. A store failure for each finder keeps the object for the
  next look [agent]. The radius is `DetectionVolumeSphereRadius` of its variation row, else the sphere of its DetectionVolumeComponent
  (13 m for the CorrStaff ambush, 15 m the blight ambush, 30 m the Yonas retreat, 5 m the AE ambush and the Legion attack, 4.5 m the
  cave fight) [data: the rule of `quest_objects.py`]. Live QG5c3 L2: the walk into the 30 m sphere of the Yonas retreat finds it, and the
  kill stage starts at once.
- Stage QG5c3: an area trigger (a proximity object with no Interact component: `ai_encounter_area_trigger_20m` of Grivaros, 20 m, the row
  `PressurePlate_Ancient`) has no template and no record [data: no Interact bound; agent: no record]: the runner keeps it as a member with
  no entity (an id of the allocator, `World::reserve_id`) and finds the characters in its sphere as for a proximity object.
- Stage QG5c3: a member that ends itself (a slice whose delayed events of the init reach the trigger of an entity with a
  `DestroyGDEComponent`: the smoke burst of the lost pylons 4.2 s, the powder keg of the pirate waves 13.15 s, the lightning strike of the
  spectral lure 4 s) counts as destroyed at its end in a stage that needs it [data: the delays and the event types of `spells.py`: 0 a
  trigger; agent: the count]. The backend makes no record of it (a visual and a hazard: the bombardment volume of the smoke burst does not
  hurt) [agent]. It is a member with no entity; its end is a due entry of its generation (`DueKind::memberEnd`). The smoke burst makes the
  count of `Wave_04 - Objective: Kill Lost` of the lost pylons (3 creatures and the burst for the count 4) [data].

### The participants and the HUD

- A participant is a character in the detector sphere of the root (`m_Radius`, 3D) while the instance runs; a character that leaves the
  sphere stays a participant for `m_gracePeriodSec` of the tracker [data]. The participants of an instance are the characters near its
  root (the position index), never a scan of the world [agent: MP-8].
- Stage QG5c3: the detector of the participants is the set of the DetectionVolumeComponents of the entity of the ParticipantTracker: a
  sphere (`m_Radius`), a cylinder (`m_Radius` and `m_Height`, centered on `m_center`: Torment, 40 m and 10 m, 5 m up), or an axis box
  (`m_aabb`: the half sizes; the Bathory hall) [data: the classes and the fields; the z extent 4.25 m of the box is the half of the 8.5 m
  of the polygon prism of the same entity]. 29 of the 65 encounter slices of the build have two volumes on that entity (the master
  template: a sphere of 30 m and one of 12 to 50 m), and no field names one of them: a character in one of them is in the detector [agent].
  The test is the capsule of the character (`capsule_of`: upright, from its feet) against each shape [agent: a detection volume overlaps
  the body; a character on the ground at the height of the root is at the floor of the cylinder of Torment]. The offset `m_center` is in
  world axes, and a box is aligned with the world axes [agent: the shapes of the data have no horizontal offset].
- Each participant has the G of the root in the field 5 of its player Objectives state (one encoder: `objectives_state.cpp`); the client
  then shows the encounter at the top of the tracked list with the teal diamond icon [live QG5c1 I2]. The link goes at the end of the
  instance or when the character is no participant [agent]. A session stores no encounter state: the links are in the entity of the
  character (`Entity::encounterLinks`) [agent: MP-1].
- The quest system takes the root instance of a linked encounter in the pinned list of the client (message 1458), and it does not store it
  [live QG5c2 L2: with no such rule a quest grant took the encounter off the HUD].
- A fight stage with no participant fails the encounter: the end of the instance, and a new instance after the respawn time of the
  parent, as after a success [agent].

### The credit

- The participants of the success are the ones of that time: the runner looks at the detector again before the snapshot (the deaths and
  the timers of a tick come before its scan, so a grace can end between two scans) [review QG5C2R-R1].
- At the success the runner takes a snapshot of the participants and the generation, and gives each one the event of the recipe one time
  through `Quests::event` (gameEvent) [agent]. The outcome (`QuestEventResult`) tells credited, no match, repeated, failed, and store
  failed [agent]. A store failure keeps the recipient in the ledger: it tries again after 2 s, at most 5 times; a payout is never marked
  paid before the store took it [agent]. A recipient that is not in the world at the time of the payout gets nothing [agent].
- The event of a start object goes only to its gatherer [data: the gatherable row; live QG5c2 L4].

## Commands

- `tools/world encounter CHARACTER [METERS]` (route `GET /v1/dev/encounters`; the admin state member `encounters`, 200 m): the instances
  near a character with their stage, the progress, the participants, the members, the links of the character, and the counts (stage
  QG5c3: `objectsFound`, `proximityLooks`, `timedEnds`, `spawnsSkipped`).
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
- Stage QG5c3: a participant event (`m_participantAddedGameEventId`) comes when the character joins the detector, also before the find of
  the proximity object: at the Yonas retreat it completed the task DefendAdded and the partial container (the fallthru) 5 ms before the
  find, and the task DefendStart became inactive; the tag stays active through the next task DefendComplete, so the find counts [live
  QG5c3 L2]. Grant a quest before the character comes near the placement: the event of the join is sent at the start of the instance.
- Stage QG5c3: a recipe with `m_overrideAggroDelay` 0 (the Yonas retreat) gives its members no target at their spawn: they take a target
  by their sight only (the hound at 14 m did not come, the warrior of the third subwave came) [data; live QG5c3 L2].
- Stage QG5c3: the boss ghost of the siphon fight stays at its place on the ruin top (its state FIGHT with a target, "stays at its place":
  the cause is not proved; possibly the navigation data has no free floor at its place); the fight is at range [live QG5c3 L1].
- Stage QG5c3: the client runs the actions of a stage status itself: the sky goes dark at the start of the boss stage of the siphon, the
  line of Yonas ("There are too many!") comes at the start of the kill stage, and an exit teleport comes at the end of Helvan [live
  QG5c3 L1 to L3]. The runner sends only the states (2133); no message of an action is needed.
- Stage QG5c3: the HUD of a timed stage shows a red 00:00:00 under its counter (the task start time of the generated tasks is not sent;
  the time of the server runs) [live QG5c3 L3]. The siphon fight and the Yonas retreat have no title on a user-wave stage: the HUD shows no
  row for them; Helvan shows "Helvan the Navigator" and "0 / 2 - Defeat Helvan's Lackeys" [live QG5c3 L1 to L3].
- Stage QG5c3: the HUD row of an encounter with timed user-wave stages (the intermissions of Thorpe, `InvasionWaveObjective` with
  `m_userWave` 1) stays at the first intermission ("To Stand Against The Dark: Stage 1/5", "Intermission") through the later stages,
  while the runner and the states go on [live QG5c3 L4]. The kill rows of the stages before it show ("0 / 9 - Harass Thorpe's
  Supporters"). The index of the generated tasks of a timed user-wave stage is not proved: the rule 2k + 1 of the static read B of
  QG5c2 was proved only for destroyed stages. Open.
- Stage QG5c3: the kill of Thorpe completes the task `DefeatThorpe_Solo` (a kill contribution) and with it the partial container of the
  fight; the success event of the encounter (`MSQ_0036_Thorpe_Interact`) comes after the cleanup of 12 s and finds no active task
  [data: the partial container; live QG5c3 L4]. A participant that stays in the detector is a participant of the next generation (10 s
  after the success) [data: the respawn of the parent; live QG5c3 L4].
- Stage QG5c3: the area of an officer of Helvan (Officer_AnchorSlam) staggers the character at close range and stops a bow draw [live QG5c3
  L3]: step back before a shot.
- Stage QG5c3: the duplicate placements of the data (two spawners of one encounter in one parent slice: Gazsala, the Bathory hall, the
  rotating bookcase) are refused by the rule of the parent spawner; a later stage that runs one of them must take one spawner of each
  pair.
