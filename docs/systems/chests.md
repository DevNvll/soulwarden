# Chests

Read this document before a change of the chests of the world: the open of a chest, its loot, the loot claim of a character, its
reset, and the chest data. The wire contracts are in the protocol notes (section 4.41). The interaction base (the request, the reply,
the cancels) is in [Interactions](interactions.md), the roll of a loot table in [Rewards](rewards.md).

## Start here

| Task | Start here |
| --- | --- |
| The open of a chest: the handler of GatherOption, the open time, the completion, the cancels, the roll, the claim, the full bag, the reset, the LootTracker | Protocol notes 4.41, `apps/backend/src/world/chests.h` (`Chests`, `chest_rules`), `world/interactions.h` (`InteractionHandler::owns_sight`, `ends_at_damage`, `Interactions::use_valid`, `end_use`), `world/rewards.h` (`Rewards::roll_root`, `grant_items`, `LootContext::container`), `world/inventory.h` (`Inventory::grant` with `whole`), test cases LC2 to LC7 |
| The wire: the Gathering state (type 81, slot 36) and the LootTracker state (type 982, slot 33) of the owner | `server/rep/states/chest_states.cpp`, `server/rep/states/state_codecs.h`, `world/entity.h` (`GatherProgress`, `LootTrackerEntry`), `tests/support/state_decoder.h` |
| The claims in the store | `state/store.h` (`LootClaimRow`, `list_loot_claims`, `write_loot_claim`, `delete_loot_claims`; schema 9: the tables `loot_claims`, `loot_claim_items`), test cases LC4, LC6, B11 |
| The chest data (`chest-data.json`) and its tool | `gamedata/chest_data.h` (`ChestData`, `ChestDefinition`, `ChestGatherable`, `for_each_near`), `tools/gamedata/chests.sh` (`chests.py`, `check_chests_tool.py`), [Game data](../development/game-data.md), test cases LC1, LC9 |
| The loot roots of the chests and the container floor (`loot-tables.json` format 2) | `gamedata/loot_tables.h` (`chestRoots`, `limits`, `container_floor`), `tools/gamedata/loot_tables.py`, test case LT8 |
| The commands | `server/http/chest_commands.cpp`, `server/http/character_commands.h`, `tools/world chests`, `tools/world chest clear`, `tools/world chest reset` |
| The load | Test case LC8 (100,000 far chest definitions, 50 characters) |
| The corrections of the code review of stage 3.5b.1 (CH-R1 to CH-R8) | Test cases RCH1 to RCH6 (`tests/review_chests_tests.cpp`), the cases `missing-metadata`, `missing-territory`, `wrong-record-first`, `record-conflict` of `check_chests_tool.py` |

## Rules

The source of each rule is in brackets: game data, client code, live check (the live checks L1 to L9 of stage 3.5b.1, protocol notes
4.41), decision of the user (through the orchestrator), decision of an agent.

- The kinds of stage 3.5b (decision of the user): 3.5b.1 is the ordinary chests of the world (supply, provision, alchemy, the ordinary
  elite chests); 3.5b.2 the personal loot bag of a creature (the AILoot bag) and the discard of a player; 3.5b.3 the special locks, the
  keys, and the chests of quests and events, after their predicates exist (after stages 3.6 and 1.8). Since 3.5b.2 the items of a kill
  go into a personal loot bag ([Loot bags](loot-bags.md)).
- A chest is a placement of the main world whose variant name is a row of the variation table `Gatherable_LootContainers` (the slice
  `master_lootcontainer`) [game data]. The row names the gatherable row (the open time, the reset values, the restrictions) and the loot
  root [game data]. The record of the layer metadata (GatherableLocations) of the same layer 0.06 m or less from the placement, whose
  GatherableId is the CRC-32 of the gatherable row of the variation, gives the territories of the chest [game data; research K1; code
  review of stage 3.5b.1, finding CH-R7: a record of a different row at the same place is not the record of the chest]. The LootTags and
  the level of a territory come from each TerritoryDefinition table of the game data (the territories, the areas, the points of
  interest, the arenas) [game data; finding CH-R6]. The tool stops, and keeps the earlier file, when a chest that can be ordinary has no
  record (finding CH-R5), when two records of its row at the chest have different content, or when a record names a territory with no
  row [decision of an agent]. The tool `chests.py` writes each chest with its state: `ordinary` or
  `unavailable` with its reason [game data]. The reasons: a placement of the lock slice `master_lootlock` (a script lock of an event), a
  lock slice of the variation row that is not the map marker, a gatherable row that needs an item (a key), a tool, a trade skill rank,
  a status effect, a song, a faction, a cost, OpenAfterGathering or IsFFARestricted, a gatherable of a quest, a record of an encounter or
  not of POILoot, and a root `Empty` or with no table [game data]. The retail data: 5,519 chests, 5,149 ordinary (4,201 supply, 480
  provision, 217 alchemy, 210 elite, 41 other), 370 unavailable.
- The request is the interaction request of stage 2.5a: the key E sends GatherOption (action 14, committed) of the chest object of the
  interaction data (the kind `gathering`, the placement key) [live check L1]. The base checks it: the reference, the option, the 3D range
  of the bound (`rangeSq` 100: 10 m), the requirements of the bound (an achievement) [game data]. The handler of this system
  (`ChestHandler`) then checks, in this sequence: the backend has chest data, the object is a chest of the data, the chest is ordinary
  (an unavailable chest is refused with its reason), the root has a loot table, and the character has no Looted claim on the chest. Each
  refusal changes nothing and gets the reply 3743 result 2 [stage 2.5a].
- The sight: each chest bound has `m_requiresLineOfSight` [game data]. The client checks it before it shows the prompt (its LOSRayCastCheck
  entity) [client code]. The server has no ray of the physics of the client, and the navigation cells of the crate stop a line to it: the
  handler owns the sight (`InteractionHandler::owns_sight`), and the server checks only the 3D range [decision of an agent].
- The open time: the BaseGatherTime of the gatherable row (2.5 s for a supply chest, 5 s for an elite chest) [game data]. The Gathering
  state of the owner gets Success, the remaining time, the speed 1 (field 2), the count 1, the target G of the chest, and new reference
  counters; the client shows the progress ring of the prompt for remaining / speed seconds [live check L3]. The open is a timed use of
  the base with this time (`InteractionHandler::use_time`; code review of stage 3.5b.1, finding CH-R2): its timer of the world (the time
  plus 1 ms), the checks again, and the completion of a release at or after the time before its end (finding ST-R1) [stage 2.5b].
- The ends before the time give no item and the Gathering state Failure: the release of the client (2473), a walk out of the range, a
  travel, the death, and damage of the character (the action Gather of the client ends at a hit: `playeractions_gather.actionlist`,
  IfDamagedByPowerLevel 0 or more) [game data; live check L4]. A new press after a cancel starts a new open with the full time [live
  check L4]. The same block in each input report is one open [stage 2.5a]. A delayed input report of the held block that the client made
  before a cancel (or before the echo of a server end) starts no new open: the session keeps the datagram of each cancel (code review of
  stage 3.5b.1, finding CH-R1; [Interactions](interactions.md)).
- At the end of the time the base checks the use again (the source, the life, the exact position, the range) [stage 2.5b]. The roll:
  the root of the chest with the roller of stage 3.5a [client code 0x368B070]; its context: the level of the character, the content
  level (the largest AIVariantLevelOverride of the territories of the chest, else the level of the character) for the numeric tags
  `poilevel`, `containerlevel`, and `minpoicontlevel` of the conditions, the LootTags of its territories (for example `Everfall`), and
  the container floor of the gear score (ContainerLootGSFloor of VitalsLevels) in place of the ceiling of a creature [game data; the
  choice of the content level: decision of an agent, the column POILevel of the data is an enum, not a level]. The perks of each
  instance come from the perk roll of stage 3.5a [game data].
- One transaction of the store: the items go into the bag with the cause ItemGathered (1: the loot notice of the HUD) and the claim gets
  the state Looted with its reset time (MP-7) [live check L3 for the notice]. Then the Gathering state gets Done and the LootTracker of
  the owner has the Looted claim: the client removes the prompt and ends its use itself (2473) [live checks L2, L3]. A write that the
  store refuses changes nothing: no item, no claim, the Gathering state Failure, and 3746 [project rule].
- A full bag [decision of the user]: the client has no weight rule for the Gather action (the action list has no encumbrance condition),
  so the backend checks no weight [game data]. When the bag (500 slots) has no place for the whole result, no item goes into the bag,
  the roll stays in the claim (the state Rolled with its items), the chest stays closed (the prompt stays), and the use ends (3746). The
  next open gives the kept items, with no new roll. No item is lost. No client notice for a full bag was found: the loot notice does not
  come. An item of the roll that the item catalog does not have (or with no item type) has the same rule: no item, and the claim keeps
  the complete roll (Rolled) until the catalog has each item [code review of stage 3.5b.1, finding CH-R3; decision of an agent].
- The reset [game data]: the MinRespawnRate to MaxRespawnRate of the gatherable row when it is above 0 (the ordinary elite chests:
  82,800 s), else the reset of an instanced chest of the base attributes of a player (`Instanced Loot Chest Reset Time Mins`: 60 minutes).
  The client does not end a claim at its reset time [live check L8]: the backend ends it (an entry of the due list), removes the row,
  and sends the map with no entry. The row has the reset time on the wall clock: a restart keeps the remaining time, and the time goes
  on while the backend is off [decision of an agent]. A claim whose reset came while the character was away goes at its next entry.
- The chest file has one chest for each actor: the loader refuses a file with two chest keys of one actor (the actor is the key of the
  LootTracker of the client: a repeated actor hides a different chest) [code review of stage 3.5b.1, finding CH-R4]. The loot file of
  format 2 must have a container floor for each level of its gear score ranges: else the loader refuses it (a level with no floor gave
  weaker gear) [finding CH-R8].
- Stage 1.8e ([Quests](quests.md)): the LootTracker state of a character also has the entries of its destroyed quest objects
  (`Entity::questObjectTracker`: the managed actor of the placement, the state Looted, the kind 0); `encode_loot_tracker_state` adds them
  to the claims of the chests. The client removes the object for that entry [live I0 of stage 1.8e].
- The claims are values of the world with the character id as their key (MP-1): each character has its own claims on one chest, and
  only the owner gets its Gathering state and its LootTracker [research; live check L2]. A second session of the character gives no
  second open [stage 2.5a].
- The LootTracker of a character [decision of an agent for the distances; decision of the user for the count]: its claims 192 m or less
  from it, and an entry with the state Locked for each unavailable chest 192 m or less from it (the client hides the prompt of a Locked
  chest: live check L9). The map is made again when the character goes into a new cell of 64 m, at a claim change, and at the entry.
  The data has a count of 60 (`Max Instanced Loot Chest Count`) [game data]; the client reader takes more [client code 0x3727A00]. The
  store keeps each active claim also above 60, and the map has only the claims near the character: an active claim is never removed to
  give a new reward.
- The cost (MP-8): a request finds its chest by its key (a hash); the map of a character looks at its own claims and at the cells of
  64 m near it; an open is one timer of the world; a reset is one entry of the due list. No look at each chest of the world (test case
  LC8: the same work with 1,000 and with 100,000 far chest definitions).
- The handler of GatherOption of the kind `gathering` (stage 3.6a): the gathering system wraps the chest handler. A chest of the chest
  data goes to the chests; a node of the gathering data to the gathering; an object that neither system knows goes to the chests when
  they have no data (they refuse it: "no chest data"), else to the gathering (it refuses it: "not a node") ([Gathering](gathering.md)).
- The development override of a reset (`tools/world chest reset`, the route `POST /v1/dev/chests/reset`, only with
  `SOULWARDEN_HARNESS=1`): a looted claim resets after 1 s to a week. It is not a rule of the product (live check L8).
- Not in this stage: the loot limits of the data (`LootLimits`: exported, but no chest table has a limit condition), the HWMMult and the
  LootBiasing of the roller, the umbral currency and the territory standing of the event `Gather_LootContainer_EliteChest` (15 and 5 in
  the data: the backend has no such systems yet), the locks, keys, quests, and events (3.5b.3). The personal loot bag of stage 3.5b.2 is
  in [Loot bags](loot-bags.md).

## Commands

```sh
tools/world chests Soulwarden            # the open, the claims, the chests 30 m or less from the character (kind, state, reason, claim)
tools/world chests Soulwarden 100        # the same in 100 m
tools/world chest clear Soulwarden KEY   # remove the claim of the character on the chest KEY (its placement UUID): it can open it again
tools/world chest reset Soulwarden KEY 20  # DEVELOPMENT: the looted claim resets in 20 s (not the rule of the data)
tools/gamedata/chests.sh --list elite    # the elite chests of the data (also: ordinary, unavailable, supply, provision, alchemy)
```

The admin interface has the command `clear_chest_claim` and the member `chests` of the state of a character (the same values as
`tools/world chests`).

## Traps

- A chest needs the focus of its prompt: the key E sends no request when the prompt is not on the chest (too far, or the camera
  looks past it). Go to 2 m and look at the chest.
- After a Looted claim, the state Initial alone does not bring the prompt back until the focus changes; the map with no entry brings it
  back at once (live check L2). The backend removes the entry at the reset.
- The GatherableController state (slot 3, type 12) of the chest object does nothing for an instanced chest: the client skips it (live
  check L2, client code 0x3371590). The state of a chest for a player is only its LootTracker entry.
- Field 2 of the Gathering state is a speed, not the total time (the research named it the total time): 1 gives the time of the
  data; 2.5 made the ring of 2.5 s run in about 0.8 s (live check L3).
- A supply chest does not change its look when it is looted (at night no change was visible); an elite chest shows its depletion
  effect, and its prompt becomes "X ELITE SUPPLY CHEST" with no light of the loot (live check L9).
- The interaction data of stage 2.5b had a condition requirement on each chest bound (an empty condition of the data): the backend
  refused each open. `interactions.py` of stage 3.5b.1 keeps a condition only when it has a value. A backend with an old
  `interaction-data.json` refuses each chest: write the file again.
- The client of a development run can have night: the pictures of a chest are dark. The street at the default spawn has lamps.
- After `tools/world chest clear` the prompt of the chest comes back only after a new focus (a step, a turn of the camera).
- `tools/world fragment` keeps its bytes on the entity: `tools/world touch` sends the map of the backend and then the probe map again
  (the probe wins). A new cell of 64 m (a travel) sends the map of the backend. Do not use `touch` to undo a probe of the LootTracker.
- A walk during an open gives two lines of an open in the log: the 2473 of the walk ends the open, and the next input step repeats the
  block one time (live check L4), which the session takes as a new press (rule INT-R2); its release ends it 30 ms later with no item.
- A chest behind a glyph gate (the variation rows with a lock slice) shows the prompt "X" in the client also with no Locked entry: the
  client knows the gate. A chest of a quest shows no prompt and no light of the loot with the Locked entry.
- A `chest-data.json` of stage 3.5b.1 has only the LootTags of the territories and the areas: 1,228 chests have no tag of their point
  of interest (for example `EPOI`, `Corrupted`, `Elite`), and 48 lock chests have the record of a different row. Write the file again
  with `tools/gamedata/chests.sh` (stage 3.5b.2).
