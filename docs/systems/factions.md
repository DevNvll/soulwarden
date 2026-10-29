# Factions

Read this document before a change of the faction of a character, of its join or change, of the reputation or the tokens of a
faction, of a quest rule of a faction (the gates, TaskHaveLevel of a faction, TaskChooseFaction, the rank-up rows, the faction
rewards), or of the Social state. The wire contracts are in the protocol notes (section 4.63). Stage QG7 made this system (roadmap
4.5).

## Start here

| Task | Start here |
| --- | --- |
| The faction of a character, the join, the change, the wait, the cost | `apps/backend/src/world/factions.h`, test cases QG7A, QG7B, QG7C |
| The Faction state (slot 49, type 3152) and the request 3161 | Protocol notes 4.63, `server/rep/states/faction_state.cpp`, `server/rep/progress_requests.h`, `RepSession::handle_set_faction` |
| The empty Social state (slot 55, type 4176) | Protocol notes 4.63, `server/rep/states/social_state.cpp`, test case QG7A |
| The reputation and the tokens (slot 65, type 911) | `Skills::add_faction_points`, `Skills::set_faction_rank` (`world/skills.h`), test case QG7D |
| The quest rules of a faction | `world/quests.h` (`faction_changed`, `next_rank_up`, `self_completes`, `choose_faction_active`), test case QG7E |
| The conversation states of the recruiters | `world/persons.cpp` (RequiredFactionCooldown), test case QG7H |
| The faction data | the member `factions` of `progression-tables.json` (`tools/gamedata/progression_tables.py`), [Game data](../development/game-data.md), test case QG7I |
| The two sessions (MP-10), the restart, the harness gate of the commands | test cases QG7G, QG7F, QG7J |
| The static read and the live runs of the stage | `~/Games/soulwarden/work/evidence/quests/qg7/implement/` (`static/FACTION-STATIC.md`, `RESULT.md`) |

## Rules

The source of each rule is in brackets: [data] the game data, [client] the client code (with its address), [live] a live check of
stage QG7, [agent] a rule of the project that an agent chose.

### The faction of a character

- The faction is a value of the entity of the character (`Entity::faction`: the faction, the time of the last change, the count of
  the changes) and a row of the table `character_factions` (schema version 18). Only the faction system changes it: the request 3161
  of the client or a command of the development and admin routes. The delete of a character removes the row.
- The faction numbers are the enum of the client: 0 None, 1 Syndicate, 2 Marauder, 3 Covenant (4 Any is not a faction of a player)
  [client: the enum validator 0x2E6A8A0 accepts 0 to 4].
- A change writes its rows in one transaction: the faction row, with a cost the Azoth of the skills rows, and the end of each quest that
  the change ends (`Quests::faction_ends` with a commit hook of the quest system; code review of stage QG7, finding QG7R-R1, case QGR7A).
  A refused row changes nothing: the faction, the Azoth, and the quests stay. Then the system changes the entity, marks the Faction
  state (`World::touch`), and gives the change to the quest system (`Quests::faction_changed`: the gates of the pushed rows and the
  rank-up rows; finding QG7R-R6, case QGR7D).
- The Faction state goes to the owner and to each observer. The owner gets the time of the last change, the count, and the faction.
  An observer gets only the faction (the public group): the name plate of a different player uses it [client: `marker.luac`
  callback 0.6.18, the setter 0x30C0A40]. An observer gets no fragment for a character with no faction.
- The Social state (slot 55, type 4176) goes to the owner one time, after the first 2709 of the session. It is empty (no title, no
  friend, no invite, no block). Without it, the client crashes when it opens the faction poster of a recruiter or the conversation
  options [live: an access at 0xD08 in the vector size getter 0x2F2D50, from `JavSocialComponentBus.GetFriends`].

### The join and the change (3161)

- The route of a 3161 frame must be the actor of the character of the session. A request on the route of another actor changes
  nothing, and the owner gets its Faction state again (code review of stage QG7, finding QG7R-R7, case QGR7E; MP-5) [agent].
- The request 3161 (`RequestSetFaction`) asks for a faction. The checks, in this order; the first refusal wins:
  1. The backend has the faction data (`noData`).
  2. The faction is 1, 2, or 3 (`badFaction`).
  3. The faction is not the faction of the character (`sameFaction`) [agent: the client check 0x30AD140 has no refusal for the same
     faction; a join again of the same faction would restart the wait for nothing].
  4. The data path offers the faction (`notOffered`), see below.
  5. The wait of a change is over (`onCooldown`).
  6. The Azoth balance pays the cost (`notEnoughAzoth`).
- The data path offers a faction when [data; client 0x30AD140 result 10; agent for the join of a character with no faction]:
  - the recruitment objective of the faction is active with its TaskChooseFaction task Active or CompleteActive, and the character
    has the service Faction_Selection (4) of a conversation (the recruiter state `98xx_Rep*_01_b`), or
  - the character has the recruitment achievement of the faction (RequiredAchievementId or RequiredLegacyAchievementId of the
    faction table). A character with no faction also needs the service. Live: the Bio panel "Change Faction" sends 3161 for the
    Marauders with their recruitment achievement and no conversation; the backend changes the faction (the count 2, the time now).
- The other checks of the client (a cross-world match, PvP, a sanctuary, a war, the territory of the factions, a company) have no
  state in the offline world, and the backend does not do them [client 0x30AD140; agent].
- A join (a character with no faction) keeps the time 0 and the count 0: the first change after it has no wait. A change sets the
  time to now (the wall clock of the world, Unix nanoseconds) and adds 1 to the count [data: `owg_faction_select_confirm` says that
  one later change is available before the 30-day wait; client: the getter 0x30B1DE0 has no branch for the count; agent for the time
  0 of a join].
- The wait: the time of the last change plus Faction Change Cooldown Seconds (2,592,000 s, 30 days) [data: Faction Data of
  `playerbaseattributes.pbadb`; client 0x30B1DE0]. A time of 0 ends in 1970: no wait. The time is Unix nanoseconds of the wall clock
  of the world [live: with the time of a change now, the client shows the wait "30d"; after a restart too].
- The cost: 0 with no faction, and 0 when the level index (the level of the screen minus 1) is below Faction Change Cost Level (20).
  Else `min(Max, Min + count × Increment)` [client: 0x30B1EC0, the provider 0x67F8670 to 0x5C41E30]. The data of this build has Min 0,
  Max 0, and Increment 50: each change costs 0 Azoth.
- The client publishes the faction of a request on its HUD before the answer [client: 0x30C00B0 calls the HUD setter 0x30C2E50]. A
  refused request changes nothing and marks the Faction state: the owner gets the faction of the server again (a received faction
  field republishes its value, 0x30B6210). A request with a bad form or a bad facet also marks the state.
- A command (`tools/world faction`, the admin command `set_faction`; a harness command) sets the faction with no check of the data
  path, the wait, or the cost. It can set the time of the last change (`now`, `clear`, or nanoseconds) and the count.

### The reputation and the tokens

- The reputation and the tokens of a faction are categorical progressions of the skill system (slot 65, type 911). Their rows are in
  the progression data [data: CategoricalProgression]:

  | Faction | Reputation | Rank table | Tokens | Rank table |
  | --- | --- | --- | --- | --- |
  | 1 Syndicate | `Syndicate` (56FBDE12) | MerchantGuild | `SyndicateTokens` (EBD7B082) | SyndicateTokens |
  | 2 Marauder | `Marauder` (540B63C5) | ProcurerGuild | `MarauderTokens` (89599279) | MarauderTokens |
  | 3 Covenant | `Covenant` (F4EB80F7) | ExplorerGuild | `CovenantTokens` (A0989588) | CovenantTokens |

- The points are a total, not the points since the last rank. A gain stops at the cap of the current rank (MaximumInfluence of the
  rank; AutoRankUp false, AllowOverflow false): it does not change the rank [client: 0x3CAAF15, 0x3CAB135, 0x3CAB188 to 0x3CAB1B5].
- The largest rank is MaxLevel 4, or Expansion01MaxLevel 5 with the expansion [data]. A command or a reward with a higher rank is
  refused.
- The rank of a faction goes up only with a rank-up row (below). A new rank keeps the points; the next gain can go to the cap of the
  new rank [client: the rank setter 0x3CBF9F0 keeps the points].
- A negative change of the points (a later cost of a shop) is refused when the balance is too low (`notEnoughBalance`).

### The quest rules of a faction

- The gate RequiredFaction is a mask of the faction bits. Bit 0 bypasses the test: a mask with bit 0 (for example `!Syndicate`) passes
  each character [client: 0x445CA30 parses the mask; the test 0x3A4C166].
- TaskHaveLevel with a faction progression in ItemName reads the points (ShouldCheckPoints) or the rank of the progression. It is
  satisfied when the value is at least TargetQty [client: 0x3B04F40, the callbacks 0x3B07100 and 0x3B07140]. A change of the points or
  of the rank looks at the tasks again at the next tick.
- TaskChooseFaction is satisfied when the character has a faction and it is the faction of the recruitment objective (the faction whose
  RequiredAchievementId is the completion achievement of the objective); with no recruitment faction, each faction satisfies it
  [agent: the client predicate is not found, static read S7].
- A faction story row with no destination person (Type `FactionStory_*`, an empty NpcDestinationId) completes when its root is done:
  the recruitments and the rank-up rows have no hand-in at a person [agent; live: the recruitment of the Syndicate completed after the
  join, with the enlistment box].
- The rank-up rows: the Progression Validation Achievement Data names a list of trial achievements and a list of rank-up objectives
  for each faction [data]. Entry 0 of both lists is the recruitment. After the last trial achievement K that the character has, entry
  K + 1 of the rank-up list is the next rank-up row. The quest system grants it when it is available: at the world entry, after a
  faction change, and after a change of the achievements [agent].
- ProgressionReward of a row (`ID RANK [POINTS]`) raises the rank of a faction progression to RANK (a lower rank stays) and then adds
  POINTS (at most the cap of the new rank). The progression must be a faction progression of the tables and RANK at most the largest
  rank [client: the parser 0x4469B00; agent: the grant of the original server is not found, the client preview shows the rank name and
  adds the points].
- FactionReputation and FactionTokens of a success event go to the progressions of the faction of the character. A character with no
  faction gets none [client: the grant 0x31F5400, 0x31F6DFE, 0x31F7197]. The reputation is `floor(f32(raw) × (1 + bonus))` with the
  GainFactionRep card of the territory of the hand-in; the tokens have no card [client: 0x31F6D98 to 0x31F6E08; the setting
  `javelin.territory-bonus-rep-affects-tokens` is false, 0x617665C].
- A faction change ends each active objective with ShouldResetOnFactionChange and each active objective whose RequiredFaction does not
  pass now. The end has the rules of an abandon: the undo of its unlocks, no reward. Its completion achievements stay [client: the
  callback 0x3A571A0, the set of the loader 0x445E890; agent for the end rules; live: the HUD shows "Objective failed" for the rank-up
  row of the Syndicate after a change to the Marauders].

### The introduction and the recruiters

- The introduction is a chain of rows for each faction: the push row `Vector_Faction_PushQuest`, then `FactionIntro_<faction>_01`, a
  vector row, `_02`, a vector row, `_03`, and the recruitment `FactionIntro_<faction>_Recruitment` [data]. The rows use the quest
  systems of the earlier stages. Some tasks of `_01`, `_02`, and `_03` wait for the point of interest data or for an encounter of the
  level data (the support manifest names each block).
- The recruitment has two branches: the join (TaskUnlockAchievement `S_factionhandoff`, then TaskChooseFaction at the recruiter) and the
  decline (a conversation topic `<faction>_DeclineRecruitment`) [data].
- The recruiters (Astrid for the Syndicate, Smiggs for the Marauders, Beyza for the Covenant) select their conversation state with the
  rules of the persons. RequiredFactionCooldown: `InCooldownMultipleDays` needs more than 24 whole hours of the wait,
  `InCooldownSingleDay` 1 to 24 whole hours [client: 0x37AB5F9 to 0x37AB6B5; live: Astrid says "We cannot recruit you at this time"
  to a character in the wait].

### The support manifest

- A history of the manifest has a faction (`SupportProfile::faction`). The profiles `new:Archetype_Soldier:Syndicate`, `:Marauder`, and
  `:Covenant` play a new character that joins that faction at its first TaskChooseFaction.
- The facts of a faction: TaskHaveLevel of a faction progression, TaskChooseFaction, the faction rewards of a hand-in, and the next
  rank-up row are in the history. A rank-up row that the validation lists do not name has a reason of its own.

## The values of this build

| Value | Number | Source |
| --- | --- | --- |
| The wait of a change | 2,592,000 s (30 days) | Faction Data, `playerbaseattributes.pbadb` |
| The cost level, Min, Max, Increment | 20, 0, 0, 50 (each change costs 0 Azoth) | the same |
| The reputation caps of the ranks 0 to 5 | 3000, 11000, 26000, 49000, 85000, 85000 | MerchantGuild, ProcurerGuild, ExplorerGuild |
| The RequiredLevel of the reputation ranks | 10, 20, 35, 50, 60, 61 | the same |
| The token caps of the ranks 0 to 5 | 15000, 30000, 45000, 75000, 150000, 300000 | the token tables |
| The largest rank | 4, with the expansion 5 | CategoricalProgression |

## Commands

```sh
tools/world faction NAME                          # the faction, the wait, the cost, the reputation and the tokens of each faction
tools/world faction NAME syndicate                # set the faction (no check); --changed now|clear|NS, --changes N
tools/world reputation NAME syndicate 1 3000      # the rank and the points of the reputation of a faction
tools/world tokens NAME syndicate 0 100           # the rank and the points of the tokens of a faction
```

- The development routes: `POST /v1/dev/progress/faction` and `POST /v1/dev/progress/faction-progress`. The admin commands:
  `set_faction` and `set_faction_progress`. The view `GET /v1/dev/progress` has the member `faction`. Each one uses the common layer
  (`server/http/character_commands.h`).
- The two changes are harness commands [rule of AGENTS.md; test case QG7J]: they refuse (403) with no `SOULWARDEN_HARNESS=1` in the
  backend process, also through the admin routes. The tool window has no faction control.

## Traps

- Without the Social state (slot 55), the client crashes at the faction poster of a recruiter and at the options of a conversation
  (0x2F2D50, the friend list of `GetFriends`). A state of the faction 0 alone does not prevent the crash.
- The Talk prompt of a person came only after a travel with a loading screen (`tools/world travel ... --screen on`) far away and back.
  A world entry at the saved place near Astrid showed no prompt.
- The Bio tab of the character screen (K) is the faction panel: the faction, its rank name with the reputation, the faction tokens,
  and the button "Change Faction" [live]. Click the tab 3 s or more after K: in the first live runs of stage QG7 an earlier click kept
  the attributes page (the cause is not proved; with the wait of 3.5 s the tab opened each time, also after a restart).
- The button "Change Faction" gets its state when the screen opens: after a change or after the end of a wait, close the screen and
  open it again. In the wait its tooltip is "Cannot change factions while on cooldown. 30d", and its panel greys out each other
  faction; the client sends no 3161 [live]. The panel also greys out a faction whose recruitment achievement the character does not
  have (the achievement check of CanSetFactionWithResults).
- The Faction Board of a recruiter does not open (the faction boards are stage QG6b; the client log has errors of
  `UpdateInfluenceWarInfo`). Stage QG6a made the town board of a settlement ([Mission boards](mission-boards.md)).
- A backend of schema 17 refuses a database of schema 18. For a comparison with an earlier backend, use a copy of the data folder.
- The progression ticker of the HUD shows a faction gain only with an experience event (`eXpEventType_Reputation`,
  `eXpEventType_ProgressionCurrency`). The backend sends none: a command changes the points with no ticker. The reward notice of a
  hand-in shows the faction reward ("+150 Faction Tokens") [live].

## Input for the later stages

- The faction shops (stage 5.1): the token balances are categorical progressions of slot 65. A purchase is a negative change with
  `Skills::add_faction_points` (it refuses a balance that is too low). The rank gate of a shop row is
  RequiredCategoricalProgressionRank against the rank of the reputation.
- The faction missions of the boards (stage QG6b): the hand-in already pays FactionReputation and FactionTokens of an event with the
  GainFactionRep card. The mission bonus (RewardModifierData FactionMissionBonus: token modifier 4, reputation modifier 0) and the
  request 3817 (`RequestPurchaseRankIncrease`) are not done.
- PvP, the wars, and the territory owner need the public faction of the Faction state and the checks of 0x30AD140 that the backend does
  not do now (PvP flag, war, territory count, company).
