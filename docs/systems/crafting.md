# Crafting

Read this document before a change of the crafting: the craft request of a client at a station or a camp, the recipes and the station
rows, the plan of the materials and of the output, the craft batch and its jobs, the progress message, the empty storage search of the
crafting screen, the station variants of decision D1, and from stage 3.7b refining, the bonus items of decision D2, the trade skill
experience of a craft, the first-craft bonus, and the cooldown of a recipe (the Crafting state 205). The wire contracts are in the protocol
notes (sections 4.50 and 4.51). The station use (the prompt, the request of action 20, the Assembly, the StationContext) is in
[Interactions](interactions.md); the bag and the item instances are in [Items and equipment](items-and-equipment.md); the trade skills are in
[Level, attributes, and skills](progression-and-skills.md). Stage 3.7a is the first part of stage 3.7: the fixed recipes; stage 3.7b the
second part: refining and the reward progress. The procedural items, salvage, and repair are the runs 3.7c to 3.7e ([Roadmap](../ROADMAP.md),
section 3.7).

## Start here

| Task | Start here |
| --- | --- |
| A craft request: the checks, the batch, the jobs, the progress, the stop | Protocol notes 4.50, `apps/backend/src/world/crafting.h` (`Crafting`, `crafting_rules`, `CraftVerdict`, `CraftEnd`), `world/entity.h` (`CraftJob`, `CraftProgress`), test cases CRAFT1, CRAFT2 |
| The plan of the materials and of the output (one transaction for each job) | `world/inventory.h` (`Inventory::plan_craft`, `CraftCost`, `CraftOutput`, `coin_written`), `world/item.h` (`sync::kItemCreated`), test case CRAFT2 |
| The messages 2997, 2999, 2993, 7044, 7039, 7046 | `server/rep/crafting_messages.h`, `server/rep/rep_session.cpp` (`handle_craft_request`, `handle_craft_cancel`, `handle_storage_search`, `send_craft_progress`), test case CRAFT1 |
| The crafting data (`crafting-data.json`) and its tool | `gamedata/crafting_data.h` (`CraftingData`, `Recipe`, `CraftingStation`), `tools/gamedata/crafting.sh` (`crafting.py`, `check_crafting_tool.py`), [Game data](../development/game-data.md), test case CRAFT-TOOL |
| The station variants of decision D1 | `tools/gamedata/interactions.py` (`station_overrides`), `gamedata/interaction_data.h` (`SpawnedPlacement::authoredTemplate`), the log line of `main.cpp`, test case CRAFT-D1, the case "decision D1" of `check_interactions_tool.py` |
| Two characters, the load | Test cases CRAFT-MP and CRAFT-LOAD |
| The corrections of the code review of stage 3.7a (CR7A-R1 to CR7A-R4) | `tests/review_37a_tests.cpp`, test cases CRV1 to CRV4; `~/Games/soulwarden/work/evidence/crafting/37a/code-review/FINDINGS.md` |
| The commands | `server/http/crafting_commands.cpp`, `server/http/character_commands.h`, `tools/world crafting`, `tools/world craft preview`, `tools/world craft stop`, test case CRAFT-CMD |
| The bonus items of a craft (the chance of the client, decision D2, the seeded preview) | Protocol notes 4.51, `world/crafting.h` (`CraftYield`, `Crafting::yield_of`, `draw_extra`), `gamedata/crafting_data.h` (`roll_bonus`, `material`), test case CRAFTB1 |
| The trade skill experience and the first-craft bonus (its achievement) | `Crafting::experience_of`, `world/skills.h` (`Skills::plan_experience`), `world/ownership.h` (`write_achievements`, the crafting source of `achievement_bit`), test case CRAFTB2 |
| The cooldown of a recipe and the Crafting state 205 | `world/entity.h` (`CraftCooldown`), `Crafting::cooldown_left`, `entered`, `set_cooldown`, `server/rep/states/crafting_state.cpp`, the table `craft_cooldowns` (`state/store.h`, schema 14), test case CRAFTB3 |
| The crafting data of format 2 (the rank tables, the first-craft bits, the bonus values of the items, the tier lists) | `gamedata/crafting_data.h`, `tools/gamedata/crafting.py` (case refining of `check_crafting_tool.py`), test cases CRAFTB-DATA, CRAFT-TOOL |
| Two characters, the load of stage 3.7b; the commands | Test cases CRAFTB-MP, CRAFTB-LOAD, CRAFTB-CMD |
| The correction of the code review of stage U4 (U4-R1: an output with a gear score range) | `CraftingData::match_catalog`, the check of `Crafting::check`, `tests/review_u4_tests.cpp` (CRU1, CRU2); `~/Games/soulwarden/work/evidence/menu-equipment/u4/code-review/FINDINGS.md` |
| The research and the result | `~/Games/soulwarden/work/research/crafting-stage.md` (sections 1 to 5, 8 to 12), `crafting.md`; `~/Games/soulwarden/work/evidence/crafting/37a/implement/RESULT.md`, `~/Games/soulwarden/work/evidence/crafting/37b/implement/RESULT.md` |

## Rules

The source of each rule is in brackets: game data, client code, live check (the checks L1 to L10b of stage 3.7a, protocol notes 4.50),
decision of the user, decision of the orchestrator, decision of an agent.

- The recipes [game data]. A recipe is a row of a table of the type CraftingRecipeData (15 tables, 8,294 recipe ids of the supported
  build). Its id on the wire is the CRC-32 of its lower-case id [client code 0x4485AF0]. A station row permits a recipe when one of the
  recipe keys StationType1 to 4 is in its CraftingTypes [client code 0x2E5ED00: membership, not the last digit]. The loader defaults of the
  client apply to the perk options: a blank PerkItemsBucketPush is true; a MaxPerkItemsAllowed of 0 is 1 for a procedural recipe [client code
  0x4481730, research K4].
- The recipes that the backend crafts [decision of an agent from the briefs: 3.7a the fixed crafts, 3.7b refining and the reward
  progress]: a recipe with no cause of the tool. The causes: a procedural result (3.7c), a currency ingredient (3.7c: the Azoth balance; no
  recipe of the criteria of 3.7b needs one), perk items or an Azoth upgrade (3.7c), a BaseGearScore or an output with a gear score range and
  no override (3.7c), an unlock or an achievement rule, SkipGrantItems, world tags, a temporary recipe, an output or an ingredient that is not
  an item, a category with no item, a quantity of 0, no station row for its keys; from stage 3.7b only data with no rule: a reward event
  with no row, with character experience (UniversalExpAmount), or of a progression with no rank table, a first-craft achievement with no row,
  a cooldown with only a time or only a count, refining whose trade skill has no rank table. In the supported build 948 recipes have no cause
  (stage 3.7a: 6): 610 refining recipes, 699 craft-all recipes, 942 with a reward event, 870 with a first-craft achievement, 45 with a
  cooldown. The backend also refuses a recipe that needs an expansion (RequiredExpansion) [decision of an agent: no check of the expansion
  in this stage].
- The request [client code 0x2DF48A0, 0x2E173D0; live check L3]. Message 2997 has the recipe CRC, the count, the fee and the gear score
  of the client, the perk items, the upgrade level, and one entry for each ingredient of the recipe in its order: the type Item, the
  quantity 1, and the CRC of the selected item (the client converts each selection to such an entry, 0x5BA9FB0). The server takes the
  quantities of the recipe times the count, not the entries [research crafting.md 4.1]. A body above a limit of the reader of the client
  (a count of 10,000, a fee of 100,000,000, a gear score of 10,000, 4 perk items, the upgrade 5, 7 entries, an entry quantity of 50,000,
  an entry type above 4) or with a byte after the last entry changes nothing [client code readers 0x2D93E30 to 0x2E6ACB0]. The facet of the
  request is the Crafting component of the character of the session (MP-5).
- The checks of a request, in this sequence (each refusal changes nothing):
  - the character lives, the crafting data and the item catalog are there [decision of an agent];
  - the character uses a crafting station or a camp (the committed use of the interaction base; never a value of the request), and the
    Assembly key of the use is a row of the station database [client code: lookup 0x2E804E0 has no alias];
  - no batch of the character runs [decision of an agent: one batch for each character; a repeated request gives no second item];
  - the recipe has no cause; a station key of the recipe is a type of the row; the trade skill rank is RecipeLevel or more [client code
    IsRecipeKnown 0x2E5E720]; the count is 1 to 10,000; no perk item and no upgrade;
  - the entries: their count is the count of the ingredients, each one has the type Item, an item ingredient names its item, a category
    ingredient names a member of the category (the IngredientCategories of the master items; the UI table craftingcategories does not
    define membership) [game data; client code 0x5BA9FB0];
  - the plan of the whole batch on a copy (`Inventory::plan_craft`): the costs from the unlocked stacks of the bag, then of the quick slots
    1 to 4 (the cache of the client counts these: craftinginventorycache.luac, ResetCache), two ingredients of one item are one cost (one
    stack pays a unit once), and the output of each job must fit in the bag (500 slots) [client code; decision of an agent for the
    order of the sources: the bag first]; the loot overflow of `Inventory::grant` does not apply to a paid craft [research]. The plan makes
    the jobs one after the other, each one with its costs and then its output, as the jobs of the batch run (code review of stage 3.7a,
    finding CR7A-R1; test case CRV1): a bag that has no place for the output of a job before the last job refuses the batch at the request.
    Before the correction the costs of all jobs left first and freed their slots: a bag with one free slot and stacks of 3 accepted 3 knives
    and stopped after the first one. A plan of 10,000 jobs takes 2 ms (test case CRV1);
  - the coin for the fee of the batch.
- The fee [client code 0x2E513A0, 0x2E57580; live check L3]: no fee when the Assembly of the station does not charge one
  (m_chargeCraftingFee: the camps have none); else the count times CraftingFee times the fee factor of the territory, to an integer, and
  for a recipe with UseCraftingTax round(max(1, fee x (1 - the station, territory, and company taxes))). The backend has no territory and
  no tax: the factor is 1 and the taxes are 0 [decision of an agent]. Thus a fixed recipe with CraftingFee 0 costs 1 (0.01 coin) for each
  batch: the live client sent the fee 1 for the counts 1 and 3. The server takes its own fee; a different fee claim is counted, not refused.
  The job k of a batch pays fee(k) - fee(k - 1): a stop after k jobs paid fee(k).
- The gear score claim [client code 0x2E41E10]: the server facet of the client compares the claim only for a recipe with a BaseGearScore;
  the recipes of this stage have none, and the backend does not compare it. The output instance gets the values of its item table
  (GearScoreOverride, the durability, the bind flag) as a give.
- The batch [decision D5 of the orchestrator, option A; client code 0x2E41E10, 0x617139E]. The client sends the whole count in one 2997
  (the setting javelin.crafting-enable-craft-all-override is true; live check L10b: count 3). The server checks the whole batch and keeps it
  as a value of the entity (`Entity::craftJob`); it reserves nothing in the store. The first job completes one craft time after the request
  (1.0 s: the default of javelin.crafting-duration-seconds, registered at 0x6171204; the server facet of the client schedules its delayed
  execution with it after one check of the whole count) [client code]; the next jobs complete in the next world ticks, 20 in each tick
  (`crafting_rules::kJobsPerTick`) [decision of an agent for D5 and the cost rule]. Each job runs the checks of the station use and the plan
  of one job again, then writes its rows and its fee in one transaction, then changes the entity (the items with the cause ItemCreated).
  A job that fails (the bag changed, the coin, the use) ends the batch before its cost and its output; the completed jobs stay.
- The stops: message 2999 of the client (the completed jobs stay; the client sends it after each final progress too, then it changes
  nothing) [live check L10b], the end of the station use (2473, 3746 of a command, the range, a new use, a travel, the death: the handler of
  the station options of this system), the leave of the world, and the command `tools/world craft stop`. A stop of the server sends the final
  progress (the completed count of the count, or the stop form with no job) [decision of an agent].
- The crash recovery [decision D5]: a batch is not in the store. Each completed job is one committed transaction, and a job is never half
  written. A restart or a crash ends the batch: the completed jobs stay, the other jobs did not take a cost. The next session has no batch.
- The result [live check L3, L10b]: the session sends the progress 2993 (the recipe id, the completed count, the count) after the record of
  the items of the same tick, so that the item comes before the final progress. The single craft: the client shows its result screen
  (the item card, "100 GEAR SCORE", "COMMON") 2 s after the item. A batch: the client shows "N ITEMS CRAFTED" with each item 2 s after the
  last item (craftingscreenv4.luac: OnSlotUpdate restarts a 2 s timer; the final progress sets the expected count). The completed count of
  the client ends its crafting (handler 0x2E602A0 calls 0x2E4B360, which sends 2999).
- A refusal [live check L3]: the session sends the stop form of 2993 (an empty recipe, 0 of 0): the client ends its crafting (it sends
  2999), but its screen waits for its own 10 s timer and then shows "Crafting attempt failed." No change of the bag or the coin. A second
  request while a batch runs gets no message: the stop form would end the crafting of the batch in the client.
- An accepted batch drops a progress of the character that the session did not send yet (code review of stage 3.7a, finding CR7A-R4;
  test case CRV4) [decision of an agent]: a refused request and an accepted request in one tick (two reliable messages in order) gave the
  stop form of the refusal after the acceptance; the client ends its crafting at it and sends 2999, which stopped the new batch before
  its first item. The first job of the batch sends its own progress.
- Stage 3.7b, the bonus items [client code 0x2E4FF50, 0x5C52A40, 0x2E57A70; live checks L4 and L6]. The chance of the client is
  max(0, m + r x 0.0001), which it shows x 100 ("%.0f%%": "Chance to craft additional items"):
  - m, the chance of the recipe and of the materials (0x5C52A40): BonusItemChance of the recipe; the ExtraBonusItemChance of the item of the
    first ingredient when it is an item (an integer; blank in the supported build); for each later category ingredient the value of the
    tier list for the tier of its selected member above (BonusItemChanceIncrease, index difference - 1, the last value for a larger
    difference) or below (BonusItemChanceDecrease) the base tier (the tier of the item of the first ingredient, or of the selected member of
    a first category ingredient), and the ExtraBonusItemChance of that member. A later item ingredient adds nothing.
  - r, the roll bonus points (0x2E57A70): IngredientBonusPrimary of the item of the first entry and IngredientBonusSecondary of each other
    entry (the item field QualityContribution of the client is not a column of the tables: 0); with no DisallowBonusesToGS (the recipe
    flag at +0xD) and a trade skill, the RollBonus of the rank (TradeskillRankData; Smelting 510 at rank 1, 1000 at 50, 2500 at 200) and the
    ROL effects of the trade skill (the ROL column of the statuses of the character and of its gear: `Gathering::effect_sum`).
  - Live check L4: the iron ingot shows 0% at Smelting 0 and 5% at Smelting 1 (RollBonus 510); the steel ingot (reagents FluxT5 and
    CharcoalT1) 8% at Smelting 50 (-0.02 + 0.10); Asmodeum 0% at Smelting 200 (-0.25 + 0.25 and 0.0005 of the Cinnabar: the screen rounds).
  - The server takes the chance of the request (the selected items, the rank, and the effects at the request; a rank up in the batch does
    not change it) [decision of an agent]. To 6 decimals: the float sums of the data give no tiny chance [decision of an agent].
- Decision D2 of the orchestrator (option A): the expected extra units of one job are e = OutputQty x chance (the chance is a share of the
  output of the job: a recipe with OutputQty 3 and the chance 0.5 has e = 1.5). Each job gives floor(e) extra units, and one more unit when
  one draw of the generator of the world (`World::random_unit`: the one random source) is below e - floor(e); a whole e takes no draw. A
  chance above 1 (100 percent) gives floor(e) units each job with no draw. The mean of the extra units is e, the value that the client shows
  (test case CRAFTB1: 200,000 jobs). The plan of a request reserves the largest output of each job (OutputQty + ceil(e)): no paid output is
  lost [decision of an agent]. A seeded batch: `tools/world craft seed NAME N` sets the generator, `tools/world craft preview NAME RECIPE
  COUNT --seed N` gives the extra units of each job (live: seed 37, 10 iron ingots at Smelting 200: the preview 4 bonus, the screen "TOTAL
  14, BASE AMOUNT 10, BONUS 4"). With the creature behavior on, the creatures take draws too: a seeded batch is exact only with no other
  user of the generator.
- The result of a craft-all recipe (CraftAll; each refining recipe has it) [live check L4; craftingscreenv4.luac OnSlotUpdate]: the screen
  shows TOTAL, BASE AMOUNT, and BONUS at the first slot update with the cause ItemCreated (3) or ItemAddedWithoutLimit (32) whose count
  reaches the count of the batch times OutputQty. A later update with the cause 3 shows the right total but the row of the base and the bonus
  goes away (L4 test B); an update with the cause ItemAdded (0) before it does not start the screen (L4 test A). Thus the output of each job
  before the last job of a craft-all batch has the cause ItemAdded, and the last job the cause ItemCreated (`CraftOutput::action`). A recipe
  that is not craft-all keeps ItemCreated for each job (the summary "N ITEMS CRAFTED" of stage 3.7a).
- Stage 3.7b, the trade skill experience [game data; client code 0x5C47AC0; craftingscreenv4.luac and craftingstatspanel.luac; live check
  L4]: each job gives floor(the CategoricalProgressionReward of the event of the recipe x the units of its ingredients that are not a
  currency) experience of the progression of the event (IngotT2: 11 x 4 = 44). The screen shows the gain from the categorical state 911
  ("+44 Smelting"). The faction buff and the progression boost of the screen have no source in the backend (1). The territory standing of
  an event (487 recipes with no cause) is not given: the backend has no territory standing [decision of an agent]. With no progression row of
  the progression the request is refused (noData).
- The first-craft bonus [client config javelin.crafting-first-craft-bonus-exp-multiplier, default 3 at 0x6171316 and 0x617133F; the screen
  reads the achievement of the character with IsAchievementUnlocked; live check L4]: while the character does not have the
  FirstCraftAchievementId of the recipe (its bit of the achievement state 66, AchievementIndex of the achievement tables: FC_IngotT2 is
  5330), the first job of a batch adds (3 - 1) x its base experience and writes the achievement row in its transaction; the bit and the
  achievement state follow the commit (`Ownership::write_achievements`, `achievements_written`). The next jobs and batches have no bonus.
  Live: the batch of 3 iron ingots at Smelting 0: "+220 Smelting" (44 x 3 + 88), the server 220 (rank 1, 112 of 114); then "+44"; the
  tooltip of the client: "Base 132 XP, First Craft 88 XP (x2), Total 220".
- Stage 3.7b, the cooldown of a recipe [game data: CooldownSeconds, CooldownQuantity; client code 0x2E897C0; live check L6]: the window starts
  at the first craft after the end of the last window, with `used` 1 and the end CooldownSeconds later; each next craft in the window adds
  1. A request whose count is above CooldownQuantity - used is refused (`cooldown`; the client limits its count too). The client ends a
  window itself at its end (L6: "9s per 9 items", then "per 10 items"); the server takes a window whose end passed as no window. One row for
  each recipe of a character (`craft_cooldowns`, schema 14) in the transaction of the job. The end is a time of the run clock of the world
  (the time that the backend ran): a restart keeps the time that is left, and the downtime does not count [decision of an agent, as the
  respawn of a gathering node of the decision of the orchestrator of stage 3.6a; live: 86,390 s left before a stop of 79 s, 86,333 s after
  57 s of run time].
- The Crafting state 205 (slot 63) [live checks L1 of stage 3.7a and L6; client code 0x2E897C0, 0x2E89910]: owner only; field 0, the full
  cooldown map (`{recipeCrc:u32 used:u8 end:u64}`, 100 entries at most) with the end on the synced wall clock of the client in nanoseconds
  (the clock of the end of a status effect; L6: the end in one hour showed "59m 57s per 9 items"); field 1, the gear score bonus map
  (`{skillCode:u8 bonus:u16}`, 21 at most; the code is the TradeskillType of the client with Weaponsmithing 0: Engineering 6, L6), empty in
  this stage (its sources are stage 3.7c). No fragment in the first record of a character with no active window; at each change the full
  maps (also empty). With no entry the client shows a time that counts down from the start of the session ("23h 55m per 10 items": a client
  default; L6).
- The empty storage search [live check L2; client code 0x2D13950]: the crafting screen of a station sends 7044 (a search UUID and its
  parameters); the session answers 7046 with that UUID and no result, and answers a 7039 with the same empty result. The craft takes the bag
  and the quick slots only: there is no storage before stage 5.2. A camp sends no search (craftingtreeview.luac).
- The state 205 (Crafting, slot 63) of stage 3.7a: not sent [live check L1: the empty fragment is applied (trace 0x2E819C0) and changes
  nothing that shows; the screen opens with no such state]. From stage 3.7b the backend sends it for the cooldowns (the rule above).
- Decision D1 of the orchestrator (option B): each station child gets a valid runtime variant. The master slices of five families have an
  authored key ending in 2 with no row of the station database: Smelting2, Carpentry2, Masonry2, Tanning2, Weaving2. The interaction tool
  gives such a child the slice of the `_t2` variant of its family, whose key ends in 3 (Smelting3, Carpentry3, Masonry3, Tanning3,
  Weaving3: the smallest supported replacement) and keeps the authored template (`authoredTemplate`, `override`). The backend writes one log
  line with the overrides at its start. The supported build has 9 such children (Carpentry 3, Smelting 2, Tanning 2, Weaving 1, Masonry 1).
  Live: the carpentry station of Oxboro shows "WOODSHOP TIER 3" (before: "WOODSHOP TIER 5" with the check "Woodshop Tier 2"). An interaction
  data file from before stage 3.7a has no override: such a station refuses each craft (unknownStation).
- The camp [game data; research section 8]: the same request and the same rules; the camp keys Camp1 to Camp5 permit the recipes with a
  camp key (108 recipes, 6 of them in this stage); a camp has no fee and no storage. Live: the client hides the option OpenCraftingOption of
  a camp (open since stage 2.5b, live check L9: the Ownership state with the permission rule 1, all permission options, and an owner
  identification of the character in two orders changed nothing; the prompt says "Camping not unlocked yet."). The backend part has the test
  case CRAFT2.
- More than one player (MP-1 to MP-10): the batch and the progress are values of the entity of the character; the bag, the coin, and the
  progress go only to the session of the owner; a request names its character only through the session (a facet or an entity of a different
  character changes nothing); the stop of one character does not stop a different batch.
- The cost depends on the requests and the batches, not on the size of the world (MP-8): the recipe, the station row, and a category member
  are hash lookups; the station comes from the active use of the character; a plan looks only at the bag and the quick slots of the character;
  a batch is one timer of the world. Test case CRAFT-LOAD: 20,000 far station placements and 100 more characters give the same visits; 10,000
  validations and 200 crafts take less than 2 s each.
- Code review of stage U4, finding U4-R1 (test cases CRU1, CRU2) [the rule of the tool: an output with a gear score range and no override
  needs the craft gear score rule of stage 3.7c]: a recipe with no cause whose output has a gear score range (MaxGearScore above 0) and no
  GearScoreOverride in the item catalog is refused at the request (`unsupported`, before any cost), and the backend refuses a crafting file
  with such a recipe as a whole at its start (`CraftingData::match_catalog`: the file and the catalog do not agree). Before the correction
  the check of CR7A-R2 tested only the BaseGearScore of the recipe: a craft gave a plain item of gear score 100. The supported build has no
  such recipe (0 of 948).
- No data: with no `crafting-data.json` the backend starts, and each request is refused with no change (the stop form). The loader refuses a
  file as a whole when a part is not complete or not consistent (two recipes with one CRC-32, a CRC that is not the CRC of the id, an
  ingredient type that is not a type of the client, a station row with no type, a recipe with no cause and no output, no quantity, no station
  key, or a category with no item). It also refuses a recipe with no cause and a rule that the backend does not apply (code review of stage
  3.7a, finding CR7A-R2; test case CRV2): an unlock (`known` false, `requiredAchievement`, `hiddenByAchievement`, `unlockedAchievement`), a
  procedural result, a temporary recipe, perk items, a BaseGearScore, SkipGrantItems, or world tags; in a file of format 1 also a first-craft
  achievement, refining, craft-all, an event, or a cooldown (format 2: their data, the rule of the format above). The output of a recipe with
  no cause and a gear score range in the item catalog is the rule of U4-R1 above (the request and the start). The request checks do not test
  the other rules: the cause list of the file is the gate, and an empty list must agree with the rules of the recipe.
- The format of the file: format 2 (stage 3.7b) has the members `events`, `ranks` (the RollBonus of the ranks 0 to N of each trade skill;
  a rank table with no RollBonus column, AzothStaff and Riding: 0), `achievements` (the bit of each first-craft achievement of a recipe),
  `materials` (the items with a bonus value), and the tier lists of each recipe. The loader refuses a file of format 2 with a recipe with no
  cause and an event that the file does not have, with character experience, or of a progression with no rank table; a first-craft
  achievement with no bit; a cooldown with only a time or a count; refining with no rank table (test case CRAFTB-DATA). A file of format 1
  (stage 3.7a) is read with the rules of stage 3.7a (no refining, no event, no first craft, no cooldown), and the backend log says so.
- The data tool skips an ingredient entry with no type (the type Invalid: the client skips it; 88 entries of the supported build have an
  id and no type). An entry with a type and no id stops the tool, and the earlier file stays (code review of stage 3.7a, finding CR7A-R3;
  `check_crafting_tool.py`, case incomplete; test case CRV3) [decision of an agent: the supported build has no such entry]. Before the
  correction the tool skipped it, and the recipe took one material less.

## Commands

```sh
tools/gamedata/crafting.sh                    # crafting-data.json: the recipes, the station rows, the categories, the events, the resources
tools/gamedata/crafting.sh --recipe 1hSkinningKnifeT1   # print a recipe with its causes (no file)
tools/world crafting NAME                     # the station of the use, the batch, the counts
tools/world craft preview NAME 1hSkinningKnifeT1 3      # the plan of a recipe (the checks, the costs, the output, the fee): no change
tools/world craft stop NAME                   # the batch stops (the completed jobs stay)
tools/world craft preview NAME IngotT2 10 --seed 37   # stage 3.7b: the bonus chance, the experience, the cooldown, the extra units of each job
tools/world craft seed NAME 37                # stage 3.7b: the next state of the generator of the world (a seeded batch)
tools/world craft cooldown NAME IngotT51 9 20 # stage 3.7b: 9 of 10 used, 20 s to the end of the window (0: no window)
```

- The routes: `GET /v1/dev/crafting?character=ID`, `POST /v1/dev/crafting/preview?character=ID&recipe=ID[&quantity=N][&seed=N]`, `POST
  /v1/dev/crafting/stop?character=ID`, and from stage 3.7b `POST /v1/dev/crafting/cooldown?character=ID&recipe=ID&used=N&seconds=S` and
  `POST /v1/dev/crafting/seed?character=ID&seed=N` (the development routes, with `SOULWARDEN_HARNESS=1`). The admin interface has the
  commands `craft_preview` (`recipe`, `quantity`, `seed`), `stop_craft`, `set_craft_cooldown`, and `set_craft_seed`, and the state of a
  character has the member `crafting` (the same view, with its cooldowns). The tool window has no tab for them yet.
- The preview with no station use gives the verdict "the character uses no crafting station and no camp" and the plan (the costs, the output,
  the slots): a check of the materials away from a station.

## Traps

- The crafting screen of the client (positions in the window of 1256x660): the search field `250 99`, the first row of the list `230 250`
  (each next row is 28 pixels lower: the flint knife is the fourth row of the search "Flint", `230 334`), the button CRAFT `1015 550`, the
  "+" of the count `1128 426` (the count slider shows only when the materials permit more than one), the result menu "Continue to Craft"
  `300 264` (single craft) or `300 297` (batch summary), "Exit Station" `300 369`. Take a screenshot before you use a position.
- The screen counts the materials when it opens and after "Continue to Craft": a give while the recipe screen is open does not change the
  counts. Close the station (Escape) and open it again.
- A refused request shows "Crafting attempt failed." only after 10 s: the client waits for its own timer (the stop form ends its crafting
  state, not its screen).
- During the craft (the dimmed screen) Escape skips the animation; the player cannot leave the station. The server cancel (3746, `tools/world
  interaction cancel`) closes the screen at once.
- The ids CRA1 and CRA2 of the research are the ids of two cases of stage 3.4f-C (the areas): the cases of this stage are CRAFT1 to CRAFT-TOOL.
- The interaction data file must be made again with the tool of stage 3.7a for the station variants of D1 (the backend log line "decision
  D1 ... 9 override(s)").
- A crafting file of format 1 (the tool of stage 3.7a) gives no refining and no reward progress: the screen shows "Crafting attempt
  failed." for an iron ingot. The backend log says "the crafting data has the format 1". Make the file again with `tools/gamedata/crafting.sh`.
- The screen rounds the bonus chance to whole percents: Asmodeum at Smelting 200 shows 0% for the server chance 0.05% (5 roll bonus points
  of the Cinnabar). Compare with `tools/world craft preview`.
- A stop of a craft-all batch before its last job (a server stop, the end of the use) leaves the screen with no update of the cause
  ItemCreated: it shows "Crafting attempt failed." after its 10 s, and the completed jobs stay (their items are in the bag).
- With no entry of state 205 the cooldown line of a recipe with a cooldown counts down from the start of the session ("23h 55m per 10
  items" some minutes after the world entry): it is a default of the client, not a window of the server.
- The client reads the state 205 at once, also while the recipe screen is open (the cooldown command changes the line in less than 1 s).
  The materials of the screen still change only at its open.
- The positions of the stations of Oxboro (live run of stage 3.7b; `scratch/tool/bounds.py` of the evidence gives the bounds of each
  station): the smelter `8850.4 4258.7 138.8`, the woodshop `8839.4 4254.0 138.6`, the tannery `8897.0 4188.3 139.2`, the loom `8885.6
  4185.9 138.6` (its first bound faces a wall), the stonecutter `8820.4 4241.5 137.2`.
