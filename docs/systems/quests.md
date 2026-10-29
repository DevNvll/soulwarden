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
| Stage QG2a: the small quest rules (the category rule, TaskHaveLevel, the salvage events, the Azoth amount, no arc gate, the push) | `LootTables::kill_categories` (`gamedata/loot_tables.h`), `Rewards::killed`; the TaskHaveLevel predicate of `Quests::Plan::predicate`, `Quests::level_changed` (called by `Progress::finish`); `gamedata/game_event.h` (`read_game_event`, `azoth_amount`); `ItemContainers::open`, `open_refusal`; `Quests::available`; `Quests::PushIndex`, `push_changed`, `pushed`; `QuestSupport` (the reasons, the location rule, the push source); `tests/quest_gap_tests.cpp` (QG2A to QG2G, but QG2D), `tests/quest_gap_support_tests.cpp` (QG2D); the second computation `~/Games/soulwarden/work/evidence/quests/qg2a/implement/scripts/support_check.py` |
| Stage QG3: the places (the area shapes, the origins of each layer and region, the causes of the tags with no shape, the trigger volumes of the capitals, the instance volumes, the place view) | `tools/gamedata/pois.py` (`origin_of`, `read_plots`, `unshaped_tags`), `tools/gamedata/triggers.py` (`Walk`), `check_pois_tool.py`, `check_triggers_tool.py`; `gamedata/poi_data.h` (`PoiData::cause`, `at`), `gamedata/trigger_data.h` (`TriggerData::elsewhere`, `at`); the reasons of `QuestSupport::evaluate`; the command `quest_places` (`server/http/quest_commands.cpp`, `tools/world quest places`); `tests/quest_places_tests.cpp` (QG3A to QG3E); the review of QG2a: `tests/review_qg2a_tests.cpp` (QGR2A1 to QGR2A5); the second computation `~/Games/soulwarden/work/evidence/quests/qg3/implement/scripts/support_check.py` |
| Stage QG4: the quest givers (the artifact grant at the equip, the item level, the causes of an NPC with no slot, the rows with no source; the corrections of the review of QG3) | `Quests::item_equipped`, `Quests::equip_grant` (`world/quests.h`), `ProgressionTables::item_level`, `InteractionData::persons_apart`, the grant `equip` and `apart_cause` of `world/quest_support.cpp`; cases QG4A to QG4D, QGR3A1 to QGR3A4 |
| Stage QG5b: the ObjectiveFlow of an NPA, the restriction ItemRequired of a gather, the categorical progression reward (the AzothStaff rank), the quest gimmicks; the corrections of the review of QG5a | `gamedata::parse_objective_flow`, `QuestNpa::flowClauses` (`gamedata/quest_data.h`), `flow_passes` (`world/quests.cpp`); `Gathering::check_gates`, `GatherRefusal::missingItem`, `Inventory::bag_count`; `QuestGameEvent::progression` (`gamedata/game_event.h`), `RewardJoin::progression`, `Skills::add_experience(Plan&)`; `tools/gamedata/gathering.py` (`task_event_form`, `Tables.task_events`), `quest_objects.py` (`destructible_form`), `interactions.py` (the slice conditional); `QuestSupport` (`rankEvents`, `gatherEventItems`, the running trees of a history); `tests/quest_flow_gap_tests.cpp` (QG5BA to QG5BD), `tests/quest_object_gap_tests.cpp` (QGR5A1, QGR5A2); the second computation `~/Games/soulwarden/work/evidence/quests/qg5b/implement/scripts/support_check.py` |
| Stage QG5c1: the stationary targets and the named creatures of the kill tasks, the kept-out cause of a kill target in the manifest, the encounter states (the wire proof for QG5c2); the corrections of the review of QG5b | [Creatures](creatures.md) (the stationary targets), protocol notes 4.62 (the encounter states), `QuestSupport` (the kill branch: `CreatureData::kept_out`; the ranked producer with an item), `Quests::refresh` (the settle of the waits), `gamedata/gathering_data.cpp` (itemCount); `tests/target_tests.cpp` (QG5C1A to QG5C1C), `tests/review_qg5b_tests.cpp` (QGR5B1 to QGR5B3); the second computation `~/Games/soulwarden/work/evidence/quests/qg5c1/implement/scripts/support_check.py` |
| Stage QG5c2: the encounters of the quests (the success event of each participant, the start object event of its gatherer), the event outcome, the pins of a linked root, the encounter rules of the manifest; the corrections of the review of QG5c1 | [Encounters](encounters.md), `Quests::event` (`QuestEventOutcome`, `QuestEvent::callerRetries`), `Quests::client_tracked`, `encounter_unlinked`, `QuestSupport` (the member kinds and the events of the taken recipes); `tests/encounter_tests.cpp` (QG5C2A to QG5C2I), `tests/review_qg5c1_tests.cpp` (QGR5C1A to QGR5C1C) |
| Stage QG5c3: the producers of the manifest as a whole (R6), the start object row gates (R5), the participant and proximity events, the quest pins of a plan (R4) | `QuestSupport::Index::gatherSources`, `Quests::commit` (the pins of a plan); `tests/review_qg5c2_tests.cpp` (QGR5C2B, QGR5C2D, QGR5C2E), `tests/encounter_pattern_tests.cpp` (QG5C3G) |
| Stage QG5c4: the alternative producers of a task (finding QG5C3R-R4), the encounter producers of the new patterns | `TaskFact::alternatives` (`world/quest_support.cpp`); `tests/review_qg5c3_tests.cpp` (QGR5C3D), `tests/encounter_sequence_tests.cpp` (QG5C4G) |
| Stage QG8: the quest item sources (a possession task drops at a quest object, a quest row with a loot table that gives nothing, the loot table of a game event, the reasons of the item sources kept apart); the corrections of the review of QG6b | `Quests::plan_drops` (`questObject`, the key "qd:"), `Quests::Plan::apply_event`, `gamedata::reward_table`, `Gathering::complete`, `QuestObjects::destroy`; `tools/gamedata/gathering.py` (`Tables.empty_loot`), `loot_tables.py` (`read_event_roots`); `QuestSupport` (`Index::questObjectRows`, `item_causes`, `itemTasksWithNoSource`); `tests/quest_source_tests.cpp` (QG8A to QG8D), `tests/review_qg6b_tests.cpp` (QG6BR1 to QG6BR7); the second computation `~/Games/soulwarden/work/evidence/quests/qg8/implement/scripts/support_check.py` |
| Stage QG5a: the quest chest drops, the proximity objects, the per-character state of a gathered quest object, the glyph locks, the new quest objects of the data tools; the corrections of the review of QG4 | `Quests::plan_chest_drops`, `roll_drop` (`world/quests.h`), `Chests::complete`; `QuestObjects::moved`, `tick`, `look` (`world/quest_objects.h`), `QuestObjectData` (member `proximity`); `Gathering::quest_object_due`, `GatherRefusal::gatheredQuestObject`, `GatheringType::azothStaff`; `tools/gamedata/gathering.py` (`QUEST_TABLES`, `quest_form`, `children`), `quest_objects.py` (`proximity`), `interactions.py` (`varied_bounds`, `quest_children`, `read_flow_npas`); `QuestSupport` (the chest source, `refusedEvents`); `tests/quest_object_gap_tests.cpp` (QG5A to QG5E), `tests/quest_event_tests.cpp` (QG5F), `tests/review_qg4_tests.cpp` (QGR4A1 to QGR4A3); the second computation `~/Games/soulwarden/work/evidence/quests/qg5a/implement/scripts/support_check.py` |
| The research | `~/Games/soulwarden/work/research/quest-engine.md`, `quest-engine-3.md`, `quest-display-18a.md`; the results of the stages: `~/Games/soulwarden/work/evidence/quests/18a/implement/RESULT.md`, `18b/implement/RESULT.md`, `18c/implement/RESULT.md`, `18d/implement/RESULT.md`, `18e/implement/RESULT.md`, `18f/implement/RESULT.md`; stage 1.8d: `research/scratch/quests-18d/`; stage 1.8e: `research/scratch/quests-18e/`; stage 1.8f: `~/Games/soulwarden/work/evidence/quests/18f/implement/static/STATE-SELECTION.md`; stage QG2a: `research/quest-gaps.md`, `research/quest-gaps-qg2b.md`, the static read of the category rule `evidence/quests/qg2a/implement/static/category/CATEGORY.md` |

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
  of the kind; stage QG2a: also the category of its spawn point and the direct parent of each, the rule of the section of stage QG2a) and
  the kill event of its vitals row (KillEventIdOverride, or KillEvent). TaskKillContribution counts a kill whose categories
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
  Stage QG3: also the capitals of each region and the slices that they and the chunks spawn at their start, and the volumes of the other
  worlds apart (the section of stage QG3): 860 volumes with 791 tags of the open world, 48 volumes of 7 instances.
- The cutscenes [live A1 of stage 1.8a, C1 and I1 of stage 1.8b; research conversations.md]: the client runs the AchievementEvents script of
  a cutscene spawner with the bits of its player (`Start_IGC_X && !Watched_IGC_X`) and sends 6703 with the CRC-32 of the cutscene name. The
  server grants it (6707 with the CRC) only when an Active TaskCutsceneComplete of the character has that name: it is the pending cutscene of
  the character then. 6706 with the CRC marks it started; the 6705 after it completes the tasks of the cutscene (and the session answers 6709
  as for each 6705). A 6703 that no task waits for gets no answer; a 6705 with no started pending cutscene (the end of a conversation, a
  mismatched grant) changes no quest [agent; the client sends 6705 also after a mismatched grant]. Live I1: the start quest of a new
  character plays the beach cutscene by itself at the world entry; the Grace cutscene of 06_survivors starts at its trigger area.
- The cinematics (6606 started, 6542 ended, the CRC-32 of the name) [research quest-engine.md Q3; no live check]: 6606 counts for
  TaskCinematicStart and is the pending cinematic of the character; a 6542 counts for TaskCinematicComplete only for the pending cinematic.
- Stage DG1 ([Instances](instances.md)): the points of interest, the area shapes, and the trigger volumes are places of the main world.
  A character in an instance is in none of them: a task of a place, a trigger task, an exit task, and the location rule of a drop or an
  event wait until the character is in the main world again; its territory is 0 [agent: the level data of an instance is a later stage].
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
- The rewards that stay refused (no owner in the backend): CategoricalProgressionReward, Azoth,
  UmbralCurrency, PvpXp, SeasonsXp, AzothSalt, CursedSilvers, CursedCrowns that is not 0, an EntitlementId, a StatusEffectId, a
  ProgressionReward of the row that is not a faction progression, a GearScoreRange or a TerritoryStanding that the reader cannot read ("#REF!"), an item that the catalog does
  not have, a negative quantity, a next objective that the quest data does not have [brief of stage 1.8d]. Stage QG7 pays FactionReputation,
  FactionTokens, and the ProgressionReward of a faction ([Factions](factions.md)); a character with no faction gets no faction reward.
  The refusal names the reward (`tools/world quest rewards`).
- The items: ItemReward and ItemRewardName have the rules of a new instance of stage 1.3b (`Rewards::grant_items`: GearScoreOverride of the
  item table, the fixed perks, the free perk places with the level of the character and ForceRarity of the layout); the rolled perks go into
  the instance (finding QR8B-R1) [client; agent]. The archetype variants of 06_boarfeast pay their own item (the item of the archetype of the
  character) [data].
- GearScoreRange of the event [client parser 0x31E43A0: the text splits at "-"; one number is both ends]: the gear score of the item of the
  event, a uniform roll of the world generator in the range [agent: the three rows of the data have one value].
- A loot container item (the class has `LootContainer`: the Meat Pouch `QuestReward_MB_Meat_Pouch` of 06_skinningknife) is a normal reward
  item of the bag. The loot data must have its table (member `containerRoots` of `loot-tables.json`), or (stage QG2a) its salvage event
  must pay only coin (the coin purses), else the hand-in is refused with the cause (`ItemContainers::open_refusal`): a container that
  cannot open would lose its reward [brief of stage 1.8d]. The open is in [Items and equipment](items-and-equipment.md).
- TerritoryStanding (stage QG1; it replaces decision D3 option B of stage 1.8d) goes to the territory of the character at the hand-in
  (`World::territory_of`: the territory bounds of the POI data at its position) [client: the grant of a game event 0x31F5400 takes the
  territory of its context and, with none, the detected territory of the player, 0x31F6D2B to 0x31F6D63; the writer of the context of an
  objective completion was not found (static read of stage QG1, Q1a), and the mission data has its own flag GrantRewardsToOriginTerritory
  (0x39C609E)]. Outside each territory, or in a territory with no standing row (Great Cleave 3, Shattered Mountain 7, First Light 9 of
  the data), it goes to the origin territory of the instance; with neither it has no owner, and the hand-in is refused [agent: the client
  grant would give nothing; a completed quest keeps its reward] (`Quests::standing_territory`). The standing is an entry of the categorical
  progression of the territory (`Skills::add_standing`, [Progression and skills](progression-and-skills.md)): a rank-up is paid (the
  ranks, a token for each rank, the experience of the ranks in the same transaction), and the GainStanding card of the territory applies.
  The experience of the hand-in has the GainXP card of the territory of the character. The reward preview of an offer and the journal
  show the standing as a number; the reward notice of the hand-in names the territory ("+100 Windsward Standing") [live QG1].
- The completion achievement of the row and of the event are set in the same plan [data].
- The availability (`Quests::available`) has the checks of the client in their order [client 0x37AE910 at the person, eligibility
  0x3A4BFE0; research quests-18d rules Q2]: no active instance; no other active objective with the same completion achievement
  (HasObjectiveWithCompletionAchievement 0x3A47310: the variants); the completion achievement (IsObjectiveComplete 0x3A48A30): a repeat on
  its cooldown is `onCooldown`, else `completedBefore`; a gate whose service rule the backend does not have (RequiredWorldTags, a required
  categorical progression) is false [agent: an unknown rule is false, as an unknown achievement is, research U7]; RequiredGameModeId (row
  +0x34) is false: it is IsInGameMode of the GameModeParticipant component (0x32BA3B0), and the open world has no active game mode [client
  0x3A4C0A3 to 0x3A4C0E6; research quest-gaps-qg2b.md section 1]; stage QG2a: QuestArcTag (row +0x20) is no gate (the eligibility 0x3A4BFE0
  does not read it); RequiredLevel against the level of the screen [client 0x3A4C13E]; RequiredAchievementId as a conditional (with `!`,
  `&&`, `||`, and the NPA rows) [client]; RequiredFaction as a mask [client parser 0x445CA30: blank or None 0xF, Any 0xE, Syndicate 0x2,
  Marauder 0x4, Covenant 0x8; a leading `!` gives the complement in the four bits; bit 0 bypasses the test at 0x3A4C166]. The faction of
  the character comes from the faction system (stage QG7, [Factions](factions.md)); a character with no faction has the faction 0.
- The client shows no locked offer: it hides an offer that fails a check [client; live I2 of stage 1.8d: Urda shows no offer before the
  hand-in of 06_skinningknife and the offer of 06_boarfeast after it]. The person system offers only what `available` permits.
- PushObjectiveOnReqMet: stage QG2a pushes 18 of the 21 rows (the rules of the section of stage QG2a; decision B of the orchestrator, the
  decision of the user is open). SkipReqAchievmentWhenInvalid and ShouldResetInProgressQuestOnScheduleEnd are read and not applied (no row
  of the beach chain has them).
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
- AzothReward of a success event goes into the Azoth balance (the categorical progression `Azoth_Currency` of stage 3.7c) [research
  quest-engine-3.md Q4 names the Azoth balance as the owner]. The gain stops at the cap of the crafting data (2,500; AllowOverflow 0, the
  rule of `Skills::add_balance`). With no cap in the crafting data the hand-in is refused. The skills plan of the hand-in has the standing
  and the Azoth: one write. Case QF7. Stage QG2a replaces the chance rule of stage 1.8e: the amount is the amount of the client, and the
  chance has no effect (the section of stage QG2a).
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
  the offer (live Q1 of stage 1.8f). Stage QG1: its hand-in at Saatvik Agrawal (Corinth, Windsward) pays the standing 100 to Windsward
  (the territory of the character), with the rank-up of 120 + 100 (live: "+100 Windsward Standing", "+1325 XP", the map "70 / 375").
- The countdown row of a timer task (open since stage 1.8d) has a static read in this stage (`research/scratch/quests-18e/timer/`): a new
  TaskTimer of the client has the deadline 0; the objective initialization (0x3A551C0) binds each timer of the tree (0x3B2FD90) before the
  activation of the root, and the bind publishes a copy of that zero deadline to `MissionTimers.<instance>.TargetTime`; the activation and
  the tick do not publish again; the task state, the parent transitions, a resend of the active list, 5684, and the pin and journal
  readers do not bind again; all timers of one objective share the node. The Lua code shows the row when the node exists (also for a
  hidden timer: `06_skinningknife_03_swapDelay`). The UI data build 0x3AD7BE0 reads the TimeLimit of the task (+0x1F4), not its Duration.
  No server field or message that makes the row count is known [client, inference of the read; no live trace]. The row stays open, with
  the trace checks A and B of the read for a later run.

### The small quest rules (stage QG2a)

The research is `~/Games/soulwarden/work/research/quest-gaps.md` (QG0) and `quest-gaps-qg2b.md` (QG2b); the static read of the category
rule (GPT-6.1-Sol, read only) is `~/Games/soulwarden/work/evidence/quests/qg2a/implement/static/category/CATEGORY.md`; the result and the
live checks are in `~/Games/soulwarden/work/evidence/quests/qg2a/implement/RESULT.md`.

- The vitals categories of a victim (`LootTables::kill_categories`, one rule for the kill credit, the quest drops, and the source index of
  the manifest) [client: the predicate of TaskKillContribution 0x3B19F40, called by the event callback 0x3B171D0; the drop helper of
  ItemDropVC 0x5C75830; data `javelindata_vitalscategories`, `GroupVitalsCategoryId`, loader 0x5BBF960, row +4]: the VitalsCategories of
  its base row (row +0xF0), the category of its spawn point (the VitalsCategoryId of the level metadata, the separate category of the
  victim, also when the base row has categories), and the direct parent of the spawn category and of each base-row category. One level: a
  parent of a parent is no match (0x3B1A081 to 0x3B1A098, 0x3B1A0DA to 0x3B1A0F3: one lookup of the category row, no second lookup). The
  kill credit also matches the id of the base vitals row (0x3B1A057); the drop helper does not. A Bronzeroot Soldier (`Dryad_Soldier_31`,
  spawn category `Dryad_Soldier_02BW04`, parent `Bronzeroot`) counts for "Destroy Bronzeroot" of `02A_Side02_02_EarthProvide` [live QG2a:
  the journal "0 / 3", then "1 / 3" after one kill]. Each parent target of the kill tasks of the data is a direct parent (49 targets).
- TaskHaveLevel [client: factory 0x4461150 case 48, constructor 0x3B01C80, activation 0x3B04F40, callback 0x3B073A0; research
  quest-gaps.md section 8]: with a blank ItemName the task is the level of the character: it is satisfied at the level of the screen
  TargetQty or more (the callback compares the level index with TargetQty - 1, unsigned: a TargetQty of 0 never). Its target is 1 (the
  constructor passes 1 to the base task and keeps TargetQty at +0x550). It looks at its activation and at each change of the level
  (`Progress::finish` calls `Quests::level_changed`; the next tick looks at the tasks of the key "level"). With an ItemName (Covenant,
  Marauder, Syndicate: 24 of the 30 tasks of the data) it is the points (ShouldCheckPoints) or the rank of a categorical progression of a
  faction (stage QG7, [Factions](factions.md): at least TargetQty; a change of the points or the rank looks at the task again).
- The salvage event of a loot container (`SalvageGameEventID` of the item row: the coin purses `QuestReward_MB_Coin_Purse2` and
  `QuestReward_WW_Coin_Purse2` name `MB_CoinPurse2_Event`, CurrencyReward 2500; `Coin_Stack_500` 50000) [data; research quest-gaps.md
  section 8]: the open pays the CurrencyReward of the event (with CoinRewardChance; a blank chance is 1) for each unit, in the transaction
  of the take, up to the wallet limit; a container with a table and an event rolls the table and pays the event. An event with another
  reward (an entitlement: a skin box; an achievement: a mount box; AzothSalt; an item, the experience, or the Azoth of an event: no path
  of the open yet; a range "5000-10000"; a loot limit) or with no row is refused, and the item stays [agent: the open path pays the coin
  only; no loot table is made]. Live QG2a: the "Bag of Coin" of the bag opens with a double click (1733): 986.02 coin before, 1,011.02
  after, one purse less. Details in [Items and equipment](items-and-equipment.md).
- One reader of the reward cells of a game event (`read_game_event`, `gamedata/game_event.h`): the quest data and the salvage events of the
  loot data. Stage QG2a: CurrencyReward must be a whole number (a text after the number is a reward with no rule, not its first number);
  LootTags and LootLimitId are rewards with no rule.
- The Azoth amount [client: the integer getter 0x5AC0ED0 truncates the float cell toward zero (cvttss2si at 0x5AC0F7B), the event loader
  keeps 16 bits (0x31E802A, row +0xC); the offer, the journal, and the hand-in screen show that amount and no chance (objectivedatahelper,
  research quest-gaps-qg2b.md section 2)]: 37.5 is 37, 12.5 is 12, a blank AzothRewardChance loads as 0. A project rule of the
  orchestrator (2026-10-28): the hand-in pays the amount that the client shows, with no roll of the chance (the payment rule of the original
  server is not known). The loader normalizes it one time (`azoth_amount`); a negative amount or a text is a reward with no rule. Live
  QG2a: "Dentistry Artistry" (`12A_Side_08_GatherFangs`, AzothReward 37.5) shows 37 in the journal and at the hand-in, and the balance goes
  from 393 to 430.
- QuestArcTag (row +0x20) is no gate [client: the eligibility 0x3A4BFE0 does not read row +0x20; the check of row +0x34 at 0x3A4C0A3 is
  RequiredGameModeId; research quest-gaps-qg2b.md section 1]. The tag groups the QuestArc meta-achievements: after a completion the client
  calls 0x38A6E20 (row +0x20 against meta row +0xDC) for their progress. The backend has no meta-achievement progress yet (a later stage).
- PushObjectiveOnReqMet (loader 0x445CA30: +0x368; Retrigger +0x369) [decision B of the orchestrator, 2026-10-28; the user can change it:
  the 18 rows of the released game]: the server grants a push row when the normal availability passes (`Quests::available`: no active
  instance, the completion achievement, RequiredLevel, RequiredAchievementId with its `_pushedonce` bits, the faction, the gates) and the
  character is in its ExclusiveTerritory when the row has one (eligibility 0x3A4C20F, 0x3A4C218). The grant has the source `push`, the
  territory of ExclusiveTerritory (none in the 18 rows), and the automatic pin. Not pushed: `01_SoleSurvivor2` (the old start: its
  condition is true for each new character) and `Quest_03189`, `Quest_03190` (seasonal rows behind `debug_1`) (`kPushExcluded`).
  - Project rules (the schedule of the original server is not known; research quest-gaps-qg2b.md section 3): the test runs when the saved
    state of the character is ready at the world entry (each push row), and at the next tick after a change of an achievement of the
    character (each change of the bits: a completion, an action, an undo, a command) or of its level. Retrigger is a grant of one time: no
    other rule.
  - The `_pushedonce` bits (the ten Dunwood rows) are normal achievement actions of the task tree: the first task unlocks the bit at the
    activation (StayActive 0: an undo row), and the abandon and the reset undo it. After an abandon the row is available again, and the
    change of the bit grants it again at the next tick (a project rule that follows from the rules above).
  - The cost (MP-8): an index of the push rows by their dependencies (`Quests::PushIndex`: the bits of RequiredAchievementId with its
    non-persisted achievements, the completion achievement, RequiredLevel). A change visits only the rows of the bits that changed since
    the last check of the character (the watched bits that the character has: 55 bits for the 18 rows) and of the levels that it passed.
  - Live QG2a: the entry of the development character (level 65) granted `Quest_18000`, `Quest_18008`, `Vector_Faction_PushQuest`,
    `Vector_Kiln_PushQuest`, `Vector_Mounts_PushQuest` (the journal: "Acquire a Mount", "Faction Introductions", "Orbs of Power"); the
    bit `06A_MSQ_03` (a command) granted `Vector_Crafting_WW_PushQuest` ("Learn to Craft") at the next tick.
- The manifest (stage QG2a): a push row with no ExclusiveTerritory is a grant source of a kind that the backend runs, and a history plays
  it when the availability passes; TaskHaveLevel with a blank ItemName completes in a history of that level or more. The reasons say what
  a block is: a task with no TP_TargetTag (a pin or a cyclic puzzle: `impossible_task`, `DummyPin`), a never-true trigger tag (`nevertrue`,
  `NEVERCOMPLETE`, `nevetrue`, `nvere true`), a game event task with no event (a pin or a map task), a task with no ItemName (of a mission
  template or a community goal: its mission instance gives the item), KillEnemyWeaponID of an ability table (a weapon filter:
  `BowAbilityTable`) or of neither an item nor a table (`Turkey`), TaskHaveLevel of a faction progression, and a loot container whose
  salvage event pays a reward with no owner [research quest-gaps.md sections 2 and 8]. No row becomes supported by these reasons.
- The location rule in the manifest (stage QG2a; its scope corrected in stage QG3, finding QG2R-R2): a task with EnforceLocationRequirement
  and a POITag progresses only in a point of interest of a tag when its runtime path tests the rule: the possession and the equip
  predicates (`Plan::location_ok`), the quest drops (`plan_drops`), and the counters of an event (`Quests::matched_tasks`: the kills, the
  game events, the gathers, the crafts, the refines, the screens, the interactions, the cutscenes, the cinematics, the states of a
  person). TaskAchievement, TaskTriggerArea, TaskHaveLevel, and TaskConversationTopic (direct progress: `Quests::topics_finished`) do not
  test it. For a task of the scope with no shape of any of its tags in the POI data the task never progresses, and the manifest blocks it
  ("the location rule (EnforceLocationRequirement) needs a point of interest shape") [the rule of the runtime; agent]. The manifests of the stages 1.8e to QG1 did not test it: 13 rows were marked supported that no character can
  complete (among them `FactionIntro_Marauders_01`, `06A_MSQ_12_DefeatPirateCaptain`; the tags `abandoned_village_40289`, `brightwood8`).

### The places of stage QG3

The rules of the places do not change (the shape of a point of interest and the volume of a trigger area at the accepted position; 2297
and 6416 are a cue). Stage QG3 gives the data the places that the tools of 1.8b and 1.8c did not find [research quest-gaps.md sections 1
and 2; data].

- The origin of a POI shape is a POI or an area plot of any layer and any region of the open world [data]: the placements whose slice is
  POI_CapitalPlot or Area_CapitalPlot (1,262 plots). The tool of 1.8c took only the POI plots of the layer "pois" of the folder of the
  shape. The fit rule does not change (90 percent of the entries of the id in the shape, at most 5 percent wrong entries, the margin of
  6 m); the check uses the entries of each region (40300 Everfall_Settlement_01 has its shape file in r_+04_+03 and its entries and its
  plot in r_+04_+02). Of two plots with the same fit (plots a few meters apart: 8 shapes) the plot of the layer "pois" of the folder of the
  shape comes first, then a plot of that region [agent: the rule of 1.8c keeps its origins; no origin of the 476 shapes of before
  changed]. The source of most new origins: an Area_CapitalPlot of the layer "pois" (45), a plot of another layer (87, for example the
  shipwrecks of 04nwindward_persist_capt and the shrines of 08queensport_persistence), a plot of another region (7).
- A shape file `poi_NAME_ID.vshapec` has its id at the end (`poi_ancient_observatory_20423.vshapec`) [data].
- The area shapes (`objects/worldprisms/areas/FOLDER/NAME.vshapec`, the form of a POI shape) are in world meters: the origin is zero
  [data: the entries agree at the origin zero; Great Cleave area 5 spans x 8625 to 9416]. The file has no id, and its name does not always
  agree with the name key of a row (`monarchsbluffs/area1` and `@ui_area_monarchsbluff_1`; the files of the folder `queensport` are the
  areas of Ebonscale Reach; `windswardshores/area2` is the area of the row `windswardshores2_v2`). The id of an area file is each area row
  (IsArea) whose entries agree with the shape at the origin zero, by the same fit rule [agent: the fit is the data link; 131 of the 171
  files agree with the name key, and each of the 171 files binds exactly one row]. The 5 older Windsward Shores rows (9112, 9116 to 9119)
  have no entry: their V2 and FFA rows have the shapes.
- Each point of interest of `poi-data.json` keeps its kind (`poi`, `area`), its source (the shape file), its origin (the capitals file
  and the row of its plot, or the world origin of an area), the count of the origins that agree, and its world. The backend uses only the
  shapes of the open world (`gamedata::kOpenWorld`); a shape of another world is counted and not used [agent: an instance has its own
  stage].
- The tags with no shape stay apart from the tasks with no tag [agent]. `unshaped` lists each tag of the territory tables with no shape
  and the cause for each id of the tag: no shape file of the id, no POI or area plot that agrees, no entry of the region metadata, no row,
  a second shape. `PoiData::cause` gives it; a tag of no row has the cause "no row of the territory tables has the tag". The manifest
  names the cause in the block of a place task and of the location rule. A place task with no POITag has its own reason: the place of a
  mission template (its mission instance gives it), a pin task (OverrideLocation with no tag), or a blank cell (it never completes).
- No bounding box of entries is a shape [project rule of the brief: it is not the shape of the game]. The search of the inline shapes
  (the territory components of the placed slices of the open world, the walk of the trigger tool: 1,481 components; 1,045 POI plots, 207
  area plots, 13 siege forts with m_useLocalBounds 1, the masters of some objects) found no id that binds an inline shape to a territory
  id or a tag [data]: a component has only m_isPOITerritory, m_useLocalBounds, m_Radius, the guild and player names, and the storage
  overrides; its charted shape has an empty shape asset and the default prism of 100 m (6 POI plots have an inline prism of their own, also
  with no id). The tags whose row has no shape file stay in `unshaped`.
- The trigger volumes [data; the placement rule of the collision tool]: the tool also walks the capitals of each region of the open world
  (each placement that is not `<PLOT>`: its slice at the position, the rotation, and the scale of the placement) and the slices that a
  chunk or a slice spawns at its start (m_spawnOnInitialize 1, at most 12 levels, no slice that spawns itself). A volume keeps its slice
  (`source`), its placement (`via`: the chunk, or the capitals file and its row), and its world. The build: 11 new volumes (the tag
  Obj_GotoOutpost_Complete_00 of the outpost slices; no task of the data names it); no volume below a spawner; 13,475 spawners with no
  start (their slices have no volume).
- A volume of another world (an instance: a dungeon level script, a trial, the introduction worlds ftue_v2 and climaxftue_02) is in the
  member `elsewhere` with its world, never in `volumes` [agent: it belongs to its instance stage]. The backend never tests it; the
  manifest names its world ("the volume of the tag ... is in the instance ..."): 35 of the 43 trigger tags of the tasks with no volume of
  the open world have a volume of an instance (48 volumes in 7 worlds). The other 8: the never-true tags, `triggervol_msq_dummytask`,
  `maudlinbug_waltz`, `tv_17zq_galbrionoffice`, `tv_17zquru3_teleportedchallenge2` (no volume in any world).
- The place view (the command `quest_places` of the admin and development routes, `tools/world quest places CHARACTER`) [agent: a
  development aid]: the territory, the points of interest and the areas, and the trigger volumes at the accepted position of a character,
  with their sources. A grid of 256 m of each loader visits only the shapes and the volumes near the position (no scan of each shape; MP-8).

### The quest givers of stage QG4

The research is `~/Games/soulwarden/work/research/quest-gaps.md` (QG0, section 3 and the appendix "missing giver source index") and
`quest-engine.md` (section "Expansion and level closure"); the result and the live checks are in
`~/Games/soulwarden/work/evidence/quests/qg4/implement/RESULT.md`. The persons of the stage are in [Persons](persons.md).

- The artifact grant [data: MasterItemDefinitions.OnEquipObjectiveId of 73 artifact items, `artifactObjectives` of `item-catalog.json`;
  client: the server facet of the Objectives component in the image has an equip callback (table 0x82574A8, entry 0x3A547B0) that calls
  the objective-add path with OnEquipObjectiveId (item definition +0xC0, loader 0x442E9FC) at 0x3A5483E; the client facet callback
  0x3A54630 only pins an objective that is active already (1458); no client request asks for the objective: the static read of stage QG4,
  `evidence/quests/qg4/implement/static/ANSWERS.md` Q3]: an equip of such an item into the paperdoll (`Inventory::finish`: a
  slot with the action ItemEquipped: an equip request, a swap of two paperdoll slots, a build preset, a command) calls
  `Quests::item_equipped`; the next tick grants the objective (source `equip`, the territory of ExclusiveTerritory, the automatic pin)
  when both rules pass:
  - the item level [client: GetLevelRequirement 0x5C4C960 and the lookup 0x4403360 for an item with an empty RequiredLevel; research
    quest-engine.md Q7]: the level of the character is K + 1 or more for the first row K of XPLevels whose MaxEquippableGearScore admits
    the gear score of the instance (`ProgressionTables::item_level`, the member `equipGearScores` of `progression-tables.json`): 700
    needs the level 65 (the row 64, cap 800). The objective rows have no RequiredLevel: the rule is the rule of the item. The client equips
    no item above the level of its player (the normal equip path 0x67A5C50 checks the level, 0x67A5D8D to 0x5C5FB00, before it sends 3895;
    static read Q3c); the backend does not refuse such an equip yet (items-and-equipment.md), and so it tests the level at the grant. A row of XPLevels with IgnoreItemRarity false adds a rarity offset that is not known: the level of the row (no
    artifact reaches such a row) [agent]. Progression tables of before stage QG4 (no member): no item level and no grant [agent: the
    default with no data];
  - the normal availability (`Quests::available`): no active instance (no second instance at a new equip), no completion achievement (a
    build preset gives the completion achievement of its artifacts in its transaction, before the tick: no grant), the gates.
  The world entry grants nothing: an artifact that is in the paperdoll at the entry is no equip [the brief: no grant of each artifact
  objective at the entry]. An equip while the character is not in the world (the rows only) grants nothing. The cost (MP-8): one lookup of
  the item catalog for each equipped slot of a commit; the tick visits only the pending equips. Cases QG4A, QG4D. Live QG4-I1.
- An artifact objective has no NpcDestinationId: the server has no hand-in for it (the trap of stage 1.8f below); the completion of an
  artifact is an open point.
- The manifest: the equip is a grant source of a kind that the backend runs when the item level of the gear score of the item row
  (GearScoreOverride, else MinGearScore) is not above the level cap; a history grants it when it has the item (a source of the world or an
  item of the history) and the item level. 7 artifact rows are supported now; no history plays one (no hand-in).
- The causes of an NPC with no person slot (the reason of an offer grant): the member `personsApart` of the interaction data names the
  cause and the world (a person of an instance, of the old introduction world `climaxftue_02`, of a seasonal world, of an event spawn); an
  NPC with no entry has "no placement in the data". The line "grant: no source of a kind that the backend runs" of the blocks has the
  group of the first grant that the backend does not run (`(a person of an instance)`, `(a person with no placement in the data)`, `(a
  faction rank-up row (the faction stage))`, `(a mount race row (no grant))`, `(an artifact with no item level)`).
- A row with no grant source, by its Type [data]: `FactionStory_*` (the 12 rank-up rows `9804_*`, `9806_*`, `9809_*`, `9812_*`) is a
  rank-up of the faction rank system (stage QG7: the rows that the validation lists name have the grant source "faction"; the others
  keep "a faction rank-up row that the validation lists do not name"); `MountRace` (the 4 rows `MountRace_ScaleTest_*`) is a
  course of QuickCourseData with no grant row. The other rows with no source keep "the data of the backend has no source"
  (`06A_S_02_PowerSpheres`, `09A_S8_FixAncientWiring`, `S_getoffbeach`, `Quest_08015`, `Quest_08016`); the seasonal rows
  (`Quest_03193`, `Quest_03195`, `Quest_03198`) keep their gate RequiredWorldTags; the developer rows keep their kind. No spawn is made for
  an NPC with no placement (`0424_Yeti_WW_NPC`, `09a_Morgaine_RW`, `09a_Morgaine_SM`, `1225_Giacomo_FL`, `1338_MasterOfCeremonies`,
  `1533_BloomHerald`, `placeholderNPC_81`) [the brief].
- The ordinary accept 3772 keeps its checks: a new person slot is a slot of the interaction data; the server takes the giver from the
  validated interaction and the selected conversation state (Persons::accept), with its presence rule.

### The quest chest drops and the quest objects of stage QG5a

The research is `~/Games/soulwarden/work/research/quest-gaps.md` (QG0, sections 4 and 5) and the static read of stage QG5a
(`~/Games/soulwarden/work/evidence/quests/qg5a/implement/static/ANSWERS.md`); the census, the result, and the live checks are in
`~/Games/soulwarden/work/evidence/quests/qg5a/implement/RESULT.md`. The wire is in the protocol notes (4.60).

- The quest drop of a chest (ChestDropProbability; 1,142 tasks of the quest data, 59 story rows) [data; client: the item task field +0x6A4, the
  description 0x825F500 "0-100, probability to drop items for loot chests/POI loot"]: at a successful open of an ordinary chest of the chest
  data (`Chests::complete`), each Active possession task of the character (TaskHaveItems, TaskHaveAndReturnItems, TaskGatherItems,
  TaskGatherAndReturnItems: the enum values 25, 26, 29, 30 of the item task constructor, factory 0x4461150) with the chance above 0 and
  MinGearScore 0 rolls one item (`Quests::plan_chest_drops`). The rules of the other drops apply (one helper, `Quests::roll_drop`):
  PauseAndClearLootOnTaskComplete, AwardOnlyOnce, GuranteedDropAttempts, EnforceLocationRequirement (the character in the point of
  interest).
  - The roll of the client [client 0x3B0B2F3 to 0x3B0B336]: the integer part of a number of 0 to 100 at or below the chance (40 is 41 of
    100; 100 is each open). One item for each open [client 0x3B0B359: one item in the list of the task; agent: the data has no quantity].
  - The place of the chest [agent; client: the data check 0x3A6D4D0 sums the chest counts of the territory record of the task, 0x3A6E2E3 to
    0x3A6E30A]: a task with a POITag takes only a chest in a point of interest of its tags (and of its TerritoryID); a task with no POITag
    takes each chest. The place filter of the original server is not in the image.
  - One result [MP-7]: the chest system owns it. The quest items go into the same grant as the chest loot (whole or not at all, the cause
    ItemGathered: the loot notice), and the claim and the award rows (`Quests::write_drops`) are in the same transaction. A full bag gives
    none of them, and the drops are not a part of the kept roll of the claim (the next open rolls them again). A refused write gives no
    item, no claim, and no award row. A Looted claim refuses a second open: no second award. Cases QG5A, QG5B. Live I1.
- The proximity quest objects (the variation table Gatherable_Quest_Proximity: trails, footprints, torches; 284 objects of the open world)
  [data: the slice master_quest_proximity_instanced has a DetectionVolumeComponent whose sphere has the radius DetectionVolumeSphereRadius
  of the row, and m_isProximityGatherable in the server facet of its GatherableController; client: the handler of 5078 (0x4900030) only sets
  a value of the HUD; live P1: the client sends nothing at a trail]: the server finds the character in the sphere
  (`QuestObjects::moved`, `tick`, `look`). The rules:
  - an accepted position of the character (World::place marks it; the next tick looks one time; a camera estimate, a travel, a cutscene,
    or a cinematic: no look), 3D distance to the place of the object at most the radius [data];
  - InteractOnlyOnQuest: an Active task of the character has the InteractTag of the object [client 0x8261FC0];
  - the object is not found for the character now; then one plan of the quest system (the quest drops of its row, its game event, its
    tag: `Quests::object_destroyed`), its LootTracker entry Looted for the character, and the respawn of the row (a uniform draw of its
    bounds) [the rule of the destructible objects, stage 1.8e];
  - the find is at once (no time of a work) [agent: the row has BaseGatherTime 1 and GatheringAction None; no client time was found];
  - no strike hits a proximity object; a strike of a destructible object is as before;
  - the cost (MP-8): one look for each marked character at a tick, the cells of the grid of 64 m within the largest radius (10 m in this
    build). Case QG5C. Live I2.
- The quest objects of a gather keep the per-character state of the destructible objects (stage QG5a) [the brief; data: InstancedLootType 4]:
  after the commit of a gather of a quest row, the character has the LootTracker entry Looted of the managed actor of the object until a
  uniform draw of the respawn bounds of the row (`Gathering::quest_object_due`), and a second gather of the character is refused
  (`gatheredQuestObject`). The other characters gather their own object. The store has no row (a new world entry has the objects again).
  Live I4: the trapped boar of `09A_S1_SaveAnimals` and its marker go for the character. Case QG5F.
- The quest objects that the data tools take now (stage QG5a; [Gathering](gathering.md), [Interactions](interactions.md), [Game
  data](../development/game-data.md)):
  - a quest placement of any slice path (the custommqi_ and io_ slices of the main story, not only slices/gatherables/), with the variant of
    the placement, else the m_selectedVariant of the slice [data];
  - the variation tables Gatherables_NPCRescue_01 (the rescues) and Gatherable_Quest_AncientGlyph (the glyph locks) [data];
  - a direct binding: a GatherableController with no variation table names its row; the row is a quest object when it has the form of a
    row of the quest tables (a loot container with the loot table Empty or blank and a GameEventID) [agent: the bodies of the creatures, the
    doors with no event, the hazards, and the fishing spots are no quest objects];
  - the variation of a bound: "Set Interact Tag", "Set InteractOnlyOnQuest", and "Set interaction req achievement" of the selected variant
    set the tag, the flag, and the required achievement of the Interact component of their entity [client 0x42D5240 and 0x35D60F0,
    0x42D4B70 and 0x35D5EF0, 0x42D4BF0 and 0x35D5FA0]. A requirement that is an expression (`A || B`) is the member `conditional` of the
    bound, which the base evaluates with the conditional of the quests [client 0x35C2E60 evaluates the text through 0x2ED8BB0];
  - the children of a PrefabSpawner of a level placement (not client-authoritative, made at the init) whose slice is a quest object, with
    the client id rule of a child of a level placement (interactions.py `child_groups`, client 0x6160B60) [client: the rule of the person
    children; no live check of a quest child];
  - the proximity objects and the direct and the nested destructible objects (`quest-objects.json`).
- The glyph locks [data: the row 16BS_AncientGlyphLock (GatheringType AzothStaff, Tradeskill AzothStaff level 5, BaseGatherTime 3); client:
  the Azoth Staff in the paperdoll slot 38, 0x5CAB873; the rank gate 0x33732F0]: the gathering type AzothStaff is a tool type of the backend
  (slot 38; the weapon rows with GatheringTypes AzothStaff: Darkness_AzothStaff_T1, Skin_AzothStaff), with the rank gate of the gathering
  (the trade skill AzothStaff of the progression tables at the level of the row) and the required achievement of the variation. The event
  is given at the completion of the gather only (not at the sight of the glyph). Live I3 (with a command for the rank: the backend pays no
  ProgressionReward, the source of the ranks of AzothStaff).
- RequireControllingFaction of a gatherable row is a flag [client: the loader 0x440D2A0 reads it with the flag converter 0x5BF6BC0 at
  0x440DC07]: the cell "0" (557 rows) is no rule. Before stage QG5a any text of the cell refused the row ("a faction"), also for the quest
  objects of the earlier stages.
- Kept out (each with its reason in the manifest): the scripted state of a quest object (QG5b: a quest object of a slice with no GatherOption,
  a pressure plate, a teleport, a swap slice), the encounter and wave completions and the named creatures (QG5c: an event with no producer,
  for example `04a_side02_02_encounter_01_success`), the objects of an instance (the tools read only the open world), the rows that the
  backend refuses (stage QG5a: ItemRequired with an item, the rank of AzothStaff with no source; stage QG5b has both), a quest child with m_spawnOnInitialize false (452:
  `excluded` of the interaction data), and the tags with no source (`wcStation`, `settlementSmelting`, `settlementBlacksmith`,
  `specialty_doubloon`, `specialty_reekwater`; `IO_Generic_Hostage_Quest` is the tag of the mission task `FM_Task_RescueNPC_Generic_01`:
  the mission boards, QG6).

### The flows of the persons, the items of a gather, the AzothStaff ranks, and the quest gimmicks (stage QG5b)

The static reads are `~/Games/soulwarden/work/evidence/quests/qg5b/implement/static/A/ANSWERS.md` (the flow, the categorical reward) and
`static/B/ANSWERS.md` (the item restriction, the scripted objects); the live checks are in `.../qg5b/implement/RESULT.md`.

- The ObjectiveFlow of a non-persisted achievement (NPAData column ObjectiveFlow) [client: the parser 0x43BD930, the evaluator 0x318A4F0,
  the clause matcher 0x3189DA0; static read A1 to A3; live I1]: the clauses are split at ",". A clause is `u:` (unlock) or `l:` (lock), then
  OBJECTIVE, `/TASK`, and `:MODIFIER`; a clause with another prefix is left out (0x43BE66E). The modifiers of an objective are a, c, r; of a
  task a and c; no modifier and an unknown modifier are a (`gamedata::parse_objective_flow`). The clauses are ordered overrides: from the
  last clause to the first, the first clause that matches decides; a `u` passes and an `l` fails; no match fails; a flow with no clause
  passes (0x318A5B1 to 0x318A6A6). A clause matches when its objective is complete (the completion achievement of the row, IsObjectiveComplete
  0x3A48A30), else: OBJECTIVE and OBJECTIVE:a when the character has an instance of it (HasObjective), OBJECTIVE:c never, OBJECTIVE:r when the
  root of the instance is done; OBJECTIVE/TASK and OBJECTIVE/TASK:a when the task element is Active, Complete, or CompleteActive (states 1, 2,
  3), OBJECTIVE/TASK:c when it is Complete or CompleteActive. An objective that the data does not have matches nothing. The NPA is true
  when each of its gates passes (AND): a conditional (if any), the flow (if any), the status (if any; the backend has no rule for a
  status: false). One rule for the plans of the quests and for the persons (`flow_passes` of `world/quests.cpp`). Case QG5BA.
- The persons whose presence names such an NPA (211 persons, most of Nighthaven: `*SwapActive_18007_Cecilia` is
  `u:Quest_18018,l:Quest_18018/Quest_18018_Task_5:c`) are resolved again ([Persons](persons.md)). A change of the Objectives state of a
  character with a person conversation is a check of its presence at the next tick (as a change of its bits) [agent: the client refresh
  event of A4 is not found; one path for each writer]. Live I1: the caged Cecilia shows when the character gets Quest_18018.
- The restriction ItemRequired of a gatherable row [client: the gather validation 0x35C26E0 takes only the restriction 4 to its item branch
  0x35C2C8E; the Container check 0x48D6200; static read B1, B2; live I2]: the bag must have ItemRestrictionQuantity of ItemRestrictionId
  (the stacks of the bag slots; no paperdoll, no storage); a blank or 0 quantity needs nothing (0x48D626D). NoToolNecessary and ToolRequired
  with an item do not check it. The gather checks it at the begin and at the completion (`Gathering::check_gates`, the refusal
  `missingItem`); a refusal changes nothing. The gather does not take the item [agent: the client has no removal path (static read B4), and
  the data does not decide: 0826_0801_ToBeFree returns its 4 keys at the hand-in after 4 cages that each need one; a decision for the user].
  Case QG5BB.
- The categorical progression reward of a success event (CategoricalProgressionId, CategoricalProgressionReward) [client: the event loader
  0x31E70B0 reads the id at 0x31E8126 (row +0x40) and the reward as an integer at 0x31E8182 (row +0x3C); data: the rank table AzothStaff
  has 1 experience for each rank, AutoRankUp]: the hand-in pays it when the id is a trade skill of the progression tables (AzothStaff of 5
  main story success events, and the trade skills Riding and Fishing of their quests), with the rule of the trade skill experience
  (`Skills::add_experience` on the plan of the hand-in: the ranks, the cap of the row; at the cap the experience is 0), in the transaction
  of the hand-in; the categorical state (slot 65) of the character goes to its owner. A reward of another progression (a faction, an event,
  a season) refuses the hand-in with its cause. A reward with no id, and an amount that is not a whole number of 0 to 1e9, stay unpaid [agent:
  the conversion of a fraction is not read]. The global progression modifier (UsesGlobalProgressionMod) is not applied [agent: its value is
  not found, static read A6]. Only a hand-in pays it: the game event of a task and the salvage event of a container do not (the open of
  Award_SeasonalAzothStaffBox stays refused). The client shows no rank line in the reward notice (ShowAsObjectiveReward false). Case QG5BC.
  Live I3: the hand-in of `99G_MSQ_SM_PrepareWarfare` gives the rank 5 (4 before), and the glyph of rank 5 opens.
- A quest gimmick of the level data [data; agent: the form of the rows]: a direct binding (no variation table) whose row has no tool, the
  loot table Empty or blank, and a GameEventID that a quest task names (KillEnemyType of the task tables) is a quest object of a gather (the
  cannon `Gimmick_BreakingWallsCannon_Vignette` of `12A_MSQ_08_DestroyRune`, 6 placements). A direct destructible controller with such a row
  (no loot container: the crate is Kicking) is a destructible quest object with the tag and the InteractOnlyOnQuest of the Interact
  component of its slice (the 5 crates of `12A_MSQ_13_StormBeach`). Live I4: the cannon completes UseCannon; the shot, the projectile, and
  the debris of its server timeline do not play (an open point).
- Kept out with their reason (`manifest/kept-out.txt` of the result): the shrine teleport of `99D_MSQ_07_PortalFight` and the lift of
  `Quest_16008` (the action 82 InteractTeleporter: a teleport handler and the rule of its row event are not found in the client; the lift uses
  a flow NPA), the fogwall of `99E_MSQ_10_ToCaerDun` (a pressure plate in the game mode QuestYonas), the crown of `12A_MSQ_24_DefeatGawain`
  and the heartgems of `99B_MSQ_10_cleansefort` (spawners with m_spawnOnInitialize false of the game modes QuestGawain and QuestCaerDun: an
  encounter runner, QG5c).


### The encounters of stage QG5c2

The source of each rule is in brackets ([Encounters](encounters.md); [live] here: the live checks of stage QG5c2).

- `Quests::event` returns a `QuestEventOutcome`: the result (`notApplied`, `repeated`, `noMatch`, `credited`, `failed`, `storeFailed`) and
  the matched tasks (the instance and the index of each) [agent]. A caller that keeps the event and tries again itself
  (`QuestEvent::callerRetries`: the ledger of an encounter credit) gets `storeFailed` with the matched tasks, and the quest system plans no
  retry of its own; the retry has `restricted` with these tasks, so that it credits only them [agent].
- The pinned list of the client (message 1458) can have the root instance of a linked encounter: the quest system takes it and does not
  store it; at the end of the link (`encounter_unlinked`) it goes from the list [live L2: with no such rule a quest grant took the
  encounter off the HUD].
- The support manifest: the success event and the start object events of each recipe that the runner takes are producers with no item
  (`gatherEvents`, `gatherEventsFree`), and the member kinds of these recipes are kill sources; an event that only a refused recipe gives
  names the cause of the recipe (`refusedEvents`) [agent; test case QG5C2H]. The build: 527 supported rows before, 535 after (2,147 rows);
  the second computation of the stage (`~/Games/soulwarden/work/evidence/quests/qg5c2/implement/scripts/support_check.py`) has the same
  counts.
- Code review of stage QG5c1, finding QG5C1R-R3: a ranked producer with no item gate stays a free alternative [agent; test case QGR5C1C].
  Stage QG5c3 keeps it as a producer of its own (see below).

### The encounters of stage QG5c3 and the corrections of the code review of stage QG5c2

The source of each rule is in brackets ([Encounters](encounters.md); [live] here: the live checks of stage QG5c3; [review] the findings
QG5C2R-R1 to R6 of `~/Games/soulwarden/work/evidence/quests/qg5c2/code-review/FINDINGS.md`, each with a case that fails on the tree of stage
QG5c2: `tests/review_qg5c2_tests.cpp`).

- The producers of a game event (`QuestSupport::Index::gatherSources`) are a list for each event: each producer has its kind, its rank gate
  of a trade skill that only a hand-in gives (AzothStaff), and its ItemRequired item. A task takes one producer as a whole, in this order:
  no rank gate and no item or an item of a source of the world; the smallest rank gate with no item or an item of the world; no rank gate
  and an item of a quest (the history must have it); a rank gate and an item of a quest. The first target of the task with such a producer
  decides; a target with only producers whose item has no source gives the block of the item [review QG5C2R-R6: a keyed rank 2 row and a
  free rank 5 row of one event give "rank 5"; test case QGR5C2E]. This replaces the indexes `gatherEvents`, `rankEvents`,
  `gatherEventItems`, and `gatherEventsFree`; the counts of the supported build do not change by it.
- The start object of a taken encounter recipe is a producer through its gathering row (`GatheringData::encounter_object`): the refusal
  causes, the rank gate, and the ItemRequired item of the row stay [review QG5C2R-R5; test case QGR5C2D]. The success event, the event of
  a new participant (`m_participantAddedGameEventId`, stage QG5c3), and the event of a proximity object (the start of an ambush, an area
  trigger: its find has only its quest tag gate) are producers with no gate [agent; test case QG5C3G].
- The store has the pins of the quests only: the plan of a quest change writes the instances of its list that the plan has as quests, and a
  pinned root of a linked encounter stays in the list of the entity for the client [review QG5C2R-R4; test case QGR5C2B: after a quest grant
  and after the end of the encounter, no row of `character_quest_pins` has the root].
- The build (stage QG5c3): 535 supported rows before, 544 after (2,147 rows). The 9 new ones: `09A_S18_ScanElafryMagic`,
  `1605_0301_awakenanubis`, `1625_0100_brimstoneundersiege`, `1625_0103_stirringinthesands`, `99A_MSQ_03_ClimbArcturus`,
  `99B_MSQ_05_findingfort`, `99D_MSQ_08_ThorpeFight`, `Objective_Artifact_Set1_MediumLegs` (the kill category of the Thorpe boss), and
  `Quest_18010`; none is lost. A new character still completes 67; 4 of the 25 groups of the template histories have new counts
  (`1625_0100_brimstoneundersiege` 36 to 47, `09A_M15_FightArtemis` 40 to 41, `16_Side_19_HelpWarden` 40 to 42, `99A_MSQ_02_Rumors` 34 to
  36). The first reasons of the others: "no producer of its game event" 27 to 19, "no creature of its categories" 36 to 35. The other quests
  of the new recipes keep blocks of other systems (a point of interest with no shape: `99E_MSQ_04`, `99E_MSQ_07`, `12A_MSQ_15`,
  `FactionIntro_Covenant_03`; a quest item with no source: `C10A_Q1_Build`). The second computation of the stage (a copy of
  `support_check.py` in `~/Games/soulwarden/work/evidence/quests/qg5c3/implement/scripts/`, with its own code of the rules of the runner of
  stage QG5c3 and of the findings R5 and R6) has the same counts: 32 recipes taken, 544 supported, 0 row, fact, history, and group
  differences.
- Code review of stage QG5c3, finding QG5C3R-R4: the task fact keeps the other producers of its event that a history can use, each with
  its own gates (`TaskFact::alternatives`, the member `alternatives` of the task in the manifest): a history takes the task when the gates
  of the first producer or of one alternative pass (an unranked producer with a key that a quest gives, beside a free producer with the
  rank 5 of AzothStaff: a history with the key and the rank 0 takes the task; a history with no key still needs the rank 5) [review
  QG5C3R-R4; test case QGR5C3D]. The counts of the supported build do not change by it.
- Stage QG5c4: the start object of an encounter whose gatherable row has a cause of the gathering data (a rule that the backend cannot
  check) refuses its recipe, so its events are no producers [agent: the gathering system refuses its gather; test case QG5C4A].
- The build (stage QG5c4): 544 supported rows before, 548 after (2,147 rows); none is lost. The 4 new ones: `1434_0103_Pyre`,
  `1602_0401_headofsnake`, `C10A_Q4_SpectralArtifacts`, and `C10A_Q4R_SpectralArtifacts`. A new character completes 68 (67 before); 3 of the
  25 groups of the template histories have new counts (`99F_MSQ_ER_DeliverPoem` 44 to 45, `99F_MSQ_ER_GatherAllies` 42 to 43, the new
  character 67 to 68). The first reasons of the others: "no producer of its game event" 19 to 13, "the location rule with no point of
  interest shape" 15 to 17. The other quests of the new recipes keep blocks of other systems (a point of interest with no shape:
  `09A_S9_HackDefenses`, `Quest_18016`; a quest item with no source: `1624_0401_thedemonscauldron`, `99A_MSQ_10_GetCrosspiece`; a pin task:
  `09A_S17_EstablishDefenses`). The second computation of the stage (a copy of `support_check.py` in
  `~/Games/soulwarden/work/evidence/quests/qg5c4/implement/scripts/`, with its own code of the rules of the runner of stage QG5c4 and of
  the findings R3 and R4 of the review of QG5c3) has the same counts: 47 recipes taken, 548 supported, 0 row, fact, history, and group
  differences.
- The build (stage QG5c5): 548 supported rows before and after (2,147 rows); no row changes. The only row that waits for the Darkness
  events is the mission template `FM_corruption_breach` (task 0: the game event `Darkness_Any_GlobalSuccess`): its event has producers
  now (the five Darkness recipes), and its grant has no source (the mission boards). No quest task names the success events of the boat
  reinforcements. The kinds of the parts of the group members are kill sources. A new character completes 68; the 25 groups keep their
  counts. The second computation (a copy of `support_check.py` in `~/Games/soulwarden/work/evidence/quests/qg5c5/implement/scripts/`
  with its own code of the rules of stage QG5c5 and of the finding QG5C4R-R4) has the same counts: 54 recipes taken, 548 supported, 0
  row, fact, history, and group differences.
- The build (stage QG7, [Factions](factions.md)): 548 supported rows before, 573 after (2,147 rows); none is lost. The 25 new ones: the
  first rank-up row of each faction (`9806_S_0001_RankUp`, `9806_M_0001_RankUp`, `9806_C_0001_RankUp`), the 12 rank-up rows `98_*_0102`
  to `98_*_0405`, and 10 trials (for example `98_S_01_theexperiment`, `98_M_02_ultimatewarrior`, `98_C_01_theapostate`). The histories
  have a faction: the three new profiles `new:Archetype_Soldier:Syndicate`, `:Marauder`, and `:Covenant` (the groups "new character,
  faction NAME") join at the first TaskChooseFaction and complete 68 each; the unaligned new character completes 68 (no change); 28 groups
  (25 before). The first reasons: "ProgressionReward has no owner" 10 to 0, "TaskHaveLevel of a faction progression" 20 to 0, and the new
  "a faction rank-up row that the validation lists do not name" 5 (`9804_S_0001_RankUp`, `9809_S_0001_RankUp`, and the three rows
  `9812_*_0001_RankUp`). The second
  computation (a copy of `support_check.py` in `~/Games/soulwarden/work/evidence/quests/qg7/implement/scripts/` with its own code of the
  faction rules and of the finding QG5C5R-R1) has the same counts: 573 supported, 0 row differences, 0 of 6,762 own facts differ, 309
  histories and 28 groups with 0 differences.

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
  child objectives; a hand-in pays the standing on the skill set of the history (stage QG1: a rank-up is paid; the territory of the
  standing is the territory of the present slot of the destination person, else the origin territory: `Quests::standing_territory`).
  The check of a row (an empty history) takes the territory of the first resolved slot of the destination, else ExclusiveTerritory, else
  the start territory.
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

- The counts of stage QG1 (the same game data, with the progression tables of the format 4 and the POI data of the format 2): 304
  supported objectives (160 before). A new character of each archetype completes 25 (15 before; new: `06A_MSQ_01_Quartermaster`,
  `1125_0101_SongoftheSea`, `1129_0101_SoulMusic`, `1129_0102_JaansenintheNorth`, `1129_0102_NewPerspective`, `1129_0103_CliffHanger`,
  `1524_1514_FirstThing`, `1525_1518_Communication`, `C10A_S12_DisturbingSilence`, `C10A_S7_AnimalInstincts`). A level 60 character with
  no template completes 11 (4 before). The 298 template histories in 23 groups complete 8 to 17 (1 to 7 before; `09A_M9_MeetShirzad`: 17,
  `09A_M15_FightArtemis` and `99F_MSQ_ER_DeliverPoem`: 16; 158 templates with no start objective: 8 to 16). The first reasons of the 723
  objectives that are not supported: 183 a point of interest with no shape, 151 an item with no source, 65 no creature of the categories,
  65 no producer of a game event, 44 no grant source that the backend runs, 40 TaskGameMode, 35 TaskQuickCourse, 20 TaskHaveLevel, 18 the
  gate QuestArcTag, 18 no object of the interact tag, and 24 smaller groups; no objective stops at its standing now (158 before). The
  second computation (a copy of `support_check.py` with the rules of stage QG1 in the evidence of the stage: its own point-in-polygon test
  of the bounds and of the places of the person slots) gives the same counts: 2,147 rows, 304 supported, 0 row differences, 306 histories
  with 0 differences, 25 groups.

- The counts of stage QG2a (the same game data, with `loot-tables.json` of stage QG2a: the category groups and the salvage events): 356
  supported objectives (304 before; 65 new, 13 no longer: the location rule). The first reasons before of the 65 new rows: 18 the gate
  QuestArcTag, 15 no grant source (the push rows and their chains), 10 an Azoth fraction, 10 no creature of the categories (the category
  rule), 4 a loot container (the coin purses and `Coin_Stack_500`), 8 an item or another task that a new grant or reward reaches. A new
  character of each archetype completes 35 (25 before; new: `06A_MSQ_02_VisitInn` (the purse), `06A_MSQ_03_VisitMarket`,
  `06A_S_01_FindArcheologists`, `06A_TS_01_GatherAndRefine`, `06A_Z_02_SpreadJoy`, `12A_MSQ_02_InnTutorial` (the purse),
  `12A_MSQ_03_VaultTutorial`, `12A_MSQ_04_MeetPayne`, `12A_MSQ_06_ToMiclot`, `12A_Side_01_PlaceFlowers`, `12A_Side_02_GatherGift`,
  `12A_Side_03_CollectPicks`, `12A_Side_15_CollectResources`, `12A_Side_16_ToGoodUse`; gone by the location rule: `1125_0101_SongoftheSea`,
  `FactionIntro_Marauders_01`, `FactionIntro_Marauders_02`, `FactionIntro_Marauders_Vector`). A level 60 character with no template
  completes 7 (11 before). The 298 template histories in 23 groups complete 7 to 14 (8 to 17 before; `09A_M9_MeetShirzad` and
  `16_Side_19_HelpWarden`: 13; 158 templates with no start objective: 7 to 14). No history completes a push row: the requirements of the 18
  rows are false in the histories (17_MSQ_22, the Dunwood bits), or a later gap blocks them (`Quest_18008`: no NpcDestinationId,
  `Vector_Faction_PushQuest`: a topic wait, `Vector_Kiln_PushQuest`: a recipe). The first reasons of the 671 objectives that are not
  supported: 183 a point of interest with no shape, 141 an item with no source, 63 no producer of a game event, 39 TaskGameMode, 38 the
  location rule with no shape, 36 no creature of the categories, 35 TaskQuickCourse, 28 no grant source that the backend runs, 20
  TaskHaveLevel of a faction progression, 18 no object of the interact tag, and 25 smaller groups. The second computation (a copy of
  `support_check.py` with the rules of stage QG2a in the evidence of the stage: its own kill facts with the category rule and the location
  rule, its own TaskHaveLevel, an allow list of the cells of a salvage event, the Azoth amount, the push) gives the same counts: 2,147
  rows, 356 supported, 0 row differences, 0 kill and level fact differences, 306 histories with 0 differences, 25 groups; on the manifest
  of stage QG1 it finds 230 row differences.

- The counts of stage QG3 (the same game data, with `poi-data.json` and `trigger-data.json` of stage QG3, and the corrections of the
  review of QG2a): 414 supported objectives (356 before; 58 new, 0 no longer). The first reasons before of the 58 new rows: 46 a point of
  interest with no shape (the new places), 12 the location rule (9 a tag with a new shape, 3 the correction QG2R-R2:
  `06A_MSQ_06_RescueFarmers`, `06A_MSQ_14_SolveMapPuzzle`, `0713_0101_explorerift`). The corrections of the review alone (the old place
  data) give 359. A new character of each archetype completes 37 (35 before; new: `C10A_S8_SnaggleTooth`, `C10A_S9_Catnipping`). A level 60
  character with no template completes 7 (7 before). The 298 template histories in 23 groups complete 7 to 16 (7 to 14 before;
  `99F_MSQ_ER_DeliverPoem`: 16, `16_Side_19_HelpWarden`: 14, 158 templates with no start objective: 7 to 15); 116 of the 306 histories
  have a new played list. The first reasons of the 613 objectives that are not supported: 157 an item with no source, 99 a point of
  interest with no shape (183 before; their causes: 66 no plot agrees, 25 no shape file of the id, 7 no entry of the region metadata, 1
  no row), 73 no producer of a game event, 48 TaskGameMode, 39 no creature of the categories, 35 TaskQuickCourse, 30 no grant source
  that the backend runs, 21 no object of the interact tag, 20 TaskHaveLevel of a faction progression, 15 the location rule with no shape
  (38 before), 4 a trigger volume of an instance, and 26 smaller groups. The second computation (a copy of `support_check.py` with the
  rules of stage QG3 in the evidence of the stage: its own readers of the shapes and the volumes of the open world, its own place, trigger,
  and location facts, the corrections QG2R-R1, R4, and R5) gives the same counts: 2,147 rows, 414 supported, 0 row differences, 0 of 3,905
  own facts differ, 306 histories with 0 differences, 25 groups; on the manifest of before with the new data it finds 34 row differences.

- The counts of stage QG4 (the same game data, with `interaction-data.json` and `progression-tables.json` of stage QG4): 434 supported
  objectives (414 before; 20 new, 0 no longer). The first reason before of each of the 20 new rows was "no grant source that the backend
  runs": 13 offers of the direct NPC templates of Morrowtide and East Consolation (`1102_0301_musslebeard`, `1104_01a01_crashing`,
  `1105_0201_teardropgems`, `1105_0202_excommunication`, `1110_0101_GreetTheNewcomers`, `1111_0401_ancienttruth`,
  `1111_0402_corruptedenergy`, `1502_1501_Sieged`, `1503_1506_Boars`, `1505_1510_Slaying`, `1510_1507_Scouting`, `1510_1508_Damnation`,
  `1510_1512_PayingRespects`) and 7 artifact rows (the equip grant: `Objective_Artifact_Set1_2hBow`, `_Set1_Earring1`, `_Set1_MediumFeet`,
  `_Set1_MediumHead`, `_Set2_1hRapier`, `_Set2_2hBow`, `_Set2_2hStaffLife`; no history plays them: no hand-in). A new character of each
  archetype completes 39 (37 before; new: `1104_01a01_crashing`, `1502_1501_Sieged`). A level 60 character with no template completes 9
  (7 before). The 298 template histories in 23 groups complete 9 to 18 (7 to 16 before; `99F_MSQ_ER_DeliverPoem`: 18; 158 templates with
  no start objective: 9 to 17); each of the 306 histories has a new played list. The offers of `06_NessaHarrower05` are grant sources of a
  kind that the backend runs now (its presence rule): of its 13 rows 3 were supported before by an other grant, and 10 stop at an other
  gap (a pin task, TaskGameMode). The 24 rows of an offer of the 10 direct NPC templates have a grant of a kind that the backend runs
  now; 13 of them are supported. Each of the 73 artifact rows has the equip grant (the item level 65). The first reasons of the 593 objectives that are not supported: 158 an item with no source, 100 a point of
  interest with no shape, 73 no producer of a game event, 48 TaskGameMode, 39 no creature of the categories, 35 TaskQuickCourse, 21 no
  object of the interact tag, 20 TaskHaveLevel of a faction progression, 15 the location rule with no shape, 10 a ProgressionReward, 8
  no grant source that the backend runs (30 before), 2 a person whose client id an other slot has (`1625_Ghazi_p4`), and 36 smaller
  groups. The second computation (a copy of `support_check.py` with the rule of stage QG4 in the evidence of the stage: its own item
  level of the artifacts from `equipGearScores`, the equip grant of a row and of a history) gives the same counts: 2,147 rows, 434
  supported, 0 row differences, 0 of 3,905 own facts differ, 306 histories with 0 differences, 25 groups, the new Soldier completes 39;
  on the manifest of before with the new data it finds 117 row differences and 306 history differences.

- The counts of stage QG5a (the same quest data, with `gathering-data.json`, `quest-objects.json`, and `interaction-data.json` of stage QG5a,
  and the corrections of the review of QG4): 486 supported objectives (434 before; 74 new, 22 no longer). The first reasons before of the 74
  new rows: 38 an item with no source (36 of them have the source "the quest drop of the chests" now), 24 no producer of a game event (the
  proximity objects, the quest placements of any slice path, the rescues, the direct bindings, the faction flag), and 12 no object of the
  interact tag (the variation tags). The 22 rows no longer supported are corrections: 16 have a person of Nighthaven whose presence names an
  NPA with an ObjectiveFlow (QG4R-R2: never there; the task or the standing territory of the hand-in at that slot), 4 need a gather that the
  runtime refuses (ItemRequired with an item), 1 the rank of AzothStaff with no source, and 1 an item of such a refused row. A new
  character of each archetype completes 66 (39 before; 27 new: `04A_Side01_01_RecoverSupplies` (the trails), `04A_Side08_01_GetReagents`,
  `1127_0101_MalignDominion`, `1128_0101_VillageChampion`, `12A_MSQ_07_FreeMiners` (the rescues), `12A_Side_13_DevilHoney`, `1502_1502_Stolen`,
  `1504_1503_Fury`, `1505_1509_Menacing`, `1505_1510_Slaying`, `1510_1507_Scouting`, `1510_1508_Damnation`, `1510_1512_PayingRespects`,
  `1524_1515_FootstepsBeach`, `1524_1516_TrueTerror`, `1527_1524_Steadfast`, `1527_1525_Treasured`, `1528_1526_NotAnotherFish`,
  `95_s01_01_CROW_GracelessRequest`, `95_s01_03_CROW_BestOfAeternum`, `C10A_S13_TrackingByTeeth`, `C10A_S14_HeartOfTheMarsh`,
  `C10A_S1_SeadogBrew`, `FactionIntro_Covenant_01`, `FactionIntro_Covenant_02`, `FactionIntro_Covenant_Vector_01`,
  `FactionIntro_Covenant_Vector_02`; none lost). A level 60 character with no template completes 31 (9 before). The 298 template histories
  in 23 groups complete 27 to 43 (9 to 18 before; `99F_MSQ_ER_DeliverPoem`: 43; 158 templates with no start objective: 27 to 41); each of
  the 306 histories has a new played list. The first reasons of the 541 objectives that are not supported: 115 an item with no source, 102 a
  point of interest with no shape, 54 no producer of a game event, 44 TaskGameMode, 41 no creature of the categories, 34 a person with no
  slot (6 before: the Nighthaven persons of QG4R-R2), 34 TaskQuickCourse, 20 TaskHaveLevel of a faction progression, 14 the location rule
  with no shape, 10 no grant source that the backend runs, 7 a standing with no territory, 7 a ProgressionReward, 7 an item that the catalog
  does not have, 3 no object of the interact tag (21 before), and 49 smaller groups. The second computation (a copy of `support_check.py`
  with the rules of stage QG5a in the evidence of the stage: its own producers of a game event, its own tags of an interaction, its own
  chest source of an item with its own point-in-polygon test of the chests) gives the same counts: 2,147 rows, 486 supported, 0 row
  differences, 0 of 6,772 own facts differ (860 chest drops, 860 game events, 1,178 interactions), 306 histories with 0 differences, 25
  groups, the new Soldier completes 66; on the manifest of before with the new data it finds 182 row differences and 306 history
  differences.

- The counts of stage QG5b (the same quest data, with `gathering-data.json`, `quest-objects.json`, and `interaction-data.json` of stage QG5b):
  519 supported objectives (486 before; 33 new, none lost). The manifest takes the rules of stage QG5b: the flow of the presence with the
  trees that the history plays (each running tree is an instance of the history with the states of its nodes), the AzothStaff rank of the
  history (the categorical reward of its hand-ins; a gather with a rank gate of AzothStaff needs the rank), the item of a gather with
  ItemRequired (a source of the world, else a quest item that the history must have, else blocked), and the paid trade skill rewards. The
  first reasons before of the 33 new rows: 11 a person of Nighthaven with no resolved slot (the flow persons), 7 a standing with no
  territory (the flow person of the hand-in gives the territory now), 9 a gather that the backend refused (ItemRequired, the rank of
  AzothStaff), and 6 others whose change is a different rule (the AzothStaff reward of `99G_MSQ_SM_PrepareWarfare`, `99C_MSQ_12_Vector`,
  and `99B_MSQ_10_cleansefort`; the Riding reward of `9401_TutorialFood`; the item of the ItemRequired gather of `1624_0302`; a flow person
  of `Quest_16026`). A new character of each
  archetype completes 66 (no change). The 25 groups of the histories do not change. The first reasons of the 508 objectives that are not
  supported: 120 an item with no source, 102 a point of interest with no shape, 48 TaskGameMode, 42 no creature of the categories, 36 no
  producer of a game event (54 before), 35 TaskQuickCourse, 20 TaskHaveLevel of a faction progression, 15 the location rule with no shape,
  10 a ProgressionReward (the faction rows), 8 no grant source that the backend runs, 7 an item that the catalog does not have, 6 a person
  with no slot (34 before), and 59 smaller groups. The second computation (a copy of `support_check.py` with the rules of stage QG5b in the
  evidence of the stage: its own flow parser and evaluator with the node states of the play, its own rank loop, its own reward rule) gives
  the same counts: 2,147 rows, 519 supported, 0 row differences, 0 of 6,760 own facts differ, 306 histories with 0 differences, 25 groups,
  the new Soldier completes 66; on the manifest of before with the new data it finds 135 row differences and 29 own fact differences.

- The counts of stage QG5c1 (the same quest data, with `creatures.json` of stage QG5c1 and the corrections of the review of QG5b): 527
  supported objectives (519 before; 8 new, none lost). The first reasons before of the 8 new rows: 6 no creature of a spawn point (the
  stationary targets: `0716_0101_malevolentofferings`, `99G_MSQ_SM_PlantBombs`, `16_Side_17_HuntScorpion`, `Quest_02792`; the named
  creatures of the slices that show now: `1605_0701_brokenkey`, `99B_MSQ_02_meetgalahad`) and 2 an item with no source (the quest drops of
  creatures of those slices: `16_Side_28_LightsOut`, `99F_MSQ_ER_StealBoat`). A new character of each archetype completes 66 (no change); 49
  of the 306 histories have a new played list; the 25 groups stay (3 have new counts). The first reasons of the 500 objectives that are not
  supported: 118 an item with no source, 102 a point of interest with no shape, 48 TaskGameMode, 36 no creature of the categories, 36 no
  producer of a game event, 35 TaskQuickCourse, 20 TaskHaveLevel of a faction progression, 15 the location rule with no shape, 10 a
  ProgressionReward, 8 no grant source that the backend runs, 7 an item that the catalog does not have, 6 a person with no slot, and the
  smaller groups. The reason of a kill task with no creature names the kept-out cause of the creature data when it has one (the member
  `keptOut` of `creatures.json`: "(fl_chameleon_elite_named_09fl01: the client does not show its slice chameleon.dynamicslice ...)", "an
  entry of an encounter of the level data (IsEncounter ...)"); the kind of the reason stays "no creature of its categories". The kill tasks
  that stay blocked (`manifest/kept-out.txt` of the result): 109 tasks of 68 objectives an encounter member (QG5c2 and QG5c3), 103 tasks
  of 66 objectives with no entry of the open world (an instance, another world), 21 tasks of 17 objectives a slice that the client does not
  show, 3 tasks an entry that the level data does not spawn. The second computation (a copy of `support_check.py` with the rule of
  QG5BR-R3 in the evidence of the stage: a ranked producer with an item needs the rank, then the item fact) gives the same counts: 2,147
  rows, 527 supported, 0 row differences, 0 of 6,759 own facts differ, 306 histories with 0 differences, 25 groups, the new Soldier completes
  66; on the manifest of before with the new data it finds 5 row differences and 10 own fact differences.
- The counts of stage QG8 (the same quest data, with `loot-tables.json` and `gathering-data.json` of stage QG8, and the corrections of the
  review of QG6b): 652 supported rows (609 before; objective 575 to 618, missionTemplate 32, communityGoal 2): 43 new objectives, none
  lost, for example `12A_Side_14_CollectWine`, `02A_Side08_02_LostReturned`, `06A_MSQ_05_FixWindmills`, `12A_MSQ_08_DestroyRune`,
  `C10A_Q1_Build`, `13A_SQ_28_BearUrine`, `Quest_02790`, the 7 archetype variants each of `99B_MSQ_09_killumberto` and
  `99D_MSQ_09_Outpost`. The board missions that run: 1,835 to 1,832 (finding QG6BR-R6: 3 hunt missions of the island boar
  `CK_Boar_Named_10WS08_Isle`, whose skinning row Cursed10ws_Boar the backend refuses, IsFFARestricted). A new character completes 80 (71
  before; new: `06A_IR_01_FindCreed`, `06A_MSQ_05_FixWindmills` to `06A_MSQ_07_MeetIronRose`, `12A_MSQ_08_DestroyRune` to
  `12A_MSQ_10_ConfrontLochnir_alt`, `12A_Side_14_CollectWine`, `C10A_Q1_Build`). The first reasons of the others: "no source of its item"
  116 to 67; then 104 a point of interest with no shape, 48 TaskGameMode, 36 no creature of the categories, 35 TaskQuickCourse, 18 the
  location rule with no shape, 13 no producer of a game event. The item tasks with no source of the objective rows that are not supported
  (`itemTasksWithNoSource`): 143 (248 before: the census of the result), task drop 62, loot table 53, chest drop 13, recipe 10, none 5. The
  second computation (a copy of `support_check.py` with the rules of stage QG8 and the findings QG6BR-R4 and R6 in the evidence of the stage)
  gives the same counts: 2,147 rows, 652 supported, 0 row differences, 0 of 7,532 own facts differ (1,568 of them the item drops of the
  possession tasks), 309 histories and 28 groups with 0 differences, the new Soldier completes 80, 3,122 board missions with 1,832 that run.

### The missions of the boards (stage QG6a)

The rules of the boards, the offers, the accept, and the hand-in are in [Mission boards](mission-boards.md). The rules of the quest system:

- A mission instance is a quest instance (`QuestInstance`) with the mission values: `mission` (the CRC-32 of the MissionID), `communityGoal`,
  `provider` (the key of the board placement), and `destination` (the 16 bytes of the actor id of the live board: a world value, not
  stored). The Objectives state writes them into the element of the instance (protocol notes 4.64). The rows of `character_quests` have
  the first three (schema 19).
- The tree of a mission instance is the effective objective of its mission (`QuestData::objective_by_crc(crc, mission)`): the template
  with the kill override of the row. Each lookup of the tree of an instance (the tasks, the Objectives state, the refresh, the hand-in)
  gives the mission [data; client: the factory 0x4461150].
- `Quests::grant_mission` makes the instance (the grant `board`): the checks of a grant, a new instance id, the origin territory of the
  board. The rewards of the hand-in are the success event of the mission row (SuccessGameEventIdOverride), else the event of the template
  [data].
- The return task (CM_ReturnToGiver: TaskInteract with the tag `returntogiver`) stays Active until the hand-in 2461 at the provider
  board of the instance, which completes it and then hands in [client: the ready check 0x3A3CC50 needs the return pending; agent: the
  request at the provider is the return].
- A commit hook (`QuestCommitHook`: `write` in the transaction, `apply` after the commit) puts the rows of another system into the
  transaction of a quest change: the town entry of the hand-in of a mission, and the faction row and the cost of a faction change
  (finding QG7R-R1). A refused row of the hook changes nothing (MP-7).

### The missions of the boards (stage QG6b)

The override rules, the faction boards, the daily bonus, and the patrols are in [Mission boards](mission-boards.md). The rules of the
quest system:

- The effective objective of a mission row has each override cell of the task factory (`QuestData::apply_overrides`; the static read
  `~/Games/soulwarden/work/evidence/quests/qg6b/implement/static/factory/FACTORY.md`): the common cells (TerritoryID, POITag,
  OverrideLocation), the item cells of the possession tasks, the give item, the timer, the patrol of one POI. The task rows of a variant
  are copies; the template keeps its values [client; case QG6BA].
- The location rule of a possession or an equip task (EnforceLocationRequirement with a POITag; finding QR8C-R6) gates the gain only:
  outside its place the value does not rise, and it falls only with the items (the minimum of the count and the progress before). An item
  that the character has before it comes into the place does not complete the task outside (case QRW6) [data: the mission templates
  collect at a place and return to the giver elsewhere; live stage QG6b: the loot mission of the farm of Everfall; case QG6BC].
- PatrolTask (66): the dwell task of a patrol (the rules in [Mission boards](mission-boards.md)): a place task of the index "place",
  StayActive, its target the whole seconds of its Duration; its value is the time in a POI of its tags, summed across the visits [client
  0x3B094A0; case QG6BD].
- The hand-in of a mission completes its pending return tasks (the tag `returntogiver`) in its own plan (`Quests::hand_in` with the
  tag): a refused hand-in leaves them pending (code review of stage QG6a, finding QG6AR-R2).
- `RewardBonus`: the hand-in of a faction mission can have the additions of a reward modifier (the daily bonus): experience × (1 +
  experience), coin × (1 + currency), reputation × (1 + reputation), tokens × (1 + reputation + tokens), with the truncation and the floor
  of the preview 0x31F4630 [client].
- A reset (5600, and a TaskDie with ResetOnCondition) keeps the mission values of the instance (finding QG6AR-R3).
- CurrencyReward is the float32 cell of the data sheet (the rule of AzothReward): the source text 113.99999999999999 of
  CMission_Success_Crafting_ProvisionT1_Small is 114 [live: the town screen and its popup show 1.14 coin; the hand-in paid 114]. A
  fraction of the float32 cell (2500.75) stays unpaid (finding QG2R-R4; case QG6BA).
- The quest drop of a gather of a skinned body matches the gatherable row and the categories of its creature (the separate category, the
  categories of the base row, one direct parent of each; not the id of the vitals row) [client: the direct key 0x3B03A21 to 0x3B03A4E,
  the helper 0x5C75830; static read `static/gather-drop/GATHERDROP.md`; case QG6BF; gathering.md].

### The quest item sources of stage QG8

The census of the item tasks with no supported source (the item tasks of the objective rows that are not supported), the result, and
the live checks are in `~/Games/soulwarden/work/evidence/quests/qg8/implement/RESULT.md` (the census script `scripts/census.py`).

- A possession task (TaskHaveItems, TaskHaveAndReturnItems) drops its item at the gather of a quest object whose row its ItemDropVC names
  (a gatherable row of the quest form, a destructible or a proximity quest object), with the rules of the other drops (`Quests::roll_drop`)
  [client: the text of the item of TaskHaveItems 0x825F310 "This item will generate in gatherable loot and/or AI loot", the text of
  ItemDropProbability 0x825F470 "This includes AI loot, quest gatherables, or other gatherables"; the drop predicate 0x3B03890 has no
  gather-mode gate for the source type 4 (0x3B039CE to 0x3B039F3: the direct key, then the category helper; GATHERDROP.md); data: 70
  possession tasks name a quest gatherable row (the coffins of 02A_Side08, the weapon rack of 06A_IR_02, the grain sacks of 06A_MSQ_05),
  2 a destructible row (the pirate crates of 06A_MSQ_11, the rune tablet of 12A_MSQ_08)]. The subscription key "qd:" of the row; the
  gather of a quest object (`Gathering::complete`) and the destruction of a destructible quest object (`QuestObjects::destroy`) give
  `plan_drops` the flag `questObject`. Cases QG8A, QG8B.
  - The natural nodes and the bodies keep the gather-mode gate of the source types 2 and 7 (only the gather tasks) [agent: the source
    type of each producer is not found; no possession task of the data names a natural row; 7 name a skinning row whose name is also a
    vitals category (`wolf`, `boar`): their kill drop stays].
  - Before stage QG8 the destruction of a destructible object gave no drop to these two possession tasks (only the key of a gather).
- A quest row whose loot table gives nothing is a quest object (the rule of the loot table Empty): each entry of the table is an item (no
  [LTID] or [LBID] reference) whose quantity cell of the table NAME_Qty is 0 [data: QuestInteractable has the one entry FlintT1 with the
  quantity 0; 48 gatherable rows use it]. `tools/gamedata/gathering.py` (`Tables.empty_loot`, `quest_form`); 21 quest rows of the export
  lose the refusal "a loot container" (the stone tablets and the crystal cluster of 0502 to 0514, the bookcase, the map, the urine puddle
  of 13A_SQ_28, the bomb cargo of MSQ_0052). The gather of a quest row has no roll (the rule of the quest rows).
- An ItemReward of the form "[LTID]NAME" is a loot table [data: 149 game events (the lottery 10A_Q1_Lottery of C10A_Q1_Build, the
  Darkness, the arenas, the invasions); the prefix of the loot tables]. The activation of a TaskTriggerGameEvent rolls the table at the
  level of the character for each unit of ItemRewardQty (a blank quantity: 1) and gives the items of the roll, whole or not at all (a
  full bag: the task waits, and the next change of the bag rolls again) [agent: the quantity of a table entry is the count of its visits,
  the rule of the loot tables]. The largest quantity is 100 rolls (`kRewardTableRolls`, a work limit of one tick) [agent]: a larger one is
  refused, not cut (the task waits, and the manifest blocks it; code review of stage QG8, finding QG8R-R4). The data has the quantity 1 for
  each such event [data]. A table that the loot data does not have gives no item; the event applies. `gamedata::reward_table`,
  `Quests::Plan::apply_event`. The loot tool exports the tables of these events (`loot-tables.json` member `eventRoots`). Case QG8C.
  - The hand-in does not roll a table: a success event with such an item stays refused ("the item catalog has no reward item") [agent: no
    success event of the quest data has one].
- The support manifest has the same rules: the quest object rows of the world (`Index::questObjectRows`: the rows of the quest form that
  the backend gathers and the rows of the destructible and proximity objects) are drop sources of a possession task; the items that the
  table of the event of a TaskTriggerGameEvent reaches are items of the tree of its objective (the history decides; the rule of the world
  items: each entry of a reached table, no test of the tags) [agent: the project rule of the items of stage 1.8f]. Case QG8D.
- The reason of an item that no supported source gives names each declared source and the cause that the backend does not run it, and
  the member `itemSource` of the task has the class of the first one (the precedence of the census): `task drop` (the quest drop of an
  ItemDropVC entry: a refused row, a row that is not a quest object, a category with no creature of the open world and its kept-out
  cause), `chest drop` (no available chest in a point of interest of the tags, with the cause of a tag with no shape), `loot table` (a
  table of the loot data has the item and no source of the world reaches it; the roots above it: the loot of a vitals row, a loot
  container item, a chest root), `recipe` (the crafting refuses the recipe, with its cause), `none`. The count
  `itemTasksWithNoSource` of the manifest counts them by class (the objective rows that are not supported) [agent].
- Kept apart (each with its reason in the manifest and in the census of the result): the creature drops of an instance or of an
  encounter that the runner does not take, the creatures that the creature data keeps out (the client does not show the slice, an entry
  that the level data does not spawn), the chest drops of a tag with no shape or with no chest in its place (QG3 places), the artifacts of
  the season 8 loot containers (a season), the archetype weapons of the old introduction (an instance and its chest), the CraftingQuestRecipe
  work orders (a temporary recipe that is not known by default: the crafting has no rule of the recipe unlock), the seasonal server
  recipes (S8), the game event rewards of a gather that the gathering does not pay (the soul fragments of Gather_LostMemory: a collectible
  progression, a standing, an achievement), the rows of an instance or of a raid (the gems and the ice of the Great Cleave, the flowers of
  C10A_M4), and the items with no declared source.

### The corrections of the code review of stage QG8 (in stage DG1)

The findings: `~/Games/soulwarden/work/evidence/quests/qg8/code-review/FINDINGS.md`. Each case fails on the tree of stage QG8.

- QG8R-R1 and QG8R-R2 (case LT8, the cases `event-child` and `shared-event-root` of `check_loot_tool.py`): the loot tool keeps the required
  references (a child table or a bucket of a table of the document) apart from the optional roots of a container and of an event, and it
  finds the absent optional roots before a change of a list ([Game data](../development/game-data.md)). The export of the supported build
  does not change.
- QG8R-R4 (case QG8R4): an ItemRewardQty of a loot table above 100 rolls is refused, not cut: no item, the event task waits, and the support
  manifest blocks the task with the cause; 100 rolls give 100 items.
- QG8R-R5 (case QG8R5): the support manifest indexes the items of an event only for a quantity that the world gives (above 0; for a loot
  table not above 100): an event with ItemRewardQty 0 completes and gives no item, and it is no source of an item task.
- QG8R-R3 (rejected in stage DG1): the cause text of the two tasks of `17_CQ_SoulFragment` needs the item reward of the gather events
  (`Gather_LostMemory_NN`) in the game data; the events of `gathering-data.json` keep only a progression and its amount. A change of the
  format of that file belongs to the stage of the collectibles. The tasks stay unsupported with the class `none`.

### The corrections of the code review of stage QG6b (in stage QG8)

The findings and their cases are in [Mission boards](mission-boards.md) (`tests/review_qg6b_tests.cpp`). The rules of the quest system:

- QG6BR-R3: the experience of the daily bonus of a faction mission is the carded experience of the event (the GainXP card and its
  truncation) times the daily factor in f32; `Plan::cardedExperience` keeps it out of the card product of the commit (QG6BR3).
- QG6BR-R6: the categories of a skinnable creature are gather drop sources of the manifest only when the backend gathers its skinning row
  (no refusal cause, no rank gate of a trade skill that is not a gathering skill: the rule of the other gather rows) (QG6BR6).
- QG6BR-R7: a PatrolTask publishes its progress only at an inside sample (QG6BR7).

### The corrections of the code review of stage QG7 (in stage QG6a)

The findings QG7R-R1 to QG7R-R7 (`~/Games/soulwarden/work/evidence/quests/qg7/code-review/FINDINGS.md`) are corrected in stage QG6a;
each case fails on the tree of stage QG7 and passes now (`tests/review_qg7_tests.cpp`; QGR7G is a control that passes on both). The
rules of R1 and R7 are in [Factions](factions.md), the rules of R3 and R4 in [Encounters](encounters.md).

- QG7R-R1 (QGR7A): a faction change writes the faction row, the cost, and the end of each quest that the change ends in one transaction
  (`Quests::faction_ends` with the commit hook). A refused quest row changes nothing: the faction, the Azoth, and the old trial stay.
- QG7R-R2 (QGR7B, QGR7C): a refused automatic hand-in (a row with no destination person) stays pending. A store failure tries again
  after 250 ms of the world clock (`kSelfHandInRetry`); a full bag or a balance that is too low waits for a change of the items or of the
  progression (`m_selfHandInWaits`). The count `selfHandInRetries` is in the counts of the quest system.
- QG7R-R5 (QGR7F): a history of the support manifest keeps its earned categorical progressions (the faction reputation and tokens) at
  the RequiredProgression checks of a person offer. No count changes.
- QG7R-R6 (QGR7D): a faction change alone (no other change) tests the pushed rows of the faction gates again (`PushWatch.faction`).

### The corrections of the code review of stage QG5c5 (in stage QG7)

The findings QG5C5R-R1 to QG5C5R-R5 (`~/Games/soulwarden/work/evidence/quests/qg5c5/code-review/FINDINGS.md`) are corrected in stage QG7;
each case fails on the tree of stage QG5c5 and passes now (`tests/review_qg5c5_tests.cpp`). The rules of R2 to R4 are in
[Encounters](encounters.md), the rule of R5 in [Gathering](gathering.md).

- QG5C5R-R1 (QGR5C5A): the kinds of the parts of a group member (`placement.partKinds`) are kill sources of the manifest. No count
  changes.
- QG5C5R-R2 (QGR5C5B): DestroySliceAction removes a group part also through the body path of the selected creature.
- QG5C5R-R3 (QGR5C5C): a count of an earlier object is refused when a KillSpawnsAction or a DestroySliceAction removes that object before
  the stage of the consumer (the runner and the exporter `tools/gamedata/encounters.py`, case `removed-earlier` of
  `check_encounters_tool.py`). `encounter-data.json` of the game data does not change.
- QG5C5R-R4 (QGR5C5D): the end of a parent stage runs the end actions (killSpawnsAtEnd, destroyAtEnd) of a started child that it ends.
- QG5C5R-R5 (QGR5C5E): one reward for two gathers of an encounter object with an ordinary row: the first completion claims the object,
  and a claimed object is contested until it goes.

### The corrections of the code review of stage QG5b (in stage QG5c1)

The findings QG5BR-R1 to QG5BR-R3 (`~/Games/soulwarden/work/evidence/quests/qg5b/code-review/FINDINGS.md`) are corrected in stage QG5c1;
each case fails on the tree of stage QG5b and passes now (`tests/review_qg5b_tests.cpp`).

- QG5BR-R1 (QGR5B1): the loader of the gathering data refuses a row of ItemRequired with an item and no member `itemCount` (a count of 0
  would need nothing): the empty bag gets no gather. A blank quantity cell of a table stays the count 0 of the tool (no item gate).
- QG5BR-R2 (QGR5B2): a refresh of the quests that changes a task (an item, a place, a level) settles the indexed achievement waits of the
  character again: a TaskAchievement whose NPA has an ObjectiveFlow of that task follows it (no bit changes). The wait of a second
  character stays (MP-10).
- QG5BR-R3 (QGR5B3): the manifest keeps the ItemRequired item of a producer with a rank gate (AzothStaff): the task needs the rank and the
  item. In the supported build `1625_0701_challengingpharaoh` task 43 (the Code Pillar) now needs `1625_0601_eyeofhorus` (a quest item): no
  count changes.

### The corrections of the code review of stage QG5a (in stage QG5b)

The findings QG5AR-R1 and QG5AR-R2 (`~/Games/soulwarden/work/evidence/quests/qg5a/code-review/FINDINGS.md`) are corrected in stage QG5b;
each has a case that fails on the tree of stage QG5a and passes after the correction.

- QG5AR-R1 (the tool case `person-slice-expression` of NC1): a required achievement of the slice (m_requiredAchievementOverride) that is an
  expression (!, &&, ||, parentheses) is the member `conditional` of the bound (the rule of a variation), not one unresolved achievement id
  (`interactions.py`, the common bound writer; 50 bounds of the build, among them the Morgen gravestones of `99B_MSQ_06_lostcathedral` and
  the spyglasses of Dunwood; the unresolved bounds go from 80 to 30).
- QG5AR-R2 (QGR5A1, QGR5A2): an accepted position report with no movement is a look of the proximity objects too (`World::report`): a task
  that becomes Active, or an object that respawns, while the character stands in its sphere is found at the next tick. The set of the
  marks makes one look for each character at a tick (the cells of the grid near it, as for a move).

### The corrections of the code review of stage QG4 (in stage QG5a)

The findings QG4R-R1 to QG4R-R3 (`~/Games/soulwarden/work/evidence/quests/qg4/code-review/FINDINGS.md`) are corrected in stage QG5a; each
has a case of `tests/review_qg4_tests.cpp` (QGR4A1 to QGR4A3) that fails on the tree of stage QG4 and passes after the correction.

- QG4R-R1 (QGR4A1): an equip is a new item instance in a paperdoll slot with the action ItemEquipped (`Inventory::finish` compares the
  instance of the slot before and after the commit). The action stays on a slot after its equip: a perk edit of an equipped artifact is no
  equip and grants no artifact quest.
- QG4R-R2 (QGR4A2, the tool case `person-flow` of NC1): the NPA keeps its ObjectiveFlow (column 2 of the row); an NPA with a flow was false,
  also when it has a bit conditional, and the persons whose presence names such an NPA were not resolved (211 persons of Nighthaven). Stage
  QG5b has the rule of the flow (above): QGR4A2 still passes (its flows match nothing: the character has no instance), and the tool case
  `person-flow` expects a resolved binding now.
- QG4R-R3 (QGR4A3): the loaders of `trigger-data.json` and `poi-data.json` refuse a corner of a volume or a shape point outside 1e9 m before
  a cell number is made of it; the count, the check, and the grid use one span of each volume (`cell_span`). A far volume of `set` (a test)
  goes into the list of each lookup.

### The corrections of the code review of stage QG3 (in stage QG4)

The findings QG3R-R1 to QG3R-R4 (`~/Games/soulwarden/work/evidence/quests/qg3/code-review/FINDINGS.md`) are corrected in stage QG4; each
has a case of `tests/review_qg3_tests.cpp` (QGR3A1 to QGR3A4) that fails on the tree of stage QG3 and passes after the correction. No
correction changes the files of the supported build or a count of the manifest.

- QG3R-R1 (QGR3A1): a trigger volume with no world of its own is of the world of the file (the member `world` at its root; no member: the
  open world). A file of an instance never gives a volume to the open world.
- QG3R-R2 (QGR3A2): a volume with no height limit whose local height axis is not vertical has no horizontal limit: it is in a short list
  (64 at most in a file) that each lookup of the place view tests, and the place view agrees with the predicate of the runtime.
- QG3R-R3 (QGR3A3): the loaders of `poi-data.json` and `trigger-data.json` count the cells of a shape in double before the grid: a shape
  of more than 16,384 cells (a square of 32 km) or a file of more than 2,000,000 cells is refused (the backend starts with the default of
  no data); a lookup far outside each world converts no large value. A test volume of `set` that is too large goes into the list of each
  lookup. In a process limit of 384 MiB the case passes; on the tree of QG3 it stops with `std::bad_alloc`.
- QG3R-R4 (QGR3A4, `check_triggers_tool.py`): the duplicate key of the trigger tool has each value of the predicate (the axes, the sizes,
  the radius, the height, the height rule, the filter): two volumes with one tag and one center and different axes stay two volumes.

### The corrections of the code review of stage QG2a (in stage QG3)

The findings QG2R-R1 to QG2R-R5 (`~/Games/soulwarden/work/evidence/quests/qg2a/code-review/FINDINGS.md`) are corrected in stage QG3; each
has a case of `tests/review_qg2a_tests.cpp` (QGR2A1 to QGR2A5) that fails on the tree of stage QG2a and passes after the correction.

- QG2R-R1 (QGR2A1): the categories of a known base row with the member VitalsCategories are its list, also when it is empty [client: the
  drop helper returns false for an empty base list, 0x5C758FE; the kill tests the base id apart, 0x3B1A057]. Only a row of a file of
  before stage 1.8c (9 members: `LootVitals::hasCategories` false) uses the vitals row as its category.
- QG2R-R2 (QGR2A2): the location rule of the manifest applies only to the task types whose runtime path tests it (the rule of the
  section of stage QG2a above).
- QG2R-R3 (QGR2A3): a loot container that the runtime refuses to open (`ItemContainers::openable`: a salvage event with a reward that has
  no owner) is no source of the items of its table in the manifest.
- QG2R-R4 (QGR2A4): a CurrencyReward with a fraction (2500.75) stays unpaid; the open of its container is refused with no change.
- QG2R-R5 (QGR2A5): AzothReward is the whole text of a number of 0 or more; a negative cell (-0.5) and a text after the number (37junk)
  stay unpaid.

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
  them, and a move evaluates them again). Stage QG6b: it gates the gain only; outside the place a satisfied task keeps its value (the
  section of stage QG6b).
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
tools/world quest places CHARACTER              # stage QG3: the territory, the points of interest, the areas, and the trigger volumes at its position
tools/gamedata/pois.sh OUT.json --quests QUESTS # stage QG3: with the comparison of the tags of the place tasks (member tasks)
```

The admin interface has the route `GET /admin/v1/catalog/quest-support` (stage 1.8e: the support manifest), the commands `apply_backstory` (argument `template`), `quest_rewards` (arguments `objective` and `territory`; stage 1.8d), `quest_places` (no argument; stage QG3: a reader), `abandon_quest` and `reset_quest` (argument `instance`, 16
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
- Stage QG2a: the push grants its rows also to an existing character at its next entry: the development character of level 65 got five
  quests at once (two main story rows of Dunwood, three vectors). An abandon of a push row gives it back at the next tick (its
  `_pushedonce` bit, or its requirement, is true again).
- Stage QG2a: the HUD of the client shows its own list of pins: a quest of a command (`tools/world quest grant`) can be pinned on the server
  and not on the HUD when the client has many quests. Read the counter of a task in the journal (J, the category of the quest).
- Stage QG2a: a kill task with EnforceLocationRequirement and a tag with no shape never counts: the manifest blocks such a row. Choose a
  quest of the manifest of the same tree for a live check. (Stage QG3: the tag `brightwood8` of the bears of `02A_Side06_01_Bears` has
  its area shape now, and the row is supported.)
- Stage QG2a: in a live fight with the journal open the creatures still attack: the development character died at the ruins of
  `ancient_ruins_40214`. Take the pictures with `tools/world ignore CHARACTER on`, then turn it off for the kill (the mode gives no credit).
- Stage QG3: a POI shape can fit near its plot and still fail the fit rule: 79 tags of the tasks have such a shape (40116 Shattered
  Obelisk: 355 of 406 entries inside at the plot, 112 wrong; 40268 Green Defile: 96 of 100 inside, 17 wrong). The file and the entries of
  the region metadata are of different builds of the place, or the origin is not a plot. Do not relax the fit for them: a wrong origin
  credits a quest at a wrong place. They stay in `unshaped` with their cause.
- Stage QG3: the fit of a shape with few entries is weak evidence: 86 shapes of the file have fewer than 10 entries in them (the influence
  towers have 1 to 9). The only plot that agrees is the origin for 84 of them (`candidates` 1), but check such a place in the client
  (`tools/world quest places`, the cue 2297) before a live test relies on it.
- Stage QG3: an area file and its row have names that do not agree for 40 of the 171 files (the folders `monarchsbluffs` and
  `queensport`, the V2 and FFA rows of Windsward Shores): do not bind an area file by its name. The entries decide.
- Stage QG3: the client can give its cue of an area some meters before the border of the data (live QG3: the entry and the exit of
  9027 Faith's Bounty 6.5 m west of the common border of 9027 and 9022 in the data, both directions; the cues of 9022 1.9 to 2.5 m from
  it; the watchtower 30220 within 1 m). The server completes a task of the place when the accepted position crosses the border of the
  data: up to about a second after the cue. Read the place view (`tools/world quest places`), not only the cue, for a check of a border.
- Stage QG5a: the sphere of a trail is small (0.75 m around a point near the ground): a walk must go over the trail. A walk 2 m beside it
  finds nothing (live I2: the first walk passed 2.1 m north). Read the place with `tools/world list` and walk on the line of the trail.
- Stage QG5a: the HUD tracks only the pinned quests: read the counter of a chest task in the journal (J, the category of the quest).
- Stage QG5a: a glyph tablet keeps its prompt "E UNSEAL ANCIENT GLYPH" after its gather (no instanced slice); a second gather in the 25 s
  of its respawn is refused (`gatheredQuestObject`). An instanced quest object (the trapped boar) goes for the character.
- Stage QG5b: the client shows "E UNSEAL ANCIENT GLYPH" also with the rank 4 of AzothStaff, and the server refuses the gather of a glyph of
  rank 5 (`rank`): the client starts nothing. Read the rank with `tools/world tradeskill` or the log, not the prompt.
- Stage QG5b: a hand-in that gives an AzothStaff rank shows no rank line in the reward notice (ShowAsObjectiveReward false of the row):
  read the log line "N AzothStaff experience (rank R)" of the hand-in.
- Stage QG5b: the reward of `99G_MSQ_SM_PrepareWarfare` opens the next offer of Father Russo in the same conversation: the key E on the
  next page accepts it (live I3: "Walls Came Tumbling Down" was granted).
- Stage QG3: a travel can put the character in the discovery sphere of a POI (DiscoveryRadius, 150 to 1000 m): the client sends 2297
  "entered" for it and the shape of the data says outside (live QG3: the shipwrecks 30215 and 30216 at 177 m and 127 m, radius 200). That
  is no disagreement of the shape.
