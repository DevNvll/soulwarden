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
  [Projectiles](../systems/projectiles.md); from stage 2.2, format 4, also the time constraint of each spawn point: the phase mask,
  the despawn flag, and the two cooldown flags of its spawner; with a file from before each point makes its creature in each phase,
  [World time and weather](../systems/world-time-and-weather.md); from user test 2 also the member `reactionLengths` of a slice (the
  length of the variant of each reaction fragment: its end blend, else the end of its animation from the CAF file of the clip in
  `animations/gameplay/character/npc/`; -1: not known), and the length of an attack with no end blend from its animation (else its
  last contact plus 0.9 s); with a file from before a reaction and its push last 1.5 s, [Creatures](../systems/creatures.md)), the folder
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
  column of the hit rules, and the loader refuses a file with only one of the two members; from stage 3.3b-S6 (code
  review of stage 3.3b-S5, finding F2), format 2: CanCrit of a hit rule is true (the cell 1), false (an explicit 0: 42
  rows of the game cannot be critical), or null (a blank cell); the loader takes format 1 too, and then no row is a row
  with no critical hit ([Combat and health](../systems/combat-and-health.md))), `loot-tables.json` (stage 3.5a: the experience and the
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
  categories (the ValueLimits of StatusEffectCategoryData); from stage 3.3b-S6 (code review of stage 3.3b-S5, finding
  F10) the tool stops and the loader refuses a file when a SelfApplyStatusEffect or an OtherApplyStatusEffect of an
  ability row names a status that the status tables do not have (`check_ability_tool.py` case unknown-target-status);
  the loader takes the formats 1
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
  RK11); [Projectiles](../systems/projectiles.md)), `weapon-actions.json` (stage 3.3b-M1, `tools/gamedata/weapon-actions.sh`:
  the attack graph of each tag set of a melee weapon of the weapon tables (12 sets; the dagger is excluded) from
  Attack_Start_Charge: the fragments of the body file of the weapon and of the common file (the variant with no FragTags and the
  most tags of the weapon), their transitions with the conditions of the basic attacks (CanAttack_Heavy, CanAttack_Primary,
  CanAttack_Primary_Rapier, Attack_Heavy_NotHeld, Attack_Primary_Released, none), the strikes of the body and of the weapon
  attachment (a damage key, its window, the row of each clip with the learned nodes of its named condition, the shapes), the
  payments with their condition, the dodge windows, the mark CanTransition, and the length of each fragment from the animation
  files (the motion parameters chunk of the CAF; a second clip of one animation continues its time); 119 fragments, 275
  transitions, 89 strikes, 0.07 MB, 5 s; without it each melee hit uses the light or the heavy row of the weapon table; the backend
  refuses a file whose form or counts are not correct, as a whole: a transition to a fragment that the weapon does not have, a
  window with its end before its start, a strike with no row, two tag sets with one key; the tool stops when a transition of a
  basic attack names an action that no action list has and when a damage clip has no row, and lists the tag sets with no graph,
  the transitions of other conditions, and the clips with a condition that is not an AND of node tests (`check_weapon_actions_tool.py`,
  case MB9; [Combat and health](../systems/combat-and-health.md)); from the code review of stage 3.3b-M1 (finding M1-R6) the tool
  stops with status 1 and keeps the old file when a fragment that the graph reaches is not in the sources, when an animation
  database is empty, when a transition goes to a fragment that the file does not have, and when a tag set has no strike (case
  "incomplete" of `check_weapon_actions_tool.py`, RB6); from the code review of stage 3.3b-M2 (finding M2-R7) the tool also stops
  for the great axe and the war hammer (`FRAGMENT_WEAPONS`: the backend plays their abilities as fragments) when the fragment of an
  ability or a fragment that its action selects is not in the sources, and when an ability has no strike in the fragments that it
  can play, but for `AREA_ABILITIES` (Shockwave and Path of Destiny: the area of a spell of the server) (case "incomplete
  abilities", RH7); from the code review of stage 3.3b-M2b (finding M2b-R4, stage 2.5a) the tool stops also when the ability action
  list of these weapons is not in the sources, and when one choice of an ability (its fragment, or one fragment that its action
  selects, each with the fragments of its own transitions) has no strike: a strike of one choice does not prove a strike of a
  different one (`NO_STRIKE_FRAGMENTS` is the list of the exceptions: empty in the supported build; case "incomplete abilities", RX4;
  the file of the game copy is the same with this rule); from stage 3.3b-M2 the file has the format 2: the abilities of each
  weapon (the action of the ability action list with its cooldown, its fragment, the fragments that it selects with
  ChangeActionFragment and their node and perk conditions, and its spells with CastSpell and the mark of their condition), the
  start windows of the abilities in each fragment (CanAttack_<ability>), the transitions inside an ability with their condition
  as terms (a node, a perk, the attack input, the block press, a collision, the hits of the ability, the fragment that plays),
  the shape of each row of a strike, the spell casts (CAGE-CastSpell) and the named marks of each fragment, and the uses of each
  node (a row, a transition, a selection, a cast, a grit, a conditional of the movement); 12 tag sets, 208 fragments, 2,574
  transitions, 243 strikes, 71 abilities, 9 spell casts, 0.4 MB; a clip with ExitTime -1 starts at the end of the clip before it,
  and a fragment whose last clip loops has the length 0 (not known: the sprint attack of the hatchet had -0.6 s and the backend
  refused the file; case "length after the clip before"); [Abilities](../systems/abilities.md)), `travel-places.json` (the spirit shrines and the inns
  of each territory with their names and positions; without it the list of the travel
  places is empty, and a travel with numbers works), `item-catalog.json` (the
  item list with the index of each item on the wire, the items of each backstory, and the
  list of the backstories of the character creation; from stage 2.2, format 2, also the
  removal protections of each item (`nonRemovable`, `notDroppable`;
  `check_item_catalog_tool.py`, case RLB1); from stage B0, format 3, also the upgrade limit of
  each item (`maxCraftUpgradeGS`) and the objective of each artifact (`artifactObjectives`);
  without it the backend gives no item,
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
  values of a character and refuses a command or a request that needs a table), and `interaction-data.json` (stage 2.5a,
  `tools/gamedata/interactions.sh`: each placement of the 1,334 layers of the 49 regions whose slice has an Interact component,
  with its key (the UUID), its kind (a table of the tool from the option names), its slice asset, its managed actor id (the UUID
  rule), and each bound with its slot, its facet target and the instance id of the entity of each option (the XOR rule), its place
  (the m_worldTM of the entity, turned by the rotation of the placement), its range, capacity, line of sight, arc, the
  requirements, and the achievement of a lore page (`javelindata_loreitems`, `javelindata_achievements`); the option rows of
  `uidatabase.uidb`; 43,225 objects, 43,485 bounds, 75 options, 1,152 achievements, 24 MB, about 4 minutes with 4 processes; the
  file lists the placements that are not objects with the cause (182,417 plots, 42,205 placements whose slice has no Interact
  component, the other PrefabSpawner children) and the 58 names of the data with no row; format 2 (stage 2.5b): the server
  children of the placements (`spawned`: 425, the stations of the settlements and the camps, each with its place and rotation from
  the placement matrix and the spawner entity, its parent, and its template), the templates of their slices (`templates`: 93, with
  the slots of the PositionInTheWorld, BuildableController, Ownership, and Vitals components, m_initialState,
  m_initialPermissionRule, the Assembly with "resolved" from `craftingstations.craftstationdb`, the bounds in the frame of the
  slice, and the template children such as the forge of a station), and the fixtures (powderKeg, campT5, camp); 25 MB, about 2 to
  4 minutes; the tool stops with no file for a source that is not complete or not consistent (`check_interactions_tool.py`, cases
  IT10 and IR5: also a layer with no list Capitals, a named slice with an asset id of a wrong form, one placement id with two
  different placements, two referenced achievements of one bit, and a fixture with no source; from the code review of stage 2.5b, cases
  RY6 to RY8 with `--review-stations`: a named placement with no position object with x, y, and z, a concrete child asset of a
  PrefabSpawner with no path in the catalog, and one placement id in two spellings of its UUID at two places; each placement key is the
  lower-case form of its UUID; the three child assets of the retail data that no record of the catalog has, `ASSETS_NOT_IN_GAME`, are
  left out with their own cause); the backend refuses a file whose
  form, indexes, or counts are not correct, as a whole (cases IR4, SC8), and it reads a file of format 1 with no server child;
  without it the backend makes no station, refuses each request of an interaction, and a read lore page is not kept:
  [Interactions](../systems/interactions.md)).
- `chest-data.json` (stage 3.5b.1, `tools/gamedata/chests.sh`, [Chests](../systems/chests.md)): each chest placement of the main world
  (5,519 of the 49 regions): its key (the placement UUID), its managed actor id, its place, its variation row of
  `Gatherable_LootContainers`, its gatherable row (the open time, the reset values, the restrictions), its loot root, the loot tags and the
  content level of its territories, and its state: `ordinary` (5,149: a player can open it) or `unavailable` with its reason (370: a lock,
  a key, a status, a quest; stage 3.5b.3 and 1.8); the rules of the base attributes of a player (the reset of 60 minutes, the count of 60);
  the census of the gatherable records of the layer metadata; 3.6 MB. The tool stops with no file for a source that is not complete or
  not consistent (`check_chests_tool.py`, case LC9). The backend refuses a file whose form, keys, gatherables, times, or places are not
  correct, as a whole; without it no chest opens (the request of a chest gets the refusal of the interaction).
- `gathering-data.json` and the folder `gathering/` (stage 3.6a, `tools/gamedata/gathering.sh`, [Gathering](../systems/gathering.md)):
  the gatherable rows (295; each with the causes of a rule that the backend cannot check: 54 rows), the events, the trade skill
  ranks (Mining, Logging, Harvesting, Skinning), the tools (230: the weapon rows with a gathering type), the gathering slices (127: the
  template of each one with its root, the slot of its GatherableController, and its Interact bound with GatherOption), the capital
  nodes (33,071 placements by their UUID), the skinning of the creature slices (117, from `creatures.json`), and for each region of
  the world one region file `gathering/SET/REGION.swgn` ("SWGN", the format 1, x, y, the records, the tails, then 14 bytes for each
  primary record: the slice index u16 and the place x, y, z f32; 2,728,908 records, 2,450,322 of them gathering nodes; the heights of
  the backend from the folder of `heights.sh`, and the water surface for a record with a height mode) with the version of its
  distribution file (the first 16 hexadecimal digits of its SHA-256) and the digest of the region file (the first 16 hexadecimal
  digits of the SHA-256 of the file). 40 MB, 1 minute. The tool stops with no file for a source that it needs and for a value of a
  wrong form (`check_gathering_tool.py`, case GAT1), and (code review of stage 3.6a, finding GA-R4) for a GatherableData table with no
  column of a gate of its rows (the named exceptions of the retail data are in `MISSING_GATE_COLUMNS`), for a flag text that is not
  "", "0", "1", "true", or "false", and for a GameEventData table with no column CategoricalProgressionReward (case GR4). The tool
  publishes one set (finding GA-R6, case GR6): the region files go into `gathering/.staging-PID`, the tool checks them, moves the folder
  to `gathering/SET`, and writes `gathering-data.json` last; an export that stops leaves the published set as it was. The backend
  refuses `gathering-data.json` as a whole when its form or a value is not correct; a row that the file marks supported with an item,
  a restriction that it cannot check, or more than one gatherer gets a cause of the loader and is refused (finding GA-R5, case GR5); a
  region file that is not complete or that has another digest than the document gives no node of its region, and the stored
  depletions of the region stay (findings GA-R2 and GA-R6, cases GAT1, GR2, GR6); without the files each gather is refused.

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
tools/gamedata/stamina.sh            # one time: stamina data for the backend (the stamina of the player slice, the dodge and its times; 2 KB); needs an Oodle source
tools/gamedata/perks.sh              # one time: perk data for the backend (perk list, affixes, buckets, item perk places, consumables, equip load, swaps; 6.5 MB)
tools/gamedata/spells.sh             # one time: spell entity data for the backend (slices, slots, delayed events, the values of format 2; 1.6 MB); needs an Oodle source
tools/gamedata/projectiles.sh        # one time: projectile data for the backend (profiles, slices, ammunition, weapons, shots, ability shots; 72 KB, 10 s); needs an Oodle source
tools/gamedata/weapon-actions.sh     # one time: the attack graphs and the abilities of the melee weapons for the backend (fragments, transitions, strikes, lengths; 0.4 MB, 5 s); needs an Oodle source
tools/gamedata/places.sh             # one time: travel places for the backend (the spirit shrines and the inns of each territory)
tools/gamedata/chests.sh             # one time: the chests of the world for the backend (placements, gatherable rows, loot roots, territories; 3.6 MB); needs an Oodle source
tools/gamedata/interactions.sh       # one time: the objects of the world that a player can use (lore pages, persons, the stations, ...; 25 MB, 2 to 4 minutes); needs an Oodle source
tools/gamedata/heights.sh            # one time: terrain heights for the backend (a creature that walks needs them; 136 MB, 2 minutes, no Oodle source)
tools/gamedata/gathering.sh          # one time, after heights.sh: the gathering nodes of the world for the backend (40 MB, 1 minute); needs an Oodle source
tools/gamedata/gathering.sh --region r_+04_+02 --near 8817 4281 30   # the gathering nodes near a place, with their G (no file)
tools/gamedata/navigation.sh         # one time: navigation data of the world (1.2 GB, 43 minutes with 8 processes, 2.1 GB of work files; needs an Oodle library, a C and a C++ compiler; it also makes the folder heights again)
```

## Traps

- Stage B0 changes `item-catalog.json` (format 3). `item_catalog.py`: one more column of each item, `maxCraftUpgradeGS`, from the
  cell MaxCraftUpgradeGS of the master row (0 for an empty cell; the 73 artifacts have 800, no other item has a value; client code:
  the parser at the image RVA 0x442DD20+0x1997 keeps the value with a presence byte). The 22 store tables
  (`MasterItemDefinitions_MTX_*`) have no such column (0); a different master table with no column, and a cell that is not a whole
  number or an empty text, stop the tool. The new member `artifactObjectives` has `[item id, objective id, achievement id,
  achievement index]` for each row of the type Artifact of the tables `Objectives`: the item of the TaskEquipItem in the task tree
  of its Task (the columns SubTask1 to SubTask5 of the tables `ObjectiveTasks`) and the AchievementIndex of its AchievementId in the
  tables `AchievementData` (73 rows; Boltcaster: `Achievement_Objective_Artifact_Set1_2hBow`). The tool stops for a task, an item,
  or an achievement that does not resolve, for an objective with no equip item or with two, and for two objectives of one item
  (`check_item_catalog_tool.py`: cases upgrade, upgrade-invalid, upgrade-missing, objective-broken). The other columns and members
  are equal to the file of stage 2.2. The backend loads the formats 1 to 3: a file of an earlier format has no upgrade limit (a give
  of an artifact then stops at the range of its row, 700 for each artifact) and no artifact objective: the check at the start disables
  the build preset `bow-boltcaster` (its artifacts have 800), and a preset of artifacts at 700 would give no achievement (the client
  then shows the perks 2 to 5 as locked). Write the file again with `tools/gamedata/items.sh`.
- The code review of user test 2 (finding U2-R4) changes the loader of `combat-tables.json`: a file with the member `reactions`
  must have one entry for each damage row (the tool writes one for each row); a file with a partial or an empty list is refused as
  a whole (before, each missing row took the old full reaction). A file with no member keeps the old rule (test case RT4). The
  files of user test 2 have each row: no new write.
- User test 2 changes `creatures.json` (still format 4: the member `reactionLengths` of a slice, the attack lengths from the
  animation files, the counts `attackLengths`, `reactionLengths`, `slicesWithReactionLengths`; the tool now reads the CAF files of
  `animations/gameplay/character/npc/`, and a CAF file of a clip with no motion parameters chunk stops it) and `combat-tables.json`
  (still format 3: the member `reactions`; the tool stops for a missing reaction column, a power or a hit stun that is not a whole
  number that is not negative, and a NoReaction cell that is not a flag). The backend of this stage reads the older files: then a
  reaction and its push last 1.5 s, an attack keeps its old length, and each hit plays the reaction of stage 3.2a. Write the files
  again with `tools/gamedata/creatures.sh` and `tools/gamedata/combat.sh` (the checks: `check_creature_tool.py`, cases length-*, and
  `check_combat_tool.py`, cases no-reaction-column, part-power, text-flag).
- The code review of stage 3.6a changes `gathering.py` (findings GA-R4 and GA-R6): the gate columns, the flag texts, and the reward
  column of the events must be there (see above); the region files are in a set folder `gathering/SET/` with a digest in the document,
  and the tool publishes the manifest last. The files of the game data folder from before (`gathering/REGION.swgn`, no member
  `digest`) load as before with no digest check: the loader reads the same 295 rows (54 refused) and the 49 regions. A new write gives
  the check. After a publication the tool keeps each published set (code review of user test 2, finding U2-R6: a backend that runs
  can hold any earlier manifest) and removes only the staging folders of stopped exports; `--prune` removes the sets that the manifest
  does not name, only when no backend runs (cases `held-manifest` and `prune` of `check_gathering_tool.py`). The retail tables pass the new checks (the census of the supported build: the exceptions are GatherablesDunwood,
  QuestGatherables, and Gatherables_IsleOfNight; each event table has the reward column; the flag texts are "", "0", "1", and one
  "FALSE"). An export of the retail data with the new tool (into a work folder) has the same rows, events, tools, templates, capital
  nodes, and creature slices as the file of stage 3.6a, and its 49 region files have the same bytes; only the member `file` (the set
  folder) and the new member `digest` differ. The files of stage 3.6a need no new write.
- Stage 3.6a adds `gathering-data.json` and the folder `gathering/`, and changes `loot-tables.json` (still format 2) and the checks of
  `item_catalog.py`. `loot_tables.py`: the member `gatheringRoots` (the FinalLootTable of each gatherable row that is not a loot
  container and not `Empty`; their tables are read), `gatheringRootsWithNoTable` (a root of a quest or event row that no loot table
  has: named, not a stop), and their counts. A `loot-tables.json` from before has no gathering roots: each gather is refused (the
  backend log names it at the start). Write it again with `tools/gamedata/loot.sh`. `item_catalog.py` (code review of stage 2.2,
  finding WT-R3): each master table must have the columns Nonremovable and NotDroppable, and each protection cell must be a flag, the
  number 0 or 1, or the text "", "0", "1", "true", or "false"; else the tool stops and the old file stays. The one exception of the game
  data: the 22 store tables (`MasterItemDefinitions_MTX_*`) have no column NotDroppable (their items have no such protection). The
  retail tables pass, and the output is the same as the file of stage 2.2: `item-catalog.json` needs no new write. The loader of the
  backend (finding WT-R4) reads the format of the file: a file of format 2 must have the two protection columns, a file of format 1
  (or with no format) the 12 old columns; another format is refused.
- Stage 2.2 changes `creatures.json` (format 4). `creature_data.py`: the reader of a spawner slice also reads its
  TimeOfDayConstraintComponent (type e949f549-71a8-4914-861c-20a2ca6b75c4: the flags m_night, m_dawn, m_day, m_dusk,
  m_despawnOnTimeEnd of its server facet) and the cooldown flags of the spawner (m_cooldownAfterDespawn,
  m_cooldownAfterConstraintFail). A spawn point has three more values: the phase mask (bit 0 night, bit 1 dawn, bit 2 day, bit 3
  dusk; 15 with no constraint), the despawn flag, and the cooldown flags (bit 0, bit 1). The tool stops, and keeps the old file, for two
  constraints in one spawner and for a flag that is not one byte of 0 or 1 or that is not there (`check_creature_tool.py`: cases
  phase-*). The retail data: 405 points with a constraint (dawn and day 135, dawn, day, and dusk 12, day 1, night and dawn 27, night,
  dawn, and dusk 17, night and dusk 213), each with the despawn flag; one point (the day-only random encounter of the Elk Elemental)
  has the two cooldown flags. The research counted 403: it read only the point spawners. The other members and values are equal to
  the file of stage 3.5b.2. The loader takes the formats 1 to 3 too (each point in each phase), and it refuses a file of format 4
  with a mask above 15, a flag that is not a truth value, or cooldown flags above 3, as a whole. Write the file again with
  `tools/gamedata/creatures.sh`.
- Stage 2.2 changes `item-catalog.json` (format 2) and the checks of `progression_tables.py` (the corrections of the code review of
  stage 3.5b.2). `item_catalog.py` format 2: two more columns of each item, `nonRemovable` and `notDroppable`, from the cells
  Nonremovable and NotDroppable of the master item tables (finding LB-R1). Such a cell can be a flag, a number, or a text (`""` or
  `"1"`); a house item has no protection. The retail tables have 3,699 rows with Nonremovable and 328 rows with NotDroppable (4,027
  items with one of them; most are quest items). The other columns and members do not change. `check_item_catalog_tool.py` is the new
  check of the tool (cases protections and control; case RLB1 loads the file of the case protections). The backend also loads a file
  of format 1 (12 columns): then no item has a protection. `progression_tables.py` stops, and keeps the old file, when an attribute
  table does not have one row for each value of the attribute range of Constitution, in sequence (finding LB-R7: a table with no row
  for Focus 100 lost its mana threshold with no error; `check_progression_tool.py`: cases short-attribute-table and attribute-gap; its
  made-up attribute tables now have the values 5 to 100). The retail tables pass, and the output is the same as the file of stage
  3.5b.2: `progression-tables.json` needs no new write. Write `item-catalog.json` again with `tools/gamedata/items.sh`.
- Stage 3.5b.2 changes `interaction-data.json` (format 3), `progression-tables.json` (still format 2), and `chest-data.json` (still
  format 1), and the loaders of `chest-data.json` and `loot-tables.json`. `interactions.py` format 3: also the member `lootContainers`
  with the personal loot container (`personal`: the template of `slices/instanced_loot_container.dynamicslice`, the kind 1 AILoot, the
  LootDrop slot 6, the lifetime 180 s (KillSelfDelay at the init), the end after the last item 1.25 s (FastKillSelfDelay at
  completelyLooted), the hide delay 1.5 s, 5 rarity particles) and the option TakeAllOption of the kind `lootDrop`; the tool stops when
  the slice is not AILoot, not instanced, has no Storage unique for a player, or has not exactly one KillSelfDelay of the init and one
  end of completelyLooted (`check_interactions_tool.py`: cases bag-missing, bag-no-lifetime, bag-shared). A file of format 2 loads: then
  the backend has no loot bag (the items of a kill go into the bag of the character). `progression_tables.py`: also the threshold
  abilities of each attribute (`attributes.abilities`: for example `focus` [[25, [Foc_Bonus_25_1, Foc_Bonus_25_2]], ...], 8 thresholds
  for each attribute; `check_progression_tool.py`: cases attribute-thresholds and no-equip-abilities); a file with no such member loads
  (no mana bonus). `chests.py` (code review of stage 3.5b.1): the record of a chest must have the gatherable row of the variation
  (CRC-32; finding CH-R7), each TerritoryDefinition table of the game data gives the territories (the points of interest and the arenas
  too: 1,228 chests get the LootTags of their point of interest; finding CH-R6), and the tool stops for a chest that can be ordinary with
  no record (finding CH-R5), for two records of one row at a chest with different content, and for a territory with no row
  (`check_chests_tool.py`: cases missing-metadata, missing-territory, wrong-record-first, record-conflict). The loaders: the chest file
  must have one chest for each actor (finding CH-R4), and a loot file of format 2 must have a container floor for each level of its gear
  score ranges (finding CH-R8; the retail file has 80 floor rows for 70 levels). Write the three files again with the tools of the
  stage.
- Stage 3.5b.1 adds `chest-data.json` and changes `loot-tables.json` (format 2), `interaction-data.json` (still format 2), and
  `spell-data.json` (format 5). `loot-tables.json` format 2: also the loot roots of the chests (`chestRoots`: the LootTableComponent of
  each row of `Gatherable_LootContainers`, 64), the loot limits (`limits`: LootLimits, 338 rows, exported only: no chest table of this stage
  has a limit condition), and the container floor of each level (`containerFloor`: ContainerLootGSFloor of VitalsLevels, 80 levels); the
  tool stops for a chest root with no table, but for the three roots of `CHEST_ROOTS_NOT_IN_GAME`, and the loader refuses a chest root
  that names no table; a file of format 1 loads, and then a chest has no loot (the backend writes a warning). `interactions.py`: a
  condition of a bound is a requirement only when its node has a value (a nonzero byte) or a class that is not one of the three empty
  condition classes (`EMPTY_CONDITION_CLASSES`): before, each chest and each gathering node had a condition requirement, and the backend
  refused each open; now 20 bounds keep their condition (the doors of quests). `spells.py` format 5 (code review of stage 3.4f-D, finding
  SU-R3): each volume has the names of the parents of its entity (1,001 volumes have parents); the loader of this stage takes format 4 too
  (with no check of the parents). `creature_behavior.py` (finding SU-R4): a cast of a spell of the list `areaSpells` with no spell row, no
  slice asset, or no slice stops the tool; the `creatures.json` of the retail data does not change (the same bytes). Write the four files
  again with the tools of the stage.
- Stage 3.4f-D changes `spell-data.json` (still format 4) and `creatures.json` (still format 3). `spells.py`: a volume whose own entity
  has an activation event at the init has the time of the earliest activation of it or of a parent, also when the slice marks it active
  at the start (code review of stage 3.4f-C, finding AR-R5: 11 volumes of the build change, among them the mage area, 0 to 1 s); a size
  field of a shape (the radius of a sphere or a cylinder, the height of a cylinder), m_center, and m_localTM that is there must be a
  finite 32-bit float of its size (finding AR-R6, cases `area-cut-radius`, `area-cut-height`, `area-cut-center` of
  `check_spell_tool.py`): else the tool stops and the old file stays. The loader of the backend refuses a file of format 4 whose volume
  time does not agree with the delayed events of its entity, and the creature areas need format 4 (finding AR-R4). The creature tool
  stops for a cast of a supported area spell with a malformed event or a broken join (finding AR-R7, cases `review-area-*` of
  `check_creature_tool.py`), but for the authored cases of the retail data in `AREA_EXCEPTIONS` of `creature_behavior.py` (15: the
  ghosts with the shared action list of the plague nova, the two Dynasty summoners); `areasLeftOut` has only the casts of spells and
  actions with no support. `creatures.json` has the area attacks of 10 spells (the list `areaSpells`: the four casts at the caster of
  stage 3.4f-D added; 22 slices, 177 kinds, 539 spawn points), the status actions (`statusActions` of a slice and the list
  `statusActions` of `creature_slices.json`: 3 slices, 18 kinds, 91 spawn points), and `lowHealth` (119 slices), with their counts.
  3,101 of the 3,848 kinds attack (747 do not, 7,997 spawn points). The backend of stage 3.4f-D refuses the `spell-data.json` of stage
  3.4f-C as a whole (the volume of the mage area): write both files again with the tools of the stage.
- Stage 3.4f-C changes `spell-data.json` (format 4) and `creatures.json` (still format 3). `spell-data.json` format 4: each detection
  volume has its shape (`sphere`, `cylinder`, `none`, or `other`: the QueryShape of the volume), its shape radius, its height, its
  offset above the root of the slice, its start, and its end (the earliest deactivation at the init of its entity or of a parent; -1:
  never); each area status has its initial delay and its reapply delay (`m_initialDelaySeconds`, `m_reapplicationDelaySeconds`). The
  loader takes the formats 1 to 3 too; then no creature casts an area. The loader refuses a file of format 4 with a volume or an area
  status whose values are not finite or not in their ranges. `creatures.json`: a slice can have `areas` (its area attacks: the action,
  its length, and the casts with their times, spells, shapes, clip values, and for a strike the damage row), and the counts
  `slicesWithAreaAttacks`, `areaAttacks`, `areaCasts`, `areaSpells`, `kindsWithAreaAttacks`, `spawnPointsWithAreaAttacks`,
  `areasLeftOut`. The creature tool reads the spell tables and the spell slices (`spells.AreaSpells`) and the list `areaSpells` of
  `tools/gamedata/creature_slices.json` (the spells whose record has a live check: 6); it stops when the list is not correct. Its file
  of this build has 789 kinds with no attack. `stamina-data.json` is equal to the file of stage 3.4f-B; the tool and the loader now
  refuse a dodge fragment whose next dodge starts before its payment (code review of stage 3.4f-B, finding CS-R5, case
  `replace-before-pay` of `check_stamina_tool.py`).

- Stage 3.4f-B adds `stamina-data.json` (`tools/gamedata/stamina.sh`, format 1; 2 KB; needs an Oodle source for the player slice): the
  Stamina component of `slices/player.dynamicslice`, the stamina rows of BaseVitals_Player, StaminaCosts_Player, and the Player row of
  EncumbranceData, the action Dodge of `playeractions_dodge.actionlist` (its fragment and cost id for each load class, the delay of its
  exit, the conditional costs of a weapon rule that the backend does not apply), the action Exhausted of
  `playeractions_global.actionlist`, and the times of the dodge fragments of `player.adb` (the variant with no tag; each standing
  variant must have the same times to 1/60 s; the prone variants are listed as not used). The tool stops for a time that is not a
  finite number, a cost id with no row, a standing variant with different times, no Stamina component, and an exhausted action that
  does not run from 0 to 100 percent (`check_stamina_tool.py`, case SM1). With no file the backend works: the stamina is full, the
  dodge has no rule of the server (each dodge input is `noData`), and no payment changes anything. `ability-data.json` format 6 adds
  `actionHitStatuses` (the hit-taken actions of the player lists, with their tests; Mauler's Stamina) and
  `actionHitStatusesNotExported` (with the cause); the loader takes the formats 1 to 5 too. `creatures.json` (still format 3): a slice
  with no ranged FireBow has its ranged `Cast_One_Projectile` as its basic shot (the casters); the corrected tool (code review of stage
  3.4f-A, findings CR-R5 and CR-R6, `check_creature_tool.py` review cases) stops for a basic ranged action whose projectile has no
  catalog entry, no source, or no damage row, and for a release time or an aim value that is not a finite number (a time "nan" was 0
  before). Its file of this build has 805 kinds with no attack (814 before). The backend refuses a release whose ammunition key or
  damage table key does not agree with its names (finding CR-R7).

- Stage 3.4f-A changes `creatures.json` (still format 3): each slice can have `ranged` (its ranged attacks with their releases and
  joins), and the file has `projectiles` (the projectile slices: the asset, the root, the slot of the Projectile component, the flight
  values) and `playerCapsule` (the capsule of the player slice). The backend of this stage reads a file of the tool of stage 3.3b-S5 to
  2.5b too: then no creature shoots. The tool reads the ranged attack data (`rangedattackdatabase.radb`), the ammunition table, and the
  default tags of each slice; it stops with no ranged attack data. `interaction-data.json` of the corrected tool (code review of stage
  2.5b) is equal to the file of stage 2.5b, except the counts of the left-out children (`excluded`: the five uses of the three assets
  that no record of the catalog has have their own cause).

- Stage 2.5a adds `interaction-data.json`. With no file the backend works as before for the other systems: each request of an
  interaction is refused (a conversation then closes at once: make the file). The other files of the tools are equal to the files
  of stage 3.3b-M2b (also `weapon-actions.json` with the stricter checks of finding M2b-R4).
- Stage 3.3b-M2 changes four files. `weapon-actions.json` format 2 (the abilities and their fragments; the backend reads a file of
  format 1 too, and then the six abilities of stage 3.3b-M2 are refused: `noRule`). `combat-tables.json` format 3 (`rowEffects`:
  the StatusEffect of each damage row with EffectOnlyOnDamage, `[row, [status, ...], onlyOnDamage]`: the root of Gravity on the
  pull rows `_RP2`; with an older file no row gives a status, and the plan of Maelstrom keeps its root). `spell-data.json` format
  3 (`areaStatuses` of each slice: the AreaStatusEffectComponents, `[entity, detection, radius, [status, ...], removeOnExit,
  targets]`: the fortify in the well of Unyielding). `ability-data.json` format 5 (`actionStatuses`: the actions of the action
  lists that give statuses while a condition of nodes and tags holds, `[list, action, terms, [status, ...]]`: Guarded Sprint; the
  loader refuses a file whose status action names a status that the file does not have: code review of stage 3.3b-M2, finding
  M2-R9, case RH9).
  Write the four files again with their tools. The other files of the tools are equal to the files of stage 3.3b-M1.
- Stage 3.3b-M1 adds `weapon-actions.json`. The backend of this stage reads no older form of it; with no file it uses the rule of
  stage 3.2a for the melee hits. The other files of the tools are equal to the files of stage 3.3b-S6.

- Stage 3.3b-S6 changes `combat-tables.json` (format 2: the CanCrit cells as true, false, or null). The backend of this
  stage reads a file of format 1: then the 42 rows with an explicit CanCrit 0 (the burst of Gravity Well) can be critical,
  as in stage 3.3b-S5. Write the file again with `tools/gamedata/combat.sh`. The other files of the tools of this stage
  are equal to the files of stage 3.3b-S5.

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
