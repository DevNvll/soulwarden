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
| Stage QG5c4: the entity-state stage and its trigger, the objectives of a stage (`more`), the objectives met at their start, the task tree of the client and the timer, the shape entity of a detector, the parent spawner rules, a recipe with no creature member | `world/encounters.cpp` (`look_triggers`, `in_shapes`, `count_objective`, `publish_objectives`, `resolve`), `gamedata/encounter_data.h` (`EncounterTrigger`, `EncounterObjective::leaf`, `EncounterStage::more`, `EncounterRecipe::taskLeaves`), `world/entity.h` (`EncounterBody::taskStarts`), `tools/gamedata/encounter_recipes.py` (`spawner_rules`, `state_senders`, `detector_shape`), test cases QG5C4A to QG5C4H (`tests/encounter_sequence_tests.cpp`, the fixture `tests/support/encounter_pattern_fixture.h`) |
| The corrections of the code review of stage QG5c3 (QG5C3R-R1 to R4) | Test cases QGR5C3A to QGR5C3D (`tests/review_qg5c3_tests.cpp`) |
| Stage QG5c5: the Darkness events and the boat reinforcements: the season rule, the group members and their parts, the spawns of an alias, the slices of a tracked alias, the stages that wait for their parent, the DestroySliceAction, the parts that the client does not show (the fill), the start objects of an earlier stage | `world/encounters.h` (`encounter_rules::kDarknessAtAllTimes`, `EncounterPlacement::partKinds`, `fills`, `EncounterMemberRun::part`), `world/encounters.cpp` (`resolve`, `spawn`, `count_destroyed`, `children_done`, `destroy_slices`), `gamedata/encounter_data.h` (`EncounterPart`, `EncounterDestroy`, `EncounterSpawn::alias`, `EncounterObjective::trackedSlices`, `EncounterObjectiveKind::waitParent`, `EncounterRecipe::season`), `world/entity.h` (`Creature::memberRoute`), `tools/gamedata/encounter_recipes.py` (`group_parts`, `part_route`, `alias_tiers`, `Reader.alias`), test cases QG5C5A to QG5C5F (`tests/encounter_darkness_tests.cpp`) |
| The corrections of the code review of stage QG5c4 (QG5C4R-R1 to R5) | Test cases QGR5C4A to QGR5C4F (`tests/review_qg5c4_tests.cpp`; QGR5C4F starts the world with each encounter data file missing) |

## Rules

The source of each rule is in brackets: [data] the game data, [client ADDR] the client code (build 1.400.6031.6004151, image-relative
addresses), [live] a live check of stage QG5c1, QG5c2, QG5c3, QG5c4, or QG5c5, [agent] a decision of the agent of the stage (a choice where the
data does not decide), [user] a decision of the user, [review] a correction of a code review.

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
  timed wave (`InvasionWaveObjective` with a time limit and `succeedOnTimeout`), a destroyed objective (`SliceDestroyedObjective`), a
  spawned objective (`SliceSpawnedObjective`, stage QG5c3), or (stage QG5c4) an entity-state objective with a trigger
  (`EntityActiveStateChangedObjective`); the other objectives of a stage are destroyed or spawned objectives with no time limit; no stage
  has a failure condition, a background activation, or an action other than `EntityEventAction` and `KillSpawnsAction`; each start object
  is in a stage whose destroyed objective tracks it; each start object has a gatherable row that the backend gathers (stage QG5c4); and
  the spawns that the runner makes reach the count of each destroyed or spawned objective with no timed end [agent; review QG5C2R-R3].
  Stage QG5c4: a recipe with no creature member is taken (the Disk of Atum of 1624_0401: its stages count its start objects) [agent: the
  stages of the data count objects as well as creatures].
- A spawn is needed when its stage or a stage above it (the child stage lists) has a destroyed or a spawned objective that tracks its member
  or tracks no slice; a timed stage needs no spawn (it ends at its time) [data: the objective classes; agent: the rule]. A member that no
  stage needs and that the runner cannot make is not made, and the recipe stays [agent]: a visual (the smoke burst of the lost pylons in
  their tracked stages, the cannon storm of Helvan in a timed stage, the bramble walls), a creature member that the client does not show
  (the swarmers of the timed stage of Thorpe and of the subwaves of the Yonas retreat; the count `spawnsSkipped`). A tracked slice of an
  alias counts for the creature member whose alias has that slice [data].
- Stage QG5c3: the parent spawner is an area spawner that spawns at its enable (`m_spawnOnEnable`) [data].
- Stage QG5c4, the twin spawners: the three pairs of stage QG5c3 (Gazsala, the Bathory hall, the rotating bookcase) were a fault of the
  reader, not a pair of the data. (1) The root entity of a slice has a SliceComponent whose list `Entities` repeats the entities of the
  slice; a look into it found the fields of a spawner of an other entity: it is no spawner [data: the class afd304e4 and its list]. (2) A
  spawner entity has the base SpawnerComponent (the spawned slice) and the component of its kind with its rules on the same entity; the
  rules come from that component (`spawner_rules`): an area spawner (`m_liveCount`, the respawn range, the start `m_spawnOnEnable`), a
  prefab spawner (`m_editPrefab`, the start `m_spawnOnInitialize`), or a point spawner (the cooldowns, `m_spawnOnInitialize`) [data].
  Now one recipe stays for each placement: the area spawner of Gazsala (it spawns at its enable; the recipe stays refused for the
  armored dragon that the client does not show), and the prefab spawners of the Bathory hall and of the rotating bookcase with
  `m_spawnOnInitialize` 0 and `m_spawnOnTrigger` 0 (an event of their parent slice starts them, a slayer script: refused) [data]. 67
  recipes become 64.
- Stage QG5c3: a reset event of the manager (`m_resetEncounterEventId`, `Reset_Encounter` of the stone tablet of Helvan and of Overseer
  Zane) is no cause: no slice or table of the data that the study QG5c0 and this stage read sends it, and the fail of the runner (a fight
  with no participant) is the reset [agent].
- Stage QG5c3: the quest tasks switch some encounters on and off with an achievement (`EncActive_99E_MSQ_07_StaffCave`,
  `EncVol_16_AEAmbush`, `EncActive_99B_MSQ_05_YonasRetreat`), and no slice of the encounter or its parent and no table reads it [data: the
  achievements table and the task tables only]. The runner does not read the achievement; the quest gate of the proximity object
  (`InteractOnlyOnQuest` and its tag) decides who starts an ambush [agent].
- Kept out, each with its stage [agent: the scope of the briefs of stages QG5c2 to QG5c5]: the summoned opponent with phases (BT3), the
  scheduled and seasonal events of the folder `slices/worldevents/seasons/` (none in the recipes of the build), the arenas, and the
  instances (later). Stage QG5c5 runs the Darkness events (the season rule below) and the world events of the open world. The table `LATER` of
  `encounters.py` names them, and the tables `PATTERNS` and `PATTERN_FOLDERS` give the label of the other patterns (stage QG5c4: the
  interaction sequences, the defenses, the world events of the open world); the support manifest names the cause of each event that only
  a refused recipe gives.
- The counts of the supported build (stage QG5c4): 64 recipes, 52 with no cause of the tool, 47 taken by the runner, 17 refused [data].
  By pattern: the kill waves 25 taken and 3 refused; the summoned opponents 3 taken and 3 refused (the armored dragon of Gazsala and the
  hercyne boar: the client does not show their slice; the prefab spawner of the Bathory hall); the ambushes 4 taken; the interaction
  sequences 13 taken (the 8 tarot towers, the Disk of Atum, the banner of Drusilla, 3 shipwrecks) and 1 refused (the braziers: a nested
  encounter); the defenses 2 taken (the hacker of 09A_S9, the pyre of 1434_0103) and 2 refused (the M5 wave event: a stage waits for a
  gather of the root slice, and its boars have no body; the wagon attack: the hercyne reindeer has no body); the world events 2 refused
  (group members); the Darkness 5 and the summoned opponent with phases 1 refused (their stages). Stage QG5c3: 67 recipes, 32 taken.
- Stage QG5c3: an area trigger (a proximity object with no Interact component: the 20 m trigger of Grivaros, the rows
  `PressurePlate_Ancient`) makes no recipe a quest recipe: its row is a shared row of the pressure plates, and its event names no
  encounter (one task of the instance `QuestYonas` names it) [data; agent: the selection]. The runner gives the event of its row to each
  character that finds it [data].
- Stage QG5c4, the defenses: the four defense recipes of the open world have no defended target with a health and no failure condition:
  the hacker, the pyre, and the wagon attack have no NPC or object with a Vitals component in their slice, and each objective has
  `m_failureCondition` 0 [data: the entities of the three slices; research quest-encounters.md section 1: "a defense title does not prove
  an NPC health objective"]. The NPC of the title (Kievan Sidorov of "Defend Kievan Sidorov from the Anubian Guardians") is a person of
  the settlement, not a member. So the runner adds no target policy for a non-player target: the members attack the participants, a
  timed fight ends at its time (`succeedOnTimeout` 1: a success also with living members; 0: a failure), and a fight with no participant
  fails [data; agent: no rule of a target that the data does not have]. The alternative `enc_defense_09a_m5_angryearth` has a failure
  objective for the destruction of `09a_m5_defense_encounter_objective`, and no placement of the main world places it [data].
- Stage QG5c4, the world events of the open world (the boat reinforcements of Monarch's Bluffs): the manager starts each one by itself at
  its placement (`m_autoActivate` 1; the area parent spawns at its enable), and its setup spawns a spyglass (a start object; its gatherable
  row has no game event): the look through the spyglass brings the boat (a timeline of the client, 61 s), then the landing waves, an
  intermission, a named creature, and the cannon of the explosion [data]. So the start is an interaction of the data, not a schedule.
  Refused: each landing wave member has two creature spawners on paths of three waypoints (an officer and a navigator: a group member, a
  later stage), and the spyglass of Scallywag Shore is spawned in a timed stage and counted by the next stage (a stage that tracks a slice
  that an earlier stage spawned) [data]. The only quest event of the two recipes, `IO_Gimmick_BreakingWallsCannon_interact`, also comes
  from the placed cannons of stage QG5b: no manifest row waits for them [data; the manifest of stage QG5c3].
- Stage QG5c5, the season rule (a project rule of the orchestrator of stage QG5c5; the user can change it): the backend has no season
  schedule. A Darkness event (a recipe of the folder `slices/darkness/`: the tool writes `"season": "darkness"`) is available at all times.
  It starts only by the start rule of its recipe (its placement near a character, its manager `m_autoActivate`, its detector), as each other
  encounter; it never starts by itself across the world. The rule is one constant: `encounter_rules::kDarknessAtAllTimes` (true). With
  false the runner refuses each Darkness recipe with the cause "the season rule" [project rule; the tool refuses no seasonal recipe].
- Stage QG5c5, the Darkness events of the build [data]: 3 monoliths and 2 portals, all in Myrkgard (Shattered Mountain, levels 64 to 66).
  Their success event is `Darkness_Any_GlobalSuccess`; one row of the manifest names it (the task 0 of the mission template
  `FM_corruption_breach`, whose grant has no source in the backend: the mission boards). The parent of each one respawns after 3,000 s.
  A monolith: the setup (4 s), "Destroy the Pylons" (3 pylons; a sustain child), "Damage Carapace" (the core shell; an attack child and a
  sustain child), "Destroy Core" (the core; a sustain child), the cleanup (3 s; KillSpawnsAction and DestroySliceAction). A portal: the
  setup, "Disrupt the Ritual" (2 acolytes), a transition, "Defeat Enemy Waves" (11 child stages: 6 sub-waves and 5 transitions; the boss
  is the last one), a transition, "Destroy the Portal" (the core, 10 m above the ground), the cleanup (5 s).
- Stage QG5c5, the boat reinforcements of Monarch's Bluffs [data; live QG5c5 L3]: the setup spawns the spyglass (a start object of the
  direct row `BoatEncounter_Scallywag`: Handcrafting, no tool, 3 s, no loot, no event); the next stage tracks it (Scallywag Shore) or the
  setup itself counts it (Brysedd Shore); the boat stage (61 s) has the timeline of the client (the ship comes and docks: no record of the
  backend, live L3); the landing (a group of two parts on their paths), the intermission, the named creature (Captain Esteban of Scallywag
  Shore), the cannon (a start object), the cleanup (10 s), and the success events (no quest task names them).
- Stage QG5c5, the counts of the supported build: 64 recipes, 59 with no cause of the tool, 54 taken by the runner, 10 refused [data]. The
  Darkness 5 taken (by the season rule), the world events (the boats) 2 taken; the other patterns as in stage QG5c4.
- Stage QG5c4, the refusals of the other sequences [data]: the braziers of `99A_MSQ_10_GetCrosspiece` (each of the four spawns is an
  encounter itself, with its own manager and stages: a nested encounter; the stage has four objectives, one for each destructible brazier
  of the nested encounters; a DestroySliceAction removes the solid door), and the M5 wave event of `09A_M5_DefendSettlement` (its stages
  "Wait for Players" wait for the deactivation of `Start_Trigger_Event`, which the gather of the horn of the root slice sends: an object of
  the root record, a later stage).

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
- Stage QG5c4, the objectives of a stage: `m_objectives` of a stage can have more than one objective, each with its tracked slice and its
  count (the four braziers; the brute and the Horus of the tarot tower 10306; the berserker, the grenadier, and the smoke burst of a kill
  stage of the lost pylons; the swordsman 1, the cultist 1, the hounds 2, and the swarmers 6 of the Yonas retreat). Each objective counts
  its own spawns, and the stage meets its count when each one is met [data: one objective for each member slice; agent: all, the client
  facet joins no objectives (static read D of stage QG5c4, D6)]. Stage QG5c3 read only the first objective: the kill stage of the Yonas
  retreat ended at the first kill (live QG5c3 L2: "1 of 1").
- Stage QG5c4: in a stage with more than one objective, an objective with a tracked slice whose spawns are only creature members that the
  client does not show (no kind) is met at the start of its stage, and its members are not made (the 6 swarmers of the Yonas retreat); a
  stage whose objectives are all such objectives refuses the recipe [agent: the runner cannot make the creature, and the stage keeps the
  fight of its other objectives; the count `objectivesVoided`].
- Stage QG5c4, the entity-state stage: an `EntityActiveStateChangedObjective` waits for a change of its target entity (`m_targetEntityId`;
  `m_completeOnDeactivated` 1 in each objective of the data). The change comes from a DetectionVolumeEventComponent of the slice whose
  `m_eventsOnEnter` (or `m_eventsOnFirstEnter`) sends the event (3 deactivate, 2 activate) to the target: its detection volume is the
  trigger (`state_senders`) [data]. In the eight tarot towers the target `Trigger_Start` is a sphere of 20 m (`PlayerOnly`,
  `m_detectDeadTargets` 1) at the top of the tower, about 120 m above the root, that deactivates itself [data]. The runner looks at each
  armed trigger at its scan (500 ms): a character with an accepted position (no camera estimate) whose capsule is in a shape of the
  trigger ends the stage; a dead character counts (`m_detectDeadTargets`) [data; agent: the scan, the capsule; live QG5c4 L3: the walk to
  the top starts the boss]. A target that no volume of the slice changes refuses the recipe with the source of the change (the gather of
  the horn of 09A_M5).
- Stage QG5c4: a KillSpawnsAction removes the creature members and the objects of the stages that ended (no count, no reward), not the
  objects of a stage that runs [data: the action; agent: the objects; live QG5c4 L1].
- Code review of stage QG5c3: the activation finds the instance again after its first stage (a spawned objective that its spawn meets at
  once ends the encounter in the activation) [review QG5C3R-R1]. A spawn due at or after the time limit of its own stage gives no count
  for the count check (the end of the stage cancels it) [review QG5C3R-R3].
- Stage QG5c5, a child stage with a WaitForParentObjective (the sustain waves of the monoliths: 9 stages) ends with its parent (the end of
  a stage ends its child stages that run, with its status) [data: the class; client: it makes no task for it, static read D1 of stage
  QG5c4, 0x7C7DF0]. A parent whose child stages are not optional does not wait for such a child: it ends when it has its count and each
  child stage that is not of this kind ended [agent]. A WaitForParentObjective stage of the manager, or one before an other child stage
  of its parent (that one would never start), refuses its recipe [agent]. The data has neither.
- Stage QG5c5, a DestroySliceAction (`m_sliceAsset`, `m_aliasAsset` of the action) in the start or the end actions of a stage removes each
  member of the instance whose slice, alias, or body is the slice of the action, its alias, or a slice of its alias (no count, no reward,
  as a KillSpawnsAction) [data: the fields; agent: the removal]. The cleanups of the monoliths and of the portal SM_00 name the alias
  `alias_darkness_corruption_hazard_major`: its slices are the corruption hazards of the setup, a visual that the runner does not make, so
  nothing goes (the count `slicesDestroyed`); the client removes the corruption at the end of the instance [live QG5c5 L1].
- Stage QG5c5, the parts that the client does not show [agent: the rule of stage QG5c4 for the creatures that the client does not show,
  for the parts of a group]: a part with no kind is not made (the swarmers: live QG5c5 L0, the client shows only their spawn particles).
  An objective whose count the made parts cannot reach, and the made and the hidden parts can, gets the missing count at the start of its
  stage (`EncounterPlacement::fills`); the objective needs each part that the runner makes. The portal waves 5 and 6: 3 of 8 and 6 of 11
  (live L2: "3/8" and "6/11" at the start). An objective with no made part refuses its recipe.
- Stage QG5c5, a destroyed objective with a tracked slice counts a start object of an earlier stage of the manager order (its stage ended)
  that has that slice [data: "Look through Spyglass" of Scallywag Shore tracks the spyglass that the timed setup spawns; agent: the rule,
  only for a start object; live QG5c5 L3].
- Code review of stage QG5c5 (corrected in stage QG7; `tests/review_qg5c5_tests.cpp`): a DestroySliceAction also removes a group part
  whose selected creature has the body path of the action [review QG5C5R-R2; case QGR5C5B]. The count of a start object of an earlier
  stage takes the object only when no KillSpawnsAction or DestroySliceAction of the stages between removes it before the stage of the
  consumer; else the recipe is refused (the runner and `tools/gamedata/encounters.py`, case `removed-earlier` of
  `check_encounters_tool.py`) [review QG5C5R-R3; case QGR5C5C; `encounter-data.json` does not change]. The end of a parent stage runs
  the end actions (killSpawnsAtEnd, destroyAtEnd) of each started child stage that it ends [review QG5C5R-R4; case QGR5C5D].
- Code review of stage QG7 (corrected in stage QG6a; `tests/review_qg7_tests.cpp`): a DestroySliceAction removes a group part when
  any name of the action (its slice, its alias, or a slice of its alias) names the selected body of the part [review QG7R-R3; case
  QGR7H]. The check of an earlier count source walks the stages in the order of the run: the start actions of each stage on the path to
  the consumer, and the start and the end actions of each earlier child (the end actions of a child after its own children). A child
  that removes the source before the consumer starts refuses the count (the runner and `tools/gamedata/encounters.py`, case
  `removed-by-child` of `check_encounters_tool.py`) [review QG7R-R4; case QGR7I; `encounter-data.json` of this build does not change].
- Stage QG5c5 (live QG5c5 L2): a death counts only for the stages that ran when it came. The end of a sub-wave starts its next sibling (a
  transition that counts each death of a member) in the same step, and that stage does not count the same death.
- Code review of stage QG5c4: a trigger meets only its own objective; the stage ends when each objective is met [review QG5C4R-R2]. The
  count check takes the time limits of the stages above a spawn: its earliest time from the start of each stage above it adds the earlier
  child stages that end only at their time, and a stage with a time limit at or before that time ends first [review QG5C4R-R4]. A spawned
  objective publishes its count (a met spawned objective of a stage that waits for a kill reaches the HUD of each observer) [review
  QG5C4R-R5].
- Stage QG5c3: a destroyed stage with no tracked slice and no spawn of its own or of its child stages counts each death of a creature
  member of its instance while it runs (Wave_06 - Objective: Kill Helvan Boss: Helvan came in an earlier stage) [agent: a stage with no
  spawn has nothing else to count; live QG5c3 L3]. Such a stage needs a time limit with `succeedOnTimeout` (it has 180 s) [review
  QG5C2R-R3].
- The sequence of the stage states increases by one for each change; the history keeps 10 entries (the reader of the client takes at most
  10) [client 0x26F7550]. The backend does not send 1278: the state 2133 carries the same status [agent].
- The root Objectives has one active element with `isPoiObjective` 1 and the task elements of the generated tree. Stage QG5c4 (static read
  D of stage QG5c4): the collector 0x26F1FD0 walks the stages of the manager and their child stages in their order and takes each
  objective of a user-wave stage in the order of `m_objectives`; each objective that makes a task (a timed, a destroyed, or a spawned
  objective; an entity-state objective with a description; no WaitForParentObjective) has a leaf 2m+1 and a wrapper of one child 2m+2
  (m: the number of the tasks before it), and the outer consecutive container is 2T+1 (T tasks) [client 0x44650F0, 0x272E0E0,
  0x272D660, 0x2714D40, 0x7C7DF0]. The target of a leaf is `m_requiredCount` (destroyed, spawned) or 1 (timed, entity state); the
  progress of a kill leaf is the N of "N / M" [client 0x3ADDDB0; live QG5c2 L2]. A satisfied task has its target: the leaf target/2, the
  wrapper 1/2; the outer container counts the satisfied wrappers and shows the next one [client 0x3ADDEC0, 0x3AC0A22, 0x3AD6C60; live
  QG5c4 L1: Thorpe "Stage 2/5" after the intermission]. A running objective: its count and 0, state 1. The taskCRC is 0 (its generated
  value is not found; no compare gates the progress) [client 0x3A43FD7].
- Stage QG5c4, the timer of a row: a leaf whose objective has a time limit shows a countdown from `m_timeLimit` (`GetUIData("TimeLimit")`,
  task +0x1F4) after its start time; the start is field 0 of the root Objectives (`instance:u64 index:u8 start:u64`, the synced wall
  clock in Unix nanoseconds), which the player request finds through the link of its field 5 [client 0x3A41440, 0x3A68CB0; live QG5c4 L1:
  "Intermission 00:00:07", "Curse is lifting 00:00:06", "Defend Kievan Sidorov from the Anubian Guardians 00:00:57"]. The runner adds the
  entry at the start of the stage (`EncounterBody::taskStarts`), in the same state as the completion of the leaf before it. With
  `m_countDownProgress` the row shows no "N / M" [client 0x3AD3FB0].
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
  next look [agent]. Code review of stage QG5c3: a finder whose write the store refused stays in the ledger of the object and is tried
  again at the next looks, also out of the sphere, at most 20 looks (`kFindAttempts`: the 10 s of the credit ledger); the object goes,
  and its stage counts it one time, when each finder has its write or ran out of attempts and one write was taken [review QG5C3R-R2;
  agent: the limit]. Code review of stage QG5c4: the ledger keeps the tasks that each event of the find matched at its first attempt; a
  retry credits only these tasks, when they are still active (a quest that became active after the find gets nothing) [review
  QG5C4R-R3]. The radius is `DetectionVolumeSphereRadius` of its variation row, else the sphere of its DetectionVolumeComponent
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

- Stage QG5c5, a group member (a member slice with more than one creature spawner: the portal waves of 8 to 11 parts, the mini-boss wave,
  the subwave of a monolith, the boat landings) makes one creature for each part: each entity of the slice with an AIVariantProvider and a
  spawner of a creature slice or alias (its vitals row, its category, its body; its kind is `SLICE#N` of `creatures.json`) [data]. The
  place of a part is the spawn location plus the offset of its entity in the slice (m_worldTM), turned by the yaw of the location; its yaw
  is the yaw of the location plus the yaw of its entity [data; agent: the yaw only, no tilt]. A part whose spawner names a patrol path
  (`m_patrolPathEntity`, the AIPathComponent with its waypoints) has that route in the world (`Creature::memberRoute`) and the patrol rules
  of a spawn point ([Creature behavior](creature-behavior.md)) [data; live QG5c5 L3: the navigator of the landing walks its path]. An
  objective counts each part that it tracks (with no tracked slice: each part) [data: the counts 7, 2, 8, 11 of the sub-waves are parts of
  the groups].
- Stage QG5c5, a spawn of an alias (`m_aliasAsset` of a spawn entry; the pylon and the two cores): the tool takes the first slice of the
  highest tag of the alias (`Reader.alias`: dt6) and writes the alias with the spawn (`alias`); an objective that tracks the alias counts
  it [agent: the data has no field that names the tier of a placement; the events are at the levels 64 to 66; their direct members are dt6
  slices]. An entry with a slice and an alias takes the alias [agent from the data: the core stage of the monolith SM_01 names the shell of
  the stage before it and the core alias, and its objective tracks the core alias]. A tracked alias has its slices
  (`trackedSlices`): a part whose body is one of them counts (the `01_Attack` of the monoliths tracks
  `alias_corrupted_entity_and_impaler_elite`).
- Stage QG5c5, the pylons and the cores are start objects with an ordinary row of the gathering data (the variation table
  `Gatherable_Darkness`: AzothStaff, ToolRequired, the rank 5 of the trade skill AzothStaff; the pylon 10 s, the monolith core 12 s, the
  portal core 7 s; a loot table) [data]. The gather has the rules of its row (the Azoth Staff in slot 38, the rank), its items, and its
  trade skill experience; then the stage counts it ([Gathering](gathering.md)) [live QG5c5 L1, L2: "E CORRUPTED PYLON", "E CORRUPTED
  MONOLITH", "E CORRUPTED PORTAL"].
- Stage QG5c5, the core shells of the monoliths ("Damage Carapace": `Darkness_Monolith_Shell_*`, Elite, levels 65 and 66) are direct
  stationary members: a Vitals component (`m_rowReference`), a PositionInTheWorld component, no CharacterComponent, no movement, and the
  list `targets` of `creature_slices.json`; the record and the hits are the rules of a stationary target of stage QG5c1 ([Creatures](creatures.md))
  [data; live QG5c5 L1: "Crystallized Anxiety" with its name plate and its health bar, arrows hurt it, its death ends the stage].

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
  the body; a character on the ground at the height of the root is at the floor of the cylinder of Torment].
- Stage QG5c4: a detector shape whose `m_shapeEntityID` names an other entity of the slice is at the place of that entity: its offset from
  the root in world axes plus `m_center` (`shapeEntity`) [data]. In 5 tarot towers this is the entity `DetectionVolumeShape` at the top
  (about 131.8 m up: the boss arena) [data]. In the towers 10304, 10306, and 20338 the id is in no slice of the game copy (a scan of all
  slices found each id only in its own slice): the box stays at the root, and the sphere of the trigger of the setup is a shape of the
  detector too (`fromTrigger`) [agent: the fight is at the top, where the trigger is; with the box at the ground no character at the top
  is a participant]. The offset `m_center` is in
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
  QG5c3: `objectsFound`, `proximityLooks`, `timedEnds`, `spawnsSkipped`; stage QG5c4: `triggersFound`, `triggerLooks`, `objectivesVoided`,
  `findRetries`; stage QG5c5: `partsMade`, `partsHidden`, `partsFilled`, `slicesDestroyed`, and the line `running` of each stage that runs,
  also a child stage, with the count of each objective).
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
- Stage QG5c3 (corrected in stage QG5c4): the red 00:00:00 under a stage (no start in field 0) and the HUD row of Thorpe that stayed at its
  first intermission (a satisfied leaf had the progress 0) [live QG5c3 L3, L4; static read D of stage QG5c4]. The siphon fight has no
  user-wave stage (the fresh slice: `m_userWave` 0 in its five stages): the HUD shows no row for it [static read D].
- Stage QG5c3: the kill of Thorpe completes the task `DefeatThorpe_Solo` (a kill contribution) and with it the partial container of the
  fight; the success event of the encounter (`MSQ_0036_Thorpe_Interact`) comes after the cleanup of 12 s and finds no active task
  [data: the partial container; live QG5c3 L4]. A participant that stays in the detector is a participant of the next generation (10 s
  after the success) [data: the respawn of the parent; live QG5c3 L4].
- Stage QG5c3: the area of an officer of Helvan (Officer_AnchorSlam) staggers the character at close range and stops a bow draw [live QG5c3
  L3]: step back before a shot.
- Stage QG5c4: a start object with `InteractOnlyOnQuest` shows no prompt with no Active task of its tag: the hack device of 09A_S9 needs
  the task `09A_S9_StartHack` (task 14 of its quest, after the GoTo task of a place with no shape) [data; live QG5c4 L2]. The live check
  used `tools/world encounter use`.
- Stage QG5c4: a bush near a start object takes the prompt (the chest A of the shipwreck from the north); stand on the other side [live
  QG5c4 L1].
- Stage QG5c4: a character that stays in the trigger of a tarot tower starts the next instance at once (10 s after the success) [data: the
  respawn of the parent; live QG5c4 L3].
- Stage QG5c4: the anubian guardians of the hacker defense kill a level 65 character in some seconds (live QG5c4 L2: 735 damage for one
  hit); the live check used `tools/world heal` and `tools/world ignore`.
- Stage QG5c5: the client does not show the swarmers (`corruption_swarmer*`, the list `notShown`): a spawn of one shows only its red
  particles, no body and no name plate [live QG5c5 L0, `shots/L0-swarmers-near.png`]. The portal waves and the sustain waves have such
  parts (see the rule of the parts that the client does not show).
- Stage QG5c5: the place of a core shell is the center of its orb, about 2 m above the ground, and its hit body starts there: an arrow at
  the lower half of the orb passes under it (live L1). Aim at the upper half.
- Stage QG5c5: the creatures of the attack child of a monolith walk between the character and the shell: an arrow hits them first. The
  core of a portal floats 10 m above the ground: look up at it (live L2: a bush and a flint node took the prompt first).
- Stage QG5c5: a Darkness event respawns after 3,000 s, and a fight with no participant fails it: in a development run use
  `tools/world encounter reset NAME KEY` for a new start.
- Stage QG5c5: the cannon of the boat reinforcements showed no prompt from its side (live L3); the live check used `tools/world encounter
  use`. The side of its bound is not checked.
- Stage QG5c5: the sub-waves of a portal are not user-wave stages: the HUD shows only "Defeat all Enemy Waves"; their counts are in
  `tools/world encounter` (`running`). The loot tables of the pylons and of the cores (`DarknessPylon`, `DarknessCorruptionCore`) give
  `FlintT1` with a weight of 0: a gather gives no item [data; live L1].
