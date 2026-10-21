# Quests

Read this document before a change of the quests of a character: the quest data, the task trees, the quest system of the world, the
achievement actions of a task, the start quest of a new character, the backstory templates, the Objectives state, or the pins. Stage 1.8a
is the first part of the generic quest engine (roadmap row 23). The persons, the offers, the hand-in, the cutscenes, the whispers, and the
trigger areas are stage 1.8b; the counters and the world predicates 1.8c; the rewards, the availability, and the recurrence 1.8d. The
achievements are in [DLC and entitlements](dlc-and-entitlements.md), the wire contracts in the protocol notes (section 4.53).

## Start here

| Task | Start here |
| --- | --- |
| The quest data (objectives, compiled trees, task cells, achievement table, territory starts, backstories) | `tools/gamedata/quests.py`, `quests.sh`, `check_quests_tool.py` (case QG-TOOL), `apps/backend/src/gamedata/quest_data.h` (case QG11) |
| The rules of the tasks, a grant, the change event, a timer, the commit | `world/quests.h`, `world/quests.cpp` (`Quests::Plan`), cases QG7, QG8 |
| The start quest of a new character | `Quests::new_character_rows`, `server/http/gateway_routes.cpp` (`create_character`), `Quests::entered` (`run_pending`), case QG3 |
| The rows of the quests | `state/store.h` (`QuestRow`, `QuestTaskRow`, `QuestGrantRow`, `QuestUndoRow`), schema 15, case QG4 |
| The Objectives state (slot 22) and the seasons readiness state (slot 73) | `server/rep/states/objectives_state.cpp`, the gate of 2709 in `rep_session.cpp` (`handle_auto_pinning`), cases QG3, QG10 |
| The pins of the client (1458) | `RepSession::handle_tracked_objectives`, `Quests::client_tracked`, case QG10 |
| A backstory template for an existing character (decision D2 option C) | `Quests::apply_template`, `server/http/quest_commands.cpp`, case QG9 |
| The research | `~/Games/soulwarden/work/research/quest-engine.md`, `quest-engine-3.md`, `quest-display-18a.md`; the result of the stage: `~/Games/soulwarden/work/evidence/quests/18a/implement/RESULT.md` |

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
  Each other type stays active: its supplier is a later part.
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
- The cost: an achievement change of a character visits the active TaskAchievement tasks of that character only; a timer is one timer of
  the world clock; an entry loads the rows of that character (case QG2: 50 characters with 20 waiting quests, each event visits 20 tasks)
  [project rule MP-8].
- The change event: each change of the bits of a character that the quest system did not make (a lore page, a first craft, a build
  preset, the DLC, a development command) calls `Quests::achievements_changed` [agent: the one path of a bit change].
- An unlock action with no StayActive and a lock action with no StayActive write an undo row; a temporary unlock always writes one (lock at
  the end of the quest). Stage 1.8a only writes the rows: the abandon and the reset are stage 1.8c [research quest-engine.md Q1;
  agent: StayActive is the prevention of the undo].
- TaskTimer: the start is a time of the synced wall clock (Unix nanoseconds) [client 0x3A41440, 0x6830A40]; the end is the start plus
  Duration with its fraction (25.4 s) on the world clock [client 0x4461150: the client keeps the fraction]. The time while the character
  is away counts: the next entry ends a timer whose end is past [agent: the client shows start + Duration against the synced wall clock;
  decision D4 of the user is open]. FailOnCondition: the end of the timer fails the task (5); the failure of the objective is stage 1.8d.
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
  state goes back to the client [research quests.md 6.2]. A list with a foreign instance changes nothing.
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

## Commands

```sh
tools/gamedata/quests.sh [OUTPUT.json]          # the quest data (a test: a path outside the common game data folder)
tools/gamedata/quests.sh --objective 06_getoffbeach   # print a tree (no file)
tools/world quests CHARACTER                    # the quests, their tasks, the pins, the one-time grants, the counts
tools/world quest templates                     # the backstory templates and their start objectives
tools/world quest template CHARACTER TEMPLATE   # decision D2 option C (the command apply_backstory)
tools/world quest grant CHARACTER OBJECTIVE     # development only: a new instance of an objective
tools/world achievement CHARACTER ID on|off     # development only: the change event, the lock of a last bit
```

The admin interface has the command `apply_backstory` (argument `template`), the route `GET /admin/v1/catalog/backstories`, and the member
`quests` of the state of a character. The tool window has no control for them in this stage.

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
