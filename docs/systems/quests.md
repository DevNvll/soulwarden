# Quests

Read this document before a change of the quests of a character: the quest data, the task trees, the quest system of the world, the
achievement actions of a task, the start quest of a new character, the backstory templates, the Objectives state, or the pins. Stage 1.8a
is the first part of the generic quest engine (roadmap row 23); stage 1.8c (the events of the other systems: the kills, the items, the
equipment, the gathers and the quest objects, the crafts, the places, the screens, the child objectives, the deaths, the abandon, the reset,
the failure) is the third part. The persons, the offers, the hand-in, the cutscenes, the whispers, and the trigger areas are stage 1.8b; the
rewards, the availability, and the recurrence 1.8d. The achievements are in [DLC and entitlements](dlc-and-entitlements.md), the wire
contracts in the protocol notes (sections 4.53 and 4.54).

## Start here

| Task | Start here |
| --- | --- |
| The quest data (objectives, compiled trees, task cells, achievement table, territory starts, backstories) | `tools/gamedata/quests.py`, `quests.sh`, `check_quests_tool.py` (case QG-TOOL), `apps/backend/src/gamedata/quest_data.h` (case QG11) |
| The rules of the tasks, a grant, the change event, a timer, the commit | `world/quests.h`, `world/quests.cpp` (`Quests::Plan`), cases QG7, QG8 |
| The start quest of a new character | `Quests::new_character_rows`, `server/http/gateway_routes.cpp` (`create_character`), `Quests::entered` (`run_pending`), case QG3 |
| The rows of the quests | `state/store.h` (`QuestRow`, `QuestTaskRow`, `QuestGrantRow`, `QuestUndoRow`, `QuestAwardRow`), schema 16, cases QG4, B11 |
| The events of the other systems (stage 1.8c) and the task index | `Quests::event`, `killed`, `plan_drops`, `items_touched`, `moved`, `tick`, `died`, `Quests::Subscriptions`; the callers: `Rewards::killed`, `Gathering::complete` (`quest_events`), `Crafting::run`, `Interactions` (the start and the completion of a use with a tag), `World::touch`, `World::place`, `Health::apply`; cases QE1 to QE9, QRV7 |
| The abandon, the reset, the failure | `Quests::abandon`, `reset`, `end_instance`, `timer_due`, `died`; the messages 3836 and 5600 (`RepSession::handle_objective_call`); the commands `abandon_quest`, `reset_quest`; cases QE11, QE12 |
| The points of interest | `tools/gamedata/pois.py`, `pois.sh`, `check_pois_tool.py`; `gamedata/poi_data.h`; the messages 2297 and 6416 (`RepSession::handle_territory_report`); case QE6 |
| The quest objects (quest gatherables) | `tools/gamedata/gathering.py` (`QUEST_TABLES`), `gamedata/gathering_data.h` (`GatherableRow::quest`, `GatheringLink::interactTag`), `Gathering::begin` (the refusal `notOnQuest`); case QE4 |
| The Objectives state (slot 22) and the seasons readiness state (slot 73) | `server/rep/states/objectives_state.cpp`, the gate of 2709 in `rep_session.cpp` (`handle_auto_pinning`), cases QG3, QG10 |
| The pins of the client (1458) | `RepSession::handle_tracked_objectives`, `Quests::client_tracked`, case QG10 |
| A backstory template for an existing character (decision D2 option C) | `Quests::apply_template`, `server/http/quest_commands.cpp`, case QG9 |
| The research | `~/Games/soulwarden/work/research/quest-engine.md`, `quest-engine-3.md`, `quest-display-18a.md`; the results of the stages: `~/Games/soulwarden/work/evidence/quests/18a/implement/RESULT.md`, `18c/implement/RESULT.md` |

## Rules

The source of each rule is in brackets: [data] the game data, [client] the client code, [live] a live check, [user] a decision of the
user, [orchestrator] a decision of the orchestrator, [agent] a decision of the agent of the stage (with the cause).

- The quest data is game data: `tools/gamedata/quests.sh` makes `quest-data.json` (format 1) from the game copy. With no file the backend
  starts and has no quest: no start grant, no quest state, no quest row, and the other systems do not change (rule A5a) [roadmap rule].
- The tree of an objective has the index rule of the client [client 0x4466350]: a child gets its index before its container, the index
  goes up by one after each child, a container gets the index after its children; a container that the tree built before is a hole (no
  node; one in the build: `99E_MSQ_10_ToCaerDun`). The tool and the loader refuse a missing task link, a cycle, two ids with one CRC-32,
  an index above 255, and a child that is not below its container [agent: the client would build a different tree or none].
- A task cell keeps the source text: a blank cell is not in the file, an explicit 0 is 0 [data; research quest-engine.md Q1]. The target
  of a leaf is its TargetQty (blank: 1); of a simple or consecutive container the number of its children; of a partial container 1 [client
  0x3AD0C60, 0x3AD6D20]. A task is satisfied when its progress reaches its target; the progress of a container is the count of its
  satisfied children [client 0x3AD6C60, 0x3AD6D20].
- The rules of the tasks [client; research quest-engine.md Q1 and quest-display-18a.md Q2]: a consecutive container activates its first
  unsatisfied child and the next one after each completion; a simple container activates all children and completes when each is
  satisfied; a partial container activates all children and completes when one is satisfied, and its unfinished children with no
  effective StayActive become Inactive (4). TaskUnlockAchievement, TaskLockAchievement, and TaskTemporaryUnlockAchievement run their action
  at the activation and complete. TaskAchievement completes when its conditional is true. TaskTimer completes at its start plus Duration.
  The types of stage 1.8c are in the rules below. Each other type stays active: its supplier is a later part (the list is in the result
  of stage 1.8c: trigger areas, conversations, cutscenes, whispers (TaskTriggerGameEvent), game modes, fishing, songs, and more).
- A satisfied task is Complete (2), or CompleteActive (3) when it has an effective StayActive: a container inherits it from any child, a
  TaskTimer with FailOnCondition has it [client 0x3ACDC10, 0x3B2BA40; research quest-display-18a.md Q3].
- The conditional of TaskAchievement: achievement ids with `!`, `&&`, `||`, and parentheses [client 0x31898D0, 0x3A8C640]. An id is true
  when the character has its bit; a non-persisted achievement (NPAData) is its own conditional; an NPA with a status requirement is false
  (no status rule in 1.8a) [agent: the status of a quest is a later part]; an id that the data does not know is false [research U7].
- One change of the quests of a character is one plan: a copy of its quests and of its bits, the rules to a fixed point (an action can
  complete a TaskAchievement of the same character), then one transaction (the quests, the tasks, the pins, the achievement rows, the
  undo rows, the grant rows), and only then the entity, the bits (`Ownership::quest_achievements`), the marks, and the timers. A plan that
  the database does not take changes nothing; a plan with more than 100,000 steps changes nothing (a loop of actions) [project rules MP-1,
  MP-7].
- The cost: an achievement change of a character visits the indexed TaskAchievement tasks of that character only (Active or
  CompleteActive), and the counter counts each visit before any filter (code review of stage 1.8a, finding QR8A-R7; case QRV7); a timer is
  one timer of the world clock; an entry loads the rows of that character (case QG2: 50 characters with 20 waiting quests, each event
  visits 20 tasks) [project rule MP-8].
- The change event: each change of the bits of a character that the quest system did not make (a lore page, a first craft, a build
  preset, the DLC, a development command) calls `Quests::achievements_changed` [agent: the one path of a bit change].
- An unlock action with no StayActive and a lock action with no StayActive write an undo row; a temporary unlock always writes one (lock at
  the end of the quest). The abandon, the reset, and the failure run the undo rows of the instance (stage 1.8c) [client 0x8260730,
  0x8260880, 0x8260590; agent: StayActive is the prevention of the undo ("Prevent Reverting", 0x8260800)].
- TaskTimer: the start is a time of the synced wall clock (Unix nanoseconds) [client 0x3A41440, 0x6830A40]; the end is the start plus
  Duration with its fraction (25.4 s) on the world clock [client 0x4461150: the client keeps the fraction]. The time while the character
  is away counts: the next entry ends a timer whose end is past [agent: the client shows start + Duration against the synced wall clock;
  decision D4 of the user is open]. A failure timer (FailOnCondition) is satisfied while it runs: CompleteActive with its target and its
  start, so that its container goes on [client 0x3B2BA40, 0x3B2D250: research quest-display-18a.md Q4; code review of stage 1.8a, finding
  QR8A-R2; case QRV2]; its end fails the task (5, progress 0) and the objective (stage 1.8c: the end of the instance, below). A timer whose
  completion write the store refuses tries again after 2 s with the same instance, index, and start (finding QR8A-R5; case QRV5).
- The start quest of a new character [user D1: the Windsward beach; data: TerritoryDefinition row 06, ObjectiveID 06_getoffbeach]: the
  create route writes, in the transaction of the character, the start position on the beach (9622.62, 2306.48, ground 40.24; the world
  spawns 0.5 m above it) [data: the place of the cinematic; agent: the data has no player spawner], a pending one-time grant (source
  `territory`, key 6), and the achievement of the archetype (AchievementUnlockOverride of the backstory row, `Archetype_Mercenary` for
  `Archetype_Soldier`) [data]. The first world entry grants the objective and marks the row granted: one time only. With no quest data
  the create route writes none of these, and the character starts as before.
- The automatic pin of a grant: the flag of the objective type in the last 2709 of the client; with no flag (no 2709 yet: the start grant
  comes before the first record) the default of the client options, all types but CommunityGoal (6) [live: run r056 of quests.md; agent:
  the client has no automatic pin for these types, quests.md 3.4]. 8 pins at most [client 0x3A68340].
- The pins of the client (1458): only active instances of the character, 8 at most, no instance two times; the list is the truth, and no
  state goes back to the client [research quests.md 6.2]. A list with a foreign instance changes nothing. A 1458 before the first
  Objectives state of the session changes nothing (the client sends an empty list before its facet has the saved quests: code review of
  stage 1.8a, finding QR8A-R1; case QRV1).
- The quest state is owner only (the remote player slice has no slot 22 and no slot 73) [data]. The session sends it only after the first
  2709 of its client (`StateCodec::afterObjectivesEnable`, `Known::gatedSent`) [live L1; client OnEnable 0x3A52510]. The readiness state
  (slot 73, `initialized` 1) goes with it [live L1: the pin comes when it comes].
- The task elements of a quest are in index order [live L1; client apply 0x3A43530: the elements run in the received order and the
  callbacks of the client run between them]. A container is satisfied only with its count [client]; a Complete container with progress 0
  stops the parent of the client [research quest-display-18a.md, F1 to F4].
- The active element: acceptTime 0, originTerritory of the grant, available 1, the other flags 0 [live L1 with 0; agent: the native timer
  of the client can take its start from the creation time (research quest-display-18a.md Q4), and the start of a timer is field 0].
- In one record the client applies slot 22 before slot 66 (it sorts by slot); a task change and a bit of the same change work in this
  order [live L2]. The quest system marks both states in one tick.
- Decision D2 option C [orchestrator, 2026-10-18; user selects the template]: the command `apply_backstory` (admin and development
  routes) gives a character the achievements and the start objectives of one backstory template of the data, not its level, items, or
  faction. An archetype row is not a template. A start objective that the data does not have is listed in the reply and not given (8 of
  the 22 start objectives of the templates have no row in this build). A character that is not in the world gets the rows and a pending
  grant; its next entry gives the objective. An objective that the character has is not given again. No character changes without the
  command.

### The events of stage 1.8c

- One event path [roadmap rules MP-1, MP-2]: each system changes its own state and then calls the quest system with the fact (`Quests::event`
  and the calls below); the quest system changes only the quests, and the items of an item task in its own transaction (the plan of the
  inventory, `Inventory::plan_quest_items`). An event comes after the commit of its system: a request, a refusal, or a cancel is no event
  [research quest-engine.md Q1: "a request or an animation start is not a successful action"]. A task that the event does not change gets no
  plan; a plan copies only the quests that it changes.
- The index [MP-8; finding QR8A-R7]: each character in the world has an index of its Active and CompleteActive tasks by the key of their
  event (a vitals category, a game event, an item, a recipe, a screen, a tag, a source of a quest drop, "items", "place", "ach", "die").
  An event visits only the tasks of its keys; the index of an instance is built again after each commit of it. Case QE9: 50 characters,
  10,000 events, 2,000 creatures with no quest: 75 visits (only the subscribers), about 0.01 s.
- A result counts one time: an event with a result id (a kill: the creature and the revision of its death) that came before changes
  nothing; the store keeps the last 4,096 result ids [agent: a bound store; research quest-progress.md 8 "credit one result once"].
- A kill [client 0x825E7C0; data VitalsBaseData.VitalsCategories]: `Rewards::killed` gives each character of the credit list (the credit
  rule of [Rewards](rewards.md)) the kill with each vitals category of the victim (the VitalsCategories of its base vitals row, in lower
  case: `Corrupted_Sailor_1H_Club,Hostile,Corrupted,Pirate`; a loot tables file of before stage 1.8c: the vitals row and the category row
  of the kind) and the kill event of its vitals row (KillEventIdOverride, or KillEvent). TaskKillContribution counts a kill whose categories
  have one of its KillEnemyType; TaskGameEvent counts the event [live K1: the kill of a Corrupted Mariner, `Corrupted_Sailor_1H_Club`,
  shows "1 / 5 - Defeat Corrupted Mariners" on the HUD]. KillEnemyWeaponID (5 rows): the item of the kill must be that item; the
  backend has no item of a kill now, so such a task waits [agent]. A character that is not in the credit list gets nothing (case QE1, QE8).
- The location rule [client 0x825DAD0, 0x825E860]: a task with EnforceLocationRequirement progresses only while its character is in a point
  of interest of its POITag (and of its TerritoryID). With no such flag the POITag of a task is only the place of its pin (the kill of 5
  Corrupted of `06_crewmates` has the POITag of the shipwreck and no flag: a kill at any place counts while the task is
  Active; see the trap of its place task).
- The items [client 0x825F310, 0x825F730, 0x825E360]: the possession tasks (TaskHaveItems, TaskHaveAndReturnItems, TaskGatherItems,
  TaskGatherAndReturnItems) have the count of their item in the bag and in the paperdoll, at most their target [agent: the paperdoll counts
  too (a knife in its tool slot is a knife of the character); the global storage of UseGlobalStorage is stage 5.2: the bag only]. They look
  at the activation and after each change of the items: `World::touch` of the bag or the paperdoll state marks the character, and the tick
  looks one time [agent: one event path for each writer of the items]. TaskEquipItem: an item of the paperdoll with the id and a gear score
  of MinGearScore or more.
- The regression of a StayActive task [client 0x3ADDEC0; finding QR8A-R3]: a predicate task (a possession, an equip, a place,
  TaskAchievement) that is CompleteActive and whose value falls below its target returns to Active with its value; its container counts
  again, a consecutive container deactivates its later Active or CompleteActive children (Inactive, the progress stays; client 0x3AC0990),
  and a CompleteActive container whose count falls below its target is Active again (to the root). A Complete task (no StayActive) keeps
  its state. When the task is satisfied again, the consecutive container activates its next child that is not done [live K1: out of
  the shipwreck the HUD of `06_crewmates` shows the text of its place task 6; in the place again the kill task is Active with its
  progress 1, and the HUD shows "1 / 5"].
- The item actions [client 0x825EEA8, 0x825F020, 0x8260370]: TaskGiveItem and TaskGiveAndTakeItem give their item at the activation
  (the cause ItemGathered: the loot notice) and complete; AwardOnlyOnce: only when the award row of the objective and task is below the
  target (a reset keeps the row: no second item; an abandon removes it) [client 0x825EEF0]. A give with no place in the bag waits (the task
  stays Active and tries again at the next change of the items) [agent]. TaskRemoveItem takes its count from the bag and the quick slots (a
  character with fewer items loses what it has) and completes [agent: research Q1 "insufficient-items result needs a rule"].
- The quest drops [client 0x825F400 to 0x825F630, 0x825FA20]: an Active possession task with ItemDropVC and ItemDropProbability above 0 and
  MinGearScore 0 drops one item of its ItemName for each credited kill of a victim of an ItemDropVC category (TaskHaveItems,
  TaskHaveAndReturnItems) or each gather of an ItemDropVC gatherable row (TaskGatherItems, TaskGatherAndReturnItems), with the probability
  in percent; PauseAndClearLootOnTaskComplete: no drop while the character has the target count; AwardOnlyOnce: no drop when the award row
  has the target; GuranteedDropAttempts: the drop after that many misses is sure (the misses stay in the memory of the world only). A kill
  drop goes with the loot of the character (its part of the loot bag); a gather drop is a part of the result of the gather (one
  transaction; a full bag refuses the whole gather, and the drop is not a part of the kept roll). One item for each kill or gather [agent:
  the data has no quantity].
- A gather [client 0x825FED0; data GameEventID]: TaskRequireGatherItems counts the items of the roll (not the quest drops); the game event
  of the gatherable row (MSQ_Treasure_A_interact) counts for TaskGameEvent; the tag of a quest object counts for TaskInteract.
- A quest object [live G1; client 0x8261FC0; data javelindata_variations_gatherables_quest]: a placement of Master_Quest_Instanced
  (variation table Gatherable_Quest) is open only while an Active task of its character has the InteractTag of its variant (the gather is
  refused with `notOnQuest` else); its result is the quest drops and its game event; each character gathers its own object (no shared
  depletion: InstancedLootType 4 of the layer metadata). Live G1: with no task the client shows no prompt and no beam; with the task the
  prompt "E ASTROLABE", the beam, and the world pin.
- A bound of the interaction data with InteractOnlyOnQuest is open only while an Active task of the character has its tag (before stage 1.8c:
  refused as unsupported). The start of a use of a bound with a tag, and its completion (a use with no time completes at its start), count
  for TaskInteract with OnInteractStart 1 and 0 [client 0x825E470].
- A craft or a refine [client 0x825DDE0, 0x825E080, 0x825E1B0]: each committed job of a batch is one event with the recipe, the units of
  its output (with the bonus units), and the gear score of the output. TaskCraftRecipe (CraftingRecipeId, or CraftAnyRecipe; MinGearScore),
  TaskCraftMultipleRecipes (the recipes of ItemDropVC), TaskRefineRecipe (a refining recipe only).
- The places [live POI1; data TerritoryDefinition, the shape files; client 0x825EB30, 0x825EBF0]: TaskGoToPOITag and
  TaskGoToPOITagOnTerritory are satisfied while the accepted position of the character is in the shape of a point of interest of a POITag
  (TerritoryID limits them to the points of interest of that territory: "watchtower" has rows in 4 territories); TaskExitPOITag while it is
  outside each of them. They look at the activation and after a move (`World::place` marks the character; the tick looks one time). A camera
  estimate (no accepted position) changes nothing. With no POI data a place task waits. The membership is the shape, not the discovery
  sphere [live POI1: the client reports the sphere at 150 m and the shape 1.5 m inside the border of the data].
- 2297 and 6416 are a cue: the server tests the shape of the data itself (the client sends 6416 for the exit of the sphere and of the shape
  with no difference in the body) [live POI1]. The discovery achievement of 2297 is the map (stage 2.7): no bit now.
- A screen (2617) [client 0x8261660, sender 0x3A055A0; live: `a1126b78` is "hud"]: TaskOpenUiScreen counts the CRC-32 of its lower-case
  UIScreenTag.
- TaskObjective [client 0x82619E0]: the activation grants the child objective (a new instance with the parent instance; no second instance
  of an active objective) and the task waits; the completion of the root of the child completes the task [agent: the hand-in of the child is
  stage 1.8b; its root is the completion of the child for its parent now].
- TaskDie [client 0x825E5F0, 0x825E628]: satisfied while the character lives (CompleteActive: an effective StayActive) [agent: 184 of 185
  nodes are in a simple container, which needs each child]; the death of the character fails the objective (FailOnCondition) or resets it
  (ResetOnCondition).
- The end of an instance (the abandon 3836, the reset 5600, a failure) [client 0x825EEF0, 0x825F730, 0x825FB70, 0x825F020; live finding
  of stage 1.8a: state 0 and 4 do not reset a task of the client]: one plan removes the instance and its rows (the tasks, the undo rows),
  runs its undo rows, takes the target count of each return task that was active (at most what the character has) and the items of a
  completed TaskGiveAndTakeItem that are quest items (Nonremovable), and removes the pin. The abandon needs CanBeAbandoned (default 1) and
  removes the award rows of the objective. The reset (any objective; the client offers it for the objectives with CanBeAbandoned 0) keeps
  the award rows and adds a new instance with the pin at the place of the old one. A failure (a failure timer, TaskDie) keeps the award
  rows. The client shows the end as an objective that leaves its list (the banner "Objective Failed" of a removal with no completed entry:
  live L1 of stage 1.8a). Live L2 of stage 1.8c: the journal shows "Abandon" for an objective with CanBeAbandoned 1 and "Reset" for a
  main story objective; each asks for a confirmation, then sends 3836 or 5600 (protocol notes 4.54).
- A disconnect ends nothing: the quests stay in their rows; the entry evaluates the predicates again (the items, the place, the bits can
  have changed while the character was away).

## Commands

```sh
tools/gamedata/quests.sh [OUTPUT.json]          # the quest data (a test: a path outside the common game data folder)
tools/gamedata/quests.sh --objective 06_getoffbeach   # print a tree (no file)
tools/world quests CHARACTER                    # the quests, their tasks, the pins, the one-time grants, the counts
tools/world quest templates                     # the backstory templates and their start objectives
tools/world quest template CHARACTER TEMPLATE   # decision D2 option C (the command apply_backstory)
tools/world quest grant CHARACTER OBJECTIVE     # development only: a new instance of an objective
tools/world quest abandon CHARACTER INSTANCE    # stage 1.8c: the rule of message 3836 (CanBeAbandoned 0 refuses it)
tools/world quest reset CHARACTER INSTANCE      # stage 1.8c: the rule of message 5600 (a new instance; the award rows stay)
tools/gamedata/pois.sh [OUTPUT.json]            # the points of interest (a test: a path outside the common game data folder)
tools/world achievement CHARACTER ID on|off     # development only: the change event, the lock of a last bit
```

The admin interface has the commands `apply_backstory` (argument `template`), `abandon_quest` and `reset_quest` (argument `instance`, 16
hexadecimal digits), the route `GET /admin/v1/catalog/backstories`, and the member `quests` of the state of a character (stage 1.8c: with
the award rows and the counts of the events). The tool window has no control for them in this stage.

## Traps

- With the parent-first order of the research fixture the client shows the objective, its banner, its pin, and its rewards, and no task
  line (live L1). The order of the elements changes the result: the client runs its own container callbacks between two elements.
- A Complete container with progress 0, or a completed leaf with progress below its target (TargetQty 2), stops the consecutive parent of
  the client: the later path is deactivated, and the HUD and the journal show the title only (live L4 attempts; research
  quest-display-18a.md F4).
- State 0 (New) and state 4 (Inactive) on the wire do not change a task of the client: its setter skips them [client 0x3ADDE50]. A server
  reset of a task needs a new instance or a parent transition (stage 1.8c).
- A full active list that drops an objective with no completed entry shows the banner "Objective Failed" (live L1). Keep each active
  objective in each state.
- The bit `Start_IGC_Aelstrom_WW` makes the client request the cutscene `4817DD08` (message 6703, 2 s later) and move its camera while it
  waits for the answer (live L2). Stage 1.8a sets this bit only through task 10, which needs the trigger area of stage 1.8b.
- A second world entry in one client process keeps client state of the first one (live L2: the client sent 6703 at the entry). Restart
  the game for a clean check.
- 8 start objectives of the backstory templates have no objective row in this build, among them `Level60_MSQ6001` (`9919_0037_planb`) and
  `Level60_PTR` (`0511_0201_UmbrasReach`), the examples of the research.
- Stage 1.8c: a consecutive container counts each satisfied direct child, also a later one (client 0x3AD6C60): after the regression of an
  earlier child the count can stay above the number of the children before it (case QE2: 3 of 4 with the second child Active).
- Stage 1.8c: a travel gives an estimated position (`World::travel`): the tasks of a place change only at the next accepted position report
  of the client (some hundred milliseconds later).
- Stage 1.8c: the event of a quest gatherable has no row of the event tables (MSQ_Treasure_B_interact): the gathering tool and the loader
  accept it for a quest row (no trade skill experience).
- Stage 1.8c: the region metadata does not agree with each shape to the centimeter: entries 0 to 5.4 m from the border are on the wrong
  side (the watchtower 40148). The POI tool accepts a margin of 6 m; 285 of 761 shape files of the build have no POI placement that agrees
  (most of them have their origin in a different layer or region): their points of interest have no shape, and their place tasks wait.
- Stage 1.8c: the client sends 2297 for the discovery sphere at each entry while the discovery achievement is not set (the map of stage 2.7
  sets no bit): a 2297 is no proof of a place.
- Stage 1.8c: while the creatures ignore a character (`tools/world ignore CHARACTER on`), its kills give it no credit, and so no quest
  count (live K1: the first kill did not count). Turn the mode off for a test of a kill task.
- Stage 1.8c: the place task of `06_crewmates` (task 6, StayActive) regresses when the character leaves the shipwreck, and the kill task 8
  is Inactive then: a kill outside the place does not count, although task 8 has no EnforceLocationRequirement (live K1).
