# Loot bags

The personal loot bags of stage 3.5b.2: the bag of the items of a kill (AILoot) and the bag of the discard of a player (Discarded).
Both are the personal loot container of the game data (`slices/instanced_loot_container.dynamicslice`). Protocol notes 4.42. The chests
of the world are in [Chests](chests.md), the roll of a kill is in [Rewards](rewards.md).

## Start here

| Task | Start here |
| --- | --- |
| A bag: its record, its parts, its lifetime, the pickup, the discard, the states of the owner | Protocol notes 4.42, `apps/backend/src/world/loot_bags.h` (`LootBags`, `LootBag`, `loot_bag_rules`), test cases GL1 to GL7 |
| The bag of a kill | `world/rewards.cpp` (`Rewards::killed`: `begin_kill`, `stage_part`, `finish_kill`) |
| The base of the use: the record of a server child, the open, the end | `world/interactions.h` (`InteractionHandler::started`, `Interactions::spawn_container`, `remove_object`), [Interactions](interactions.md) |
| The item moves: an item instance that is made one time and moves | `world/inventory.h` (`make_instances`, `receive`, `release`, `release_instance`, `release_equipped`) |
| The wire: GlobalStorage (type 2938, slot 77), the selected Container (type 1755, slot 76), LootDrop (type 2027), the LootTracker entries of the bags | `server/rep/states/loot_bag_states.cpp`, `container_state.cpp`, `chest_states.cpp`, `server/rep/item_requests.h` (2939, 2942, 2801, 826), `server/rep/rep_session.cpp` (`handle_bag_request`, `handle_paperdoll_drop`) |
| The bags in the store | `state/store.h` (`LootBagRow`, `list_loot_bags`, `write_loot_bag`, `delete_loot_bag`; schema 10: the tables `loot_bags`, `loot_bag_items`; schema 12: an index on `bag_key` of each table and the column `emptied_at` of `loot_bags`), test cases GL6, RLB8, RLB9 |
| The corrections of the code review of stage 3.5b.2 (stage 2.2) | `tests/review_loot_bags_tests.cpp` (RLB1 to RLB10), the rules below with the findings LB-R1 to LB-R9 |
| The data of the bag (`interaction-data.json` format 3, member `lootContainers`) | `gamedata/interaction_data.h` (`LootContainer`), `tools/gamedata/interactions.py`, [Game data](../development/game-data.md) |
| The commands | `server/http/loot_bag_commands.cpp`, `tools/world bags` |

## Rules

The source of each rule is in brackets: game data, client code, live check (the live checks L5 to L7 of stage 3.5b.2, protocol notes
4.42), decision of the user (through the orchestrator), decision of an agent.

- The items of a kill of a creature go into a personal bag at the place of the creature; the experience, the mastery, and the coin stay
  at the kill [decision of the user]. The eligible characters are the characters with the credit of the kill (stage 3.5a); each one has
  its own part with its own roll [decision of the user]. A kill with no item for any character makes no bag. With no loot container in
  the interaction data (a file of format 2) the items go into the bag of the character, as in stage 3.5a [project rule; test case GL1].
- The bag is a server child of the interaction base with the record of its slice: the metadata state, PositionInTheWorld (slot 1),
  Interact (slot 5), and LootDrop (slot 6, the rarity) [game data; live check L5]. Only its eligible characters observe it
  (`ObjectBody::viewers`): a different character gets no record, no LootTracker entry, and no map entry [decision of the user; live
  check L5: a character with no entry sees no bag]. A known reference of a bag or of the Container of a different character changes
  nothing (test case GL3).
- The states of the owner [live check L5]: the LootTracker entry of each bag (slot 33: Rolled with items, Looted when its part is empty;
  the kind 1 AILoot or 5 Discarded; the place; the selector of the rarity particle), the GlobalStorage map (slot 77: the items of the
  part of each bag under the actor of the bag; the item list under the prompt; 30 keys at most, the nearest bags with items first), and
  the selected Container (slot 76: the items of the open bag; the panel). The client clears its selected Container at the open: the
  server sends slot 76 after it took the request.
- The selection of the GlobalStorage map: the open bag first, then the nearest bags with items, 30 keys at most. The world makes it
  again at each change of a bag of the character, at the open of a bag, and after a walk of 2 m from the place of the last selection
  (`LootBags::moved`, called by `World::place`: O(1) for a short walk; else the bags of the character only, MP-8) [code review of stage
  3.5b.2, finding LB-R3; decision of an agent: 2 m, because the prompt of a bag shows at less than 2 m; test case RLB3].
- The byte limit of the map [finding LB-R5; research storage.md: one bundle of the client takes 256,000 bytes at most, and a key
  replaces the whole item vector of the client]: the map is one fragment. The encoder writes the keys in the sequence of the selection
  and stops before the first key that makes the map field larger than 255,000 bytes. Each key that goes has its whole item vector
  [decision of an agent: fewer keys, not map journals, because a journal does not make the map of the client larger; test case RLB5:
  30 bags of 200 items give 28 keys].
- The rarity of a bag: the rarity of its best item from its perk count (the largest count of each rarity 0 to 4: 0, 2, 3, 4, 5); the
  selector is the rarity, not above the particles of the slice (5) [client code 0x5C528D0; an approximation: no ForceRarity column, no
  gem adjustment; decision of an agent]. A bag of two parts has the rarity 0 [decision of an agent: one LootDrop state for each bag].
- The requests [live check L5]: OpenStorageOption (T, or Tab near a bag; action 26) opens the panel; TakeAllOption (E; action 28) takes
  each item of the part that fits. 2939 (Take All of the panel) and 2942 (one item, or a part of a stack) name the selected Container of
  the character, not the bag: the bag is the bag of the open use of the character, never a value of the request. 2801 gives an item of
  the bag of the character into the open bag.
- A pickup is one transaction of the store: the bag rows of the character and the rows of its part [project rule, MP-7]. A full bag
  takes what fits, and the rest stays in the part [decision of the user]. No item is lost, made again, or rolled again. An item keeps
  its instance values: the instance id (a whole stack keeps it, a part of a stack gets a new id), the gear score, the durability, the
  flags (the bind flag), and the perks [project rule; test case GL2; live: the recovered Miner Goggles kept their id, gear score,
  durability, and bind flag].
- After a Take All of the prompt with items left the use ends (3746); with no item left the client ends it after the Looted entry
  [live check L6].
- The discard [live check L7]: 1838 (a stack of the bag into the area "Drop items") and 826 (an item of the paperdoll) put the items
  into a new Discarded bag at the feet of the character, private to the character. A locked item does not move. The recovery is the
  pickup of the bag (test case GL4).
- An item with Nonremovable or NotDroppable in its row of the item table does not leave the character: 1838 and 826 make no Discarded
  bag, and 2801 does not give it into a bag. The request changes nothing (the inventory error `itemProtected`, before any write) [game
  data; client: the ground drop target refuses nonremovable equipment, research chests-ground-loot.md; finding LB-R1; test case RLB1;
  [Items and equipment](items-and-equipment.md)].
- A part has 500 stacks at most (the Container of the client reads 500 elements; research storage.md V17). A give (2801) into a part
  with a free place adds a new stack (the item keeps its instance id). A give into a part of 500 stacks goes into a stack of the same
  item (the same item, bind flag, and perks) with a place for all of the quantity. With no such stack the give is refused (the refusal
  `partFull`), and the item stays with the character. The check of the part is before the release of the item [finding LB-R4; decision
  of an agent: all of the quantity or nothing; test case RLB4]. A discard (1838 has 501 entries at most) that would make a part of more
  than 500 stacks is refused (`partFull`): nothing changes. A roll of more than 500 stacks keeps the stacks after the first 500 as kept
  items (see the next rule). A stored part of more than 500 stacks (only from a backend before stage 2.2) keeps the stored stacks after
  the first 500 as waiting instances with all their fields (`BagPart::waiting`: the instance id, the durability, the bind flag); the
  client does not see them, and a take that frees a place shows them, in their order [code review of stage 2.2, finding WT-R2; test
  case RWT2].
- A kept roll keeps its whole quantity in the store, also above the stack limit of 10,000: a roll can be more than one stack. A load
  with a catalog that has the item makes its stacks only when all of them fit into the part; else the whole roll stays kept [code
  review of stage 2.2, finding WT-R1; test case RWT1].
- An item of a kill that the item catalog does not have stays in the part as a kept item: a row with no instance id (the item, the
  quantity, the gear score of the roll, the perks). The client does not see it: no map entry and no panel element. A part with only
  kept items is Looted for the owner, and the bag has no fast end: it ends at its lifetime. A log line names the item. A load with a
  catalog that has the item makes its instance (the start values of the item table, and the gear score of the roll when the roll has
  one) when the part has a place for all of its stacks [decision of the orchestrator: no item is lost, the rule of the chests (finding
  CH-R3 of stage 3.5b.1); test case RLB10].
- The lifetime [game data; live check L6]: KillSelfDelay of the slice (180 s from the bag), and FastKillSelfDelay (1.25 s) after the
  last item of the last part. The client does not end a bag at its time: the server removes the record, and an open panel closes (3746).
  The end time is on the wall clock in the rows: a restart keeps the remaining time, and the time goes on while the backend is off; a
  bag whose time ended while the backend was off goes at the start [decision of an agent]. A give (2801) does not start the time again
  [decision of an agent]. A request after the end finds no bag (test case GL5).
- The fast end is in the rows too: the take of the last item of the last part writes the absolute fast end into the row of its
  character (`emptied_at`, schema 12), in the transaction of the take. The loader takes the largest value of the rows of a bag whose
  parts are all empty. A restart does not renew it, and a bag whose fast end came while the backend was off goes at the start. The bag
  keeps its lifetime too [finding LB-R9; decision of an agent: the largest value, because each new empty time writes a later value;
  test case RLB9].
- One end check for each operation [finding LB-R2]: the open (TakeAllOption, OpenStorageOption), the takes (2939, 2942), and the give
  (2801) are refused at the end of the lifetime and at an active fast end, before a transfer can clear the fast end. The result does
  not depend on the order of the request and the due tick (test case RLB2). A give before the fast end refills the bag, as before.
- The cost (MP-8): a request finds its bag by the entity of its use (a hash); the states of a character look only at its own bags; the
  end of a bag is an entry of the due list; the interest of a session finds a bag in the grid of the interaction base. No look at each
  bag of the world (test case GL7: the same work with 2,000 and with 20,000 far bags, 50 characters, the time limit 2 s). The end of a
  bag deletes its rows through the index of the bag key (schema 12, finding LB-R8; test case RLB8: no full scan with 20,000 far bags,
  and 100 due ends in one tick below 1 s).
- The loader of the interaction data refuses a LootDrop slot that is the slot of the PositionInTheWorld, BuildableController,
  Ownership, or Vitals component, or of an Interact bound, of the template of the container [finding LB-R6; test case RLB6].
- Not in this stage: the shared bag of a death, the permissions of a bag for a group, the trade between players [decision of the user];
  the special locks, keys, quests, and events (3.5b.3).

## Commands

```sh
tools/world bags Soulwarden              # the open bag and each bag with a part of the character (kind, source, distance, end, items)
tools/world loot Soulwarden on           # DEVELOPMENT: each kill of the character drops loot and coin (the item roll can still give nothing)
tools/world ignore Soulwarden off        # a character that the creatures ignore gets no reward of a kill: no bag
```

The route is `GET /v1/dev/lootbags?character=ID`. The admin interface has the member `lootBags` of the state of a character (the same
values as `tools/world bags`).

## Traps

- A character that the creatures ignore (`tools/world ignore NAME on`) gets no reward of a kill and no bag. Set it to `off` for a test
  of a kill (with `SOULWARDEN_CREATURE_BEHAVIOR=0` the creatures do not attack).
- The label of the panel: the kill bag of the backend shows "LOOT" (the final run); a Discarded bag shows "DROPPED ITEMS". In the
  probe of live check L5 (the entry after the record, by `tools/world fragment`) both kinds showed "DROPPED ITEMS": the label does not
  come from a later entry.
- The panel needs slot 76 after the request: a selected Container that came before the open is cleared by the client ("0 items").
- The prompt E of a bag is Take All, not the panel. Walk to less than 2 m and look at the bag; a walk can go past it.
- The HUD shows no time of a bag. The DelayedEvent state of the slice changes nothing in the client (live check L6): do not send it.
- An old `interaction-data.json` (format 2) has no loot container: the items of a kill go into the bag of the character. Write the
  file again with `tools/gamedata/interactions.sh`.
- An `item-catalog.json` from before stage 2.2 (12 columns) has no removal protection: a quest item can then go into a Discarded bag.
  Write the file again with `tools/gamedata/items.sh`.
- A bag of schema 10 that was empty at the restart has no stored fast end: it gets a new fast end of 1.25 s from the start.
- A kept item gets a new instance id at each start, until a take or a give writes its part again.
- The roll of an item that the item catalog does not have has no gear score and no perk (the range and the perk places come from the
  row of the item): a later load makes the item with the start gear score of its row (as the kept roll of a chest).
- A stored part with more than 500 stacks (only from a backend before stage 2.2): the stacks after the first 500 wait with all their
  fields (since stage 3.6a; a backend of stage 2.2 made them kept items with full durability and no bind flag). The panel shows them
  only after a take.
- The storage map of many large bags has fewer than 30 keys (the byte limit): a far bag can show no item list under its prompt until
  the character comes nearer.
- `Store::list_loot_bags` joins the item rows to their parts with a linear search: its work grows with the number of item rows times
  the number of parts. It is a cost of the start only, not of the world thread after it (not measured; not a finding of the review).
