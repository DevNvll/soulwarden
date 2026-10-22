# Quests

Read this document before a change of the quests of a character: the quest data, the task trees, the quest system of the world, the
achievement actions of a task, the start quest of a new character, the backstory templates, the Objectives state, the pins, the offers,
the hand-in and its rewards, the cutscenes, the whispers, or the trigger areas. Stage 1.8a is the first part of the generic quest engine
(roadmap row 23); stage 1.8b (the offers and the hand-in at a person, the rewards that the backend can pay, the effects of a topic, the
cutscenes and the cinematics, the whispers, the trigger areas) the second; stage 1.8c (the events of the other systems: the kills, the
items, the equipment, the gathers and the quest objects, the crafts, the places, the screens, the child objectives, the deaths, the abandon,
the reset, the failure) the third. The full reward join (the loot containers, the territory standing, GearScoreRange), the availability, and
the recurrence are 1.8d. The persons are in [Persons](persons.md), the achievements in [DLC and entitlements](dlc-and-entitlements.md), the
wire contracts in the protocol notes (sections 4.53, 4.54, and 4.56).

## Start here

| Task | Start here |
| --- | --- |
| The quest data (objectives, compiled trees, task cells, achievement table, territory starts, backstories) | `tools/gamedata/quests.py`, `quests.sh`, `check_quests_tool.py` (case QG-TOOL), `apps/backend/src/gamedata/quest_data.h` (case QG11) |
| The rules of the tasks, a grant, the change event, a timer, the commit | `world/quests.h`, `world/quests.cpp` (`Quests::Plan`), cases QG7, QG8 |
| The start quest of a new character | `Quests::new_character_rows`, `server/http/gateway_routes.cpp` (`create_character`), `Quests::entered` (`run_pending`), case QG3 |
| The rows of the quests | `state/store.h` (`QuestRow`, `QuestTaskRow`, `QuestGrantRow`, `QuestUndoRow`, `QuestAwardRow`, `QuestRepeatRow`), schema 17, cases QG4, B11 |
| The events of the other systems (stage 1.8c) and the task index | `Quests::event`, `killed`, `plan_drops`, `items_touched`, `moved`, `tick`, `died`, `Quests::Subscriptions`; the callers: `Rewards::killed`, `Gathering::complete` (`quest_events`), `Crafting::run`, `Interactions` (the start and the completion of a use with a tag), `World::touch`, `World::place`, `Health::apply`; cases QE1 to QE9, QRV7 |
| The abandon, the reset, the failure | `Quests::abandon`, `reset`, `end_instance`, `timer_due`, `died`; the messages 3836 and 5600 (`RepSession::handle_objective_call`); the commands `abandon_quest`, `reset_quest`; cases QE11, QE12 |
| The points of interest | `tools/gamedata/pois.py`, `pois.sh`, `check_pois_tool.py`; `gamedata/poi_data.h`; the messages 2297 and 6416 (`RepSession::handle_territory_report`); case QE6 |
| The quest objects (quest gatherables) | `tools/gamedata/gathering.py` (`QUEST_TABLES`), `gamedata/gathering_data.h` (`GatherableRow::quest`, `GatheringLink::interactTag`), `Gathering::begin` (the refusal `notOnQuest`); case QE4 |
| The Objectives state (slot 22) and the seasons readiness state (slot 73) | `server/rep/states/objectives_state.cpp`, the gate of 2709 in `rep_session.cpp` (`handle_auto_pinning`), cases QG3, QG10 |
| The pins of the client (1458) | `RepSession::handle_tracked_objectives`, `Quests::client_tracked`, case QG10 |
| A backstory template for an existing character (decision D2 option C) | `Quests::apply_template`, `server/http/quest_commands.cpp`, case QG9 |
| Stage 1.8b: the offer (3772), the hand-in (794) and its rewards | `Persons::accept`, `Persons::hand_in` (the source), `Quests::available`, `accept`, `hand_in`, `end_instance` (the completion); `Progress::plan_experience`; cases QB2, QB3, QB5, QB6 |
| Stage 1.8b: the cutscenes (6703, 6707, 6706, 6705) and the cinematics (6606, 6542) | `Quests::cutscene_requested`, `cutscene_started`, `cutscene_ended`, `cinematic`; `RepSession::handle_cutscene_request`, `handle_cutscene_call`, `handle_cutscene_ended`; cases QB2, QB4 |
| Stage 1.8b: the whispers (5070) and the game event of a task | `Quests::Plan::apply_event`, `SessionSink::whisper`, `RepSession::whisper`; the events and the whispers of `gamedata/quest_data.h`; case QB3 |
| Stage 1.8b: the trigger areas | `tools/gamedata/triggers.py`, `triggers.sh`, `check_triggers_tool.py`; `gamedata/trigger_data.h`; the predicate of TaskTriggerArea in `Quests::Plan::predicate`; cases QB1, QB2, QB4 |
| Stage 1.8b: the effects of a topic, TaskConversationTopic, TaskConversationState | `Quests::topic_finished`, `state_selected`; `Persons::topic` (5397), `Persons::started`; case QB4 |
| The completed notice (field 2 of the Objectives state) | `QuestLog::completed`, `server/rep/states/objectives_state.cpp`; cases QB2, QB8 |
| Stage 1.8d: the reward join, the standing, the loot container | `Quests::reward_join`, `Quests::hand_in`, `Skills::add_standing`, `ItemContainers` (`world/item_containers.h`), message 1733 (`RepSession::handle_salvage`); cases QD1, QD2, QD6 |
| Stage 1.8d: the availability of an offer | `Quests::available`, `gamedata::faction_mask`; case QD3 |
| Stage 1.8d: the repeat of a quest | `Quests::repeat_due`, `schedule_repeat`, `next_cycle`, `QuestData::schedule_kind`, the rows `character_quest_repeats`; cases QD5, QD7, QD8 |
| Stage 1.8d: what a hand-in pays, and why it is refused | the command `quest_rewards` (`server/http/quest_commands.cpp`), `tools/world quest rewards` |
| Stage 1.8e: the destructible quest objects (the nodules, the monolith) | `tools/gamedata/quest_objects.py`, `quest-objects.sh`; `gamedata/quest_object_data.h`; `world/quest_objects.h` (`QuestObjects::strike`, `destroy`, `respawn_due`); the strike events of `world/attack_sequence.cpp` and `Combat::run_event`; the LootTracker merge of `server/rep/states/chest_states.cpp`; cases QF1, QF2 |
| Stage 1.8e: the latch of an Enter area, the Azoth reward | `Quests::set_progress` (TaskTriggerArea with `AreaRule::enter`); `Quests::reward_join` (`RewardJoin::azoth`), the skills plan of `Quests::commit`; cases QF3, QF7 |
| Stage 1.8e: the support manifest | `world/quest_support.h` (`QuestSupport::manifest`, `default_profiles`), the command `quest_support_json`; the second computation `~/Games/soulwarden/work/evidence/quests/18f/implement/scripts/support_check.py` (stage 1.8f); cases QF4, QRE5, QRE6, QRE8 |
| Stage 1.8e: a cinematic in a development run | the command `quest_cinematic` (`server/http/quest_commands.cpp`), `Quests::cinematic` |
| Stage 1.8f: the corrections of the review of 1.8e | `Combat::strike_objects`, `QuestObjects::strike` (the cutscene, the wall line: `Navigation::line_ready`, `line_stop`), `Quests::object_destroyed` (one plan), `QuestObjectData::load_text` (one actor, one key), `QuestSupport` (the items, the hand-in, `std::size_t` counters); `tests/review_quests_18e_tests.cpp`, cases QRE1 to QRE8, QF1 |
| The research | `~/Games/soulwarden/work/research/quest-engine.md`, `quest-engine-3.md`, `quest-display-18a.md`; the results of the stages: `~/Games/soulwarden/work/evidence/quests/18a/implement/RESULT.md`, `18b/implement/RESULT.md`, `18c/implement/RESULT.md`, `18d/implement/RESULT.md`, `18e/implement/RESULT.md`, `18f/implement/RESULT.md`; stage 1.8d: `research/scratch/quests-18d/`; stage 1.8e: `research/scratch/quests-18e/`; stage 1.8f: `~/Games/soulwarden/work/evidence/quests/18f/implement/static/STATE-SELECTION.md` |

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
  (no status rule in 1.8a) [agent: the status of a quest is a later part]; an id that the data does not know is false [research U7]. An
  expression that the reader cannot read to its end (a missing operand, a missing closing parenthesis, text after the end) is false, also
  under a negation and in an NPA [code review of stage 2.5c, finding PR5C-R7; case QRP7].
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
  stays Active and tries again at the next change of the items) [agent]. TaskRemoveItem takes its count from the bag, the quick slots, and
  then the other slots of the paperdoll (a character with fewer items loses what it has) and completes [agent: research Q1
  "insufficient-items result needs a rule"; the paperdoll: finding QR8C-R4]. The item actions of one plan apply in the order of the tasks
  (finding QR8C-R2).
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
  refused as unsupported). The start of a use of a bound with a tag, and its completion, count for TaskInteract with OnInteractStart 1 and
  0 [client 0x825E470]. The time of a use is the time of its handler or the Delay Time of its option: only a use with no time completes at
  its start (finding QR8C-R8). Only a completion that the handler accepts counts (`InteractionHandler::complete` gives true; finding
  QR8C-R12). A gather gives its completion itself after its commit, one event with the tag of its quest object and the tag of its bound,
  each tag one time (`owns_interact_event`; finding QR8C-R13).
- A craft or a refine [client 0x825DDE0, 0x825E080, 0x825E1B0]: each committed job of a batch is one event with the recipe, the units of
  its output (with the bonus units), and the gear score of the output (the craft gear score of the job, else the rule of a new instance of
  the item table: GearScoreOverride, else the smallest gear score; finding QR8C-R10). TaskCraftRecipe (CraftingRecipeId, or CraftAnyRecipe; MinGearScore),
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
  of an active objective) and the task waits; the completion of the root of the child completes the task [agent: the root of the child is
  the completion of the child for its parent; 136 of 138 TaskObjective rows are not in a tree of the data]. The reset of a parent gives its
  active child to the new parent in the transaction of the reset (code review of stage 2.5c, finding PR5C-R4; case QRP4).
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

### The offers, the hand-in, the cutscenes, the whispers, and the trigger areas (stage 1.8b)

- The offer (3772, the CRC-32 of the ObjectiveID) [client 0x3722700, 0x375A980; research quest-engine.md Q3, Q4]: the person system checks
  the source (the open conversation of the character, the person there for it, the character in the range of its bound) and that the
  selected state of the character now offers the objective (LinearObjectiveId1 to N). The quest system then needs the RequiredAchievementId
  of the objective to be true (the client shows only the variant of the archetype of its player: the eight variants of 06_boarfeast name
  one archetype achievement each) [data], no active instance, and no completion achievement (a repeat is 1.8d) [client IsObjectiveComplete
  0x3A48A30]. The grant has the source `offer` and the territory of its ExclusiveTerritory [agent: the beach chain has 06; the journal shows
  "Windsward"]. Live I2 of stage 1.8b: Aelstrom offers 06_skinningknife after the hand-in, and 3772 `d137c605` accepts it.
- The hand-in (794, the CRC-32 of the ObjectiveID) [client 0x37228F0; live P1 of stage 1.8b: `e4d66020` at "Complete the Quest!"]: the
  person system checks the source and that the NpcDestinationId of the objective is the person of the conversation [data]; the quest system
  needs an active instance whose root is Complete or CompleteActive [client]. 794 names no instance: the active instance of the objective
  of the character.
- The completion is one transaction (the plan, write, and finish of the build presets) [brief of stage 1.8b; MP-1]: the instance and its
  rows go; the undo rows of its temporary unlocks run (an unlock or a lock with no StayActive stays: client 0x8260730, 0x8260880 revert
  only at a reset or an abandon, 0x8260590 also at a completion; live P1: Aelstrom 01, whom task 13 of 06_getoffbeach shows, must stay for
  the next offer); the return tasks take their items, the pin goes; the AchievementId of the objective and of the success event are set; the
  experience (UniversalExpAmount of the success event and ExperienceReward of the row), the coin (CurrencyReward, in the units of the
  wallet: 150 is 1.50 coin; CoinRewardChance 1 for a blank cell, client 0x31E70B0), the item of the event (ItemReward, ItemRewardQty 1 for
  a blank cell) and of the row (ItemRewardName, ItemRewardQty) go in. The items have the rules of a new instance of stage 1.3b
  (`Rewards::grant_items`: GearScoreOverride, the fixed perks, the perk places with the level of the character). ObjectiveReward grants
  its objective in the same plan [research quest-engine.md Q6]. After the commit: the entity, the bits, the level (`Progress::finish`),
  the items and the coin, and the completed notice.
- A reward that the backend cannot pay refuses the hand-in, and the quest stays ready [brief of stage 1.8b: a completed quest must not lose
  a reward]: a TerritoryStanding, FactionReputation, FactionTokens, CategoricalProgressionReward, Azoth, UmbralCurrency, PvpXp, SeasonsXp,
  AzothSalt, CursedSilvers, or CursedCrowns that is not 0, an EntitlementId, a StatusEffectId, a GearScoreRange, a ProgressionReward of the
  row, an item that the catalog does not have, a loot container item (class LootContainer: the Meat Pouch of 06_skinningknife; its open path
  is 1.8d, research U9), a negative quantity. A full bag refuses the hand-in [decision D5 option A of the orchestrator]. A refused write of
  the store changes nothing (case QB5).
- The completed notice [client apply 0x3A43530 to 0x3A559B0; live L3 of stage 1.8b]: the Objectives state names the instance of a hand-in
  in field 2 (completedObjectives) in the record that removes it; the client then shows no "Objective Failed" (live L3: 06_demonstorm leaves
  the HUD and the journal). The list is a value of the world entry (16 instances at most) and is written only when it has an instance.
- TaskTriggerArea [data TP_TargetTag, TriggerAreaType; live T1 of stage 1.8b]: the client sends no message for these volumes (no 560 at
  the entry of Aelstrom_IGC_Trigger, also not at the spawn in it). The task is a predicate of the accepted position: Enter and Stay Inside
  are satisfied while the position is in a volume of the tag, Leave while it is outside each volume of the tag (the TriggerAreaType of 778,
  3, and 10 rows) [agent: the research rule "a validated entry or an in-volume position"; a character that is in the volume at the
  activation satisfies an Enter task: the start position of a new character is in Aelstrom_IGC_Trigger, and live I1 starts the cutscene at
  the first accepted position]. A StayActive task regresses when the character leaves (the rule of 1.8c). No trigger data, no volume of
  the tag, or a camera estimate: the task waits. The index has the task under "place": a move looks only at the volumes of the tags of the
  active tasks of the character (MP-8; case QB4).
- The trigger data is game data [data]: `tools/gamedata/triggers.sh` makes `trigger-data.json` from the chunk slices of each region: each
  entity with an ObjectiveDetectionVolumeComponent, its m_targetTag, and its detection volume in world coordinates (the chunk position and
  the entity transform; QueryShapeAabb, a box, a sphere, a cylinder, a capsule; with no query shape the shape component of the entity). 849
  volumes with 790 tags in the supported build; 657 of the 700 trigger tags of the tasks have a volume (each of the 10 of the beach chain).
- The cutscenes [live A1 of stage 1.8a, C1 and I1 of stage 1.8b; research conversations.md]: the client runs the AchievementEvents script of
  a cutscene spawner with the bits of its player (`Start_IGC_X && !Watched_IGC_X`) and sends 6703 with the CRC-32 of the cutscene name. The
  server grants it (6707 with the CRC) only when an Active TaskCutsceneComplete of the character has that name: it is the pending cutscene of
  the character then. 6706 with the CRC marks it started; the 6705 after it completes the tasks of the cutscene (and the session answers 6709
  as for each 6705). A 6703 that no task waits for gets no answer; a 6705 with no started pending cutscene (the end of a conversation, a
  mismatched grant) changes no quest [agent; the client sends 6705 also after a mismatched grant]. Live I1: the start quest of a new
  character plays the beach cutscene by itself at the world entry; the Grace cutscene of 06_survivors starts at its trigger area.
- The cinematics (6606 started, 6542 ended, the CRC-32 of the name) [research quest-engine.md Q3; no live check]: 6606 counts for
  TaskCinematicStart and is the pending cinematic of the character; a 6542 counts for TaskCinematicComplete only for the pending cinematic.
- TaskTriggerGameEvent [client 0x8260AE0; data GameEventData, WhisperData]: the activation applies the game event of KillEnemyType (one
  time) and completes: its whisper (the WhisperId or WhisperID of the event, else the whisper whose GameEvent names it) goes to the client of
  the character as 5070 with the CRC-32 of the lower-case WhisperId, and the AchievementId of the whisper is set; the AchievementId, the
  experience, the coin, and the item of the event too. Live W1 and W2 of stage 1.8b: the client plays the whisper and shows its subtitles.
- The effects of a topic [data ConversationTopicData; live check of stage 1.8b]: the AchievementUnlock of a topic and the TaskConversationTopic
  tasks of the person (ItemName) and the topic (UIScreenTag) come at the finish of the topic (5397 of the close), one time in a conversation.
  The live check: the client sends 3158 at the selection and 5397 at the close; the bit of 01_Grace_Goodbye_WW at 3158 changes nothing in
  the open conversation (its next topic is in no state); the person of the topic task of 06_demonstorm is a walkaway slice whose next task
  hides it, and with the finish at the close Grace walks away after the conversation [agent from the live check]. TaskConversationState
  (ItemName the person, UIScreenTag the state) counts at the selection of the state at the start of a use.

### The full reward join, the availability, the timer, and the repeat (stage 1.8d)

These rules replace the refusals of the list of stage 1.8b for TerritoryStanding, GearScoreRange, and the loot container items. The
research is `~/Games/soulwarden/work/research/scratch/quests-18d/` (rules Q1 to Q9, unknowns U1 to U9).

- The reward join is one function, `Quests::reward_join` [agent: one reader for the hand-in and for the command `quest_rewards`]. It reads
  the success event (UniversalExpAmount, CurrencyReward with CoinRewardChance, ItemReward with ItemRewardQty and GearScoreRange,
  AchievementId, TerritoryStanding) and the row (ExperienceReward, ItemRewardName with ItemRewardQty, AchievementId, ObjectiveReward)
  [data; research quest-engine.md Q6]. The hand-in pays all of it in one transaction (the plan of the quest, the inventory, the wallet, the
  progress, the skills, the achievement rows, the repeat row), or nothing.
- The rewards that stay refused (no owner in the backend): FactionReputation, FactionTokens, CategoricalProgressionReward, Azoth,
  UmbralCurrency, PvpXp, SeasonsXp, AzothSalt, CursedSilvers, CursedCrowns that is not 0, an EntitlementId, a StatusEffectId, a
  ProgressionReward of the row, a GearScoreRange or a TerritoryStanding that the reader cannot read ("#REF!"), an item that the catalog does
  not have, a negative quantity, a next objective that the quest data does not have [brief of stage 1.8d; roadmap 4.5 for the factions].
  The refusal names the reward (`tools/world quest rewards`).
- The items: ItemReward and ItemRewardName have the rules of a new instance of stage 1.3b (`Rewards::grant_items`: GearScoreOverride of the
  item table, the fixed perks, the free perk places with the level of the character and ForceRarity of the layout); the rolled perks go into
  the instance (finding QR8B-R1) [client; agent]. The archetype variants of 06_boarfeast pay their own item (the item of the archetype of the
  character) [data].
- GearScoreRange of the event [client parser 0x31E43A0: the text splits at "-"; one number is both ends]: the gear score of the item of the
  event, a uniform roll of the world generator in the range [agent: the three rows of the data have one value].
- A loot container item (the class has `LootContainer`: the Meat Pouch `QuestReward_MB_Meat_Pouch` of 06_skinningknife) is a normal reward
  item of the bag. The loot data must have its table (member `containerRoots` of `loot-tables.json`), else the hand-in is refused: a
  container that cannot open would lose its reward [brief of stage 1.8d]. The open is in [Items and equipment](items-and-equipment.md).
- TerritoryStanding goes to the origin territory of the instance (the territory of the grant: 6 for the beach chain) [decision D3 option B
  of the orchestrator, 2026-10-18: a rule of this project; the recipient territory of the data is not known, research U3]. The standing is
  an entry of the categorical progression of the territory (`Skills::add_standing`, [Progression and skills](progression-and-skills.md)).
  An instance with no origin territory refuses it. A gain that reaches the next rank is refused (the token of the rank-up has no owner yet:
  the territory pool of ProgressionPoint) [agent]. The beach chain has one standing reward (120 of `06_raisealarm`), below rank 1 at 150
  [data].
- The completion achievement of the row and of the event are set in the same plan [data].
- The availability (`Quests::available`) has the checks of the client in their order [client 0x37AE910 at the person, eligibility
  0x3A4BFE0; research quests-18d rules Q2]: no active instance; no other active objective with the same completion achievement
  (HasObjectiveWithCompletionAchievement 0x3A47310: the variants); the completion achievement (IsObjectiveComplete 0x3A48A30): a repeat on
  its cooldown is `onCooldown`, else `completedBefore`; a gate whose service rule the backend does not have (RequiredGameModeId,
  QuestArcTag, RequiredWorldTags, a required categorical progression) is false [agent: an unknown rule is false, as an unknown achievement
  is, research U7]; RequiredLevel against the level of the screen [client 0x3A4C13E]; RequiredAchievementId as a conditional (with `!`,
  `&&`, `||`, and the NPA rows) [client]; RequiredFaction as a mask [client parser 0x445CA30: blank or None 0xF, Any 0xE, Syndicate 0x2,
  Marauder 0x4, Covenant 0x8; a leading `!` gives the complement in the four bits; bit 0 bypasses the test at 0x3A4C166]. The backend has
  no faction of a character yet (roadmap 4.5): each character is unaligned (faction 0) [agent].
- The client shows no locked offer: it hides an offer that fails a check [client; live I2 of stage 1.8d: Urda shows no offer before the
  hand-in of 06_skinningknife and the offer of 06_boarfeast after it]. The person system offers only what `available` permits.
- PushObjectiveOnReqMet (21 rows) is not applied [agent: the push of `01_SoleSurvivor2` would grant the old start of 134 tasks, and the
  release status of these rows is not known; decision of the user]. SkipReqAchievmentWhenInvalid and
  ShouldResetInProgressQuestOnScheduleEnd are read and not applied (no row of the beach chain has them).
- TaskTimer: the timer of the server, its fraction, and its end action are the rules of stage 1.8a. Stage 1.8d adds: a timer keeps its start
  across a reconnect (the start is in the task row), and the end of a failure timer fails its objective with the authored action (case QD4)
  [client 0x3B2BA40]. The countdown row of the HUD is open: the client publishes `MissionTimers.<instance>.TargetTime` at the bind of the
  task (0x3B2FD90) before its activation (0x3B2D250), and shows a red 00:00:00 (live I3 of stage 1.8d). No server field that changes it is
  known (research quests-18d U5). Decision D4 of the user (the time away from the world) stays open: the time away counts.
- The repeat [data ScheduleData; client reset defaults 0x616C670, research quests-18d rules Q4]: an objective with Repeatable and a
  ScheduleId of the data repeats. The schedule kinds: hourly (`*-*-*T*:00:00`, no EndDate), daily (`*-*-*TDailyCooldown`), weekly
  (StartWeekday `WeeklyCooldown`, no StartDate). The cycles start at the shipped defaults of the client: daily 05:00 UTC, weekly Tuesday
  05:00 UTC, hourly at the full hour [client 0x6181D4B to 0x6181F87; agent: no region offset]. A completion at the start of a cycle belongs
  to that cycle. A schedule with a window (StartDate and EndDate, a season) or with no row has no repeat [decision D4 of the user is open].
- The hand-in of a repeat writes a repeat row (`character_quest_repeats`: the number of completions, the time of the completion, the start
  of the next cycle) in its transaction. The completion achievement stays set to the start of the next cycle: the person answers
  OnCooldown (8) then [client 0x37AE910]. At the start of the cycle one deadline of the world clock (`Quests::repeat_due`) removes the
  achievement in one plan and keeps the row with no deadline (the generation). The entry of a character schedules its deadlines from its
  rows; a deadline in the past runs at the entry (the time away counts, as for a timer) [agent].
- The cost: one deadline of the world clock for each repeat with a deadline; a row with no deadline has no work; the availability reads the
  active list of one character (case QD8: 2 deadlines for 2,402 rows, one due entry, 200 steps after a day in less than 2 s) [project rule
  MP-8].
- A quest whose root is satisfied waits for its hand-in, and its StayActive tasks do not regress (case QD11) [data; agent; live F of
  stage 1.8d]. The data: task 3 of `06_crewmates` (leave the watchtower) and task 6 (go to the shipwreck) are StayActive places, and the
  hand-in person (Aelstrom 03) stands in the watchtower camp; 214 objectives with a hand-in person have a StayActive place task, and none
  has a StayActive TaskHaveItems. The client function of the regression (0x3ADDEC0) has no check of the objective. Live F: before this
  rule the walk back from the shipwreck took the root of `06_crewmates_soldier` back to Active, and the HUD showed the shipwreck task
  again. A quest that is not ready regresses as before (QR8A-R3, cases QRV3 and QE2).
- Each change has two sessions in its cases (QD7: two characters with different schedules, balances, and standings; MP-10).

### The destructible quest objects, the entry of an area, the Azoth reward, and the cinematic (stage 1.8e)

The research is `~/Games/soulwarden/work/research/scratch/quests-18e/` (`destructible/`, `cinematic/`, `timer/`: static reads of the
client), the live checks are in `~/Games/soulwarden/work/evidence/quests/18e/implement/RESULT.md`.

- A destructible quest object is a placement whose slice has the variation table `Gatherable_Quest_Damageable` [data]: the four corruption
  nodules of `06_survivors` (row `MSQ_Grace_CorruptedNodule_1`, game event `MSQ_CorruptedNodule_1_interact`, task 43 with TargetQty 4) and
  the monolith of `06_demonstorm2` (`MSQ_Beach_Damned_Monolith_interact`), 169 placements and 69 gatherable rows in this build. The tool
  `tools/gamedata/quest-objects.sh` makes `quest-objects.json` (game data, [Game data](../development/game-data.md)). Without the file no
  object can be destroyed, and the rest of the backend works.
- The client makes the object from its level data. Its slice has a DamageReceiver and no HitVolume, so the client puts no melee contact on
  it into its input report [client: the contact collector 0x485FBA0 appends only an entity with a HitVolume, 0x4861DFA], and the key E has
  no option. The health bar of its marker is a fallback of the Lua code (`HealthPercent` 1) [client]. Thus the server finds the hit itself
  (`QuestObjects::strike`): each strike window of the attack graph that the combat system follows (a basic attack or an ability:
  `AttackSequences` sends a strike event for each visit, `Combat::run_event`) hits each object of the character that is 5.0 m or less from
  its exact position (3D: the reach of a melee weapon and its tolerance) and 90 degrees or less from its look [agent: the rule of a
  creature hit]. An object with InteractOnlyOnQuest takes a hit only while an active task of the character has its InteractTag [client
  0x8261FC0: the client shows the object only then].
- Stage 1.8f (the code review of stage 1.8e): only a strike entry with a usable row for the character is a strike (`Combat::strike_objects`:
  the learned-node conditions of `Combat::row_for`, the rule of the hit path) [data: `GreatAxe/Attack_Heavy` has two strike entries at one
  time, one forbids and one needs `Passive_Greataxe_Mauler_HeavyAddPull`]. A strike while a cutscene that the server granted plays (6706) or
  a cinematic plays (6606) hits nothing [agent: the client takes no attack input then; a late step of an attack from before arrives]. No
  solid of the navigation data may be on the line from the eyes of the character (1.5 m, the eye height of the sight) to the near side of
  the object (`Navigation::line_stop`, the line of a projectile): the near side is the first point from the place of the object toward the
  character, in steps of 0.25 m, that no solid holds, at most 2.0 m [agent; probe of stage 1.8f: the solid of a beach nodule of the data
  reaches about 1.1 m from its place]. A tile of the line that the world does not have yet: no hit now (the tile comes; the next strike
  tries again).
- The work of an object is BaseGatherTime of its row, rounded up, at least 1: one strike is one unit (the nodule 4 strikes, the monolith 1)
  [agent: the data has no health of the object]. One strike hits an object one time (the visit and the strike index).
- At the last unit the quest system writes the quest drops of the row, the game event of the row (TaskGameEvent), and its InteractTag
  (TaskInteract) in one plan and one transaction (`Quests::object_destroyed`; stage 1.8f). Only after the commit is the object destroyed for
  the character: the LootTracker entry of the object (InstancedLootType 0, state Looted 2, the managed actor of the placement, its
  position) [live I0: the client removes the object, its marker, and its corruption; the GatherableController state of type 12 has no
  effect for an instanced object, apply 0x3371590]. A refusal (a full bag, a refused write) keeps the object with its last unit of work, no
  LootTracker entry, and no retry: the next strike tries again. After a uniform draw of the respawn bounds of the row (the nodule 33 s, the
  monolith 200 to 300 s) the entry goes, and the object is back for the character [data; live I0: a map with no entry restores it].
- The loader refuses a file with two objects of one placement key or of one actor (the key of the LootTracker entry; the rule of the chest
  loader) [stage 1.8f; the common export has neither].
- The state is a value of the character (`Entity::questObjects`; MP-1): each character has its own objects (InstancedLootType 4 of the
  data), and the LootTracker state goes only to its own sessions (owner only, [Chests](chests.md)). The store has no row of it: at a new
  world entry the objects are back, and the quest rows keep the progress [agent].
- The cost: the strikes and the objects near the character (a grid of 64 m), not the size of the world [MP-8].
- TaskTriggerArea with TriggerAreaType Enter stays satisfied after the character leaves the volume; only Stay Inside regresses [data: 778
  Enter tasks, 3 Stay Inside; live M1: the monolith of `06_demonstorm2` stands outside the cave volume of the Enter task before it, and the
  regression took the quest back before the strike]. Case QF3.
- AzothReward of a success event goes into the Azoth balance (the categorical progression `Azoth_Currency` of stage 3.7c) when
  AzothRewardChance is 1 [data: 672 events with an amount have the chance 1; research quest-engine-3.md Q4 names the Azoth balance as the
  owner]. The gain stops at the cap of the crafting data (2,500; AllowOverflow 0, the rule of `Skills::add_balance`). A blank or another
  chance stays a reward with no owner [agent: the 4 mission events with a blank cell; the default of the loader 0x31E70B0 for this column is
  not read]. With no cap in the crafting data the hand-in is refused. The skills plan of the hand-in has the standing and the Azoth: one
  write. Case QF7.
- The cinematic `TempestRising` of `06_raisealarm` [client, static read `research/scratch/quests-18e/cinematic/ANSWERS.md`]: the lead-in
  cutscene timeline starts it (layer `Trigger_Bink_Video` at 9.7 s); the client plays a Bink 2 file and sends 6606 at the start and 6542 at
  the end (or at a skip: a hold of Escape for 2.25 s). The play function returns before it opens the video when `sys_audio_disable` is
  above 0 (0x6506CB0). A development run has no audio (a rule of the project): its client shows a white screen with the subtitles, never
  ends the video, and sends neither message (live C1). The development command `tools/world quest cinematic CHARACTER ID start|end` gives
  the quest system the same notice as the two messages (`Quests::cinematic`); it does not close the screen of the client. The game of the
  user has audio; its play of the video has no live check in this stage (open). Live F of stage 1.8e: the statue cutscene `1dc4b4f7`
  ended (6705) after 9.8 s, then the white screen with the subtitles stayed; the development command completed task 22, and the chain went
  on to the Corinth cutscene `126b8567` and the hand-in at Marcel Dupont (800 experience, 6.00 coin, the amulet, 120 standing of
  Windsward, `S_introtosettlement`).
- Closed in stage 1.8f: after the hand-in of `06_raisealarm` the client offers `06A_MSQ_01_Quartermaster` at Marcel Dupont. The server
  selected `06_01b_MarcelDupont` (no requirement) and refused the 3772. The client skips that state: a state record is valid only as
  IsInitialState XOR a requirement ([Persons](persons.md), the conversation state). The server now selects `06_01_MarcelDupont` and accepts
  the offer (live Q1 of stage 1.8f). Its hand-in is a later point: the success event pays a standing of 100, the row has no
  ExclusiveTerritory, and the instance of an offer has no origin territory (the manifest: "the standing 100 of
  06A_MSQ_01_Quartermaster_success has no territory").
- The countdown row of a timer task (open since stage 1.8d) has a static read in this stage (`research/scratch/quests-18e/timer/`): a new
  TaskTimer of the client has the deadline 0; the objective initialization (0x3A551C0) binds each timer of the tree (0x3B2FD90) before the
  activation of the root, and the bind publishes a copy of that zero deadline to `MissionTimers.<instance>.TargetTime`; the activation and
  the tick do not publish again; the task state, the parent transitions, a resend of the active list, 5684, and the pin and journal
  readers do not bind again; all timers of one objective share the node. The Lua code shows the row when the node exists (also for a
  hidden timer: `06_skinningknife_03_swapDelay`). The UI data build 0x3AD7BE0 reads the TimeLimit of the task (+0x1F4), not its Duration.
  No server field or message that makes the row count is known [client, inference of the read; no live trace]. The row stays open, with
  the trace checks A and B of the read for a later run.

### The support manifest (stage 1.8e)

The support manifest says for each row of the quest data whether the backend runs it, and for each start history what a character can
complete. `QuestSupport` (`world/quest_support.h`) reads the code contracts of the quest system (the task types with an evaluator,
`Quests::available`, `Quests::conditional`, `Quests::reward_join`, `Persons::present`, the state selection of a person) and the indexes
of the game data (the creatures of the spawn points and their vitals categories, the gatherables, the quest objects, the quest drops, the
loot tables, the recipes, the points of interest, the trigger volumes, the persons with a slot, the lore pages).

- Each row has its kind (`objective`, `missionTemplate` apart, `communityGoal`, `developer` for the folders `74_devworld_*`), its grant
  sources (a territory start, a template, an offer of a person with its state, ObjectiveReward, a child objective, PushObjectiveOnReqMet,
  a mission board) with the kind that the backend runs, its gates (a service rule that the backend does not have, a faction for a
  character with no faction: bit 0 of the mask, a level above the cap), each task with its supplier or the exact reason of its block, its
  effects, the world content that it uses, its rewards (the reward join with an empty history, the repeat of the row), and the reasons of
  its block. A row is supported when it is an objective, its tree has a completion path, its rewards are payable, a source of a kind
  that the backend runs grants it, and it has no gate.
- The histories (`QuestSupport::default_profiles`): a new character of each archetype (its archetype achievement, the start of Windsward,
  the level cap; the Soldier first, the default of the creation), a level 60 character with no template (decision D2 option A), and each
  template of the backstory data (decision D2 option C: its achievements and its start objectives, level 60) [the decision D2 of the user is
  open: the manifest counts each option and decides nothing]. A history plays rounds: a start and an ObjectiveReward are direct grants; an
  offer needs a present person (`Persons::present`), its selected state (the states in reverse, the first valid record whose requirements
  pass: stage 1.8f), and the availability; the tree runs with the achievement actions, the achievement waits, the lore pages, and the
  child objectives; a hand-in pays the standing on the skill set of the history (a rank-up refuses it).
- Stage 1.8f (finding QR8E-R6): the history hands in an objective only at its destination: its NpcDestinationId has a resolved person
  slot that is there for the character with the bits after the tasks (`Persons::present`), the rule of `Persons::hand_in`. An objective
  with no NpcDestinationId has no hand-in (the server compares the destination with the person of the conversation). A child objective
  whose hand-in cannot happen still completes its task of the parent (TaskObjective: the root of the child), but the history does not
  count it and gets none of its rewards.
- Stage 1.8f (finding QR8E-R5): the items. An item of a task has a source of the world in each history when a loot root of a source of the
  world reaches it (the vitals table of a creature of a spawn point that can drop loot, the root of an available chest, the loot root of
  a gatherable node of the world; the tables and the buckets that it reaches), or the loot table of a loot container item of such items,
  or a recipe that the backend crafts whose ingredients such items give (a fixed point: a cycle with no first item gives nothing). The
  loot tags and the conditions of an entry are not tested, and a currency ingredient passes [agent]. An item that only a quest gives (a
  reward, a give or the game event of a task, a quest drop) is the member `questItem` of its task: the history has it after the hand-in
  of a reward (the reward items of the join), and a give, the item of the game event of a task, or a quest drop only while the tree of its
  objective (or of its parent) runs [agent: a return task takes such an item at the hand-in]. A new character starts with the items of
  its creation (`start_items` of its archetype).
- The counts of this build (game data of 2026-10-22, level cap 70; stage 1.8f): 2,147 rows (1,027 objectives, 1,031 mission templates, 12
  community goals, 77 developer rows); 160 supported objectives. A new character of each archetype completes 15 objectives (the Soldier:
  `06_getoffbeach`, `06_skinningknife`, `06_boarfeast_soldier`, `06_crewmates_soldier`, `06_survivors_soldier`, `06_pillageship_soldier`,
  `06_recoveritems_soldier`, `06_demonstorm`, `06_demonstorm2_soldier`, `06_raisealarm`, `0625_0101_truffles`,
  `9932_0104_RelicReclamation`, `FactionIntro_Marauders_01`, `FactionIntro_Marauders_02`, `FactionIntro_Marauders_Vector`). A level 60
  character with no template completes 4. The 298 template histories in 23 groups (the first start objective of the template) complete 1
  to 7 (the group `09A_M9_MeetShirzad`: 7; `09A_M15_FightArtemis`: 6; 158 templates with no start objective: 1 to 4). The first reasons of
  the 867 objectives that are not supported: 185 a point of interest with no shape, 158 a standing with no territory or a rank-up, 154 an
  item with no source, 71 no producer of a game event, 65 no creature of the categories, 40 TaskGameMode, 35 TaskQuickCourse, 33 no grant
  source that the backend runs, 20 TaskHaveLevel, 18 no object of the interact tag, and 25 smaller groups. The changes of stage 1.8f: the
  hand-in rule removes `09A_S4_BlowUpFarm` (no NpcDestinationId) from 3 template histories (8 to 7, 7 to 6, 8 to 7); the item rule
  changes no count (of the 5 supported rows whose item only a quest gives, the 2 played ones have a give of their own tree, and the 3
  others have no grant source in the histories; the items of a table that no source reaches block 40 tasks of rows that other reasons
  block too); the state rule now grants `06A_MSQ_01_Quartermaster` from Marcel Dupont,
  and its hand-in stops at its standing (no origin territory). No repeatable objective is supported: the 40 repeatable Journey rows pay
  Azoth and standing, and the nearest ones (`C10A_S5R_LightningBottle`, `C10A_S7R_AnimalInstincts`) are blocked only by the rank-up of
  their standing (450 points against the 150 of rank 0).
- A second computation by a different path (`scripts/support_check.py` of the evidence of stage 1.8f: its own reader of the game data
  files, its own reward check, grant index, gates, trees, person presence and state selection with the validity of a record, the hand-in
  at the destination, the items of a quest in the history, and history; only the facts of the leaf tasks come from the manifest, with the
  member `questItem`) gives the same counts: the same 2,147 kinds, the same 160 supported rows, the same played list for each of the 306
  histories, and the same 25 groups. On the manifest of stage 1.8e it finds the 3 histories of `09A_S4_BlowUpFarm`. In stage 1.8e it found
  one error of the first manifest: the faction gate tested bit 1 (Syndicate) as the bit of a character with no faction (corrected; case
  QF4).
- The manifest is deterministic: two manifests of one world are the same bytes (case QF4). It is a reader: no change of the world.
- The file stays outside the repository (it names the rows of the game data). `tools/world quest support FILE`,
  `GET /v1/dev/quests/support`, `GET /admin/v1/catalog/quest-support` (about 6 MB and 2 s for the whole quest data).

### The corrections of the code review of stage 2.5c (in stage 1.8b)

The findings PR5C-R1 to PR5C-R7 (`~/Games/soulwarden/work/evidence/persons/25c/code-review/FINDINGS.md`) are corrected in stage 1.8b; each has
a case of `tests/review_persons_25c_tests.cpp` (QRP1 to QRP7) that fails on the tree of stage 2.5c and passes after the correction. The
quest rules: PR5C-R4 (the stored parent of a child after the reset of its parent: `Plan::grant_child` adds the child to the rows of the
plan), PR5C-R5 (a retry credits only the tasks of its first delivery: `QuestEvent::matched`), PR5C-R7 (a malformed expression is false).
The person rules are in [Persons](persons.md).

### The corrections of the code review of stage 1.8c

The findings QR8C-R1 to QR8C-R14 of the code review of stage 1.8c (`~/Games/soulwarden/work/evidence/quests/18c/code-review/FINDINGS.md`)
are corrected in stage 2.5c. Each has a case of `tests/review_quests_18c_tests.cpp` that fails on the tree of stage 1.8c and passes after
the correction. The source of each rule is the finding and the client rule that it names [agent through the review].

- QR8C-R1 (case QRW1): a result id counts as handled only after the commit of its plan (or when the event changes no task). An event whose
  plan the store refused is tried again by the world after 2 s, 5 times at most (`kEventRetry`, `kEventRetries`; the time of the retry of
  a timer, finding QR8A-R5) [agent: the source action committed; a store that refuses for 10 s has a fault that the log shows]. A second
  delivery of the same result before the retry counts one time.
- QR8C-R2 (case QRW2): the item actions of one plan (a give, a remove, a return) apply in the order of the tasks
  (`Inventory::plan_quest_items` with a list of `QuestItemAction`): a remove after a give of the same plan takes the given item.
- QR8C-R3 (case QRW3): a reset of a child objective keeps the link to its parent; a reset of a parent gives the active child to the new
  parent instance (`Plan::grant_child`), so the completion of the child completes the task of the new parent.
- QR8C-R4 (case QRW4): a return takes the counted items from the bag, the quick slots, and the other paperdoll slots: an equipped item
  that a possession task counted goes at the return.
- QR8C-R5 (case QRW5): an abandon settles the achievement waits of the other quests of the character after its undo rows (a StayActive
  TaskAchievement whose bit the undo removed is Active again).
- QR8C-R6 (case QRW6): EnforceLocationRequirement applies to the possession and equip predicates too (the index has the key "place" for
  them, and a move evaluates them again).
- QR8C-R7 (case QRW7): an exit task is satisfied only when the POI data has a shape of its tag and the character is outside each shape
  (`PoiData::knows`): a tag with no shape is no proof of an exit, and the task waits.
- QR8C-R8, R12, R13 (cases QRW8, QRW12, QRW13): see the rule of TaskInteract above.
- QR8C-R9 (case QRW9): a kill drop that the bag lost (a full bag with no loot bag data) writes no award row and keeps its misses: the next
  kill can drop it again (`delivered_drops` of the rewards).
- QR8C-R10 (case QRW10): see the rule of a craft above (`Inventory::craft_gear_score`).
- QR8C-R11 (case QRW11): the instance of a subscriber is found by its key (`Quests::instance_of` with the positions of the index), a commit
  that adds and removes no instance replaces its copies at their places, and the index is built again only for the changed instances. One
  event with 31 active quests: 1 subscriber, 0 visits of the quest list, 3 lookups by key (the counts `questVisits` and
  `instanceLookups`).
- QR8C-R14 (case QRW14): the index of the new instances is built before the inventory marks its change of the items: a give of a new
  quest reaches its new possession task in the same tick.

### The corrections of the code review of stage 1.8b (in stage 1.8d)

The findings QR8B-R1 to QR8B-R12 (`~/Games/soulwarden/work/evidence/quests/18b/code-review/FINDINGS.md`) are corrected in stage 1.8d.
Each has a case of `tests/review_quests_18b_tests.cpp` (QRB1 to QRB12; QRBC is a control) that fails on the tree of stage 1.8b and passes
after the correction [agent through the review].

- QR8B-R1 (case QRB1): the perks that `Rewards::grant_items` rolls for a reward item go into the instance (`Inventory::plan_quest_items`).
- QR8B-R2 (case QRB2): a hand-in needs the target count of each satisfied return task in the bag now (`missingReturnItems`); the
  completion takes the items [research quest-engine.md Q3].
- QR8B-R3, R4, R5, R6 (cases QRB3 to QRB6): the topic rules of 5397 are in [Persons](persons.md).
- QR8B-R7 (case QRB7): a cutscene end completes only the tasks that were Active at the grant of the cutscene (`QuestLog::cutsceneTasks`):
  an old end does not complete the tasks of a new instance.
- QR8B-R8 (case QRB8): a second 6703 for the pending cutscene, while its tasks live, keeps the grant and its start; a pending grant whose
  tasks ended gets a new grant.
- QR8B-R9 (case QRB9): an ObjectiveReward that the quest data does not have refuses the hand-in.
- QR8B-R10 (case QRB10): a negative ItemRewardQty of the event refuses the hand-in (the event and the row use one rule).
- QR8B-R11 (case QRB11): a TaskTriggerGameEvent whose item did not fit waits under the key "items"; the next change of the bag runs it
  when the item fits.
- QR8B-R12 (case QRB12): the trigger loader refuses a volume whose axes are not unit length or not orthogonal (tolerance 0.001; the
  real file has an error below 0.000001).

### The corrections of the code review of stage 1.8d (in stage 1.8e)

The findings QR8D-R1 to QR8D-R7 (`~/Games/soulwarden/work/evidence/quests/18d/code-review/FINDINGS.md`) are corrected in stage 1.8e. Each
of QR8D-R1 to R6 has a case of `tests/review_quests_18d_tests.cpp` (QRD1 to QRD6; QRDC, QRDW, QRDM are controls) that fails on the tree of
stage 1.8d and passes after the correction [agent through the review].

- QR8D-R1 (case QRD1): the return tasks of one item need the sum of their counts at a hand-in (`ReturnNeed`), because the completion takes
  the count of each task one after the other [research quest-engine.md Q3].
- QR8D-R2 (case QRD2): the open of an item container rolls as a loot container (the numeric container tags and the container floor
  ContainerLootGSFloor for a table with UseLevelGS), not as a creature (AILootGSCeiling) [data: the loot root of a container].
- QR8D-R3 (case QRD3): a repeatable objective with a ScheduleId writes its repeat row and its deadline also with no completion achievement
  (the data has such rows), and the offer is on its cooldown while the row has a deadline [data; the open parts of decision D4 are not
  decided].
- QR8D-R4 (case QRD4): a refine that reaches a craft task (TaskCraftRecipe, not TaskRefineRecipe) keeps the smallest gear score of the task.
- QR8D-R5 (case QRD5): an open whose roll gives a loot container that cannot open (no loot table) is refused before the take
  (`OpenError::innerNotOpenable`): the outer container stays.
- QR8D-R6 (case QRD6): GearScoreRange and TerritoryStanding are whole numbers of the whole cell (`whole_number`): a cell with text after
  its number is a reward that the backend cannot pay.
- QR8D-R7: case QD7 reads the standing bytes of the two sessions after the steps of the host (the host sends a changed state only at its
  steps). A tree whose categorical state goes to an observer fails QD7 (the mutation probe of stage 1.8e).

### The corrections of the code review of stage 1.8e (in stage 1.8f)

The findings QR8E-R1 to QR8E-R9 (`~/Games/soulwarden/work/evidence/quests/18e/code-review/FINDINGS.md`) are corrected in stage 1.8f. Each
of QR8E-R1 to R8 has a case of `tests/review_quests_18e_tests.cpp` (QRE1 to QRE8) that fails on the tree of stage 1.8e (QRE8 there does
not end) and passes after the correction; R9 is a correction of case QF1 [agent through the review; the rules above].

- QR8E-R1 (case QRE1): one swing gives one unit of work for each strike entry with a usable row for the character.
- QR8E-R2 (case QRE2): a solid between the character and the near side of the object stops the strike; a solid behind the object, and the
  solid of the object itself, do not; a tile that is not there yet gives no hit.
- QR8E-R3 (case QRE3, two sessions): late attack steps while a granted cutscene plays strike no object; after its end a strike does.
- QR8E-R4 (case QRE4): a refused write of the quest rows keeps the object with no LootTracker entry; the next strike works.
- QR8E-R5 (case QRE5): the history of the manifest does not take an item from the reward of the same unfinished quest; a played reward and
  a give of the tree that runs are items of the history; an item only in a loot table that no source reaches has no source.
- QR8E-R6 (case QRE6): the history hands in only at a destination person that is there for the character after the tasks.
- QR8E-R7 (case QRE7): a quest object file with two objects of one actor or of one placement key is refused.
- QR8E-R8 (case QRE8): the manifest ends for a tree with the task index 255 (the node counters are `std::size_t`).
- QR8E-R9: case QF1 counts each LootTracker fragment of B in the window, of each interest; a tree that sends the bytes of the observer
  record of A to B fails QF1 (the mutation of the review: `logs/r9-leak-mutation.log` of the evidence of stage 1.8f).

## Commands

```sh
tools/gamedata/quests.sh [OUTPUT.json]          # the quest data (a test: a path outside the common game data folder)
tools/gamedata/quests.sh --objective 06_getoffbeach   # print a tree (no file)
tools/world quests CHARACTER                    # the quests, their tasks, the pins, the one-time grants, the counts
tools/world quest templates                     # the backstory templates and their start objectives
tools/world quest template CHARACTER TEMPLATE   # decision D2 option C (the command apply_backstory)
tools/world quest rewards CHARACTER OBJECTIVE [TERRITORY]   # stage 1.8d: what a hand-in pays (or the reward that refuses it), and the offer
tools/world quest grant CHARACTER OBJECTIVE     # development only: a new instance of an objective
tools/world quest abandon CHARACTER INSTANCE    # stage 1.8c: the rule of message 3836 (CanBeAbandoned 0 refuses it)
tools/world quest reset CHARACTER INSTANCE      # stage 1.8c: the rule of message 5600 (a new instance; the award rows stay)
tools/gamedata/pois.sh [OUTPUT.json]            # the points of interest (a test: a path outside the common game data folder)
tools/gamedata/triggers.sh [OUTPUT.json]        # stage 1.8b: the trigger volumes (a test: a path outside the common game data folder)
tools/world achievement CHARACTER ID on|off     # development only: the change event, the lock of a last bit
tools/gamedata/quest-objects.sh [OUTPUT.json]   # stage 1.8e: the destructible quest objects (a test: a path outside the common folder)
tools/world quest cinematic CHARACTER ID start|end   # stage 1.8e, development only: the notice of 6606 and 6542 (no audio, no video)
tools/world quest support FILE                  # stage 1.8e: the support manifest into FILE (outside the repository)
```

The admin interface has the route `GET /admin/v1/catalog/quest-support` (stage 1.8e: the support manifest), the commands `apply_backstory` (argument `template`), `quest_rewards` (arguments `objective` and `territory`; stage 1.8d), `abandon_quest` and `reset_quest` (argument `instance`, 16
hexadecimal digits), the route `GET /admin/v1/catalog/backstories`, and the member `quests` of the state of a character (stage 1.8c: with
the award rows and the counts of the events; stage 1.8b: the counts of the offers, the hand-ins, the cutscenes, the whispers, the trigger
volumes, and the topic effects). The tool window has no control for them in this stage. An offer and a hand-in have no command: they come
from the client at a person (a development run: `tools/world quest grant` gives a quest, `tools/world achievement` a bit).

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
  earlier child the count can stay above the number of the children before it (case QE2: 3 of 5 with the second child Active).
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
- Stage 1.8b: the dialogue of a ready quest has pages (E "Next") and options (the key 1); "Complete the Quest!" sends 794, and the client
  shows the offer of the next quest of the person in the same conversation at once (live I1: the offer of 06_skinningknife after the
  hand-in). A key press that a test sends for a page can select the hand-in or the offer.
- Stage 1.8b: a refused hand-in gets no answer: the client removes the option and waits in the conversation (live I2 at Urda, the Meat Pouch
  of 06_skinningknife). Escape closes it; the quest stays ready.
- Stage 1.8b: the knife of 06_skinningknife needs the station `engineering2` or `camp1`; the camp of the watchtower is hidden for the client
  (open point of 1.8c). The live run of stage 1.8b gave the knife by command after the natural gathers of the flint and the wood.
  Stage 1.8d: the camp is not hidden. The client shows "Urda's Campfire, E Craft" at 9606.5 2367.8 when the focus is on the campfire, and
  its screen crafts the knife and the Light Ration (live I0 and F of stage 1.8d). The earlier report did not have the focus on it.
- Stage 1.8d: the client names the Meat Pouch "Essential Supplies Cache" (the bag group "Rewards"). Its open shows the reward popup of the
  client with the Weak Health Potion and 10 Travel Rations.
- Stage 1.8d: the HUD of "Basic Needs" (06_skinningknife) shows a red 00:00:00 under the tasks: the row of the hidden timer task 14
  (`swapDelay`, 25.4 s). The timer runs on the server and ends; the row stays at zero (the open point of the countdown, rule above).
- Stage 1.8d: a supply cache and the campfire of the watchtower camp are near Aelstrom and Urda, and they take the focus of E. Turn the
  camera to the person, or come from the open side, before E.
- Stage 1.8d: after the accept of an offer, the last page of the dialogue does not close with E in a development run (no sound); Escape
  closes it. A second Escape opens the bar of the main menu.
- Stage 1.8b: the character of a new creation starts in Aelstrom_IGC_Trigger: the beach cutscene starts at the first accepted position, some
  seconds after the loading screen. A capture of the world entry needs the picture loop before the client leaves the loading screen.
- Stage 1.8e: one swing of a basic attack has some strike windows; the server hits an object one time for each strike of a new visit. A
  click while a combo of the client runs can start no new visit: a test of the destructible objects sends one click, then waits (live F:
  five quick clicks gave one hit).
- Stage 1.8e: a destroyed quest object is not in the store. After a new world entry it is there again, and the quest keeps its count.
- Stage 1.8e: the corruption nodules and the monolith show only during their task (InteractOnlyOnQuest): before the task the client shows
  nothing, and the server refuses a strike (`notOnQuest`).
- Stage 1.8e: in a development run (no audio) the cinematic TempestRising shows a white screen with subtitles and does not end; a hold of
  Escape does not send 6542 [client: no video plays, so no stopped event; static read]. Use `tools/world quest cinematic CHARACTER TempestRising start`, then `end`.
- Stage 1.8e: the skinning prompt of a boar body shows only when the character stands at the body (live F: the prompt came after a walk
  into the body, and not from some meters with the focus on the body); a near bush or flint takes the prompt first.
- Stage 1.8f: the navigation data has the body of a quest object as a solid (a beach nodule: about 1.1 m from its place). A wall check to
  the place of the object finds the object itself; the strike line ends at the first free point before the object. A solid that touches
  the object with no free cell between them counts as the object.
- Stage 1.8f: a strike from a place with a rock or a slope between the eyes of the character and the object stops at that solid (probe of
  stage 1.8f: 14 of 64 lines from 2.5 m in 8 directions to the beach nodules stop). Strike from the open side. Live I1 of stage 1.8f: west
  of the first beach nodule (9674.7 2099.1) the corruption mass is a solid of the data about 0.9 m in front of the character, and the log
  has "strikes ... through a solid ... no hit"; from the southwest and the north the nodule takes its 4 strikes. A swing moves the character
  forward (two quick swings: 4.2 m): a test of a refusal travels before each swing.
- Stage 1.8f: the navigation floor under a character can be some decimeters below the place that the client reports (the southwest of the
  first beach nodule: 0.46 m). A probe with the floor of the data is not the line of a live strike; read the position of the input report.
- Stage 1.8f: an objective with no NpcDestinationId (1,206 rows of the data, among them `09A_S4_BlowUpFarm`) has no hand-in on the server:
  `Persons::hand_in` compares the destination with the person of the conversation. The client rule for such a row is not known.
