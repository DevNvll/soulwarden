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
  stage 1.3b also the HitRuneCharge of each contact, the 9th value; from stage 3.3b-S3 also the
  member `capsule` of a slice: the capsule of its CharacterComponent, radius, length, axis, and
  offset, for 501 of 503 slices; with a file from before a projectile uses the default capsule,
  [Projectiles](../systems/projectiles.md)), the folder
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
  `BASES_NOT_IN_GAME` of the tool; from stage 3.3b-S4 also the member `impacts`: the impact
  of each damage row with an impact distance, 234 rows (ImpactDistanceX, Y, Z, ImpactDecayRate,
  the same four columns with "_AI", and UseAttackerPosForReaction; the pull and the push of a
  hit, [Forced movement](../systems/forced-movement.md)); with a file from before, a hit moves
  no creature; the tool stops for a decay rate above 1, and the loader refuses a value that is
  not finite, a value above 100, and an impact of a row that the file does not have; from stage 3.3b-S5 also the members
  `criticals` (CritChance and CritDamageMultiplier of each weapon, 587 rows) and `hitRules` (CanCrit, NoHeadshot,
  NoBackstab, DmgCoefHead, DmgCoefCrit of each damage row, 414 rows): with a file from before no hit is critical; the tool
  stops for a weapon table with only one of the two critical columns, a CritChance above 1, and a damage table with no
  column of the hit rules, and the loader refuses a file with only one of the two members ([Combat and
  health](../systems/combat-and-health.md))), `loot-tables.json` (stage 3.5a: the experience and the
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
  RA12; from stage 3.3b-S1, format 3, also the caster effect rows (each row of an ability
  table of the players with a SelfApplyStatusEffect, with its cells) and the status
  categories (the ValueLimits of StatusEffectCategoryData); the loader takes the formats 1
  and 2 too; a file must name its number of status rows: a file with 0 rows and a status
  effect is refused (code review of stage 1.3b, finding F10); the tool stops for a caster
  effect row that names a status that the tables do not have and for a ValueLimits part that
  is not a name and a number; from stage 3.3b-S2a, still format 3, also the member
  `statMultipliers` (the Min and Max of each stat of StatMultiplierData: the limits of the speed
  table; a file with no such member gives the limits of the client, 0 to 1.5: code review of
  stage 3.3b-S2a, finding F2); the tool stops for a column of the status
  rules (StackMax, DurationMax, RefreshDuration, ...) or of a caster effect (OnHit,
  SelfApplyStatusEffect, ...) that a table does not have, but for the columns that the tables of
  the creatures do not have in the game (`STATUS_TABLE_DIFFERENCES`), and for an
  OnEndStatusEffect or a CastSpell that does not resolve, but for the one link of the game in
  `END_LINKS_NOT_IN_GAME`; the loader refuses a positive time below 1 ms and a link that does
  not resolve (code review of stage 3.3b-S1, findings F8, F9, F13); from stage 3.3b-S4 the tool
  also stops for a rule column of a caster row that an ability table of the players does not
  have (`CASTER_RULE_COLUMNS`: the 247 columns that each of the 27 tables has, as NumAroundMe and
  RemoteDamageTableRow; the backend copies each cell, and a value in a column whose rule it does
  not evaluate disables the row) and for a status table with no ABSStandard, ABSSiege, ABSAcid,
  DMGSiege, or DMGAcid, but for the two tables of the creatures with no ABSAcid in
  `STATUS_TABLE_DIFFERENCES` (code review of stage 3.3b-S2a, finding F4; cases RG4 of
  `check_ability_tool.py`); from stage 3.3b-S5, format 4, each row of the ability tables of the players (2,025 rows: the
  passive rules, [Passive rules](../systems/passive-rules.md)), and the spell selections of the action lists of the players
  (`actionSpells`: 52 selections, each one with its action list, action, spell, the nodes that it needs, and the nodes that
  it excludes; `actionNodes`: the 26 nodes that an action list tests; `actionSpellsNotExported`: the 2 selections whose test
  is not a list of nodes, an Or with a state); the tool stops for a test of an unknown spell, and the loader takes the formats
  1 to 3 too (with format 3: no spell selection, and only the rows of that file)), `perk-data.json` (stage 1.3b, `tools/gamedata/perks.sh`: the perk list in the order of
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
  a live client: live check I1 found the two equal; from stage 3.3b-S1, format 2, also the
  RequiredLevel of each consumable (member `level`, from its master item row; code review of
  stage 1.3b, finding F5); the loader takes only format 2, and it refuses a file whose links
  do not resolve: an affix or an ability of a perk, the new perk of a swap, a fixed place that
  is not a perk, a bucket place that is not a bucket, an ability of a heart rune (F13; the
  old perk of a swap can be a perk that the game copy does not have); the tool stops for a
  value column that an encumbrance, affix, or ability table does not have (F14), for two affix
  rows or two ability rows of one id with different values (F15), and the entries of a perk
  bucket end at the first empty perk cell or empty weight cell, and at Perk500 (the parser of
  the client, F16: one bucket of the game, PerkBucket_PvPBucket1, has 5 entries, not 15);
  `tools/gamedata/check_perk_tool.py` checks these stops with made-up tables, case RI14; from
  stage 3.3b-S2a the tool stops for a master item table with no ItemID or RequiredLevel, and a
  consumable with no master item row has no row in the file (the count
  `consumablesWithNoItem`: 138 of 586 in the game copy; code review of stage 3.3b-S1, finding
  F11), and it requires the ABS and DMG columns of each damage type in the affix table, not the
  armor columns that the table of the game does not have (F12)), `spell-data.json` (stage
  3.3b-S2a, `tools/gamedata/spells.sh`: for each spell row with a prefab slice, the slice asset
  and sub id, the slot of its Spell component, the slots and types of its replicated components,
  its interest radius (`m_sliceSpawnRadius`), and its delayed events (slot, delay, event type,
  target entity, damage row of the target, and a mark for the target with a
  DestroyGDEComponent); 1,565 spells and 934 slices, 1.1 MB, 7 s; without it a spell has its
  hits and no record; the backend refuses a file whose form, counts, or links are not
  correct, as a whole: a null asset, a Spell slot that is not of type 2912, two components on
  one slot, a delayed event on a slot that is not of type 3139, more than 250 events, a delay
  that is not finite, a spell that names no slice of the file, and, from stage 3.3b-S4, a slot of
  type 3139 with no event row (code review of stage 3.3b-S2a, finding F3); the tool leaves out a
  slice with no Spell component and a spell with no slice file, with their counts, and it stops
  for a delay that is not a finite 32-bit float (F5: a delay of one byte gave 0, and the burst
  of the vortex then came at the start; `tools/gamedata/check_spell_tool.py` checks it with
  made-up slice nodes, case RG5); from stage 3.3b-S2b the file has the format 2: a slice with no
  Spell component and with a PositionInTheWorld component (the trap) is in the file with its
  position slot, each delayed event has its owner entity and its marks `m_startDelayOnInit` and
  `m_startDelayOnTrigger`, and each slice has the values of its Vitals component (the slot, the
  row of the base vitals tables, the target of its death event), its AutoSpell (the spell, the
  life, the initial delay, the cooldown, the expire and cast targets), its detection volumes
  (the radius of a sphere, the time of the activation of its entity), its momentary offenses
  (the row, the radius), its detection events, and the destroy time of each timeline; the list
  `vitals` has the rows of the base vitals tables that the slices name (HealthBaseMax,
  HealthOverridePercentSpellOwnerMaxHealth, the armor overrides, the gear score); 1,703 spells,
  1,058 slices, 50 vitals rows, 1.6 MB; the backend reads the formats 1 and 2 and refuses a
  vitals slot that is not of type 15, a position slot that is not of type 13, a slice with
  neither slot, and a time, a radius, or a destroy time that is not correct; with a file of
  format 1 the Pylon, the trap, and the Beacon make no entity (test cases SB1, SB10;
  `check_spell_tool.py` case trap-slice; from stage 1.1b (code review of stage 3.3b-S2b) the tool stops for a time of an
  AutoSpell (m_lifetime, m_lifetimeOnHit, m_initialCastDelay, m_cooldown) that is not a 32-bit float (F12;
  `check_spell_tool.py` cases autospell and autospell-lifetime-width, RK12) and for a vitals row of a slice that no base
  vitals table has, but for the rows of `VITALS_ROWS_NOT_IN_BASE_TABLES` (the seed of the Goth elephant), which go to the
  member `vitalsNotInData`; the loader refuses a slice whose vitals row is in neither list (F8, case RK8));
  [Spell entities](../systems/spells.md)), `projectile-data.json` (stage 3.3b-S3,
  `tools/gamedata/projectiles.sh`: the 323 attacks of the ranged attack profiles of
  `rangedattackdatabase.radb`, the flight values of 85 projectile slices, 105 ammunition rows, 261
  weapons with a profile, and 58 shots of basic attacks: the CAGE-RangedAttack clips of the
  animation databases with a light or heavy row of the weapon category, and the projectile spells of
  the magic weapons with the light or heavy row of the weapon; 65 KB, 10 s; without it each shot of a
  ranged or magic basic attack is refused; the backend refuses a file whose form or counts are not
  correct, as a whole: a number that is not a finite size, a shot of an attack that its profile
  does not have, a slice index outside its list, two attacks with one name in one profile; the tool
  stops for a number of the ranged attack data or of a slice that is not a finite 32-bit value, a
  column that it reads, and a ChargeValue that is not a number (`check_projectile_tool.py`, case
  PJ11); from stage 3.3b-S2b the file also has the 33 projectile spells of the abilities (the shot
  of the Pylon, the toss of the trap, the projectile of the Beacon: the 11th value of a shot is
  true; a basic shot never fits them), 91 shots and 108 slices, 72 KB; the tool stops (and keeps
  the old file) when a SliceOverride of a clip or the prefab of a spell shot names no slice with a
  projectile component, when a player weapon profile has no shot of its light or heavy row, when
  the ammunition of an empty slot of a weapon whose shot takes the slice of its ammunition has no
  slice, and when a file that it needs has no source (an animation database) (code review of
  stage 3.3b-S3, finding F7, `check_projectile_tool.py`, case RJ7); from stage 1.1b (code review of stage 3.3b-S2b, finding
  F11) the tool stops when one light or heavy row of a player profile has no shot (not only all of them), but for the rows of
  `BASIC_ROWS_WITH_NO_SHOT` (MusketAttack2, the strike of the stock of the musket), and when a projectile spell of an
  ability that the backend launches (`REQUIRED_ABILITY_SHOTS`: the Pylon shot, the tosses of the trap, the four Beacon
  projectiles) has no shot or no slice (`check_projectile_tool.py` cases partial-basic-sources and missing-ability-prefab,
  RK11); [Projectiles](../systems/projectiles.md)), `travel-places.json` (the spirit shrines and the inns
  of each territory with their names and positions; without it the list of the travel
  places is empty, and a travel with numbers works), `item-catalog.json` (the
  item list with the index of each item on the wire, the items of each backstory, and the
  list of the backstories of the character creation; without it the backend gives no item,
  and a new character has no clothing), and `progression-tables.json` (the level table, the
  attribute and vitals values, the rank tables of the weapons and the trade skills, and the
  nodes of the weapon trees; from stage 1.3b also the mana regeneration of the row Player of
  the base vitals (rate 2, delay 0, delay 2 at 0 mana; a file from before has no
  regeneration); from stage 1.1b, format 2, also the rows of the expansion table, the 19
  entitlements that each account gets with their bits, the achievement that they give, and the
  largest rank of each progression row with the expansion (Expansion01MaxLevel); with a file of
  format 1 the players have the limits of no expansion and get no DLC; the tool stops for a link
  that does not resolve (`check_progression_tool.py`, case DL9; [DLC and
  entitlements](../systems/dlc-and-entitlements.md)); without it the backend sends the stored
  values of a character and refuses a command or a request that needs a table).

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
tools/gamedata/spells.sh             # one time: spell entity data for the backend (slices, slots, delayed events, the values of format 2; 1.6 MB); needs an Oodle source
tools/gamedata/projectiles.sh        # one time: projectile data for the backend (profiles, slices, ammunition, weapons, shots, ability shots; 72 KB, 10 s); needs an Oodle source
tools/gamedata/places.sh             # one time: travel places for the backend (the spirit shrines and the inns of each territory)
tools/gamedata/heights.sh            # one time: terrain heights for the backend (a creature that walks needs them; 136 MB, 2 minutes, no Oodle source)
tools/gamedata/navigation.sh         # one time: navigation data of the world (1.2 GB, 43 minutes with 8 processes, 2.1 GB of work files; needs an Oodle library, a C and a C++ compiler; it also makes the folder heights again)
```

## Traps

- Stage 3.3b-S5 changes `combat-tables.json` (the members `criticals` and `hitRules`), `creatures.json` (the members `behind`
  and `head` of a slice, and four counts), and `ability-data.json` (format 4). The backend of this stage reads the older
  files: then no hit is critical, no hit is from behind, no projectile hit is a headshot, the evaluator has only the rows of
  a format 3 file, and the plans keep the node rules of stage 3.3a. Write the three files again with
  `tools/gamedata/combat.sh`, `creatures.sh`, and `abilities.sh` (the creature tool also reads the skeletons of
  `objects/characters/`). `progression-tables.json` and `projectile-data.json` of the tools of this stage (with the checks
  of the review of stage 1.1b) are equal to the files of stage 1.1b; `spell-data.json` too.

- Stage 1.1b changes `progression-tables.json` (format 2: the expansions, the entitlements, the achievements, the rank
  limits with the expansion) and `spell-data.json` (the member `vitalsNotInData`; the AutoSpell times are required floats).
  The backend of this stage refuses a spell file from before (the slice of the seed of the Goth elephant names a vitals row
  that the list does not have): then the Pylon, the trap, the Beacon, and the vortex have no record. With a progression file
  of format 1 the players have the limit 60 and no DLC. Write the two files again with `tools/gamedata/progression.sh` and
  `tools/gamedata/spells.sh`, and put them into their places with the backend. `projectile-data.json` of the tool of this
  stage is equal to the file of stage 3.3b-S2b.

- Stage 3.3b-S2b changes `spell-data.json` (format 2) and `projectile-data.json` (the ability shots). The backend of this
  stage reads the older files: with a spell file of format 1 the Pylon, the trap, and the Beacon make no entity (the
  vortex works), and with an older projectile file the press of the trap and of the Beacon is refused and the Pylon does
  not shoot. Write the two files again with `tools/gamedata/spells.sh` and `tools/gamedata/projectiles.sh`.

- Stage 3.3b-S3 adds `projectile-data.json` and the member `capsule` of a slice of `creatures.json`. The backend of this
  stage reads the older creature file (each creature has the default capsule) and starts with no projectile file (each
  shot is refused). Write the two files again with `tools/gamedata/creatures.sh` and `tools/gamedata/projectiles.sh`. The
  other members of `creatures.json` are equal to the file of stage 3.3b-S4.

- Stage 3.3b-S4 adds the member `impacts` to `combat-tables.json`. The backend of this stage reads the older file: then
  no hit pulls or pushes a creature. Write the file again with `tools/gamedata/combat.sh`. The other files of the tools
  of this stage are equal to the files of stage 3.3b-S2a.

- Stage 3.3b-S2a adds `spell-data.json` and changes the contents of `ability-data.json` (the member `statMultipliers`)
  and of `perk-data.json` (the consumables with no master item row are not in it). The backend of this stage reads the
  older files: with no `statMultipliers` the speed table has no limits, and with no `spell-data.json` the vortex has no
  picture. Write the three files again with `tools/gamedata/abilities.sh`, `perks.sh`, and `spells.sh`.

- Stage 3.3b-S1 changed the forms of `ability-data.json` (format 3) and `perk-data.json` (format 2). The backend of
  this stage refuses a perk file of format 1 (no perk data: no perk of an item, no consumable, no heart rune): write the
  two files again with `tools/gamedata/abilities.sh` and `tools/gamedata/perks.sh`.
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
