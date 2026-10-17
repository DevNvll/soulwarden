# Build presets

Read this document before a change of a build preset, of the plan of a system that a preset uses (the items, the level and the
attributes, the trees), or of the gear score limit of a give. A build preset gives a character a whole build in one step (stage B0,
the bow build of the user; build sheet `~/Games/soulwarden/work/research/bow-build.md`). The items are in
[Items and equipment](items-and-equipment.md), the level and the trees in [Progression and skills](progression-and-skills.md), the
achievements in [DLC and entitlements](dlc-and-entitlements.md), the tool window in [Tool window and admin](tool-window-and-admin.md).

## Start here

| Task | Start here |
| --- | --- |
| The content of a preset (the items, the perks, the level, the attributes, the trees, the bars, the variants) | `apps/backend/src/world/build_presets.cpp` (`bow_boltcaster`), test case BP6 |
| The check at the start and the apply | `world/build_presets.h` (`BuildPresets::check`, `apply`), test cases BP3, BP4, BP5, BP8 |
| The plan of each system (a copy, the checks, no write) | `Inventory::plan_build`, `Progress::plan_sheet`, `Skills::plan_trees`, and their `write` and `finish` |
| The command, the routes, the tool window | `server/http/preset_commands.cpp`, `character_commands.h` (`apply_preset`, `presets_json`), `admin_routes.cpp`, `dev_progress_routes.cpp`, `apps/game-hook/src/client/toolwindow/ui/window_layout.cpp` (`draw_presets`), test case BP7 |
| The gear score limit of a give and the item catalog format 3 | `Inventory::give_limit`, `gamedata/item_catalog.h` (`maxCraftUpgradeGS`, `ArtifactObjective`), `tools/gamedata/item_catalog.py`, test cases BP1, BP2 |

## Rules

- A preset is project data in the repository (`project_presets`): game ids and numbers only. It has no account, no character, and
  no Steam id [orchestrator decision]. A preset has variants; the first variant is the default of a command with no variant. The
  preset `bow-boltcaster` has the variants `pve` ("PvE tree", the default) and `pvp` ("PvP tree"); only the bow tree is different
  [user decision D1].
- The content of `bow-boltcaster` [build sheet of the video; user decisions D2 to D5]: level 70; constitution 19, focus 5, strength
  50, dexterity 456, intelligence 5 (dexterity 500 with the food of the bag); the 14 items of the paperdoll with their gear scores and
  perks (the bow `Artifact_Set1_2hBow` 800, the rapier `Rapier_DoubloonShopNamedT5` 725, the four Gorgon armor items 725, the feet
  `Artifact_Set1_MediumFeet` 800, the amulet and the ring 725, the earring `Artifact_Set1_Earring1` 800, the heart rune
  `RuneBileBombT4A` 700, 50 health potions, 50 regeneration serums, 50 mana potions in the quick slots); in the bag 10 honing stones
  and 10 of each of the two foods; the bow and rapier masteries at level 20 with 19 nodes each, and the bars (bow: Penetrating Shot,
  Explosive Arrow, Rapid Shot; rapier: Evade, Flourish and Finish, Fleche). The perks are the stored ids: the ids after the swap of
  the game (the check at the start refuses a perk that the swap changes).
- The check at the start (`BuildPresets::check`, called by `main.cpp` after the construction of the world): each preset gets the
  checks of the systems on a character with nothing (`Inventory::check_build`, `Progress::check_sheet`, `Skills::check_trees` for
  each variant). A preset with an id that the game data does not have, a gear score that a give does not make, a perk that the swap
  changes, two items for one slot, a level above the cap, or a tree that the rules refuse is disabled with one log line (`presets: the
  build preset NAME is disabled: CAUSE`). One more line names the enabled presets. With no item catalog or no perk data each preset
  is disabled: the backend starts and works [project rule: the backend works with no game data].
- The apply is one task of the world and one SQLite transaction [project rules MP-1, the systems change their entities]:
  1. The plan of each system on a copy, with the checks of the single commands: the items (the give and the equip of
     `tools/world give` and `equip`), the level and the attributes (`set_level`, `set_attribute`), the trees (`set_rank`, the reset
     of the tree with no cost, `assign` of each node in the sequence of the preset, `set_bar`). No write.
  2. All rows of the three plans and the achievements in one transaction (`state::Store::Batch`).
  3. The finish of each plan: the entity and the marks of the component states (the level and the attributes first, then the items,
     then the trees), the achievement bits, and the refresh of the gear.
  A refused preset changes nothing: an unknown preset or variant, a disabled preset, a bag with no room for the old items, a level
  above the cap, a refused node, or a write that the database does not take [orchestrator criterion; test case BP4].
- The old item of each slot of the paperdoll goes into the bag [user decision D6]. A bag with no room for all of them refuses the
  whole preset (the reply 409, "the items: the bag has no free slot").
- A second apply of the same preset changes only what is not equal [agent decision]: an item of the preset that is in its slot with
  the same id, gear score, and perks stays (no new instance, no move); a stack of a quick slot with fewer items gets the difference
  (to the stack size of the item); a stack of the bag gets the difference to the count of the preset (the items of the same id with no
  perks count). A change of the variant thus changes only the trees (live: 0 old items into the bag).
- The achievement of the objective of each artifact of the preset (`Achievement_Objective_<item>`, the item catalog
  `artifactObjectives`) goes into the rows of the character and its achievement state [live check U1: the client shows the perks 2 to
  5 of an artifact only with that achievement; the tooltip says "Equip to view task." without it; game data: the test characters of
  the game get it with AchievementUnlockOverride]. A catalog of an earlier format has no objective: no achievement.
- An artifact of the paperdoll needs its objective in the item catalog (code review of stage B0, finding B0-R3; test case BP10): the
  check at the start disables a preset with an item whose class has `Artifact` and whose objective the catalog does not have (`the
  item catalog has no artifact objective of ITEM (the client would show its perks locked)`) [agent decision: the client shows the
  perks 2 to 5 locked with no achievement (live check U1); a disabled preset is better than a build that looks wrong].
- The objectives of the catalog (the loader, `ItemCatalog::load`, and the tool, `item_catalog.py`; findings B0-R3 to B0-R5, test cases
  BP10 to BP12) [client limit and agent decision]: a file of format 3 must have the member `artifactObjectives`; the achievement index
  must be below `kAchievementBitLimit` (the bit vector of the achievement state of the client, 0x2000000 bytes); one item has one
  objective, one objective id has one row, one achievement has one bit, and one bit has one achievement. Else the file does not load
  (the old catalog stays) and the tool writes no file. Before, the first row of an achievement won, and a bit at the limit loaded.
- A character that is not in the world gets the rows only; its next world entry reads them (test case BP8).
- The owner check [project rule]: the admin command `apply_preset` names a character of the account of the token, else the reply
  404. The command keeps the character id of the snapshot that the tool window drew.
- An observer of the character gets the visible gear and no state of the owner (rule MP-10; test case BP5).
- The gear score limit of a give (`Inventory::give_limit`) [game data; build sheet]: the larger of the largest gear score of the item
  table and the upgrade limit of the item (MaxCraftUpgradeGS: 800 for the 73 artifacts, no value for each other item), the upgrade
  limit not above the MaxEquipGS of the owned expansion (800). With no expansion data the upgrade limit has no such cap. A give with
  a gear score outside the range gets the nearest value of the range, with one log line (`inventory: the gear score G of ITEM is
  outside its range MIN to MAX: the new item gets V`) [agent decision: the rule of stage 1.3b for each give; a refusal would make the
  tool window and a script fail for a value that the backend can correct]. A give of `Artifact_Set1_2hBow` with 800 keeps 800, 801
  gives 800; a give of the Gorgon ring with 796 gives 725 [user decision D3: no data rule gives a ring above 725].

## Commands

```sh
tools/world presets                                    # the presets of the backend with their check (enabled, or the cause)
tools/world preset CHARACTER bow-boltcaster            # apply the PvE variant
tools/world preset CHARACTER bow-boltcaster pvp        # apply the PvP variant: only the bow tree changes
```

- Routes: `GET /admin/v1/catalog/presets` and the admin command `apply_preset` (arguments `preset`, `variant`) for the tool window;
  `GET /v1/dev/presets` and `POST /v1/dev/presets/apply` (`character`, `preset`, `variant`) for a development run.
- The reply names each change (the level and the attributes, each item, the count of the old items that went into the bag, each
  tree, each achievement). The status: 200 done; 404 an unknown preset, variant, or character; 409 a disabled preset or a refusal of a
  system (with the cause); 500 a write that the database did not take.
- The tool window (F8), tab "Character", group "Build presets" at the end of the tab (scroll down): the list of the presets, the
  variants ("PvE tree", "PvP tree"), "Apply", then the line "This replaces the equipment, the level, the attributes, and the trees of
  CHARACTER." with "Confirm" and "Cancel", then the result line ("Apply the build preset bow-boltcaster (PvE tree): done, N
  changes", or the cause of the refusal in red).

## Traps

- A perk of a preset must be the stored id after the swap of the game (the swap table changes some perks for some item classes).
  The check at the start disables the preset with the cause "a perk of ITEM is not the stored perk (the swap of the game changes it)".
  Give the item one time with `tools/world give NAME ITEM 1 --gs N --perks ...` and read the stored perks with `tools/world items
  NAME`.
- The nodes of a tree go in a sequence that the rules of the tree accept: a node needs its parent and the points of its row (a node
  of the last row needs 10 points in the tree). A preset with a wrong sequence is disabled at the start.
- The positions of the group in the tool window depend on the scroll of the tab and on the lines below it (the confirm line, the
  result line): take a screenshot after each scroll. In the window of 1256x660 after `'move 300 450' 'wheel -8'` the group is at
  the end of the tab: the variants at y 520 to 555 ("PvE tree" x 35, "PvP tree" x 135), "Apply" and then "Confirm" at x 51 to 59,
  y 560 to 585.
- The client counts the perks 2 to 5 of an artifact as locked with no objective achievement, but the gear system of the backend
  counts each perk of an item (an open point: the backend does not check the achievement).
- The bag of the development character can be full or above its weight limit after a preset: the old items go into it. The client
  then shoots no arrow ("Overencumbered"). Remove the items that a test does not need (`tools/world take NAME SLOT`).
