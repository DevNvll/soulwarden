# Items and equipment

Read this document before a change of the bag, the coin, the equipment (the paperdoll), or the items
of an archetype, and before a live test with the bag window or the inventory screen. The wire
contracts are in the protocol notes (sections 4.10 and 4.11; the perks of an item in 4.23). What the
perks and the gear give a character is in [Gear and perks](gear-and-perks.md); the use of a consumable
in [Consumables and the heart rune](consumables-and-heart-rune.md).

## Start here

| Task | Start here |
| --- | --- |
| Items, the bag, the coin of a character | Protocol notes 4.10, `apps/backend/src/world/inventory.h`, `tools/world give`, `tools/gamedata/items.sh` |
| Equipment: the paperdoll, equip and unequip, visible gear, the items of an archetype | Protocol notes 4.11, `apps/backend/src/world/inventory.h`, `server/rep/states/paperdoll_state.cpp`, `tools/world equip` |
| A whole build in one step (the build preset), the gear score limit of a give | [Build presets](build-presets.md), `Inventory::plan_build`, `Inventory::give_limit`, test cases BP1 to BP9 |
| The weight of the bag and its limits (encumbered, overencumbered) | `Inventory::encumbrance` (`world/inventory.h`), `tools/gamedata/item_catalog.py` (`item_weight`), `EquipLoadData::encumbranceMax` (`gamedata/perk_data.h`), test cases CF6, CF13 |
| The perks of an item instance: the places, the swap, the give with perks, the perk command, the loot roll | Protocol notes 4.23, `world/item.h` (`ItemInstance::perks`), `world/inventory.h` (`checked_perks`, `set_perks`, `resolve_perks`), `gamedata/perk_data.h`, `world/perk_roll.h`, test cases IZ1 to IZ3 |
| The plan of a craft (stage 3.7a): the costs from the unlocked bag and the quick slots, the output of each job with the cause ItemCreated | `Inventory::plan_craft`, `coin_written`, [Crafting](crafting.md), test cases CRAFT2, CRV1 |
| The open of a loot container item, message 1733 (stage 1.8d) | `world/item_containers.h`, `Inventory::plan_open`, `RepSession::handle_salvage`, `LootTables::container_table`, protocol notes 4.57, test cases QD2, QD6 |
| The look of the armor in the main menu (stage U4) | `world::menu_appearance`, `world::menu_equipment` (`world/inventory.h`), the columns `armorAppearanceM` and `armorAppearanceF` of the item catalog (format 5), [Identity and characters](identity-and-characters.md), test cases ME1 to ME8 |

## Rules

- The items and the coin of a character are in its entity (`world/item.h`: the bag is a
  vector of slots, as in the client) and in the tables `items` and `currencies`, with the
  character id as the owner key. Only the inventory system (`world/inventory.h`) changes
  them: it names a character, makes the change on a copy, writes all rows of the change in
  one transaction, and then changes the entity and marks the state. A request that is not
  correct changes nothing. A command for a character that is not in the world changes its
  rows. The database has the `ItemID` text of an item; the index on the wire comes from the
  item catalog at each load.
- The equipment of a character is in its entity too (`world::Equipment`: a map from the slot
  number of the paperdoll to a slot) and in the same table `items`: an equipped item is a
  row with the container 2 and the slot number of the paperdoll. The inventory system has
  equip, unequip, and the swap of two slots: a move between the bag and the paperdoll is one
  change, thus one transaction, and the item keeps its instance id. The slot of an equip
  must take the class of the item (`world::paperdoll::slot_accepts`, the rule of the
  client). A slot that loses its item stays in the map as an empty slot while the character
  is in the world: the client removes an item only when it gets the slot with no item.
- A request of a client for its equipment that the backend refuses gets the message "equip
  item fail" (type 3626) with the facet target and the entity of the request. A request
  that names the entity of a different character changes nothing and gets no reply.
- The lock of an item protects it from a request of a client that takes it away from the
  character (the drop). It does not stop an equip, and a development command has no lock rule.
- The removal protections of the item table (stage 2.2; code review of stage 3.5b.2, finding LB-R1): an item with Nonremovable or
  NotDroppable in its row (the catalog columns `nonRemovable` and `notDroppable`; for example the quest items, the Azoth staffs, and
  the artifacts) does not leave the character by a request of a client. The inventory discard (1838, with and with no loot
  container), the equipped discard (826), and the give into a loot bag (2801) refuse it with the error `itemProtected`, before any
  write (`Inventory::release`, `release_equipped`, `drop`). A development command has no such rule. A catalog file from before stage
  2.2 has no protection [game data; client: the ground drop target refuses nonremovable equipment; test case RLB1].
- The drop of a player (stage 3.5b.2; [Loot bags](loot-bags.md)): 1838 (a stack of the bag) and 826 (an item of the paperdoll) put
  the item instances into a new Discarded bag at the feet of the character, private to it, in one transaction with the bag rows; the
  character can take them back until the end of the bag (180 s). A whole stack keeps its instance id; a part of a stack gets a new id.
  An equipped item refreshes the gear. With no loot container in the interaction data (format 2) 1838 removes the items, as before.
- A new character gets the items of its archetype (the backstory table of the game, in the
  item catalog): the inventory module makes the rows (`world::start_items`), and the store
  writes them in the transaction that inserts the character. The backstory table also has
  rows of tests with many items: the create route makes a character only for a backstory of
  the character creation (the member `creationBackstories` of the catalog: the seven
  archetypes and the default backstory of the client), and it gives the reply 400 for a
  different one. A catalog with no such member gives no item to a new character.
- The perks of an instance (stage 1.3b). An item has five perk places: the PerkID text of each
  place (a gem is a perk of a place). The database has a column for each place (`perk1` to
  `perk5`, schema 7: an older database gets empty places). The wire has the index of the
  perk in the perk list of the client, from the perk data at each load (a perk that the data
  does not have is an empty place for the client; the row keeps it).
- A give with perks and the perk command (`Inventory::set_perks`) check each perk: it is in
  the perk data, in one place, one gem at most, a perk for the item class (one class of its
  ItemClass, no class of its ExcludeItemClass), and not deprecated. Then the swap table of the
  game changes a perk as the client does (an ItemId row first, else the first class row whose
  classes the item has; two or three passes): the row has the perk that the client shows and
  uses. A swap can take a perk out (the item has its new perk already): the other perks move
  forward. Two perks that swap to one perk keep it in one place (code review of stage 1.3b,
  finding F3). A stack takes only an item with the same perks; the gear score does not stop it.
- The client shows the fixed perks of an item definition also when the instance does not
  have them (the heart rune, the opal jewelry): the backend does not write them into the
  instance of a give, and the gear system counts them. From stage B2 a perk of the instance in
  a place replaces the fixed perk of that place (a slotted charm: [Gear and perks](gear-and-perks.md),
  test case BC10).
- A loot item gets the perks of the roll of the client (`world/perk_roll.h`, protocol notes
  4.23): each instance of a drop has its own roll, with the level of the character.
- A crafted procedural item (stage 3.7c, [Crafting](crafting.md)) gets the craft gear score as its gear score (also above the give limit
  of its item table, `GiveOptions::exactGearScore`) and the perks of the craft roll of the same generator (`roll_craft_perks`) in the
  transaction of its job, with the cause ItemCreated. Its rarity comes from its perk count (the client).
- The rarity of an item is a value of the client: it computes it from the perk count.
- The plan of a craft (stage 3.7a, `Inventory::plan_craft`, [Crafting](crafting.md)): the costs of a job (the quantities of the recipe)
  come from the unlocked stacks of the bag in the order of the slots, then from the quick slots 14 to 17 [client: the cache of the crafting
  screen counts the unlocked bag slots and quick slots 1 to 4; the order is a decision of an agent]; a stack that goes to 0 leaves an empty
  slot (the cause ItemRemoved). The output of the job goes into the bag (stacks of the item first, then empty slots, also a slot that a
  cost freed) with the cause ItemCreated (3: the result screen of the client takes it); a plan whose output does not fit is refused
  (`bagFull`), and the loot overflow of `grant` does not apply. A missing quantity is `missingMaterials`. The plan of a batch makes its
  jobs one after the other in this order, as the jobs of the batch run (code review of stage 3.7a, finding CR7A-R1; test case CRV1):
  a bag that has no place for the output of a job before the last job refuses the whole batch. Before the correction the costs of all
  jobs left first, and a batch was accepted and then stopped at a job. The crafting system writes the rows of a plan and the coin of the
  fee in one transaction (`write`, `coin_written`, `finish`). From stage 3.7b the output has its cause (`CraftOutput::action`): ItemAdded
  (0) for a job before the last job of a craft-all batch, else ItemCreated (the result screen of a refining batch: [Crafting](crafting.md)),
  and the output of a job can have extra units (decision D2).
- The look of an item in the main menu (stage U4, [Identity and characters](identity-and-characters.md)): an item of the paperdoll slots
  0 to 4 with the class tag `Armor` has the CRC-32 of the lower-case name of its cell `ArmorAppearanceM` (a male body) or
  `ArmorAppearanceF` (a female body) [client code 0x673EE40; game data: the master item tables, 14,992 armor rows of these slots, each
  with both names]. Each other item (a weapon, jewelry, a tool, a skin display item with no tag `Armor`, a row with an empty name) has
  no look in the menu (`world::menu_appearance`). Gear score, perks, durability, and tier do not change the look.
- The gear score of an equipped item (stage 1.1b): an equip of an item above the MaxEquipGS of the owned expansion (800;
  game data, the rule of the client in `equipmentcommon.lua`) is refused (`gearScoreAboveLimit`, the message "equip item
  fail" for a request of a client). With no expansion data there is no such limit. The item level of a gear score (the first
  level whose MaxEquippableGearScore takes it: 700 needs level 65) is a rule of the client that the backend does not check
  yet ([DLC and entitlements](dlc-and-entitlements.md)).
- The gear score limit of a give (stage B0, [Build presets](build-presets.md)): the larger of the largest gear score of the item
  table and the upgrade limit of the item (MaxCraftUpgradeGS of the item catalog format 3: 800 for the 73 artifacts), the upgrade
  limit not above the MaxEquipGS of the owned expansion (`Inventory::give_limit`; game data). A value outside the range gets the
  nearest value of the range with one log line, as before stage B0 (an agent decision; test case BP2).
- The plan of a build (stage B0, `Inventory::plan_build`, `check_build`, `write`, `finish`): the give and the equip of each item of a
  build on a copy, with the checks of the single commands, and no write; the build preset writes the rows of the plan in its
  transaction and then finishes the plan (the entity and the marks). `Inventory::change` uses the same three steps for each other
  command of the system.
- The reward item of a DLC pack (stage 1.1b): each character gets one AmuletIsabellaT1 (the reward of the entitlement
  `STEAM_APP_ID.1200562`) one time, at a world entry, through `Inventory::give` in the transaction of its reward row.
- A committed change of the paperdoll that changes the weapon in the hand (its slot or its instance) tells the abilities system
  (`Abilities::weapon_changed`: the aim of an ability ends, the window of Riposte ends, the shots of a bow volley that did not fly
  end) after the refresh of the gear (code review of stage B0, finding B0-R2; test case RQ2). A change with the same weapon in the
  hand tells nothing. The abilities system compares the slot and the instance too (`Cast::weaponKey`): a new instance of the same
  item in the same slot ends the window (code review of stage B1, finding B1-R5, test case BRV5: the check of the abilities
  compared only the item id and the slot, and a preset with a new instance of the same rapier kept the window).
- The weight of the bag (stage U3, research Q4; `Inventory::encumbrance`): the sum of the weight of each bag slot (the item weight of
  the catalog in tenths, times the quantity). The equipment and the quick slots of the paperdoll do not count. The limit is
  EncumbranceBaseMax of the row Player of the encumbrance table (2000 tenths: 200.0; perk data member `encumbranceMax`, default
  2000). The character is encumbered above the limit (the client shows "Encumbered - Cannot run, dodge, or jump" and "Cannot use
  abilities" at 200.2, not at 200.0), and overencumbered above 1.5 times the limit (the constant of `playerbaseattributes.pbadb`; the
  client shows "Overencumbered - Cannot attack" at 300.2). Client code 0x48D7170 and 0x48D1830 test the weight against the limit.
- The rules of the server for the weight (stage U3; a decision of the orchestrator from the client rule): an encumbered character
  gets no ability cast and no dodge (the verdicts `encumbered` of `CastVerdict` and `DodgeVerdict`: no cast, no cooldown, no stamina
  cost; one log line for the first press of a session with the cause), and an overencumbered character starts no attack from rest
  (`AttackBits::attackAllowed`; the log line `a press at rest starts no attack: the bag is over 1.5 x its weight limit (X of Y)`). A
  chain of an attack that plays goes on. The client sends the press of an ability and of a dodge when it is encumbered (live run
  of stage U3: before the stage the server cast Rapid Shot and started a dodge that the client did not show). The position check
  accepts the slow walk of the client (1.57 m/s at 300.2); the jog at 200.2 is not slower in the client (5.4 m/s; the research said
  1.65 m/s: not seen).
- The weight of an item (stage U3, live check of the inventory screen; `tools/gamedata/item_catalog.py`): the WeightOverride of the
  stats row of the item (the cell ItemStatsRef of the master row names a row of the WeaponItemDefinitions or ArmorItemDefinitions
  tables) when it is above 0, cut to whole tenths; else the cell Weight of the master row. The client showed 10.5 for a great axe
  (Weight 80, override 80) and a rapier (Weight 40, override 25), 1.5 for a light helm (Weight 20, override 15.09), and 2.6 for medium
  gloves (Weight 30, override 26.76). Before this rule the server counted the bag 1.5 heavier than the client and refused an ability
  at 200.0 of the client. 25,645 items of the supported build have such a weight.

- Stage 1.8c (the quests, [Quests](quests.md)): `Inventory::count` (the quantity of an item id in the bag and the paperdoll),
  `Inventory::equipped` (an item of the paperdoll with a gear score at a minimum), and `Inventory::plan_quest_items` (the plan of the item
  actions of a quest in the order of the tasks: the takes from the stacks of the bag, the quick slots, and the other slots of the
  paperdoll, also a locked or a protected item, and the gives with the cause ItemGathered; the quest system writes it in its own
  transaction; code review of stage 1.8c, findings QR8C-R2 and R4). Each change of the bag or the paperdoll state of a character
  (`World::touch`) is the item event of its quests (one look at the next tick).
- Stage 1.8b (the hand-in of a quest, [Quests](quests.md)): the reward items of a quest (ItemReward of the success event, ItemRewardName of
  the objective) are new instances of `Rewards::grant_items` (the rules of stage 1.3b: GearScoreOverride, the fixed perks and the
  ForceRarity of the perk data, the perk places with the level of the character) and go in through `Inventory::plan_quest_items` with the
  return takes of the quest. Each item must fit: a full bag refuses the hand-in, and the quest stays ready (decision D5 option A of the
  orchestrator). A loot container item (the class LootContainer: `QuestReward_MB_Meat_Pouch`) has no open path yet and refuses the hand-in
  (stage 1.8d). The coin of a hand-in goes into the wallet in the same transaction (the units of the wallet: CurrencyReward 150 is 1.50
  coin), up to the wallet limit.
- Stage 1.8d: the rolled perks of a reward item go into the instance (code review of stage 1.8b, finding QR8B-R1; case QRB1).
- Stage 1.8d, the open of a loot container (`ItemContainers`, `world/item_containers.h`) [client; data; live D1 and F of stage 1.8d]:
  the entry "Open" of the action menu of the bag (and a double click) sends 1733 (CraftingComponentServerFacet_InitiateSalvage) on the
  Crafting facet of the player with the bag slot, the instance, and the quantity 1 [research quests-18d rules Q7; live]. The checks: the
  facet is the facet of the character of the session, the container is the bag, the slot has the instance of the request, the item is
  not locked, the quantity is 1 to the stack (100 at most), the item class has `LootContainer`, and the loot data has its table (the
  member `containerRoots` of `loot-tables.json`: the RepairRecipe `[LTID]NAME` of the item row) [data]. Each unit is one roll of the
  table (`Rewards::roll_root`) with the level of the character [agent: the item row of a quest container has no ContainerLevel]; the
  items have the rules of a new instance of stage 1.3b. The take of the container (the cause ItemRemoved) and the items (the cause
  OpenRewardChest, 26) are one transaction (`Inventory::plan_open`). A full bag, a roll with no item, or a refused write changes
  nothing [agent: the rule of a gather]. The client shows its reward popup for the cause 26. Live F: the Meat Pouch ("Essential
  Supplies Cache") gives 10 FoodHealthRecoveryT2 and 5 PotionHealthT1 (the table `QuestReward_MB_Meat_Pouch` is an AND of the two rows)
  [data; live]. Cases QD2 and QD6.

## Commands

```sh
tools/world give NAME ITEM 10        # give 10 of an item (ItemID text) to a character
tools/world items NAME               # the bag and the coin of a character
tools/world coin NAME 123.45         # set the coin of a character
tools/world equip NAME 3             # put the item of bag slot 3 into its slot of the paperdoll
tools/world equip NAME 0 25          # put the item of bag slot 0 into slot 25 (weapon 2)
tools/world unequip NAME 1           # put the item of slot 1 of the paperdoll (chest) into the bag
tools/world equipment NAME           # the equipment of a character
```

- `tools/world` also has the commands for the items and the coin of a character (routes
  `/v1/dev/items`): `items CHARACTER`, `give CHARACTER ITEM [QUANTITY]` (`ITEM` is the
  `ItemID` text of the item catalog, for example `1hSwordT1`, `OreT1`, `PotionHealthT1`),
  `take CHARACTER SLOT [QUANTITY]`, `coin CHARACTER AMOUNT` (for example `123.45`), `move
  CHARACTER FROM TO` (an empty slot gets the item, a stack of the same item takes what it
  can hold, a different item changes places), `split CHARACTER SLOT QUANTITY [TO]`, and `lock
  CHARACTER SLOT on|off`. The character can be in the world or not: a command for a character
  that is not in the world changes its rows, and its next session gets the result. The bag
  window of the client (Tab key, `tools/hx 'key 0x09'`) shows the result at once.
- The perks (stage 1.3b): `tools/world give NAME ITEM 1 --gs 600 --perks P1,P2,P3,P4,P5`
  (an empty part is a place with no perk), `tools/world perks NAME SLOT P1,P2,... [--paperdoll]`
  (the perks of an item that the character has; route `/v1/dev/items/perks`), `tools/world
  perkroll ITEM [--gs N] [--level N] [--count N]` (the perk roll of a loot item, with no
  item; route `/v1/dev/perks/roll`). `tools/gamedata/perks.sh --find TEXT` prints the perks,
  the buckets, and the items with a text. The admin interface has `give_item` with `perks`
  and `set_item_perks`; the tool window has the fields "Gear score" and "Perks".
- `tools/world` has the commands for the equipment of a character (routes
  `/v1/dev/items/equip` and `/v1/dev/items/unequip`): `equipment CHARACTER`, `equip
  CHARACTER BAGSLOT [SLOT]` (with no slot: the slot of the item class, and for a weapon the
  first free one of the weapon slots 24 and 25; an item that is in the slot goes into the
  bag slot), and `unequip CHARACTER SLOT [BAGSLOT]`. `tools/world --help` lists the slot
  numbers of the paperdoll (1 chest, 3 legs, 24 weapon 1). `tools/world items` also prints
  the equipment. The fastest way to a character with gear: `give NAME LightChestT1`, `give
  NAME LightLegsT1`, `give NAME 1hSwordT1`, then `equip NAME BAGSLOT` for each one.
- `tools/gamedata/items.sh` writes `item-catalog.json` to the game data folder: each item of
  the 43 item tables and the 2 house item tables with its index on the wire (46,631 items;
  the list of the client has 46,632 entries with entry 0). The pak entries of the tables have
  Oodle data: the tool uses `SOULWARDEN_OODLE_DLL` with the helper of `tools/research/oodle_loader.c`,
  or the files that `tools/research/pakx.py` extracted (`SOULWARDEN_DATATABLES_DIR`, default
  `~/Games/soulwarden/work/assets/extract/sharedassets/springboardentitites/datatables`).
  `tools/gamedata/items.sh --find ITEM...` prints the index and the values of an item id.
  With no file the backend starts, the log says so, and `give` is refused. The tool compares
  the extracted tables with the table list of the pak files (names and sizes) and writes no
  file when they are not equal: one table that is not there makes each later index wrong.
  The catalog also has the items of each backstory of the game (member `backstories`, from
  the table `javelindata_backstorydata.datasheet`): the items of a new character, and the
  list of the backstories that the character creation can send (member
  `creationBackstories`). A catalog file from before stage 1.5 has no such list, and a new
  character then gets no item: run the tool again.
- `tools/research/item_index.py` reads the item index list from the memory of the live client
  (`tools/hx`) and compares each entry with the catalog. Status 0: the lists are equal. Run
  it after a change of the game copy or of the catalog tool: a wrong index gives a wrong
  item or an empty slot with no log line.

## Traps

- The bag window (Tab, `tools/hx 'key 0x09'`; the same key closes it): positions in the
  window of 1256x660 with one item in each section: first cell of the first section `662
  193`, first cell of the section to its right `837 193`, first cell of the second row of
  sections `662 296`. A pointer move to a cell shows its tooltip after 1 s (move in two or
  three steps: `'move 650 185' 'wait 200' 'move 662 193'`). A click on a cell opens the
  action menu (entry "Lock" at `760 336` for the first cell). A drag of a cell to `1103 325`
  (the area "Drop items", which shows only in a drag) drops the full stack into a Discarded bag at the feet: `'drag 837 193
  1103 325 1200'`. A drag to the character at the left is an equip try. The sections and
  their sequence depend on the items: take a screenshot before you use a position.
- The bag of an entity keeps its empty slots until the character leaves the world (`tools/world
  items` then prints, for example, "3 items in 28 slots"). A new item gets the first empty
  slot. The client sorts the items by their class: the slot number does not show.
- The equipment panel of the inventory screen (Tab), positions in the window of 1256x660:
  the armor cells at x `269` (head `152`, chest `195`, hands `237`, legs `279`, feet `320`),
  the weapon cells at x `586` (weapon 1 `152`, weapon 2 `194`), the amulet cell `586 290`,
  the first quick slot `264 424`. A click on a cell of the bag opens its action menu: the
  first entry ("Equip") is 126 pixels to the right of the cell and 36 pixels below it, and
  each next entry is 36 pixels lower (a weapon: Equip, Equip to Slot 1, Equip to Slot 2,
  Lock; armor: Equip, Lock, Drop; `'click 788 229'` is "Equip" for the cell `662 193`). A
  click on an equipped cell opens a menu with "Unequip" as its first entry (`'click 396 313'`
  for the legs cell `269 279`). A drag of weapon cell 1 to weapon cell 2 changes the two
  slots: `'drag 586 152 586 194 1500'`.
- The inventory screen shows the character from the front. The picture depends on the place
  of the character and on the direction of the camera at the time of the Tab key: an object
  near the character (a wall, a cart) can hide it. The spawn position with no camera turn
  gives a clear picture.
- At the equip of a weapon in a session, the client turns its own character to the direction
  of the camera and shows it in the stance of the weapon, with the weapon in the hand. The
  weapon is on the hip after the next world entry, on the inventory screen, and for an
  observer. For a front view of the character in the world, take the screenshot before the
  equip of a weapon, or after a new world entry.
- The client sends an equipment request only for an action that its own checks permit. To
  see the failure reply (type 3626) in a live client, start the run with
  `SOULWARDEN_RESEARCH_SESSION=refuse-equipment=1`: the handler of the client is the trace point
  `687BB60`.
- A paperdoll fragment has only the slots that changed: a slot with no entry keeps its item
  in the client. A first record in more than one part is different (live check I12 of stage
  1.3b): a later part with entries of the same list group (the slots that do not show on the
  body: jewelry, quick slots, bags, tools, the heart rune) took the items of that group of the
  earlier part out. The encoder keeps each group whole in one part. 15 items with perks need
  two parts. A group that does not fit one part is one part of its own, larger than the budget
  of a part: the carrier sends its bundle in chunks (code review of stage 1.3b, finding F12;
  test case RI12). A fragment of `tools/world fragment NAME 62 3183` whose three lists do not
  have the same length has no effect and gives no log line.
- The bag window does not show an ammunition item (`ArrowT1`): do not use one for a test of
  the bag. The equipment slots for ammunition (12, 13) take no item in the backend. A bow and a musket shoot with no
  ammunition: the ammunition-free setting of the client is on, and the shot names the ammunition of an empty slot
  (DefaultArrow, DefaultShot; live check L1 of stage 3.3b-S3). The server takes no item for a shot
  ([Projectiles](projectiles.md)).
- A character from before stage 1.4, and a seeded character, have no items of an archetype:
  only the create route gives them, at the creation. Give such a character its gear with
  `tools/world give` and `tools/world equip`.
- A potion or a food with a level requirement above the level of the character: the client
  uses no such item (a quick slot key sends no bit). FoodCONT5 needs the level 65: with the
  expansion of stage 1.1b a character can have it (live proof: refused at 60, used at 65).
- An item catalog of a tool from before stage U3 has the Weight of each master row: a bag with weapons or armor is heavier for the
  server than for the client, and the server refuses an ability and a dodge that the client permits near the limit (live run of
  stage U3). Make the file again with `tools/gamedata/items.sh`.
- The inventory screen (Tab) shows the weight of the bag at the top right (`200.0/200.0`; red above the limit) and of each section.
  It is the only view of the weight of the client: compare it with the sum of the server before a test at the limit.
- The main menu reads the armor of a character from its paperdoll rows in the store (stage U4). A system that changes the look of a
  worn item must write the row first (the inventory system writes the rows of a change before it changes the entity).
- An item catalog from before stage U4 has no armor appearance names: the main menu shows each character with no clothing (the
  backend log says it at the start). Make the file again with `tools/gamedata/items.sh`.
- `tools/gamedata/items.sh` writes the catalog to the common game data folder: the next start
  of the user reads it too. The backend that the user starts then gives items only through
  a development run or a later tool window: a start of the user has no development commands.
