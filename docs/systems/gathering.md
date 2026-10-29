# Gathering

Read this document before a change of the gathering of the world: the natural nodes, the capital nodes, the skinning of a creature
body, the tools, the rank gate, the work, the materials, the trade skill experience, the depletion, and the respawn. The wire contracts
are in the protocol notes (section 4.44). The interaction base (the request, the reply, the timed use, the cancels) is in
[Interactions](interactions.md), the roll of a loot table in [Rewards](rewards.md), the Gathering state (type 81) also in
[Chests](chests.md). Fishing is stage 3.6b.

## Start here

| Task | Start here |
| --- | --- |
| A gather: the node, the refusals, the work, the speed, the completion, the full bag, the depletion, the respawn | Protocol notes 4.44, `apps/backend/src/world/gathering.h` (`Gathering`, `gathering_rules`, `GatherParts`), `world/interactions.h` (`ObjectResolver`, `ExternalObject`, `Interactions::external_entity`, `level_entity`, `body_removed`), test cases GAT2 to GAT8 |
| The node state (type 12) of a node and of a creature body | `server/rep/states/gatherable_state.cpp`, `server/rep/states/state_codecs.cpp` (the creature and the object codecs), `world/entity.h` (`ObjectBody::nodeSlot`, `nodeState`, `Creature::skinSlot`, `skinState`), `tests/support/state_decoder.h` |
| The skinning of a body | `Gathering::creature_died`, `Gathering::body_object`, `world/creatures.cpp` (the call at a death), test case GAT10 |
| The store and the restart | `state/store.h` (`GatheringNodeRow`, `list_gathering_nodes`, `write_gathering_node`, `delete_gathering_node`; schema 13: the table `gathering_nodes`, the column `world_time.run_ns`), `world/world_time.h` (`WorldTime::run_ns`), test case GAT7 |
| The gathering data (`gathering-data.json`, `gathering/SET/REGION.swgn`) and its tool | `gamedata/gathering_data.h` (`GatheringData`, `natural_group`, `region_valid`), `tools/gamedata/gathering.sh` (`gathering.py`, `check_gathering_tool.py`), the gathering roots of `tools/gamedata/loot_tables.py`, [Game data](../development/game-data.md), test cases GAT1, GR4 to GR6 |
| The corrections of the code review of stage 3.6a (findings GA-R1 to GA-R7) | `tests/review_gathering_tests.cpp` (test cases GR1 to GR7), the review cases of `check_gathering_tool.py` (`gate-columns`, `flag-texts`, `event-reward`, `export-publish`) |
| The commands | `server/http/gathering_commands.cpp`, `server/http/character_commands.h`, `tools/world gathering`, `tools/world gathering respawn` |
| The load | Test case GAT9 (2.5 million placements, 50 characters, 1,000 updates, the due list) |

## Rules

The source of each rule is in brackets: game data, client code, live check (the live checks L1 to L8 of stage 3.6a, protocol notes
4.44), decision of the user (through the orchestrator), decision of an agent.

- The split of stage 3.6 [decision of the user]: 3.6a is the gathering of the resources of the world (mining, logging, harvesting,
  skinning); 3.6b is fishing.
- The nodes [game data]. A natural node is a record of a `region.distribution` file of the world whose slice is a gathering slice: a
  slice with one GatherableController component, a variation table of the ordinary resource tables (`Gatherable_Trees`, `_Bushes`,
  `_Minerals`, `_Alchemy`, `_Plants`, `_Logs`, `_Stones`, `_DyePlants`), and an Interact bound with the option GatherOption. Its variant
  names the gatherable row (the variation table). A capital node is a placement of a layer with a gathering slice (the interaction data
  has it by its UUID). A body is a dead creature whose slice has a GatherableController and an Interact bound with GatherOption (the
  skinning). The retail data: 2,450,322 natural nodes of 2,728,908 records in 49 regions, 33,071 capital nodes, 117 creature slices with
  skinning, 295 gatherable rows (241 that the backend supports), 127 gathering slices, 230 tools.
- The identity [client code; live check L2]. The client gives each record of a distribution file an offline id (0x69D5090, called by
  the record converter 0x6A01D50): G = ((counter & 0x7FFFF) << 13) | ((regionY & 0x1F) << 8) | ((regionX & 0x1F) << 3) | 1, the
  counter 1 for record 0 (the primary records, then the two tails). The actor of the node is G (little-endian) and 8 zero bytes. A
  capital node has the UUID rule of a placement. A body has the entity id of the creature. The server makes no node: the client makes
  each node from its level data, and the server sends only the state of a node that is not Healthy.
- The request [live check L2; stage 2.5a]. E sends GatherOption (action 14, committed) with the G, the actor, the facet target (the XOR
  rule of 4.2: K = root XOR G, target = (entity of the bound XOR K) + component), and the option entity. A natural node and a body are
  not in the interaction data: the resolver of this system (`ObjectResolver`) gives the base their object from the gathering data. The
  base then checks the option, the 3D range of the bound (the data: `rangeSq`), and the requirements, as for a placement.
- The capacity of a bound (MaxInteractors) does not apply to GatherResource (action 14) [client code 0x35C2E60: the capacity check at
  0x35C3858 skips the action]. Two characters can work on one node.
- The restriction ItemRequired with no item (the rows BushSmall and BushWithered) names nothing to check: the backend accepts it as a
  row with no tool [game data; the stage run: the client shows the prompt "E BUSH" of a small bush and sends the request].
- Stage QG5b: the restriction ItemRequired with an item [client: the gather validation 0x35C26E0 takes only the restriction 4 to its item
  branch 0x35C2C8E, the Container check 0x48D6200; static read B1, B2 of stage QG5b; live I2]: the bag must have ItemRestrictionQuantity
  of ItemRestrictionId (the sum of the stacks of the bag slots; no paperdoll, no storage); a blank or 0 quantity needs nothing
  (0x48D626D: 1435_0101_HerbSpot, 1602_0301_Forge). NoToolNecessary and ToolRequired with an item (1104_MedallionChest,
  95_s01_03_HeroesPoster1) do not check it (0x35C2A18). The begin and the completion check it (`check_gates`, the refusal `missingItem`;
  the key that goes during the work ends the work with no result). The gather does not take the item [agent: no removal path in the client,
  static read B4; the data does not decide; a decision for the user]. 28 rows of the build are supported now. Case QG5BB.
- A gatherable row that the backend cannot check is refused [decision of an agent: no predicate]: a restriction that is not
  `NoToolNecessary`, `ToolRequired`, or (stage QG5b) `ItemRequired`, a song, a whisper, an emote, a status effect, a faction, a cost,
  `IsLootContainer`, `OpenAfterGathering`, `RequireLootItems`, `IsFFARestricted`, more than one gatherer, and a gathering type that is
  not a tool type of this stage (AzothStaff, fishing). The tool writes the causes of each row; the first one is in the log line.
- Stage QG5c4: a gathering type that is not a tool type needs no tool with the restriction `NoToolNecessary`: the row has the rule of the
  type None (no tool slot, the speed of no tool) [data: the restriction of the row; agent: the rule; live check L1 of stage QG5c4: the
  client sends the GatherOption of the chest A of the shipwreck of C10A_Q4 (Handcrafting, 3 s) and completes it]. 9 quest rows of the
  build: the 8 Handcrafting rows (the chests A and B of 10A_Q4, the prison cells and the cage of 10A_Q2, the spectral pylon and the
  telescope of 10A_Q3) and `03_SQ_03_GraveDigger` (the type "none" in lower case). The tool (`gathering.py`) and the loader
  (`gathering_data.cpp`) have the same rule; case QG5C4A. A start object of an encounter whose row has a cause refuses its recipe
  ([Encounters](encounters.md)).
- The loader checks the mark "supported" (no cause) against the gates of the row [code review of stage 3.6a, finding GA-R5]: a row with
  no cause and the restriction ItemRequired with no member `item` (a file of before the review; stage QG5b: an item is a gate of the
  gather, no cause), a restriction that is not
  `NoToolNecessary`, `ToolRequired`, or `ItemRequired`, or more than one gatherer gets a cause of the loader ("... (the file marks the row
  supported)"), and each gather of it is refused. The row keeps its item and its count (`GatherableRow::item`, `itemCount`). A member
  `item` that is not a text, or a count that is not a whole number of 0 to 1,000,000, refuses the file. ItemRequired with an empty item
  stays supported (the bushes above).
- The source columns of the gates [code review of stage 3.6a, finding GA-R4; game data]. Each GatherableData table must have the
  columns of the gates of its rows: Restriction, ItemRestrictionId, NumRequiredToGather, ResetGatherTimeOnEndInteraction, and the columns
  of the causes above. A blank cell is no rule; a missing column stops the tool (it is not a false flag). The exceptions of the retail
  data (the census of the supported build, `MISSING_GATE_COLUMNS`): GatherablesDunwood and QuestGatherables have no IsFFARestricted,
  and they and Gatherables_IsleOfNight have no OpenAfterGathering and no ProgressionCurrencyCosts; their rows have no such rule
  (GatherablesDunwood has 9 supported rows). A flag cell must be "", "0", "1", "true", or "false" (any case; the retail tables have one
  "FALSE"); a different text stops the tool. Each GameEventData table must have EventID, CategoricalProgressionId, and
  CategoricalProgressionReward; a blank reward cell is 0 experience.
- The tool [game data; live check L6; client code]. A row of a gathering type needs a tool of that type in its paperdoll slot: 33 the
  axe (Chopping), 34 the sickle (Cutting), 35 the skinning knife (Dressing), 36 the pick (Mining). The weapon row of the item must have
  the type (GatheringTypes). A tool in the bag does not count (live check L6: the prompt "No equipped Axe"; client 0x48FF240). A broken
  tool (durability 0 of a maximum above 0) is refused [client code 0x5C65650]. A row of the type None needs no tool.
- The rank gate [game data]: the trade skill of the row (Tradeskill) at RequiredTradeskillLevel or more.
- Each refusal changes nothing: no work, no state of the node, no row, no experience [project rule]. The reply 3743 has result 2.
- The work [game data; live check L3]. The amount is BaseGatherTime of the row. The Gathering state of the owner (type 81, slot 36) gets
  Success, the amount (field 1), the speed (field 2), the G of the node (field 7), and new reference counters: the client shows the
  ring of the prompt for amount / speed seconds. The use is a timed use of the base with the time amount / speed.
- The speed [live check L7; decision of an agent]. The speed of the tool is MinGatherEFF to MaxGatherEFF of its weapon row, linear over
  the gear score range of the item (the "Gather Speed" of the tooltip: a T5 axe 625 % at 500, 738 % at 550, 850 % at 600). With no tool
  it is 1. The client has no code that composes the tool, the rank, and the effects (the trade skill window shows the rank part as
  "N %", the effects show EFF percents). The backend uses speed = tool x (1 + GatheringEfficiency of the rank + the EFF parts): the
  percents add, and the tool multiplies them. A decision of an agent: it keeps each raw part in the log and in the view, so a later
  rule can change only the composition. Stage QG1: the GainGatherRate card of the territory at the position of the character is one more
  part of the sum (`GatherParts::territory`, the member `territoryCard` of the view; 0.05 at the level 1) [agent: the static read of stage
  QG1 found no client code that applies the card; the data calls it "gathering speed", and the card text says "Increase Gathering speed in
  Windsward"].
- The EFF and ROL parts [game data]: the cells `EFF<skill>` and `ROL<skill>` (for example `EFFMining`) of the active statuses of the
  character (a food), of the status of each attribute threshold that it reaches (`ATB_Str_Bonus_25_2_Effect`: EFFMining 0.1), and of
  the perks of the tool (the StatusEffect of an affix, the OnEquipStatusEffect of a perk ability). The MULT and INS parts and
  GatheringMultiplier are kept in the view and not used [decision of an agent: no client rule for them was found].
- The luck [client code; decision of an agent]: RollBonus of the rank plus the ROL parts is the luck of the roll. It adds to the bonus of
  each table of the path, as a numeric condition of a table does (the roller 0x368B070): AddToRoll adds it to the roll, ClampMax raises
  the low end of the roll, IgnoreBonus ignores it.
- A work ends before its time with no item and the Gathering state Failure [game data; live check L4]: the release of the client
  (2473), a walk out of the range, a travel, the death, damage (the action Gather ends at a hit: `playeractions_gather.actionlist`),
  and a source that went (a body that left the world). Failure ends the use of the client; None does not (live check L4).
- The rest of a work [game data; decision of an agent]: a row with ResetGatherTimeOnEndInteraction false keeps the rest of the work of a
  character on the node: the next gather of the same node (the same generation) starts with the rest. The rest stays only while the
  character is in the world. A row with the flag true starts with the whole amount.
- A new request before the release [code review of stage 3.6a, finding GA-R3]: the base calls begin of the new use before it ends the
  old use. Begin only prepares the work (`m_pending`); the end of the old use keeps the rest of the old work first; then `started()`
  makes the prepared work the work of the character and sends Success. A new request that the handler or the base refuses leaves the
  old use and its work as they were.
- The completion. At the end of the time the base checks the use again (the source, the life, the exact position, the range). Then, in
  this sequence: the node is not depleted (two characters at one node: both can begin; the first valid completion gets the materials
  and depletes the node; the work of the other ends with Failure and 3746) [decision of an agent through the orchestrator]; the tool
  and the rank again [decision of an agent]; the roll of the loot root of the row (FinalLootTable) with the roller of the rewards and
  the luck [game data; client code 0x368B070]; the trade skill experience of the game event of the row (CategoricalProgressionId and
  CategoricalProgressionReward) [game data].
- One transaction of the store [project rule, MP-7]: the items (the cause ItemGathered: the loot notice "+N NAME"), the experience, and
  the row of the depleted node. A write that the store refuses changes nothing: no item, no experience, no depletion; Failure and 3746.
- A full bag [decision of the user through the orchestrator]: when the bag has no place for the whole result, the gather is refused
  before the commit: no item, no experience, no depletion; the node stays; Failure and 3746. An item of the roll that the item catalog
  does not have has the same rule. The roll stays with the character (in the memory) for its next completion on the same node and
  generation: no new roll, and the whole quantity (a roll can be more than one stack: the grant makes the stacks) [the rule of the
  chests and of the loot bags, code review of stage 2.2, finding WT-R1]. A character has one kept roll for each node [code review of
  stage 3.6a, finding GA-R7]: a full bag at another node keeps a second roll and does not replace the first one; a completion removes
  only the roll of its node; a roll of a node that changed since (depleted or respawned) is not used, and goes at the next kept roll. A
  leave of the character ends its kept rolls.
- The depletion [live check L2]: the node gets the state Depleted (8) in the slot of its GatherableController (type 12), and each
  session that observes the node gets it (one state, shared: research Q1). For a natural node the base makes an object entity with the
  managed actor of the node at its place (one time); for a capital node the entity of its placement; for a body the creature entity.
  The client hides the node and its marker at Depleted (a tree falls: the stage run). The owner gets Done (the client ends its use).
- The respawn [decision of the user through the orchestrator]: one uniform draw between MinRespawnRate and MaxRespawnRate of the row at
  the depletion, never at a change of interest. The node gets Healthy (4) at that time (the field replenishTime is not sent: live check
  L5, the client restores no node by itself). The due list of the world has one entry for each depleted node.
- The run clock [decision of the user through the orchestrator]: the respawn time is a time of the run clock of the world
  (`WorldTime::run_ns`: the time that the backend ran, saved with the cycle each 60 s and at a stop). A restart keeps the remaining
  respawn time, and the downtime does not count (as the cycle of stage 2.2). A crash loses 60 s of the run clock at most. A work in
  progress at a stop ends (nothing is given).
- The store [project rule]: the table `gathering_nodes` has a row only for a depleted node (key, source and version, row id,
  generation, the respawn on the run clock, the depletion time). A stored row whose node is not in the data now (another version of the
  distribution file, a region or a placement that the data does not have, another row) goes at the start [decision of an agent]. Only
  a lookup in data that the backend can read proves this [code review of stage 3.6a, finding GA-R2]: when the region file of the row is
  not there, not complete, or not the file of the document (its digest), or when the backend has no interaction data (a capital node),
  the row stays in the store with its respawn on the run clock, and no node of it can be gathered in this run (the base refuses each
  request of the region; the log names the region one time). The counts have `keptRows`. With good files at a later start, the node
  is depleted until its saved time (the run clock went on while the backend ran).
- The skinning [game data; research Q1; decision of an agent for the entry]. At the death of a creature whose slice has a skinning row
  the body gets Healthy in the slot of its GatherableController. A skinned body stays Depleted until the creature system removes it (at
  the respawn of its spawn point, or by a command): a body has no respawn of its own, and no row in the store. The node state of the
  body (`Creature::skinState`) is the rule of a request and of a completion [code review of stage 3.6a, finding GA-R1]: a body with
  Depleted is refused, also with no entry. The entry of a skinned body stays while the body is in the world, and it goes at the
  removal of the creature (`World::remove_entity` calls `Gathering::body_removed`); the due list also looks at the body each 10 s
  (`kBodyCheck`) and removes an entry whose body is gone [a time is no proof of the removal]. A new creature is a new entity: a new node.
- Tool durability does not change in this stage [decision of the user through the orchestrator: the audit of stage 3.7; the client
  registers the loss as false].
- The cost (MP-8): a request finds its node by its key or its G (a hash of the region and an index of its record; the file of a region
  only at its first use); a work is one timer of the base; a respawn is one entry of the due list (a skinned body too: one look each
  10 s while it is in the world); the tick looks only at the works and at the due entries; a depleted node is an object entity in the
  grid of the base. No look at each node of the world (test case GAT9: the same work with 25,000 and with 2.5 million far placements,
  one region file read).
- The development override of a respawn (`tools/world gathering respawn`, the route `POST /v1/dev/gathering/respawn`, only with
  `SOULWARDEN_HARNESS=1`): a depleted node respawns after 1 s to a week. It is not a rule of the product. On a skinned body it changes
  only the time of the next look at the body.
- One set of files [code review of stage 3.6a, finding GA-R6]. The tool writes the region files into a staging folder
  (`gathering/.staging-PID`), checks each one against the document (its size and its digest), moves the folder to `gathering/SET` (SET:
  16 hexadecimal digits of the SHA-256 of the region digests), and writes `gathering-data.json` last. An export that stops leaves the
  published set as it was. The entry of each region in the document has `digest`: the first 16 hexadecimal digits of the SHA-256 of its
  region file. The loader refuses a region file with another digest (a file of another export): no node of the region (GA-R2 above for
  its stored rows). A document of before the review has no digest: no check. After a publication the tool keeps each published set
  and removes only the staging folders of stopped exports: a backend that runs holds the manifest of its start and reads its region
  files later, also after more than one publication (code review of user test 2, finding U2-R6; the game of the user can run from
  Steam while a stage writes the common game data folder). `tools/gamedata/gathering.sh --prune` removes the sets and the region
  files of the layout of before that the current manifest does not name; it refuses (status 1, no change) while a process
  `soulwarden-backend` runs (case `prune` of `check_gathering_tool.py`).
- Not in this stage: fishing (3.6b); the durability loss, the refining, and the crafting (3.7); the observer action of a gather (the
  animation of a different player: type 11, not derived); the yield multiplier (GatheringMultiplier, MULT) and the instant gather
  (INS); the event and special nodes (the unsupported rows).
- Stage 1.8e: `gathering.py` reads the variation table `Gatherable_Quest_Damageable` too (`DAMAGEABLE_TABLES`), with no change of its
  output: its placements are the destructible quest objects of `quest-objects.json` ([Quests](quests.md)), not nodes of a gather.
- Stage 1.8c (the quest objects, [Quests](quests.md)): a placement of Master_Quest_Instanced (the variation table Gatherable_Quest) is a quest
  object. Its capital entry has the InteractTag and InteractOnlyOnQuest of its variant; its row is marked quest. A gather of it needs an Active
  quest task of the character with the tag (else the refusal `notOnQuest`; live check G1: the client shows no prompt then); it needs no tool
  and no loot table; its result is the quest drops of the tasks of the character (no roll of the loot root Empty); it writes no node row and
  sends no node state (each character gathers its own object: InstancedLootType 4). After the commit of each gather the quest system gets
  the items of the roll (TaskRequireGatherItems), the game event of the row (TaskGameEvent), and one completed interaction with the tag of a
  quest object and the tag of the bound, each tag one time (TaskInteract). The gathering owns this event (`owns_interact_event`): the base
  gives no completed interaction for a gather, also not at the start of a gather with no Delay Time, and a gather that the handler refuses
  (a full bag, a contested node, a store fault) gives none (`Gathering::complete` gives false; code review of stage 1.8c, findings
  QR8C-R8, R12, R13; cases QRW8, QRW12, QRW13).
- Stage 1.8c (the quest drops): `Quests::plan_drops` gives the quest drops of the tasks of the character whose ItemDropVC names the gatherable
  row (a skinning of a Boar: the row Boar); they are a part of the result (one transaction with the award rows of AwardOnlyOnce; a full bag
  refuses the whole gather) and not a part of a kept roll. Stage QG6b: the gather of a skinned body also matches the categories of its
  creature (the separate category, the categories of the base row, one direct parent of each; not the id of the vitals row): the hunt
  missions name spawn categories such as `Wolf_Black_02BW06` [client: the direct key 0x3B03A21 to 0x3B03A4E, then the category helper
  0x5C75830; static read `~/Games/soulwarden/work/evidence/quests/qg6b/implement/static/gather-drop/GATHERDROP.md`; case QG6BF]. One drop
  for each task (the planner visits each task one time).

- Stage QG5a (the quest objects, [Quests](quests.md)):
  - the quest tables are Gatherable_Quest, Gatherables_NPCRescue_01 (the rescues; its column GatherableEntryId has a lower-case d), and
    Gatherable_Quest_AncientGlyph (the glyph locks); a quest placement of any slice path is a quest object (not only slices/gatherables/),
    with the variant of the placement, else the m_selectedVariant of its slice [data]. The ordinary nodes stay the placements of
    slices/gatherables/ [agent: no change of the ordinary gathering];
  - a direct binding (a gathering slice with no variation table) is a quest object when its row has the form of a row of the quest tables:
    a loot container with the loot table Empty or blank and a GameEventID (`quest_form`); its tag and InteractOnlyOnQuest are the ones of
    its own GatherOption bound [agent]. Stage QG5b: also a quest gimmick (`task_event_form`): no tool (GatheringType None), the loot table
    Empty or blank, and a GameEventID that a quest task names (KillEnemyType of the task tables, `Tables.task_events`) [agent: the form of
    the rows; the cannon Gimmick_BreakingWallsCannon_Vignette (Handcrafting, 2 s) of 12A_MSQ_08_DestroyRune, 6 placements; live I4 of stage
    QG5b: the gather completes the task, and the shot of the server timeline of the slice does not play];
  - a quest child of a PrefabSpawner of a placement (not client-authoritative, made at the init) is the member `children` of the file, with
    the key "UUID/ENTITY" of its object of the interaction data [agent: the key of the person children];
  - the gathering type AzothStaff is a tool type: the Azoth Staff in the paperdoll slot 38 [client 0x5CAB873, the tool query 0x48FF240], the
    weapon rows with GatheringTypes AzothStaff, and the rank gate of the trade skill AzothStaff of the progression tables [data];
  - RequireControllingFaction is a flag (UNSUPPORTED_FLAGS) [client: the flag converter 0x5BF6BC0 at 0x440DC07]: "0" is no rule (557 rows of
    the data, most of them quest rows that the tool refused before);
  - the per-character state of a quest object: after the commit of a gather the character has a LootTracker entry Looted of the actor of
    the object (`Entity::questGatherTracker`), and its second gather is refused (`gatheredQuestObject`) until a uniform draw of the respawn
    bounds of the row on the world clock (`Gathering::quest_object_due`); no node row and no node state [the rule of the destructible
    objects, stage 1.8e]. Live I4 of stage QG5a: the trapped boar and its marker go; a glyph tablet (no instanced slice) keeps its prompt.


- Stage QG5c2 ([Encounters](encounters.md)): a start object of an encounter is a node of the kind `NodeKind::encounter`. Its row and its
  template come from the member `encounterObjects` of `gathering-data.json` (`[TEMPLATE, ROW, TAG, ONLY_ON_QUEST]`), its entity is a server
  child of the encounter system, and `encounter_object_made` gives it the node state Healthy [agent]. The gather has the rules of its row
  (the time, the end at damage); at the completion the gatherer gets the game event of the row, the object goes, and the encounter counts
  it (`Encounters::object_gathered`); the node has no respawn of its own (the next instance makes a new object) [agent; live L4].

- Stage QG5c5 ([Encounters](encounters.md)): an encounter start object can have an ordinary row: a variant of the variation table
  `Gatherable_Darkness` (the pylons and the cores of the Darkness events: AzothStaff, ToolRequired, the rank 5 of AzothStaff, a loot table)
  or a direct row with no tool, the loot table Empty or none, and no game event (the spyglass of the boat reinforcements:
  `BoatEncounter_Scallywag`) [data; `gathering.py` `encounter_capital`; agent: the second form]. The gather has the rules of its row (the
  tool, the rank, the time); its items and its experience as a node; no node row and no node state (the next instance makes a new object);
  at the completion the encounter counts it. A row with the loot table Empty or none of an encounter object has no roll, as a quest row
  (`Gathering::no_roll`) [agent; live QG5c5 L3: with no such rule the spyglass was refused: "the loot tables have no table of the root of
  the row"]. The gathering tool reads the alias assets (`sharedassets/genericassets/aliasassets/`) for the spawns of an alias.
- Code review of stage QG5c5 (corrected in stage QG7; case QGR5C5E) [review QG5C5R-R5]: the first completed gather of an encounter object
  with an ordinary row claims the object (`Gathering::m_claimedObjects`); a second gather of the same object is contested (no second
  reward). The claim ends when the object goes (`World::remove_object` calls `Gathering::object_removed`).

## Commands

```sh
tools/world gathering Soulwarden          # the work, the speed and the luck of each trade skill, the nodes with a state 30 m or less
tools/world gathering Soulwarden 100      # the same in 100 m
tools/world gathering respawn Soulwarden 'r_+04_+02#18627' 5   # DEVELOPMENT: the depleted node respawns in 5 s
tools/gamedata/gathering.sh --near 8985 5328 10                # the gathering nodes of the data near a place (with their G)
```

The admin interface has the member `gathering` of the state of a character (the same values as `tools/world gathering`).

## Traps

- Stage QG5a: a glyph lock needs the Azoth Staff in slot 38 and the rank 5 of the trade skill AzothStaff. Stage QG5b: the hand-ins of the
  main story give the ranks (the categorical progression reward, [Quests](quests.md)); for a test `tools/world tradeskill CHARACTER
  AzothStaff 5` gives the rank at once.
- Stage QG5b: the client checks the item of ItemRequired itself and sends no request with no item (live I2). After the item comes into the
  bag, the client sends the gather only after a new focus of the object: walk out of its range and back (a turn of the camera is not
  enough). Read the log line "gathers the capital node" of the request.
- Stage QG5a: a gathering file of before stage QG5a refuses the quest rows with RequireControllingFaction "0" ("a faction"): write the file
  again with `tools/gamedata/gathering.sh`.

- A node needs the focus of its prompt, and a tool row needs the tool in its paperdoll slot: with no tool the prompt says "No equipped
  Axe", and E sends nothing (no request in the log). Equip the tool and look away and back (a new focus).
- The prompt of a body (skinning) shows only with the knife in slot 35. A body of a creature that the backend killed by command has it
  too.
- Stage 1.8c, live L2: the client showed the prompt "E BLACK BOAR" of one boar body (on the third shipwreck), and no prompt for four
  later boar bodies of `Boar_StarterBeach` at the beach (the white outline of a body was there, the knife was in slot 35, also after
  a new equip, a look away and back, the gather of a near flint, the reset of `06_boarfeast`, and the lock of
  `tutorial_skinning_basic`). The client sent no request, so the backend refused nothing. The cause is open (a rule of the
  client, or a state of the body that the backend does not send).
- The key of a natural node in a command has `#` (`r_+04_+02#18627`): quote it in the shell. The command also takes the G in 16
  hexadecimal digits.
- A tree falls at its depletion with this backend (the stage run: also at its second depletion, on a record of the node that the
  client had for minutes). The probe backend of the live checks (Depleted by `tools/world fragment` on a probe record) gave no fall in
  six variants; the cause of the difference is not known (protocol notes 4.44). Do not use a probe to check the fall of a tree.
- The region files and `gathering-data.json` are one set: write them together with `tools/gamedata/gathering.sh`. A stored row of an
  older version of a distribution file goes at the start. Do not copy a region file from one set folder (`gathering/SET`) into another:
  its digest is not the digest of the document, and the region gives no node. The files of before the code review of stage 3.6a
  (`gathering/REGION.swgn`, no digest) load as before, with no check; write them again to get the check.
- A region file that is not there or that the loader refuses is not an error of the start: the backend starts, the log names the region
  at the first request ("no node of the region can be gathered"), and its stored depletions stay. Write the files again.
- Run one export of the gathering tool at a time into one game data folder: an export removes the staging folders of the folder after
  its publication.
- `tools/world gathering` lists a skinned body while it is in the world (its entry stays; GA-R1); the entry goes at the removal.
- A backend with a `loot-tables.json` from before stage 3.6a has no gathering roots: each gather is refused with "the loot tables have
  no table of the root of the row". The backend log names it at the start. Write the loot tables again (`tools/gamedata/loot.sh`).
