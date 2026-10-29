# Mission boards

Read this document before a change of a mission board: the board object of a settlement, its open, the town offers, the accept of a
mission (3819), the mission instance, its return task, its hand-in (2461), the town entry of a character, the override cells of a
mission row, the faction boards (3816, the daily bonus), the patrols, or the mission data. The wire contracts are in the protocol notes
(sections 4.64 and 4.65). Stage QG6a made the town board of a settlement with the offers of the always available project (roadmap 4.5).
Stage QG6b added the override rules of the task factory (the deliveries, the loot and hunt missions, the timers, the patrols), the
faction boards of the representatives with the daily bonus, and the corrections of the review of QG6a. The projects of a town, the PvP
missions, and the outposts are later stages (5.4).

## Start here

| Task | Start here |
| --- | --- |
| The mission data (`mission-data.json`) and its tool | `tools/gamedata/missions.py`, `tools/gamedata/check_missions_tool.py`, [Game data](../development/game-data.md), `gamedata/mission_data.h`, test cases QG6A-TOOL, QG6AA |
| The effective objective of a mission (the override of a kill task) | `QuestData::bind_missions`, `QuestData::mission` (`gamedata/quest_data.h`), test case QG6AA |
| The board object: its entity, its TerritoryInterface state (slot 3, type 1746) | `Interactions::make_boards` (`world/interactions.cpp`), `server/rep/states/territory_interface_state.cpp`, test case QG6AC |
| The world entity and its LandClaimManager state (slot 2, type 3086): the claims of the towns | `World::world_entity`, `Missions::child_spawned`, `server/rep/states/land_claim_state.cpp`, test case QG6AM; the static reads `static/landclaim/LANDCLAIM.md`, `static/landclaim-owner/OWNER.md` |
| The open (action 51), the provider state (slot 10, type 3829) | `Missions::opened` (`world/missions.h`), `server/rep/states/objective_interactor_state.cpp`, test case QG6AC |
| The seed and the selection of the town offers | `town_seed`, `Missions::town_offers` (`world/missions.cpp`), test case QG6AB |
| The accept 3819 and its checks | `Missions::accept`, `RepSession::handle_board_request`, test case QG6AD |
| The kill task, the return task, the hand-in 2461, the rewards, the town entry | `Missions::complete`, `Quests::hand_in` (the commit hook), test cases QG6AE, QG6AF |
| The store (schema 19), the restart, the abandon | `state/store.h` (`MissionOfferRow`, the mission columns of `QuestRow`), test cases QG6AG, QG6AH |
| Two characters at the board (MP-10), no mission data | test cases QG6AI, QG6AK |
| The support manifest of the mission templates | `world/quest_support.cpp` (`BoardMission`), test case QG6AL |
| The static read of the offers and the live runs of the stage | `~/Games/soulwarden/work/evidence/quests/qg6a/implement/` (`static/offers/OFFERS.md`, `RESULT.md`); the research `~/Games/soulwarden/work/research/mission-boards.md` |
| Stage QG6b: the override cells of a mission row (the task factory) | `QuestData::apply_overrides` (`gamedata/quest_data.cpp`), the typed cells of `MissionRow` (`gamedata/mission_data.h`), test case QG6BA; the static read `~/Games/soulwarden/work/evidence/quests/qg6b/implement/static/factory/FACTORY.md` |
| Stage QG6b: the faction board (the open at a representative, the offers, 3816, the hand-in, the daily bonus) | `Missions::faction_opened`, `faction_offers`, `select`, `complete_faction` (`world/missions.cpp`), `server/rep/states/game_event_state.cpp`, test case QG6BC; the static read `static/faction-board/FACTION.md` |
| Stage QG6b: the patrols | the place look of `Quests::refresh` (the dwell of a PatrolTask), test case QG6BD |
| Stage QG6b: the delivery at a town board, a refused hand-in | `Quests::hand_in` (the return tag), test cases QG6BB, QG6AR2 |
| Stage QG6b: the corrections of the review of QG6a (QG6AR-R1 to R7) | `tests/review_qg6a_tests.cpp` (QG6AR1 to QG6AR6); the fixture `tests/support/mission_fixture.h` |
| Stage QG6b: the live runs and the result | `~/Games/soulwarden/work/evidence/quests/qg6b/implement/RESULT.md` |

## Rules

The source of each rule is in brackets: [data] the game data, [client] the client code (with its address), [live] a live check of
stage QG6a, [agent] a rule of the project that an agent chose.

### The board object

- The town board of a settlement is a child that the client makes itself at the load of the level: the spawner `Town_Project_Board`
  of the settlement slice, with m_isClientAuthoratative false and m_spawnOnInitialize true [data: the slice of the settlement]. The
  client gives it the client id of its place (the rule of a person child: client 0x6160B60) and the actor G and 8 zero bytes [live:
  the open 1589 of the board of Windsward names the id 95f0af0ac1092587 and the target 8750122fcacd363c, the values of this rule].
- The interaction data (format 5) has each board as an object of the kind `missionBoard` with that id and the member `board` (the
  slot of its TerritoryInterface component and its provider component). The build has 13 boards.
- The backend makes the entity of each board one time, at the first scan (`Interactions::make_boards`). The entity has the actor of
  the client child. Each session near it gets its record: the metadata state, the Interact state (slot 2), and the TerritoryInterface
  state (slot 3), for the own board of the client [live: with the record the board opens, 3743 result 1]. The entity stays
  while the backend runs, as a level object with a state of the server [agent].
- A board of the backend as a server child (an entity of the id allocator) is a second entity for the client: the client opens its own
  board, and the backend refused that id [live: the first live run of stage QG6a]. Do not make a board as a server child.
- The board has the component Interaction with the option `OpenMissionBoardOption` (action 51, committed) [data]. The mission system is
  the handler of the kind `missionBoard`: the open is a request of the interaction system (the checks of range, of the entry, and of the
  committed interaction), and the answer is 3743 with result 1 [live].
- The territory of a board is the territory of its place (Windsward: 6) [data]. The projects of a town are its always available
  projects: the projects of the type AlwaysAvailable whose ProgressionLevel is at most the level of the town (`CommunityProjectId`)
  [data; agent: the backend has no town project state; the other project types are stage QG6b].
- The actor and the claim of a board do not change. The spawn observer (`SpawnObserver::child_spawned`) gives the mission system each
  board entity at its start: the projects of its town, the claim of its territory, and the destination of each mission of that board
  [agent].

### The world entity and the claims of the towns

- The town screen of a board (`townprojects.luac`) lists a project only when the LandClaimManager data of the claim of the territory of
  the player has it as an active project [client: `IsActive` of `territorydatahandler.luac` -> LandClaimRequestBus
  GetTerritoryProgressionData 0x2F84390; live: with no such data the screen opens and lists nothing].
- The data is the LandClaimManager state (type 3086, slot 2) of the world entity `slices/globalgde.dynamicslice` (asset
  F1855F94-8894-55D7-A6B0-D003EC278126, sub id 2). The backend makes the world entity at the first board (`World::world_entity`, an
  object entity with an actor of the id allocator). Each session observes it at each distance, and it stays while the backend runs
  [agent].
- Each board adds the claim of its territory (`Missions::child_spawned`): the claim key, and a progression summary with the always
  available projects of the town (type 6 AlwaysAvailable, state 0 Active, no upgrade, territory level 0) [client: the element 0x2F6AA40;
  agent: the values of a town with no project state]. The claims are in the order of the territory ids.
- The client reads several fields of the state for a claim by its index with no check of the length: the owner (field 16, the getter
  0x2F81310), the governance, the conflict, the darkness, the influence, and the war threshold [client: OWNER.md section 1]. A territory
  entry reads the owner of the claim at once (0x2A09BA0 -> 0x29FF700). Thus each of the fields 2, 3, 4, 5, 6, 7, 9 to 13, 15, 16, and
  17 has one element for each claim. The values are the quiet values of a territory with no owner company, no war, and no conflict
  (zero; an owner with 16 zero bytes as its company id) [agent; live: a travel into a territory with a claim, no fault].
- The backend sends no position of a claim (field 14) and no faction control data (field 8): with a position the land claim manager is
  "available", and the map and the HUD read more data (a later stage) [client: 0x2F848A0; live: a probe with a position stopped the
  client at once].

### The claim of a board: the provider identity and the destination

- The provider identity of a town board is the actor id of the Claim landmark of its territory, not the actor of the board [client: the
  provider component selects the landmark of type 4 of its territory and keeps it at +0x108, 0x3A47881; the static read
  `static/destination/DESTINATION.md`; live: with this identity the board still lists its offers].
- The interaction data has the claim of each board (the member `board.claim`): the CommunityGoalProvider landmark (type 8) at the
  place of the board gives its territory, and the Claim landmark (type 4) of that territory its ActorId (`TerritoryLandmarks` of the
  region files) [data]. Windsward: territory 6, claim `4b657e1a68e06c9f0000000000000000` (the placement
  `Darkness/Zone_CapitalPlot_SouthWindsward`). A board with no claim uses its own actor (the count `boardsWithNoClaim`; 0 in the build).
- The destination of a town mission (destinationOverride of its element) is that claim [client: the town screen keeps a community goal
  only when `GetDestination` finds its destination in the claims or the outposts of the map with the territory as the settlement,
  `GetObjectivesForTerritory` of `territorydatahandler.luac`; live: with the actor of the board the accepted card showed "A new
  mission will be available soon"].
- The completion check of the client compares the destination of the return task with the provider identity of the provider state
  (CanCompleteMission 0x4954180, 0x49544C5 to 0x49544E2): both are the claim [client].

### The open and the provider state

- The open gives the character the provider state (the ObjectiveInteractor state, slot 10 of the player slice, type 3829): the 16
  bytes of the provider identity of the board (its claim), the expiration, no explicit mission, and the town entries of the character
  [client: the readers 0x279E260, 0x279E480, 0x3A68000, 0x3A68C60].
- The expiration is the start of the next seed (server clock nanoseconds) [agent: the offers change there; the client reads the time
  as GetTimeTillNextProviderUpdate 0x4958040].
- The open gives the character the service `missionBoard` of the board (`Interactions::grant_service`, its revision is the seed). The
  service ends with the range, the end of the interaction, and the removal of the board, as each service [agent: the rule of the
  services].
- The open does not complete the return task (CM_ReturnToGiver: TaskInteract with the tag `returntogiver`). It stays Active until the
  hand-in: the client offers the completion only while the progress of the container is its child count minus one, with the return as
  the last child [client: the ready check 0x3A3CC50, 0x3A3CDC2 to 0x3A3CDEC; live: after a return at the open the card showed "Start
  Mission", and with the return pending the card shows "COMPLETED"]. The hand-in 2461 at the provider board completes the return, then
  pays (below) [agent].
- The provider state is for the owner only, and only after the first open in this world entry. An observer gets no fragment: the
  remote player slice has no ObjectiveInteractor component [data: the slice].

### The town offers

- The client computes the offers itself. The server computes the same list with the same rule and accepts only an offer of the current
  seed [client: GetCurrentCommunityGoals 0x4955A90 -> 0x3A34C70 -> 0x3A362A0; static read OFFERS.md; live: in five seeds the town screen
  listed the two offers that the backend computed, for example "Wolves Hunters Needed" (`CommunityProjectId_1_Wolf_5`) and "Alchemists
  Needed" (`CommunityProjectId_1_PotionManaT1_20`), with the countdown of the backend].
- The seed [client: 0x3A36220, 0x39D2A70]: the low 32 bits of FNV-1a 64 of the characterId text, plus the territory, plus the server
  clock minute rounded to the interval with the half-interval phase (a signed 32-bit value), plus the seed addition. The interval is
  30 minutes (the default of the CVar `javelin.territory-progression-mission-cooldown-min`, 0x61743A9); the addition is 0 (0x3A1C52A).
- The server clock of the seed is the clock of the time sync 335 (steady clock nanoseconds). The countdown of the board is
  `60 × (P − ((m + P/2) mod P)) − s` (GetSecondsToNextSeed 0x4957F20).
- The selection [client: 0x3A36539, 0x3A36809 to 0x3A3683D]: for each active project, the LCG of java.util.Random starts from
  `(projectCrc + seed) mod 2^32` xor 0x5DEECE66D. Each goal bucket with candidates takes one step
  (`r = (r × 0x5DEECE66D + 11) mod 2^48`); the offer is the candidate `(r >> 16) mod count`. The buckets are in the order of the
  client: Kill (4), Gather (1), Explore (5) (`townBuckets` of the mission data).
- The candidates of a bucket are its missions in the order of the client that pass the filter 0x3A09900 [client]:
  - the project of the mission is allowed: the same ProjectType as the active project, and a ProgressionLevel at most its level;
  - the territory rule of the mission (AvailableTerritoryID: a list, or `!N`) passes the territory of the board;
  - MinLevel and MaxLevel pass the level of the screen [client: the request 0x4955CAF takes the getter 0x3CEC5C0, the raw level plus the
    bonus level plus 1 (0x3CEC5CA); code review of stage QG6a, finding QG6AR-R1; case QG6AR1];
  - the trade skill rank of RequiredTradeskill is at least TradeskillLevel;
  - the achievement conditions of the row pass.
- An offer is unavailable only when the stored seed of the town entry of the character is the current seed and the mission is in its
  unavailable list [client 0x3A3684C]. A new seed makes each mission available again.
- One character id gets the wolf offer `CommunityProjectId_1_Wolf_5` at Windsward in about 1 of 53 windows (the Kill bucket has 53
  candidates for a new character). Two windows in a row never give the same Kill offer for one character: the LCG step of the next
  window moves the index by about 21 [agent: a calculation with the rule of the client].
- The backend puts no mission into the unavailable list to force an offer [rule of the stage].

### The accept (3819)

- The client sends 3819 (missionId:u32, destinationOverride:u32) to the ObjectiveInteractor component
  (`RootPlayer/006554ad-a98d-48e4-a5d6-f8a0ab3e7933`) [client: the sender 0x494E700, the writer 0x3274450]. There is no answer
  message: the Objectives state and the provider state give the result.
- The checks, in this order; the first refusal wins (`MissionError`):
  1. The backend has the mission data and the quest data (`noData`).
  2. The character of the session is in the world (`notInWorld`).
  3. The character has the service of a board: an open, the range, the same board (`noProvider`).
  4. The mission is an offer of the current seed of that board (`notOffered`): an old seed, a forged id, or an offer of another board
     is refused.
  5. The offer is available (`unavailable`).
  6. The character has no active instance of the mission (`alreadyActive`).
  7. The backend can run the mission (`unsupported`: a refusal of `QuestData::mission`).
- The second argument of 3819 is the project of the card: the card of the town screen sends the project id of the card
  (`townproject_activeproject.luac` OnTaskStart: `RequestCommunityGoalSelection(missionId, projectId)`), the detail popup sends 0
  (`townprojects.luac` OnTaskDetailCallback) [client; live: the click of a card sends 0x2591C412, `CommunityProjectId`]. The backend
  accepts 0 or the project of the offer, and refuses another value [agent].
- A refusal changes nothing and marks the provider state and the Objectives state: the client shows the state of the server again.

### The mission instance

- The accept makes a new instance of the template objective of the mission in the Objectives state (slot 22, type 3857, group 1): the
  objectiveId of the template, a new instance id, the missionId, the destination (the claim of the board: see above), the
  origin territory of the board (Windsward: 6), and isCommunityGoal 1 [client: the element reader 0x5B52F60].
- The effective objective: the client builds the tree of the template with the override cells of the mission row (the factory
  0x4461150). The backend binds each mission row to an effective objective (`QuestData::bind_missions`): a kill override
  (KillEnemyType, KillEnemyQuantity) changes the TaskKillContribution leaf of the template, with the target category and the quantity
  of the row. `CommunityProjectId_1_Wolf_5`: kill 5 of the category Wolf, then return to the board [data].
- Stage QG6b: the other override cells have their rules (section "The override cells of a mission row"). A row with a cell that has
  no rule is refused at the accept, and the manifest names the cause [agent].
- The instance stores its mission, the community flag, and its provider (the key of the board placement) in `character_quests`
  (schema 19). The destination is not stored: the world gives it at the world entry (the claim of the board of the key).
- The kill task counts a kill with the rules of the kill credit of the quests (quests.md): the vitals categories of the victim.

### The hand-in (2461)

- The client sends 2461 (instance:u64) at the board, one for each ready mission [client: the sender 0x494E910, the reader 0x39E8A10;
  live: a click on the card "COMPLETED"].
  The checks: an active mission of the character with this instance (`unknownInstance`: a forged instance, a second hand-in), the
  service of the board that is the provider of the instance (`noProvider`: another board), the source of the service; then one plan of
  the quest hand-in: the return task completes in it (`Quests::hand_in` with the tag ReturnToGiver), then its checks (a ready root, the
  items of the return tasks, the bag, the rewards). A refusal changes nothing: the return task stays pending, and the client still
  offers the completion [code review of stage QG6a, finding QG6AR-R2, the open problem 3 of QG6a; cases QG6AR2, QG6BB; live: a refused
  hand-in kept the card "COMPLETED"].
- The rewards are the success event of the mission row (SuccessGameEventIdOverride), not the event of the template [data;
  `CommunityProjectId_1_Wolf_5`: 110 experience, 135 coin units, 350 standing of Windsward; live: the notices "+350 Windsward Standing",
  "+235 XP" (110 and the 125 of the new standing rank), "+1.35 Coin"]. The standing goes to the territory of the board [client:
  GrantRewardsToOrigin].
- The hand-in writes all its rows in one transaction (MP-7): the quest rows, the rewards, and the town entry row of the character
  (`character_mission_offers`; a commit hook of the quest system, `QuestCommitHook`). A refused row changes nothing.
- After the hand-in the mission is in the unavailable list of the town entry for the current seed. The town entry also has the
  projects of the seed [agent: the active projects of the town now; the client computes the offers again for a project that the list
  does not have, OFFERS.md section 4].

### The abandon (3836)

- The abandon of a mission is the abandon of a quest (3836, quests.md; live: the journal button "Abandon", then "Yes").
- After an abandon the client itself shows the slot of the offer as "A new mission will be available soon" and sends no accept until
  the next seed; a new session of the client shows the offer again [live]. The backend makes no mission unavailable at an abandon: it
  accepts the offer again in the same seed (a new session of the client) [agent].

### The support manifest

- A template row of the kind missionTemplate or communityGoal is evaluated with a board mission: the first mission of a spawned board
  whose rules the backend runs (the effective objective, the grant `board`, the return task, the event of the mission). The counts
  `boards`, `boardMissions`, and `boardMissionsRunnable` are in the counts of the manifest. A history plays only rows of the kind
  objective.
- Stage QG6b: the faction boards give the world rows of their faction (enabled, not PvP, not an outpost destination, the territory rule;
  the count `factionBoards`). The return task of a mission goes to its provider by its tag (the factory does not read
  ReturnToGiverOverride: 0x4462726). The standing of a board mission goes to the territory of its board (no fallback: a territory with
  no standing row refuses, as the hand-in). Each row with board missions has the member `boardMissions` (each mission, supported, the
  first cause), and the manifest has the member `itemSources` (the items with a source of the world, the items of a quest).

### The override cells of a mission row (stage QG6b)

The source is the static read `~/Games/soulwarden/work/evidence/quests/qg6b/implement/static/factory/FACTORY.md` of the task factory
0x4461150 [client]. `QuestData::apply_overrides` builds the effective objective of each row with these rules; a row whose cells change no
leaf of its template runs the template.

- The override path runs only for a valid row with IsCustomObjective 0 (0x44611A5, 0x44611AE). A custom row keeps the tasks of its
  template (FM_1601_objective: its three rows are custom) [client; data].
- The common cells go to each leaf, also to the return task (TaskInteract ReturnToGiver), not to an authored container (the tree builder
  0x4466350 builds them apart): a TerritoryIDOverride that is not 0 replaces TerritoryID; a POITagOverride that is not empty replaces
  the POITag list (no append: 0x2D13A0); OverrideLocation replaces the location list (the pin), but the return task gets its template
  list back and the destination of the instance (0x446272B to 0x446273F) [client].
- TaskKillContribution (case 13): the category and the quantity of the row, always (a blank cell is the CRC 0 and 0) [client].
- TaskHaveItems, TaskHaveAndReturnItems, TaskGatherItems, TaskGatherAndReturnItems (25, 26, 29, 30; the branch 0x4463798): the item of
  TaskHaveAndReturnItemsOverride, its quantity (the target), ItemDropProbability (+0x40), ChestDropProbability (+0x44), and ItemDropVC
  (+0x118), always (a blank cell is no item, 0, or empty); PauseAndClearLootOnTaskCompleteOverride and UseGlobalStorageOverride only when
  the row has the cell (a present 0 replaces a template 1) [client]. TaskRequireGatherItems (31) keeps its item and quantity [client].
- The town column PauseAndClearLootTagOnTaskCompleteOverride (5,839 rows) has no effect: the loader asks only for
  PauseAndClearLootOnTaskCompleteOverride (0x39C5933; no string of the other spelling in the image) [client].
- TaskGiveItem, TaskGiveAndTakeItem, TaskRemoveItem (33, 34, 36): the item of TaskGiveAndTakeItemOverride, always; the quantity of the
  template [client].
- TaskTimer (case 64): the duration of TaskTimerOverride, always (a blank cell is 0). No other reader of the mission timer was found:
  the cell 3600 of each town row adds no timer to a template with no TaskTimer leaf, and no mission fails at its end [client: FACTORY.md
  section 8; not found: a mission lifetime].
- PatrolTask (case 66): the duration of PatrolDuration and the POIs of PatrolPoi1 to PatrolPoi4. One POI makes one TaskGoToDuration with
  the id, the CRC, and the index of the template task, whose target is the whole seconds of the duration (0 seconds: 1). A row with no
  POI or more than one POI changes the tree (no task, or a child for each POI and a container): refused (no row of the build has it)
  [client; agent for the refusal]. The place of the one-POI task is the effective common POITag of the leaf (POITagOverride, else the
  template): PatrolPoi1 only selects the branch (0x4461547); the branch gives the existing common block to the constructor (0x4461912,
  0x446191A) [client; code review of stage QG6b, finding QG6BR-R4].
- LootTagOverride on an item leaf, RequiredFortControlStatus, and a column that the backend does not know are refused (no row of the
  build has the first two) [agent].
- The success event of the row pays the hand-in (stage QG6a). The failure event (FailureGameEventIdOverride) has no consumer: no mission
  failure dispatches it in the client code that was read [client: FACTORY.md section 10; not found]. The backend applies no failure event.
- UseGlobalStorage: the possession tasks count the bag and the paperdoll only (the global storage is stage 5.2) [project rule of stage
  1.8c].
- The location rule of a possession or equip task (EnforceLocationRequirement with a POITag) gates the gain only: outside its place the
  value of the task does not rise, and it falls only with the items [data: the mission templates collect at a place and return to the
  giver elsewhere: `CM_explorer_acquire_X_items_POITag` (5,839 rows), `FM_loot_location` (720), `FM_huntx_location` (342); live: with a
  value 0 outside the farm the loot mission could not be handed in; quests.md].

### The faction boards (stage QG6b)

The static read is `~/Games/soulwarden/work/evidence/quests/qg6b/implement/static/faction-board/FACTION.md`.

- A faction representative is a person of the interaction data with an ObjectiveProvider of a faction (the member `provider` of the
  object: missionTypes Marauder, Syndicate, or Covenant; 57 in the build, 19 for each faction; the 5 `Wolf_Shop` providers and one with
  no type are no faction board) [data].
- The open: the conversation option "Open Faction Board" (the service Faction_Board, option 8) closes the conversation and sends action
  51 OpenMissionBoardOption at the same person [client 0x37AD685, 0x376E845; live]. The mission system is the handler of the kind `npc`
  with this option. It accepts only a character of the faction of the representative [agent: the eligibility 0x37A7940 of the service].
- The provider identity: the Claim landmark of the territory of the representative (the member `claims` of the interaction data; the
  same rule as a town board: 0x495941E) [client; live: Everfall `1b939fd32e1c1a3c0000000000000000`]. The territory is the territory at
  its place (the POI data).
- The offers [agent: the original generator is not in the client]: the offer window is the 30-minute grid of the town seed (its end is
  the expiration of the provider state). The candidates are the world rows of the faction (RequiredFaction), enabled, not PvP, not an
  outpost destination, whose territory rule passes the territory of the representative, whose MinLevel and MaxLevel pass the level of the
  screen, whose trade skill rank and achievement conditional pass, whose binding the backend runs, and whose success event the
  backend can pay at the board. Three slots (the FactionPvE weights of MissionWeightsData, slots 1 to 3; the screen shows three PvE
  cards): each slot takes a goal by its weight among the goals with a candidate that no earlier slot took, then one candidate of that
  goal; the LCG of java.util.Random from FNV-1a 64 of the characterId plus the territory, the faction, and the window [data: the
  weights; agent: the selection]. One character gets the same offers in one window.
- Field 2 of the provider state has the offers in this order (section 4.65 of the protocol notes). An offer whose mission is active, or
  that a hand-in of this window made unavailable, has available 0.
- The accept 3816 (the index of the offer): the faction board of the open, its service, the faction of the character, the current window,
  an offer of the index that is available, a binding that the backend runs, no active instance. The instance has isCommunityGoal 0,
  the destination the claim, and the origin territory of the representative. The offer stays in its place with available 0 and the
  instance (0x495A166) [client; live].
- The hand-in 2461 at the representative of the instance: the open faction board of that representative, its service and its source,
  and the faction of the character is the faction of the board (finding QG6BR-R5); then the plan of the quest hand-in with the return task (the rule of a town board). The faction rewards of the success event go to
  the faction of the character (stage QG7). The row `character_faction_offers` (schema 20) has the mission as unavailable for the window
  of the open, in the transaction of the hand-in [agent].
- After a world entry the destination of a faction mission is the claim of its origin territory (a value of the data).

### The daily bonus (stage QG6b)

- The faction missions pay the daily bonus while the character has a use left: the row FactionMissionBonus of RewardModifierData (the
  member `rewardModifiers` of the mission data: experience 2, currency 9, tokens 4) adds to the multipliers: experience × 3, coin × 10,
  tokens × 5; the reputation and the standing have no daily part (the preview 0x31F4630, the helper 0x5BF6120) [client; data; live:
  2520 experience, 70.50 coin, 400 reputation, 2475 tokens, the values of the card tooltip].
- Three uses a day: the registered default of `javelin.daily-bonus-mission-count` (0x616E608) [client]. The uses reset at 00:00 UTC [data:
  the daily reset hour 0 of playerbaseattributes, research factions.md V14].
- The uses are in the entity (`Entity::dailyBonuses`) and in the row `character_daily_bonuses` (schema 20, the transaction of the hand-in).
  The GameEvent state (slot 68, type 497, field 1) gives them to the owner: a character with no use has no fragment [client 0x31E5850;
  live: "Daily Bonuses Available 3/3", then "2/3"].
- At the reset time of a use (00:00 UTC) a timer of the world clock marks the GameEvent state of the owner: the state goes again with a
  new sequence and the uses 0 (the client keeps its replicated count until a new state: GetAvailableDailyBonuses 0x31F1140). The timer
  starts at the world entry and after a use; it is bound to the character and to the reset time (a newer use has its own timer)
  [client; code review of stage QG6b, finding QG6BR-R2; `Missions::daily_reset_due`].
- The experience of the daily bonus: the ordinary modifiers first (the GainXP card of the territory and its truncation: 0x31F4AF8,
  0x31F4DF0), then the daily factor in f32 and the truncation (0x31F4DFD, 0x31F4E02); the commit does not apply the card to that amount
  again (`Plan::cardedExperience`). 90 experience with the card of 3 percent: trunc(90 × 1.03) = 92, × 3 = 276 [client; finding
  QG6BR-R3].
- A town mission (isCommunityGoal 1) has no daily bonus (the preview path of the bonus is for the objectives of the type Mission).

### The patrols (stage QG6b)

- A PatrolTask (66) is the time in a POI of its tags: each look of the places adds the time since the last sample when that sample was
  inside; outside, the time and the progress stay [client: the update 0x3B094A0]. Only an inside sample publishes the progress: an
  outside sample keeps the saved sum (the outside branch 0x3B095E8 does not call the setter 0x3B095DD), and the next inside sample
  publishes it [client; code review of stage QG6b, finding QG6BR-R7]. The progress is the whole seconds (0x3B095C7); the
  target is the whole seconds of the duration; the task is StayActive (CompleteActive when satisfied: 0x3B0101C) [client; case QG6BD].
- The backend samples at each accepted position and one second after an inside sample while the task is not done (a character that
  stands still) [agent]. The dwell time is a value of the world entry: the progress stays in the row of the task.
- No patrol is reachable now: each of the 51 patrol rows is a PvP mission (3818, a later stage) [data].

### The corrections of the code review of stage QG6a (in stage QG6b)

Each finding of `~/Games/soulwarden/work/evidence/quests/qg6a/code-review/FINDINGS.md` has a case of `tests/review_qg6a_tests.cpp` that
fails on the tree of stage QG6a and passes now.

- QG6AR-R1 (QG6AR1): the offer filter takes the level of the screen.
- QG6AR-R2 (QG6AR2): the return task completes in the plan of the hand-in; a refused hand-in changes nothing.
- QG6AR-R3 (QG6AR3, QG6AR3B): a reset (5600, and a TaskDie with ResetOnCondition) keeps the mission, the provider, the destination, and the
  community flag of the instance.
- QG6AR-R4 (QG6AR4): the loader refuses a file with town missions and no town buckets, a bucket member of another table or goal, and a
  member in two buckets.
- QG6AR-R5 (QG6AR5): 3819, 3816, and 2461 on the actor route of another character change nothing (the rule of QG7R-R7).
- QG6AR-R6 (QG6AR6): the mission board diagnostic needs the harness gate, also on the admin path.
- QG6AR-R7: the second computation of the support manifest reads the boards of the interaction data of format 5 and fails at a
  difference of the board counts (an evidence script, not a source file).

### The corrections of the code review of stage QG6b (in stage QG8)

Each finding of `~/Games/soulwarden/work/evidence/quests/qg6b/code-review/FINDINGS.md` has a case of `tests/review_qg6b_tests.cpp` that
fails on the tree of stage QG6b and passes now (the fixture of the cases of QG6b is in `tests/support/mission_qg6b_fixture.h`).

- QG6BR-R1 (QG6BR1): the town buckets are the whole partition of the client: each enabled town row of the goals Gather, Kill, and Explore
  is a member, but a row of the member `townExcluded` (the tool leaves out a row whose objective has no row of the quest data); a missing
  member refuses the whole file, also an empty bucket list with town rows [client: the town selection of QG6a uses the complete list].
- QG6BR-R2 (QG6BR2): the daily reset reaches the connected owner at 00:00 UTC (the rule above).
- QG6BR-R3 (QG6BR3): the order of the GainXP card and the daily factor (the rule above).
- QG6BR-R4 (QG6BR4): the place of a patrol of one POI (the override rules above).
- QG6BR-R5 (QG6BR5): the hand-in at a faction board needs the faction of the board (the rule of the accept); a faction change closes an
  open faction board of the old faction (`Missions::faction_changed`). A refusal changes nothing [agent: the rule of the accept 3816].
- QG6BR-R6 (QG6BR6): a skinned body is a gather drop source only when the backend gathers its skinning row (docs/systems/quests.md).
- QG6BR-R7 (QG6BR7): an outside patrol sample keeps the published progress (the rule of the patrols above).

## The values of this build

| Value | Number | Source |
| --- | --- | --- |
| The seed interval | 30 minutes | the CVar default 0x61743A9 |
| The seed addition | 0 | 0x3A1C52A |
| The missions of the town table, of the open world table | 13,248, 3,046 | the mission data |
| The kill overrides, the projects, the events of the missions | 2,925, 50, 203 | the same |
| The town bucket sizes: Kill, Gather, Explore | 3,959, 3,418, 5,871 | the same |
| The boards of the interaction data, the runnable board missions | 13, 36 of 290 | the manifest of stage QG6a |
| The town boards, the faction boards, the board missions, the runnable board missions | 13, 57, 3,122, 1,835 | the manifest of stage QG6b |
| The faction representatives of each faction; the PvP rows; the patrol rows (all PvP) | 19; 180; 51 | the interaction data, the mission data |
| The daily bonuses of a day; the additions of FactionMissionBonus (experience, currency, tokens) | 3; 2, 9, 4 | the CVar default 0x616E608; RewardModifierData |
| The PvE cards of a faction board | 3 | owmissionboard NUM_MISSIONS_SHOWN; the FactionPvE slots |

## Commands

```sh
tools/world missions NAME                                   # the board state, the town offers of now, the missions with their tasks
tools/world missions NAME CommunityProjectId_1_Wolf_5 200   # the next windows (of 200) in which the mission is an offer of NAME
```

- The development route: `GET /v1/dev/missions?character=NAME[&mission=ID][&windows=N][&territory=N]`. The admin command:
  `mission_board`. Both use the common layer (`server/http/character_commands.h`, `server/http/mission_commands.cpp`).
- The command reads only. It changes no offer, no seed, and no instance. It is a diagnostic: it needs `SOULWARDEN_HARNESS=1` in the
  backend process, also on the admin path (code review of stage QG6a, finding QG6AR-R6; case QG6AR6). Stage QG6b: the view has the
  faction offers of the open board (`board.factionOffers`) and the daily bonuses that are left (`dailyBonusesLeft`).

## Traps

- After a world entry at the saved place near the board, the client shows no prompt at the board ("View Missions"), and E sends
  nothing. A travel with a loading screen far away and back (`tools/world travel NAME X Y Z --screen on`) gives the prompt [live; the
  same trap as the recruiters of stage QG7]. The record of the backend is not the cause: a run with no TerritoryInterface state had the
  same fault.
- A LandClaimManager state with a claim key and no owner element for it stops the client at a territory entry (an access at
  NewWorld.exe+0x2831BCF: the owner getter 0x2F81310 checks the key but not the length of the owner vector). Each field of a claim
  must have one element for each key [live: two faults; client: OWNER.md].
- A LandClaimManager state with a position (field 14) makes the manager "available" before the other vectors are copied; the client
  stopped at once [live]. Send no position until a later stage has the full data of the map.
- The popup "Complete All Missions" asks the server for the global storage first (`ContractsDataHandler:RequestStorageData`); with no
  answer its callback runs after 1 s and counts the missions for which CanCompleteMission is true then (`townprojects.luac` function
  0.10). With the return task pending it lists the mission and its rewards, and "Accept" sends one 2461 for each [live stage QG6b: "You
  have 1 completed mission(s)", the 5 rations, 50, 1.14, 350; the 0 of the live run of QG6a came before the return task stayed
  pending].
- The kill credit needs damage of 1 percent of the maximum health of the creature (`credit.healthShare` of the loot tables): for a live
  test with a creature of a command, set its health to 20, not 1, before the hit [live: a kill at health 1 gave no credit].
- The prompt shows only at about 1 m from the bound of the board (the bound is 0.9 m in front of the board): at 3 m there is no
  prompt [live].
- The seed text is the characterId (a UUID text). A character gets one offer for each bucket in a window; the wanted offer can be far
  away. For a live test, use `tools/world missions NAME MISSION N` to find the window, or give a test character a made-up id whose
  window comes soon. Do not force an offer.
- The board has the offers of the seed of the open. The client computes new offers at the next seed by itself; the backend gives a new
  service revision at the next open. An accept after the seed change with no new open is refused when the offer changed.
- The transport writes each fixed integer big-endian (`hub::Writer`). A static read of a client reader can say "le": that is the
  memory form, not the wire form.
- A backend of schema 18 refuses a database of schema 19. For a comparison with an earlier backend, use a copy of the data folder.

- Stage QG6b: the creatures of a mission place attack a test character of a low level (the loot farm of Everfall: a death at the first
  chest). Use `tools/world ignore NAME on` before a live test there.
- Stage QG6b: the first open of a faction board in a session can show "Missions refresh in 00": the screen reads the countdown at its
  transition, before the provider state comes. A second open shows the time of field 1 [live].
- Stage QG6b: the offers of a faction board change at the end of the window (30 minutes). An accepted mission stays; at the next open
  its offer is unavailable, and the card of the accepted mission shows in the first slot.
- Stage QG6b: the work order missions of a town board (`Faction_*` items) need a craft of a temporary recipe, which the crafting system
  refuses: they are offers that the character cannot complete now.
- Stage QG6b: a faction board needs the faction of its representative: a character of another faction (or none) gets no board (3743
  result 2) [agent].

## Input for the later stages

- The projects of a town (Settlement, Fortress, Lifestyle; stage 5.4): the project state of the town, the TerritoryInterface state with
  more than the always available project, the previous projects of a seed, CommunityGoalAmount.
- The claim positions (field 14 of the LandClaimManager state) and the map data (stage 5.4).
- The PvP missions (3818, `GetCurrentPvpMissions`, the 180 PvP rows, the patrols of the PvP control missions) and the outpost missions
  (IsOutpostValidDestination, the outposts of `GetOutposts()`).
- The work order missions (`CM_procurer_craft_workorder_items`): the give works, but the craft of a work order is a temporary recipe of
  the crafting data, which the crafting system refuses (crafting.md).
- The patrols of more than one POI (no row of the build) and LootTagOverride (no row of the build).
- The CurrencyReward of an event: a fraction of the float32 cell stays unpaid (quests.md); the static rule of the coin range of the
  client (0x31F3E00) is not read.
