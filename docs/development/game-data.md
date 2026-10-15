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
  five powers: with a file from before a knockdown plays the fragment of the hit reaction), the folder
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
  12 of a weapon row; with a file from before, a kill gives no mastery experience); without it an attack of a player on a creature has no effect; the backend refuses a file
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
  of one list with one id and different values), `travel-places.json` (the spirit shrines and the inns
  of each territory with their names and positions; without it the list of the travel
  places is empty, and a travel with numbers works), `item-catalog.json` (the
  item list with the index of each item on the wire, the items of each backstory, and the
  list of the backstories of the character creation; without it the backend gives no item,
  and a new character has no clothing), and `progression-tables.json` (the level table, the
  attribute and vitals values, the rank tables of the weapons and the trade skills, and the
  nodes of the weapon trees; without it the backend sends the stored values of a character
  and refuses a command or a request that needs a table).

## Commands

```sh
tools/gamedata/appearance.sh         # one time: appearance tables for the backend
tools/gamedata/slice_ids.sh          # one time: ids of the player slice for the backend
tools/gamedata/items.sh              # one time: item catalog for the backend (index of each item on the wire)
tools/gamedata/progression.sh        # one time: progression tables for the backend (levels, attributes, skills)
tools/gamedata/creatures.sh          # one time: creature data for the backend (spawn points, kinds, slices); needs an Oodle source
tools/gamedata/combat.sh             # one time: combat tables for the backend (weapons, damage rows, defense values of the creatures)
tools/gamedata/loot.sh               # one time: rewards of a kill for the backend (loot tables, experience and coin of each level, credit, gear score limits; 10 s)
tools/gamedata/abilities.sh          # one time: ability data for the backend (cooldowns, status effects, spells; 0.4 MB)
tools/gamedata/places.sh             # one time: travel places for the backend (the spirit shrines and the inns of each territory)
tools/gamedata/heights.sh            # one time: terrain heights for the backend (a creature that walks needs them; 136 MB, 2 minutes, no Oodle source)
tools/gamedata/navigation.sh         # one time: navigation data of the world (1.2 GB, 43 minutes with 8 processes, 2.1 GB of work files; needs an Oodle library, a C and a C++ compiler; it also makes the folder heights again)
```

## Traps

- The number of a status effect on the wire is the position of its CRC in the list of all
  rows of all status tables (4,517 rows). A change of the game copy changes the numbers:
  make `ability-data.json` again after it (protocol notes 4.22).
- `tools/gamedata/heights.sh` and `tools/gamedata/creatures.sh` write into the common game
  data folder: the next start of the user then has creatures that fight. For a test of a
  different file, give the tool an output path and give the run its folder with
  `SOULWARDEN_GAMEDATA_DIR` (the folder needs the other game data files and the folder
  `heights` too).
