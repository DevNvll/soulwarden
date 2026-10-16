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
| The bags in the store | `state/store.h` (`LootBagRow`, `list_loot_bags`, `write_loot_bag`, `delete_loot_bag`; schema 10: the tables `loot_bags`, `loot_bag_items`), test case GL6 |
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
- The lifetime [game data; live check L6]: KillSelfDelay of the slice (180 s from the bag), and FastKillSelfDelay (1.25 s) after the
  last item of the last part. The client does not end a bag at its time: the server removes the record, and an open panel closes (3746).
  The end time is on the wall clock in the rows: a restart keeps the remaining time, and the time goes on while the backend is off; a
  bag whose time ended while the backend was off goes at the start [decision of an agent]. A give (2801) does not start the time again
  [decision of an agent]. A request after the end finds no bag (test case GL5).
- The cost (MP-8): a request finds its bag by the entity of its use (a hash); the states of a character look only at its own bags; the
  end of a bag is an entry of the due list; the interest of a session finds a bag in the grid of the interaction base. No look at each
  bag of the world (test case GL7: the same work with 2,000 and with 20,000 far bags, 50 characters, the time limit 2 s).
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
