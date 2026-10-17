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
| The gathering data (`gathering-data.json`, `gathering/REGION.swgn`) and its tool | `gamedata/gathering_data.h` (`GatheringData`, `natural_group`), `tools/gamedata/gathering.sh` (`gathering.py`, `check_gathering_tool.py`), the gathering roots of `tools/gamedata/loot_tables.py`, [Game data](../development/game-data.md), test case GAT1 |
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
  row with no tool [game data; the stage run: the client shows the prompt "E BUSH" of a small bush and sends the request]. With an
  item it is refused (below).
- A gatherable row that the backend cannot check is refused [decision of an agent: no predicate]: a restriction that is not
  `NoToolNecessary` or `ToolRequired` (an item, a key), a song, a whisper, an emote, a status effect, a faction, a cost,
  `IsLootContainer`, `OpenAfterGathering`, `RequireLootItems`, `IsFFARestricted`, more than one gatherer, and a gathering type that is
  not a tool type of this stage (AzothStaff, fishing). The tool writes the causes of each row; the first one is in the log line.
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
  rule can change only the composition.
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
  chests and of the loot bags, code review of stage 2.2, finding WT-R1]. A leave of the character ends the kept roll.
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
  distribution file, a placement that the data does not have, another row) goes at the start [decision of an agent].
- The skinning [game data; research Q1; decision of an agent for the entry]. At the death of a creature whose slice has a skinning row
  the body gets Healthy in the slot of its GatherableController. A skinned body stays Depleted until the creature system removes it (at
  the respawn of its spawn point): a body has no respawn of its own, and no row in the store. The entry of a skinned body goes after
  the largest respawn bound of its row, at least 600 s. A new creature is a new entity: a new node.
- Tool durability does not change in this stage [decision of the user through the orchestrator: the audit of stage 3.7; the client
  registers the loss as false].
- The cost (MP-8): a request finds its node by its key or its G (a hash of the region and an index of its record; the file of a region
  only at its first use); a work is one timer of the base; a respawn is one entry of the due list; the tick looks only at the works and
  at the due entries; a depleted node is an object entity in the grid of the base. No look at each node of the world (test case GAT9:
  the same work with 25,000 and with 2.5 million far placements, one region file read).
- The development override of a respawn (`tools/world gathering respawn`, the route `POST /v1/dev/gathering/respawn`, only with
  `SOULWARDEN_HARNESS=1`): a depleted node respawns after 1 s to a week. It is not a rule of the product.
- Not in this stage: fishing (3.6b); the durability loss, the refining, and the crafting (3.7); the observer action of a gather (the
  animation of a different player: type 11, not derived); the yield multiplier (GatheringMultiplier, MULT) and the instant gather
  (INS); the quest, event, and special nodes (the unsupported rows).

## Commands

```sh
tools/world gathering Soulwarden          # the work, the speed and the luck of each trade skill, the nodes with a state 30 m or less
tools/world gathering Soulwarden 100      # the same in 100 m
tools/world gathering respawn Soulwarden 'r_+04_+02#18627' 5   # DEVELOPMENT: the depleted node respawns in 5 s
tools/gamedata/gathering.sh --near 8985 5328 10                # the gathering nodes of the data near a place (with their G)
```

The admin interface has the member `gathering` of the state of a character (the same values as `tools/world gathering`).

## Traps

- A node needs the focus of its prompt, and a tool row needs the tool in its paperdoll slot: with no tool the prompt says "No equipped
  Axe", and E sends nothing (no request in the log). Equip the tool and look away and back (a new focus).
- The prompt of a body (skinning) shows only with the knife in slot 35. A body of a creature that the backend killed by command has it
  too.
- The key of a natural node in a command has `#` (`r_+04_+02#18627`): quote it in the shell. The command also takes the G in 16
  hexadecimal digits.
- A tree falls at its depletion with this backend (the stage run: also at its second depletion, on a record of the node that the
  client had for minutes). The probe backend of the live checks (Depleted by `tools/world fragment` on a probe record) gave no fall in
  six variants; the cause of the difference is not known (protocol notes 4.44). Do not use a probe to check the fall of a tree.
- The region files and `gathering-data.json` are one set: write them together with `tools/gamedata/gathering.sh`. A stored row of an
  older version of a distribution file goes at the start.
- A backend with a `loot-tables.json` from before stage 3.6a has no gathering roots: each gather is refused with "the loot tables have
  no table of the root of the row". The backend log names it at the start. Write the loot tables again (`tools/gamedata/loot.sh`).
