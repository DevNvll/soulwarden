# Level, attributes, and skills

Read this document before a change of the level, the experience, the attributes, the weapon
masteries, the ability bars, or the trade skills, and before a live test with the character window.
The wire contracts are in the protocol notes (sections 4.12 and 4.13).

## Start here

| Task | Start here |
| --- | --- |
| Level, experience, attributes of a character | Protocol notes 4.12, `apps/backend/src/world/progress.h`, `server/rep/states/attribute_state.cpp`, `tools/world level`, `tools/gamedata/progression.sh` |
| Weapon mastery, ability bar, trade skills | Protocol notes 4.13, `apps/backend/src/world/skills.h`, `server/rep/states/skill_states.cpp`, `tools/world mastery`, `tools/world node`, `tools/world bar`, `tools/research/ability_tables.py` |
| The use of an ability of the bar (the cast, the cooldown) | [Abilities](abilities.md), protocol notes 4.22 |
| The level, the attributes, and the trees of a build preset | [Build presets](build-presets.md), `Progress::plan_sheet`, `Skills::plan_trees` |

## Rules

- The level, the experience, and the attribute points of a character are in its entity
  (`world::Progression`, `world::Attributes`) and in one row of the table
  `character_progress`. Only the progress system (`world/progress.h`) changes them: set the
  level, give experience (the level goes up at the limit of a level), set an attribute, spend
  points, and reset them. The database has the points that the character spent on each
  attribute: the value of an attribute is the minimum of the game tables (5) plus these
  points, and the free points are the points of the level minus the spent points. The
  progress system computes the maximum health (the base of a player, the health of the
  level, and the health of the constitution) and marks the vitals state. The cost of a reset
  goes from the coin through the inventory system (`Inventory::pay`), in the transaction that
  writes the points. A system that gives experience later calls `Progress::add_experience`.
- The attribute bonus (stage 1.3b, [Gear and perks](gear-and-perks.md)): the gear system sets
  `Attributes::bonus` from the perks and the effects of a character; the rules use the value
  with the bonus (`Attributes::value`), the attribute state has the base value in field 0 and
  the bonus map in field 1 (only the attributes with a bonus: a character with no bonus sends
  the same bytes as before). The maximum health has the health part of the gear
  (`Progress::refresh_vitals`). The bonus is not in the database.
- The threshold abilities of the attributes (stage B4): each ability of `attributes.abilities` of the progression tables whose threshold
  the value with the bonus reaches is a rule of the character ([Passive rules](passive-rules.md), `Passives::attribute_rules`; the client
  attribute screen tests the base value plus the bonus, `attributes.lua`). Before stage B4 only their mana parts acted (stage 3.5b.2) and the
  gathering parts (stage 3.6a). The bow build (dexterity 456, strength 50, constitution 19, focus 5, intelligence 5): the combat rows
  `Dex_Bonus_25_1` to `Dex_Bonus_350_1`, `Str_Bonus_25_1`, and `Str_Bonus_50_1` act; `Dex_Bonus_50_2`, `Dex_Bonus_300_1`, and
  `Dex_Bonus_300_2` stay not applied (the list names the cause); the rows `_2` with IsNonCombatAbility are values of the gathering.
- The weapon masteries, the nodes of the weapon trees, the ability bars, and the trade skills
  of a character are in its entity (`world::SkillSet`) and in the tables `progressions`,
  `ability_nodes`, and `ability_bars`, with the id on the wire of each row (the CRC-32 of the
  lower-case id text of the game table) and its text in the column `name`. Only the skill
  system (`world/skills.h`) changes them, and it writes all rows of a change in one
  transaction. A weapon has one mastery point for each rank. A node that a character loses
  stays in its entity with the level 0 while the character is in the world: the client
  removes a node only when it gets it with the level 0.
- The plan of a change (stage B0, [Build presets](build-presets.md)): `Progress::plan_sheet` (the level and the value of each
  attribute: the rules of `set_level` and `set_attribute`) and `Skills::plan_trees` (for each tree the rank, the reset of the tree with
  no cost, the nodes in sequence with the rules of `assign`, and the bar with the rules of `set_bar`) make the change on a copy with no
  write. `write` puts the rows into the transaction of the caller, and `finish` changes the entity and marks the states. The single
  commands use the same steps.
- A request of a client for its attributes or its skills that the backend refuses changes
  nothing. The client has no failure message for these requests, and it changed its own
  values already: the session gets the state of the component again. Only the tree reset has
  an answer (type 2858, also for a refused reset, with the facet target and the entity of the
  request). A request that names the entity of a different character gets nothing.
- The ability state (slot 46) has one element for each of the 29 ability tables of the
  client, in the sequence of the client (`gamedata::kAbilityTableOrder`), and each element has
  three places. A fragment with fewer elements makes the client read memory that is not a
  bar. The sequence is a constant of the supported client build, read from the live client:
  the game files do not give it. `tools/research/ability_tables.py` compares it with a live
  client.
- The progression tables loader refuses a file with a table that is not valid (a level table
  whose experience does not start at 0 or does not go up, a rank table that is shorter than
  the largest rank or has an experience of 0 below it, a largest rank with more than 16
  bits, a weapon with no reset cost for each rank). The backend then works with no tables.
- The limits of the expansion (stage 1.1b, [DLC and entitlements](dlc-and-entitlements.md)): each player owns the
  expansion Rise of the Angry Earth (a decision of the user), thus the largest level is the MaxDisplayLevel of the owned
  expansion row (70; `ProgressionTables::level_cap`), and the largest rank of a weapon mastery or a trade skill is the
  Expansion01MaxLevel of its row when it is not 0, else its MaxLevel (`ProgressionTables::rank_cap`, the rule of the client
  `0x3CB38E0`: the trade skills and Riding 250, AzothStaff 5, each weapon the mastery level 20). The progress system and the
  skill system use these limits for each command and each request. The grant of the expansion changes no level, no
  experience, no point, no rank, no node, and no bar. A file of format 1 has no expansion: the limits of the file (60, the
  base ranks). The loader refuses a file of format 2 whose expansion, entitlement, and achievement lists do not fit
  together, or whose largest rank with the expansion is not valid for its rank table (test case DL1).
- The format of the progression file (code review of stage 1.1b, findings F5 and F6, test cases RD5 and RD6): the loader
  reads the member `format` first (no member: format 1; only 1 or 2). Format 2 has the expansion, entitlement, and
  achievement lists, and each progression row has 5 values; format 1 has none of the lists and 4 values. A mix is refused.
  An entitlement with the reward type Entitlement must name its child entitlements ("A+B"), and each child must be an
  alias of the list; an empty list is refused. The tool stops when the MaxLevel or the Expansion01MaxLevel of a row is not
  less than the number of the ranks of its table (finding F3, test case RD3).
- The trade skill experience of a craft (stage 3.7b, [Crafting](crafting.md)): each job of a craft batch gives the experience of the event of
  its recipe through `Skills::plan_experience` (the rules of `add_experience` on a copy): the crafting system writes the rows in the
  transaction of the job and finishes the plan after the commit. The first job of a recipe that the character never crafted gives three
  times the base (the first-craft bonus of the client). Test case CRAFTB2. From stage 3.7c the experience of a job is the floor of the
  raw total of the batch up to that job minus the experience of the jobs before it (code review of stage 3.7b, finding CR7B-R1; test case
  CR7B1).
- The Azoth balance (stage 3.7c, [Crafting](crafting.md)) [game data: Azoth_Currency of CategoricalProgressionData, CRC `37504247`, rank
  0, the cap 2,500 of the AzothCurrency table]: the balance is the points of the row Azoth_Currency of the categorical progression of the
  character (the table of the skills), and an entry of the state 911 with the trade skills (protocol notes 4.52). The rank-up rule of a
  trade skill does not apply: `Skills::add_balance` (a plan of a transaction with a cap) and `Skills::set_balance` change only the points,
  and `Skills::balance` reads them. A craft takes the Azoth of its perk upgrade and of its currency ingredients in the transaction of each
  job. The command `set_currency` (the tool window, `tools/world azoth`) sets it; the world has no other source yet.
- The effect of a learned node (stage 3.3b-S5): the evaluator of the passive rules applies the row of each learned node of
  the tree of the weapon of a hit, and the fixed stats of the nodes of the weapon in the hand go into the gear values
  ([Passive rules](passive-rules.md)). A change of the learned nodes of a character in the world (an assignment, a reset of a
  tree) refreshes the gear values after the stored change (`Skills::change`; code review of stage 3.3b-S5, finding F5, test
  case RV5). `tools/world passives NAME` lists each learned node with what the backend does with it.

## Commands

```sh
tools/world level NAME 20            # set the level of a character
tools/world xp NAME 6000             # give experience (the level goes up at the limit)
tools/world attribute NAME strength 12   # set the value of one attribute
tools/world mastery NAME sword 5     # set the mastery level of a weapon (each level above 1 gives one point)
tools/world tradeskill NAME Mining 50    # set the level of a trade skill
tools/world skillxp NAME sword 100   # give experience to a weapon mastery or to a trade skill
tools/world progress NAME            # the level, the attributes, the masteries, and the trade skills
tools/world node NAME greataxe Ability_Greataxe_Charge   # assign nodes of a weapon tree (one point each, the rules of the tree)
tools/world bar NAME greataxe 0 Ability_Greataxe_Charge  # put an ability on a place (0, 1, 2) of the bar of a weapon
```

- `tools/world` has the commands for the level, the attributes, and the skills of a character
  (routes `/v1/dev/progress`): `progress CHARACTER` (the list), `level CHARACTER LEVEL` (the
  level of the screen, 1 to the largest level: 70 with the expansion of stage 1.1b; the experience becomes the start of the
  level), `xp CHARACTER
  AMOUNT`, `attribute CHARACTER NAME VALUE` (`constitution`, `focus`, `strength`,
  `dexterity`, `intelligence`; the character must have the points), `mastery CHARACTER WEAPON
  LEVEL [EXPERIENCE]` (level 1 to 20; `WEAPON` is `sword`, `bow`, `greataxe`, `firemagic`,
  ..., or the id of the game table, `SwordAbilityTable`), `tradeskill CHARACTER SKILL LEVEL
  [EXPERIENCE]` (`Mining`, `Cooking`, ...; 0 to 250 with the expansion, Riding too), `skillxp CHARACTER NAME AMOUNT`, `node
  CHARACTER WEAPON NODE...` (stage 3.3a, route `POST /v1/dev/progress/node`, admin command
  `assign_node`: the node ids of the game table in the sequence of the tree; each one costs one
  mastery point, and the skill system checks the rules of the tree as for a request of the
  client), and `bar CHARACTER WEAPON SLOT ABILITY` (route `POST /v1/dev/progress/bar`, admin
  command `set_ability_bar`: an ability node that the character has, on the place 0, 1, or 2; `none` empties the place).
  The character can be in the world or not. The client shows a change at once: the level
  notice, the character window (K key, `tools/hx 'key 0x4B'`), the HUD.
- `tools/gamedata/progression.sh` writes `progression-tables.json` to the game data folder,
  from the same sources as the item catalog tool (the Oodle helper, or the extracted tables).
  The backend log says at the start if the tables are loaded (70 levels, 36 weapon masteries
  and trade skills, 611 nodes) and the limits of the owned expansion (format 2, stage 1.1b: the
  largest level 70, equip gear score 800, craft gear score 700, trade skill level 250). The file
  has the rows of the expansion table, the entitlements that each account gets, and the
  achievements that they give ([DLC and entitlements](dlc-and-entitlements.md)).
- `tools/research/ability_tables.py` reads the ability table list from the memory of the live
  client (`tools/hx`) and compares it with the constant of the backend. Status 0: the lists
  are equal. Run it after a change of the game copy: a wrong sequence puts the abilities of a
  weapon on the bar of a different weapon with no log line.

## Traps

- The node list of the point state (slot 67) of a fragment replaces the list of the client.
  Do not send a fragment with a part of the nodes: the client then shows only that part.
- The character window (K key, `tools/hx 'key 0x4B'`; Escape closes it), positions in the
  window of 1256x660. Tabs: "Attributes" `253 104`, "Weapon Mastery" `409 104`, "Trade
  Skills" `571 104`. Attributes: the "+" buttons at x `386` (strength `176`, dexterity `245`,
  intelligence `314`, focus `383`, constitution `452`), "Assign" `733 524`, "Respec" `532
  524`, "Yes" of its popup `714 412`. Weapon mastery: the sword row `238 214` opens its tree;
  in the tree, "Assign" `765 559`, "Respec" `765 598`, "Yes" of its popup `714 397`; the
  first node of the first row of the second tree is `826 195`. The positions of the nodes
  depend on the weapon: take a screenshot before you use a position.
- A level, a mastery level, or a trade skill level that changes shows a notice at the top of
  the screen for some seconds (the level, or a list "Unlocked"). It covers a part of an open
  window: wait 6 s before a screenshot of the window.
- The text "N XP to Level M" of the open character window is one state behind after an
  experience gain: the client writes it when the window opens. Close the window and open it
  again before you read the text. The bar is correct at once.
- The button "Respec" of the attributes is on only after an attribute state of the server
  with spent points. With no achievement of the expansion (a progression file of format 1, or a
  backend from before stage 1.1b) the level window at 60 shows "Rise of the Angry Earth needed to
  advance", the trade skill limit is 200, and Riding has a lock.
- A node that a character lost must go out one time with the level 0, and a points fragment
  with no node makes the client give all points of each pool back itself. Change the nodes of
  a character only through the skill system.
