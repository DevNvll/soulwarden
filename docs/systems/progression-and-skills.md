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
- The weapon masteries, the nodes of the weapon trees, the ability bars, and the trade skills
  of a character are in its entity (`world::SkillSet`) and in the tables `progressions`,
  `ability_nodes`, and `ability_bars`, with the id on the wire of each row (the CRC-32 of the
  lower-case id text of the game table) and its text in the column `name`. Only the skill
  system (`world/skills.h`) changes them, and it writes all rows of a change in one
  transaction. A weapon has one mastery point for each rank. A node that a character loses
  stays in its entity with the level 0 while the character is in the world: the client
  removes a node only when it gets it with the level 0.
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
  level of the screen, 1 to 60; the experience becomes the start of the level), `xp CHARACTER
  AMOUNT`, `attribute CHARACTER NAME VALUE` (`constitution`, `focus`, `strength`,
  `dexterity`, `intelligence`; the character must have the points), `mastery CHARACTER WEAPON
  LEVEL [EXPERIENCE]` (level 1 to 20; `WEAPON` is `sword`, `bow`, `greataxe`, `firemagic`,
  ..., or the id of the game table, `SwordAbilityTable`), `tradeskill CHARACTER SKILL LEVEL
  [EXPERIENCE]` (`Mining`, `Cooking`, ...; 0 to 200), `skillxp CHARACTER NAME AMOUNT`, `node
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
  and trade skills, 611 nodes). The largest level of a player is the value of the expansion
  row `None` of the game (60), and the largest trade skill level is 200: the client has no
  expansion with this backend.
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
  with spent points. A level above 60 shows "Rise of the Angry Earth needed to advance", and
  the riding skill shows 0 for each level: the client has no expansion with this backend.
- A node that a character lost must go out one time with the level 0, and a points fragment
  with no node makes the client give all points of each pool back itself. Change the nodes of
  a character only through the skill system.
