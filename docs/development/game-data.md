# Game data

Read this document before a change of a game data tool (`tools/gamedata`) or of a loader of the
backend. The tool of each system is also in the document of that system.

## Rules

- A table that comes from the game files is game data. A tool in `tools/gamedata` makes it
  from the game copy into `~/.local/share/soulwarden/gamedata` (`SOULWARDEN_GAMEDATA_DIR`), the backend
  reads it at its start (`--gamedata-dir`), and the backend must start and work with a
  default when the file is not there. Numbers of a wire contract are code constants. The
  game data files now: `appearance-tables.json`, `slice-ids.json`, `creatures.json` (the
  spawn points of the creatures of the whole world, and for each kind its slice, the slots of its
  components, its vitals ids, its health, its name, and its action table; from stage 3.4b,
  format 2, also the behavior data: the vision, the leash, the speeds, the animation tags,
  and the attacks of each slice, the damage of each kind and of each level, and the terrain
  height at each spawn point; without it the world has no creature; the backend refuses a
  file whose levels, slots, assets, health values, or behavior data do not fit together, as
  a whole; with a file of format 1 the creatures stand at their spawn points; from stage
  3.4e, format 3, also the patrol route of each spawn point that has one and the ambient
  speed and the wander values of each slice: with a file of format 2 no creature roams; from
  stage 3.3a also the member `reactions` of a slice, the fragments of the reactions of the
  five powers: with a file from before a knockdown plays the fragment of the hit reaction; from
  stage 1.3b also the HitRuneCharge of each contact, the 9th value), the folder
  `heights` (the terrain heights, one file for each region, from stage 3.4d after the
  cookies of the placements), the folder `navigation` (stage 3.4d: one file for each region
  with the floors, the water, and the sight of each cell of 0.25 m, and `navigation.json`;
  1.2 GB for the world; without it a creature goes on the straight line on the terrain of
  the folder `heights`, and with neither folder no creature leaves its place; the backend
  refuses a folder with a file that is not correct, as a whole, and a tile that does not
  decode alone), `combat-tables.json` (the weapon of each item
  with its base damage, its attribute factors, and the damage rows of its light and of its
  heavy attack, the damage rows and types, the damage values of the attributes, the level
  and gear score values of the damage rule, the defense values of each vitals row, and,
  from stage 3.4b, the armor values of each armor item and the damage values of the level
  differences for the damage of a creature (with a file from before, the armor of a
  character has no effect), and, from stage 3.5a, the weapon mastery of each weapon (value
  12 of a weapon row; with a file from before, a kill gives no mastery experience), and, from
  stage 1.3b, the heart rune charge of each damage row (AttackRuneCharge, HitRuneCharge: 8
  values of a row; the loader takes 6); without it an attack of a player on a creature has no effect; the backend refuses a file
  with a damage type whose category is not `Physical`, `Elemental`, or `True`, and a file
  with an empty value list of an attribute, as a whole; the tool writes no file when a
  weapon names a damage row or a vitals row names a base row that the tables do not have,
  when a column that it reads is not there, and when a cell is not a finite number: the
  names that the game itself does not have are in the two lists `ROWS_NOT_IN_GAME` and
  `BASES_NOT_IN_GAME` of the tool), `loot-tables.json` (stage 3.5a: the experience and the
  coin of a kill of each creature level, the reward factors of each creature type, the
  level differences, the coin chance of the kill events, the credit data of the game, the
  gear score ranges of each level and the loot ceiling of each creature level, the loot data
  of each vitals row, and the loot tables and the loot buckets that a vitals row can reach;
  without it a kill gives nothing; the backend refuses a file whose parts do not fit
  together, as a whole: a reference of a vitals row, of a table entry, or of a bucket that
  does not resolve, a range whose end is below its start, a factor or a chance outside its
  range, reward levels that do not start at 1 and go up by 1, no kill event, credit data with
  fewer modifiers than receivers, a creature type or an event of a base row that the file
  does not have; the tool writes no file for a reference that does not resolve (also a
  variant with no base row, other than the four of the list `VARIANTS_WITH_NO_BASE_IN_GAME`),
  a text that is not a number, a range, or a flag, a number that is not finite, and a value
  outside the range of the backend: the texts of the game data with a typing fault are in
  the lists `FLAG_TEXTS_NOT_IN_GAME`, `CHANCE_TEXTS_NOT_IN_GAME`, and `RANGE_TEXTS_NOT_IN_GAME`
  of the tool), `ability-data.json` (stage 3.3a: the cooldown of each ability, the values of
  each status effect with its number on the wire, and the shape of each spell; without it
  the backend refuses each ability press; the backend refuses a file whose form or counts
  are not correct, as a whole; the tool writes no file for a table type that is not there, a
  number that is not finite, a flag text that is not a flag, a negative cooldown, and two rows
  of one list with one id and different values; from stage 1.3b, format 2, also the cells of each
  status row and spell row, the two cost tables of the players (ManaCosts_Player,
  StaminaCosts_Player), and the cost columns of each ability row (ManaCostList,
  ManaCostMult, StaminaCostList, StaminaCostFlatMod); the loader takes format 1 too; the
  tool stops for a column that it reads and that a table does not have (code review of stage
  3.3a, finding F11), for two rows of one id with different values other than the two rows of
  the game in `DUPLICATES_IN_GAME` (F12), for a negative cost, and for a cost id that does not
  resolve other than the ids of `COSTS_NOT_IN_GAME` (the client gives them no cost: protocol
  notes 4.23); the loader refuses status numbers that do not fit the CRC order of the rows
  (F10); `tools/gamedata/check_ability_tool.py` checks these stops with made-up tables, case
  RA12), `perk-data.json` (stage 1.3b, `tools/gamedata/perks.sh`: the perk list in the order of
  the client, each perk with its affixes, abilities, labels, scaling, and channel, the cells of
  the affixes and of the ability rows that the perks name, the perk buckets, the perk places
  of each item, the consumables, the encumbrance row and the load of each armor item, the
  exclusive labels, the perk swap table, the abilities of the heart runes, and the perk
  generation data; 6.5 MB; without it an item has no perk and a perk has no effect, and a
  consumable and a heart rune do nothing; the backend refuses a file whose form is not
  correct, or whose buckets or layouts name a perk or a bucket that the file does not have,
  as a whole; the tool writes no file when a reference does not resolve, other than the names
  of the lists `REFERENCES_NOT_IN_GAME`, `ITEM_REFERENCES_NOT_IN_GAME`, and
  `SWAP_PERKS_NOT_IN_GAME`; `tools/research/perk_index.py` compares the list with the list of
  a live client: live check I1 found the two equal), `travel-places.json` (the spirit shrines and the inns
  of each territory with their names and positions; without it the list of the travel
  places is empty, and a travel with numbers works), `item-catalog.json` (the
  item list with the index of each item on the wire, the items of each backstory, and the
  list of the backstories of the character creation; without it the backend gives no item,
  and a new character has no clothing), and `progression-tables.json` (the level table, the
  attribute and vitals values, the rank tables of the weapons and the trade skills, and the
  nodes of the weapon trees; from stage 1.3b also the mana regeneration of the row Player of
  the base vitals (rate 2, delay 0, delay 2 at 0 mana; a file from before has no
  regeneration); without it the backend sends the stored values of a character and refuses a
  command or a request that needs a table).

## Commands

```sh
tools/gamedata/appearance.sh         # one time: appearance tables for the backend
tools/gamedata/slice_ids.sh          # one time: ids of the player slice for the backend
tools/gamedata/items.sh              # one time: item catalog for the backend (index of each item on the wire)
tools/gamedata/progression.sh        # one time: progression tables for the backend (levels, attributes, skills)
tools/gamedata/creatures.sh          # one time: creature data for the backend (spawn points, kinds, slices); needs an Oodle source
tools/gamedata/combat.sh             # one time: combat tables for the backend (weapons, damage rows, defense values of the creatures)
tools/gamedata/loot.sh               # one time: rewards of a kill for the backend (loot tables, experience and coin of each level, credit, gear score limits; 10 s)
tools/gamedata/abilities.sh          # one time: ability data for the backend (cooldowns, status effects and spells with their cells, cost tables; 2 MB)
tools/gamedata/perks.sh              # one time: perk data for the backend (perk list, affixes, buckets, item perk places, consumables, equip load, swaps; 6.5 MB)
tools/gamedata/places.sh             # one time: travel places for the backend (the spirit shrines and the inns of each territory)
tools/gamedata/heights.sh            # one time: terrain heights for the backend (a creature that walks needs them; 136 MB, 2 minutes, no Oodle source)
tools/gamedata/navigation.sh         # one time: navigation data of the world (1.2 GB, 43 minutes with 8 processes, 2.1 GB of work files; needs an Oodle library, a C and a C++ compiler; it also makes the folder heights again)
```

## Traps

- Stage 1.3b changed the forms of `ability-data.json`, `combat-tables.json`, `creatures.json`, and
  `progression-tables.json`, and added `perk-data.json`: the backend of the stage reads the old
  forms, but an older backend refuses some new forms. Put the backend and the five files into
  their places together.
- The perk list of the client depends on the hash order of the perk tables: a change of the
  game copy can change each index. Run `tools/gamedata/perks.sh` again and compare it with a live
  client (`python3 -B tools/research/perk_index.py --rows`).

- The number of a status effect on the wire is the position of its CRC in the list of all
  rows of all status tables (4,517 rows). A change of the game copy changes the numbers:
  make `ability-data.json` again after it (protocol notes 4.22).
- `tools/gamedata/heights.sh` and `tools/gamedata/creatures.sh` write into the common game
  data folder: the next start of the user then has creatures that fight. For a test of a
  different file, give the tool an output path and give the run its folder with
  `SOULWARDEN_GAMEDATA_DIR` (the folder needs the other game data files and the folder
  `heights` too).
