# Mission boards

Read this document before a change of a mission board: the board object of a settlement, its open, the town offers, the accept of a
mission (3819), the mission instance, its return task, its hand-in (2461), the town entry of a character, or the mission data. The wire
contracts are in the protocol notes (section 4.64). Stage QG6a made the town board of a settlement with the offers of the always available
project (roadmap 4.5). The faction boards, the open world missions, and the projects of a town are stage QG6b.

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
  - MinLevel and MaxLevel pass the level index (the level of the screen minus 1) [client 0x4955CAF; inferred];
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
- A mission row with an override that the backend has no rule for is refused at the accept, and the manifest names the cause [agent].
- The instance stores its mission, the community flag, and its provider (the key of the board placement) in `character_quests`
  (schema 19). The destination is not stored: the world gives it at the world entry (the claim of the board of the key).
- The kill task counts a kill with the rules of the kill credit of the quests (quests.md): the vitals categories of the victim.

### The hand-in (2461)

- The client sends 2461 (instance:u64) at the board, one for each ready mission [client: the sender 0x494E910, the reader 0x39E8A10;
  live: a click on the card "COMPLETED"].
  The checks: an active mission of the character with this instance (`unknownInstance`: a forged instance, a second hand-in), the
  service of the board that is the provider of the instance (`noProvider`: another board), the source of the service; then the return
  task completes (`Missions::complete_return`), then the checks of the quest hand-in (a ready root, the bag, the rewards).
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

## The values of this build

| Value | Number | Source |
| --- | --- | --- |
| The seed interval | 30 minutes | the CVar default 0x61743A9 |
| The seed addition | 0 | 0x3A1C52A |
| The missions of the town table, of the open world table | 13,248, 3,046 | the mission data |
| The kill overrides, the projects, the events of the missions | 2,925, 50, 203 | the same |
| The town bucket sizes: Kill, Gather, Explore | 3,959, 3,418, 5,871 | the same |
| The boards of the interaction data, the runnable board missions | 13, 36 of 290 | the manifest of stage QG6a |

## Commands

```sh
tools/world missions NAME                                   # the board state, the town offers of now, the missions with their tasks
tools/world missions NAME CommunityProjectId_1_Wolf_5 200   # the next windows (of 200) in which the mission is an offer of NAME
```

- The development route: `GET /v1/dev/missions?character=NAME[&mission=ID][&windows=N][&territory=N]`. The admin command:
  `mission_board`. Both use the common layer (`server/http/character_commands.h`, `server/http/mission_commands.cpp`).
- The command reads only. It changes no offer, no seed, and no instance.

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
- The popup "Complete All Missions" of the town screen listed 0 missions and a reward of 0 for a ready mission, and its "Accept" sends
  nothing. Complete a mission with a click on its card "COMPLETED" [live].
- A hand-in that the quest system refuses after the return task completed (a full bag; not for the rewards of the wolf mission) leaves
  the return complete: the client then shows no completion for that mission. A later stage must undo the return or check the hand-in
  first.
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

## Input for stage QG6b

- The faction boards and the open world missions: the weights (MissionWeightsData), the flexible boards, the explicit offers of the
  provider state (field 2, `ObjectiveCreationParams`), the faction mission bonus.
- The projects of a town (Settlement, Fortress, Lifestyle): the project state of the town, the TerritoryInterface state with more than
  the always available project, the previous projects of a seed.
- The destination argument of 3819 that is not 0, and the override columns that have no rule now (`QuestData::bind_missions` refuses
  them): POITagOverride, TerritoryIDOverride, LootTagOverride, OverrideLocation, the item overrides of an item task, the pause of the
  loot of a possession task, the patrol overrides of a patrol task, and TaskTimerOverride of a timer leaf. A PvP mission and a mission
  with IsOutpostValidDestination are refused too.
