# DLC and entitlements

Read this document before a change of the entitlements of an account, the achievements that they give a character, the
expansion limits (the level, the gear score, the trade skills), the reward of a DLC pack, or the entitlement routes, and
before a live test of the expansion. The wire contracts are in the protocol notes (section 2 and 4.29).

## Start here

| Task | Start here |
| --- | --- |
| The grant of the DLC to an account and a character | `apps/backend/src/world/ownership.h`, test cases DL2, DL5, DL8 |
| The two owner states (entitlement 19/3133, achievement 66/3210) | Protocol notes 4.29, `server/rep/states/ownership_states.cpp`, test case DL10 |
| The entitlement routes of the client (sync, list) | Protocol notes section 2, `server/http/entitlement_routes.cpp`, test case DL3 |
| The expansion limits (level 70, gear score, trade skills) | `gamedata/progression_tables.h` (`owned_expansion`, `level_cap`, `rank_cap`, `max_equip_gear_score`), [Level, attributes, and skills](progression-and-skills.md), test cases DL1, DL4, DL6 |
| The data of the expansion and of the entitlements | `tools/gamedata/progression.sh` (format 2), `tools/gamedata/check_progression_tool.py` (case DL9), [Game data](../development/game-data.md) |
| The research | `~/Games/soulwarden/work/research/dlc-entitlements.md`; the result of the stage: `~/Games/soulwarden/work/evidence/dlc/implement/RESULT.md` |

## Rules

- Decision of the user (2026-10-15): "i want the full game with dlc. for any player, auto unlock the dlc". Each account and
  each character owns the expansion Rise of the Angry Earth and the DLC packs of the client data, with no purchase and no
  setting. The items of the in-game store are not DLC: they stay earned or bought in play (a decision of the orchestrator
  from the research of the cosmetics).
- The grant policy (`world/ownership.h`; the source: the decision of the user and the joins of the game data):
  - Each account owns each entitlement of the list of the data (`ProgressionTables::entitlements`): the expansion
    `Ent_Xpack_RiseOfTheAngryEarth_Owner` (the EntitlementId of the expansion row `Expansion2023`) and the nine Steam roots
    of the DLC packs (`RETAIL_DLC_ROOTS` of the tool, a decision of the project from the research, Q1: the alpha test title
    and the four realm rows are not DLC) with the entitlements that a root gives (reward type `Entitlement`): 19 aliases.
  - Each character owns the achievement of each entitlement of its account that names one (the column AchievementId of the
    entitlement table: `Expansion_2023_Unlock`). The client tests the achievement of the character, not the entitlement of
    the account (expansion check `0x684B340`; live check L1: the entitlement bit alone opens nothing).
  - The reward of an entitlement of the type InventoryItem (`STEAM_APP_ID.1200562`: one AmuletIsabellaT1) comes one time to
    each character, through the inventory system: the row of `entitlement_rewards` and the item rows are one transaction
    (`Store::Batch`). The table gives no quantity: one item is a rule of this project. A full bag gives nothing and no row:
    the next world entry tries again. The other rewards (skins, emotes, crests, a housing item, a title, the art book) need
    their own systems: only their entitlement bits go to the client.
- The rows: `entitlements` (the persona id is the owner key), `achievements`, and `entitlement_rewards` (the character id is
  the owner key; the delete of a character removes them), schema version 8. The backend writes the rows of the policy at the
  login of an account (the token route), at the entitlement sync of the client, and at the world entry of a character
  (before its first record). A grant again changes nothing (`INSERT OR IGNORE`). The bits of the entity come from the rows
  and from the policy: a database that refused the rows still gives the character the expansion (the policy has no
  condition), and the next login or entry writes the rows.
- The level, the experience, the spent attribute points, the ranks, the nodes, and the bars of a character do not change at
  the grant (research Q4; test case DL4). A character at level 60 keeps its experience; with the limit 70 the experience of
  the start of level 60 (1,205,642) is in the range of level 60, and 96,451 more give level 61.
- The limits of a player are the limits of the owned expansion (`ProgressionTables::owned_expansion`: the last expansion row
  whose entitlement is in the list and names an achievement; game data and client code):
  - The largest level: MaxDisplayLevel 70 (the client: `0x3CED4B0`, the row minus 1; live check L2 read 70). A level above it
    is refused, and experience at it keeps the start of the level.
  - The largest rank of a weapon mastery or a trade skill: the Expansion01MaxLevel of the row when it is not 0, else its
    MaxLevel (the client: `0x3CB38E0`, `0x3CB6330`): 250 for the 18 trade skills and Riding, 5 for AzothStaff, 19 for each
    weapon (the mastery level 20).
  - The gear score of an equipped item: MaxEquipGS 800 (`Inventory::equip` refuses an item above it, `gearScoreAboveLimit`;
    the client: `@inv_cannotequipversionlevel`). The item level of a gear score (700 needs level 65) is a rule of the client;
    the backend has no check of it yet.
  - The gear score of a craft: MaxCraftGS 700 is in the data for the crafting stage (3.7).
  - With no expansion data (a file of format 1, or no file) the players have the limits of the file (level 60, the base
    ranks), and the backend sends neither state.
- The checks of the data (code review of stage 1.1b, findings F3, F5, F6; test cases RD3, RD5, RD6): the loader reads the
  `format` of the progression file first and refuses a mix of the two formats (format 2 needs the three lists and 5 values
  in each progression row). An entitlement of the reward type `Entitlement` must name at least one child, and each child
  must be an alias of the list. The tool stops when a rank limit of a row (MaxLevel, Expansion01MaxLevel) is not less than
  the number of the ranks of its table. A refused file gives no limits of an expansion: the backend works with no tables
  ([Level, attributes, and skills](progression-and-skills.md)).
- The achievement state also has the achievements that a character earned in play (stage 2.5a: the read lore pages,
  [Interactions](interactions.md)). Their rows are in the same table; the bit of an id comes from the progression tables, else from
  the interaction data (`Ownership::achievement_bit`). With no progression tables a character still gets the bits of these rows
  at its world entry. `Ownership::unlock_achievement` writes the row first, then the bit (case IT5).
- The entitlement state (slot 19) and the achievement state (slot 66) are for the owner only; an observer gets neither. The
  first record of a character has both, before the level (the codec sequence). The sequence of a container is the world
  revision of the last change of the state.
- The routes (`client.entitlementservice.amazongames.com`): the sync applies the policy again (a task of the world) and
  replies `{}`; the list gives each alias of the account with one owned unit (`freeAmount` 1, `totalAmount` 1, no paid
  amount) and `hasMoreResults` false. The persona of the path must be the account of the token: else 403 and no change (no
  token: 401). The backend contacts no service of Steam, Prime, or Twitch.
- Steam: the hook answers true for `BIsSubscribedApp` and `BIsDlcInstalled` (slots 6 and 7 of Apps008) for each app id; the
  client did not call the installed-DLC request or the DLC enumeration (slots 10, 11) in the live checks. No change of the
  hook, of the network namespace, or of the loopback rule.

## Commands

```sh
tools/world ownership NAME           # the expansion with its limits, the entitlements of the account, the achievements, the rewards
tools/world level NAME 70            # the largest level with the expansion
tools/world tradeskill NAME Mining 250
tools/gamedata/progression.sh        # the progression tables of format 2 (the expansions, the entitlements, the achievements)
```

- `tools/world ownership CHARACTER` (route `GET /v1/dev/ownership`, the common layer `commands::ownership_view`). The admin
  state of the tool window has the same view in the member `ownership`.
- There is no command that gives or takes an entitlement: the policy has no condition (no purchase, no setting).

## Traps

- The level window of the client computes "XP to Level N" when the level changes: a grant that comes in a later state (the
  live check with `tools/world fragment`) shows "needed to advance" until the next change of the level. The first record of
  a world entry has the states before the level, and the window is correct from the start.
- A change of the achievement bit during play opens the modal "Congratulations on purchasing the new expansion" one time
  for each persona of the client (a flag of the client). Its text says "Your level cap has been increased to 65": the text
  is from the launch of the expansion; the limit of the client is 70.
- The entitlement bit of the expansion alone, and the routes alone, open nothing: the client needs the achievement of the
  character.
- A progression file of format 1 and a backend of stage 1.1b: the players have the limit 60, and the client shows the lock
  of the expansion. Make the file again with `tools/gamedata/progression.sh` and put it together with the backend.
- `tools/world fragment` with the states of this stage needs the full container form and increasing sequences:
  `~/Games/soulwarden/work/evidence/dlc/implement/scripts/frag.py` makes them.
