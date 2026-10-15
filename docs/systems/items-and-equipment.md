# Items and equipment

Read this document before a change of the bag, the coin, the equipment (the paperdoll), or the items
of an archetype, and before a live test with the bag window or the inventory screen. The wire
contracts are in the protocol notes (sections 4.10 and 4.11).

## Start here

| Task | Start here |
| --- | --- |
| Items, the bag, the coin of a character | Protocol notes 4.10, `apps/backend/src/world/inventory.h`, `tools/world give`, `tools/gamedata/items.sh` |
| Equipment: the paperdoll, equip and unequip, visible gear, the items of an archetype | Protocol notes 4.11, `apps/backend/src/world/inventory.h`, `server/rep/states/paperdoll_state.cpp`, `tools/world equip` |

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
- A new character gets the items of its archetype (the backstory table of the game, in the
  item catalog): the inventory module makes the rows (`world::start_items`), and the store
  writes them in the transaction that inserts the character. The backstory table also has
  rows of tests with many items: the create route makes a character only for a backstory of
  the character creation (the member `creationBackstories` of the catalog: the seven
  archetypes and the default backstory of the client), and it gives the reply 400 for a
  different one. A catalog with no such member gives no item to a new character.

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
  (the area "Drop items", which shows only in a drag) drops the full stack: `'drag 837 193
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
  in the client. A fragment of `tools/world fragment NAME 62 3183` whose three lists do not
  have the same length has no effect and gives no log line.
- The bag window does not show an ammunition item (`ArrowT1`): do not use one for a test of
  the bag. The equipment slots for ammunition (12, 13) take no item in the backend.
- A character from before stage 1.4, and a seeded character, have no items of an archetype:
  only the create route gives them, at the creation. Give such a character its gear with
  `tools/world give` and `tools/world equip`.
- `tools/gamedata/items.sh` writes the catalog to the common game data folder: the next start
  of the user reads it too. The backend that the user starts then gives items only through
  a development run or a later tool window: a start of the user has no development commands.
