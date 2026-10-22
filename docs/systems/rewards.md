# The rewards of a kill

Read this document before a change of the credit, the experience, the loot, the coin of a kill, or
the respawn of a creature of a spawn point. The wire contracts are in the protocol notes (section
4.21).

## Start here

| Task | Start here |
| --- | --- |
| The rewards of a kill: the credit, the experience, the weapon mastery, the loot tables and their roll, the coin, the delivery with the loot notice, the loot switch; the respawn of a creature of a spawn point | Protocol notes 4.21, `apps/backend/src/world/rewards.h` (`reward_rules`, `respawn_time`), `gamedata/loot_tables.h`, `world/inventory.h` (`Inventory::grant`), `world/creatures.h` (`Creatures::hurt`, `respawn_at`), `server/http/loot_commands.cpp`, `tools/gamedata/loot.sh` (`loot_tables.py`, `check_loot_tool.py`), `tools/world loot`, `tools/world roll`, `tools/world creature respawn`, test cases LT1 to LT8, RL1 to RL3, RL8, RL10, RL11 |
| The roll of a chest: `roll_root`, the context of a container, the container floor | [Chests](chests.md), `world/rewards.h` (`LootContext::container`, `Rewards::roll_root`, `grant_items`), test case LC3 |

## Rules

- The rewards of a kill (stage 3.5a, protocol notes 4.21; project rules in `world/rewards.h`,
  each constant in `reward_rules`, with the values of the game data `loot-tables.json`,
  `gamedata::LootTables`). A kill is the death of a creature by the hit of a character
  (`Creatures::hurt` with an attacker): the creature system calls `Rewards::hit` for each hit
  (the credit entry of the character in the creature, `Creature::credit`) and
  `Rewards::killed` at the death. A death by the health command or by the kill command gives
  nothing. The credit (the credit data of the game): the hits of a character did 1 percent or
  more of the maximum health, its last hit was 120 s or less before the death, and it is in
  the world, lives, is not ignored, and is 200 m or less from the creature; 10 characters at
  most, and the credit modifiers of the game for their number. The reset takes the credit
  away (`Creatures::restore`). The experience goes through `Progress::add_experience`, the
  same amount to the weapon mastery of the weapon in the hand through
  `Skills::add_experience` (`WeaponRow::mastery` of the combat tables: a project rule, the
  factor of the game is an empty cell), and the items and the coin through
  `Inventory::grant`, in one transaction, with the cause `sync::kItemGathered` (1, the loot
  notice of the HUD). The roll of a loot table follows the rules of the roller of the client
  with the generator of the world only (`World::random_unit`). A full bag: an item with no
  free slot is not given, and the coin and the experience are. Since stage 3.5b.2 the items go
  into the part of the character in a personal loot bag at the place of the creature (one bag
  for each kill, a part for each character with the credit; `LootBags::stage_part` writes the
  rows of the part in the transaction of the reward), and only the coin goes through
  `Inventory::grant` ([Loot bags](loot-bags.md)); with no loot container in the interaction
  data (format 2) the items go into the bag of the character as before. The loot switch
  (`Rewards::set_forced`, `tools/world loot`, the admin command `set_force_loot`) is a value
  of the world with the character id as its key, until the backend stops. A new reward of a
  kill (the territory standing, the Azoth) goes into `Rewards::killed` and through the system
  of its state. The cost: a kill looks only at the credit list of its creature, and a roll
  at the tables of its path (test case LT7, with a time limit).
- The reward of a character is one change (review of stage 3.5a, finding F1, case RL1). The
  experience, the weapon mastery, the items, and the coin of one character go into one
  transaction of the store (`state::Store::Batch`; the write of each system is a save point
  in it). When the store refuses a write, the reward writes no row, it puts the values of
  the entity back (progression, attributes, vitals, skills, bag, coin), it marks these
  component states again, and it counts the reward as refused (`RewardCounts::refused`, the
  log line "the store did not write the reward"). A refusal of a rule (a full bag, the
  largest level) is not a refusal of the store: the other parts of the reward stay.
- A drop goes into the bag whole when the bag has space for it (finding F11, case RL11). The
  limit of the give command (100,000) is a limit of that command only, not of
  `Inventory::grant`. Only the stack size and the capacity of the bag limit a drop.
- The roll of a chest (stage 3.5b.1, [Chests](chests.md)): `Rewards::roll_root` rolls a loot root with a context (`LootContext`), and
  `Rewards::grant_items` makes the items with their perk roll (the path of a kill uses it too). A context of a container
  (`LootContext::container`) gives the numeric tags `poilevel`, `containerlevel` (the content level of the chest), and `minpoicontlevel`
  of the conditions, and the gear score of an item of a table with UseLevelGS has the container floor of the level
  (ContainerLootGSFloor of VitalsLevels, `LootTables::container_floor`) as its low end in place of the ceiling of a creature (game data).
  `loot-tables.json` format 2 has the chest roots, the loot limits, and the container floors; a file of format 1 loads, and then a chest
  has no loot table (each open is refused: `noLoot`).
- The roll of a gathering node (stage 3.6a, [Gathering](gathering.md)): `Rewards::roll_root` with the luck of the gather
  (`LootContext::rollBonus`: the RollBonus of the trade skill rank and the ROL parts). The luck adds to the bonus of each table of the
  path, as a numeric condition does (client roller 0x368B070): AddToRoll adds it to the roll, ClampMax raises the low end of the roll,
  IgnoreBonus ignores it (case GAT5). A kill and a chest have the luck 0. `loot-tables.json` from stage 3.6a has the member
  `gatheringRoots` (the FinalLootTable of each gatherable row that is not a loot container, and not `Empty`) and
  `gatheringRootsWithNoTable` (the roots of quest and event rows that no loot table has: such a row is refused, `noLoot`); the format
  stays 2.
- The perks of a drop (stage 1.3b, protocol notes 4.23): an item with perk places in the perk
  data gets the perk roll of the client (`world/perk_roll.h`) for each instance, with the level
  of the character that gets it and the gear score of the drop; the swap of the game follows
  at the grant. A character below level 7 gets only the fixed perks. `tools/world perkroll`
  rolls with no item (test case IZ3; live check I12: a rapier of a drop with one perk). A craft uses the same generator
  (`perk_roll.cpp`, `generate`) with the inputs of the craft and no level gate (stage 3.7c, [Crafting](crafting.md)); a roll with no input
  is the loot roll (test case CRAFTC2: the same perks and draws).
- The loot tool writes no file with a value that the backend refuses (code review of stage
  3.3a, finding F13, case RA13): the KillExpModifier of a level difference from -1 to 100, its
  MaxRewardLevelDelta from 0 to 1,000, and the odds of a bucket candidate not below 0.
- The respawn: a command removal of a dead creature (`tools/world creature remove`) clears the
  respawn time of its spawn point (finding F8, case RL8): the point stays empty until a reset.
- The kill of the quests (stage 1.8c, [Quests](quests.md)): after the rewards, `Rewards::killed` gives each character of the credit list (the
  creditors of this rule, not each character with a hit) the kill with the vitals categories of the victim (the member VitalsCategories of
  its base vitals row in `loot-tables.json`; a file of before: the vitals row and the category row of the kind) and its kill event, with a
  result id (the creature and the revision of its death). The quest drops of the character (`Quests::plan_drops`: a possession task whose
  ItemDropVC names a category of the victim) go with its loot (its part of the loot bag, or its bag), in the transaction of its reward with
  the award rows of AwardOnlyOnce. A kill with no credit (no creditor, no loot tables) gives the quests nothing. A quest drop that the bag
  lost (a full bag, with no loot bag data) writes no award row and keeps its misses: the next kill can drop it again (code review of stage
  1.8c, finding QR8C-R9; case QRW9).
  The removal of a body by the distance keeps the respawn time.
- The loot file is complete as a whole (finding F2, case RL2). The backend refuses the file
  (and gives no reward) when a base row names a creature type that the reward modifiers do
  not have or an event that the events do not have, or when the credit data has fewer credit
  modifiers than receivers. A default is only for a file that is not there.

## Commands

```sh
tools/world loot NAME on             # each kill of a character drops loot and coin (the loot switch; off: the chances of the game data)
tools/world roll Wolf_Grey_7 --count 1000   # roll the loot table of a kind 1000 times and count the items (--forced, --character NAME)
tools/world creature respawn 2c 20   # the dead creature 2c: its spawn point makes a new creature in 20 s (a player must be near)
```

- `tools/world` has the commands of stage 3.5a: `loot CHARACTER on|off` (route
  `/v1/dev/world/force-loot`, the mark "Each kill drops loot and coin" of the tab
  "Character": the outer drop chance and the coin chance of each kill of the character pass;
  a value of the memory of the world until the backend stops), `roll KIND [LEVEL] [--count N]
  [--forced] [--character CHARACTER]` (route `/v1/dev/loot/roll`: the loot table of a kind,
  rolled N times with the generator of the world, 10,000 at most; for each item the number of
  rolls that gave it, one for each roll also when a roll gave it more than one time (finding
  F10, case RL10), and the quantity of all its drops; no item goes to a bag), and `creature respawn CREATURE SECONDS` (route
  `/v1/dev/creatures/respawn`: the spawn point of a dead creature makes its creature again
  after that time, when a player is 150 m or less from the point). `tools/world list` prints
  the line `rewards` (the kills, the kills with no credit, the rewards, the experience, the
  coin, the items given and not given, the time of the rewards, the spawn points that wait,
  and the bodies that a respawn removed), and `tools/world creatures` prints the time to the
  new creature of a dead creature of a spawn point.
- `tools/gamedata/loot.sh` writes `loot-tables.json` to the game data folder (1.8 MB, 10 s):
  the rewards of a kill of each level, the factors of the creature types, the level
  differences, the credit data, the loot data of each vitals row, the 108 loot tables and
  the 85 loot buckets that a creature can reach, and the gear score limits of each level.
  The extracted files of the work folder are sufficient for it (`SOULWARDEN_OODLE_LIB` reads
  the pak files). `--find TEXT...` prints the loot data of the vitals rows and of the tables
  with a text. The tool writes no file when a table names a loot table or a bucket that the
  tables do not have, when a column that it reads is not there, and when a cell is not a
  finite number or has a form that it does not know: the texts that the game itself has with
  a wrong form are in the lists `FLAG_TEXTS_NOT_IN_GAME`, `CHANCE_TEXTS_NOT_IN_GAME`, and
  `RANGE_TEXTS_NOT_IN_GAME` of the tool. It also writes no file (findings F3 and F4 of the
  review of stage 3.5a) when a variant vitals row names a base row that the tables do not
  have (the four variants of the game with no base row are in the list
  `VARIANTS_WITH_NO_BASE_IN_GAME`), when a base row names a creature type or an event that the
  tables do not have, when the credit data has fewer modifiers than receivers, when a kill
  reward is negative, when a reward factor is negative or a chance is not from 0 to 1, and for
  the flag text `none`. These are the ranges of the backend: the tool does not replace a
  working file with a file that the backend refuses. `tools/gamedata/check_loot_tool.py` is
  the check of these rules, with made-up tables (13 cases; case RL3 runs it). `tools/gamedata/combat.sh` from stage 3.5a also writes the
  weapon mastery of each weapon (value 12 of a weapon): with a file from before, a kill gives
  no mastery experience.

## Traps

- A character with the ignore value on gets no reward from a kill: turn it off for the
  kill (`tools/world ignore NAME off`), and on again after it. With
  `SOULWARDEN_CREATURE_BEHAVIOR=0` no creature attacks, and the ignore value is not
  necessary.
- The credit needs hits of 1 percent of the maximum health of the creature or more. The
  health command lowers the health with no credit: a creature with a low health after
  `tools/world creature health` dies by one small hit, and the kill gives nothing (a lion
  at 20 health in one run). Set a health that the hits of the test take away (250 for a
  bobcat of level 25), or use no health command.
- A creature of `tools/world creature spawn` has no spawn point: its kill gives the
  rewards, but no respawn. A test of the respawn needs a creature of a spawn point.
- With the behavior on, a creature of a command roams (a Risen walked out of the picture):
  start a run for a kill video with `SOULWARDEN_CREATURE_BEHAVIOR=0`.
- Most kills give no item: a Grey Wolf of level 7 gave an item in 83 of 2,000 rolls. A
  live test of an item uses the loot switch (`tools/world loot NAME on`, or the mark of
  the tab "Character"); `tools/world roll KIND --count 1000` shows the chances. The switch
  goes away when the backend stops.
- A critter (a Young Scorpling) gives nothing: its creature type has the experience 0,
  and its vitals row cannot drop loot.
- The client removes the body of a dead creature after 300 s (a delayed event of its
  slice). A respawn time of 360 s (a bobcat) thus comes after the body went away in the
  client. `tools/world creature respawn ID 20` makes a test of the respawn short.
- A weapon at the largest mastery rank (rank 20 of the character `Soulwarden`) gets no
  experience notice. Use a weapon at a lower rank (the war hammer at rank 8) for a test of
  the mastery notice. A world entry selects the first weapon slot: a key `2`
  (`tools/hx 'key 0x32'`) selects weapon slot 2.
- The loot notice of the HUD shows for about 5 s at the right side: take the pictures of a
  kill in a loop of `gamescopectl screenshot` directly after the last hit.
