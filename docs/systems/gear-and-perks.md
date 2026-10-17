# Gear and perks

Read this document before a change of what the equipment and the effects give a character: the
perks of the equipped items, the attribute bonus, the health, the armor, the absorption, the equip
load, the damage of a hit, and the perks of a hit (a rend, a heal, a burst, a shorter cooldown, a
damage over time, a chain, a gem conversion). The wire contracts are in the protocol notes (section
4.23, and 4.15 and 4.18 for the damage rules). The perks of an item instance are in
[Items and equipment](items-and-equipment.md).

## Start here

| Task | Start here |
| --- | --- |
| The perks of a character and their potency, the passive values | `apps/backend/src/world/gear.h` (`Gear::perks_of`, `Gear::values`, `Gear::refresh`), `world/entity.h` (`GearSummary`, `Attributes::bonus`), test case IZ4 |
| The damage of a hit with the gear: the part of the gear, the gem conversion, the DMG and ABS parts, a rend | `Gear::hit_damage`, `Combat::damage` (`HitModifiers`), `Combat::strike`, `Combat::creature_damage`, protocol notes 4.15 and 4.18, test cases IZ4, IZ5 |
| A perk of a hit: its conditions, its effects | [Passive rules](passive-rules.md) (`Passives::after_hit`, `conditions_pass`), `Gear::on_hit`, `Gear::perk_status`, `Gear::perk_damage`, `Combat::strike_amount`, test cases IZ5, PS1 |
| The attribute bonus on the wire | `server/rep/states/attribute_state.cpp` (field 1), `Progress::refresh_vitals` |
| The perk data of the game | `tools/gamedata/perks.sh` (`perks.py`), `gamedata/perk_data.h`, [Game data](../development/game-data.md) |
| The stat lines of the jewelry, the threshold abilities in the gear values, the heal parts of a perk row, the cooldown reduction line (stage B4) | `Gear::values`, `Gear::refresh`, `gamedata/item_catalog.h` (`TrinketStats`), `server/rep/states/stat_multiplier_state.cpp`, test cases BR2, BR3, BR10 |

## Rules

- The gear system (`world/gear.h`, `world::Gear`) computes what the equipment and the statuses
  give a character. It changes only the gear values of the entity (`Entity::gear`, the
  attribute bonus) through `Gear::refresh`, and the maximum health through the progress system.
  The inventory system calls the refresh after a change of the equipment, the combat system
  after a weapon change, the status system after a change of the statuses, and the world at
  the entry. The refresh gives the statuses of the affixes to the status system first
  (`Gear::gear_statuses`, stage 3.3b-S1), then it adds the values of the statuses with the
  limits of their categories (`Statuses::values`, [Status effects](status-effects.md)): each
  value counts one time. The gear summary has the speed parts of the statuses too.
- The perks of a character: the perks of the instance and the fixed perks of the definition of
  each item in the armor slots (0 to 4), the jewelry slots (5 to 7), the weapon slots (24 to 29),
  and the heart rune (45), after the swap of the game, with the potency of the gear score of the
  item. Each effective perk counts one time for an item (code review of stage 1.3b, finding F2:
  the instance has the swapped id of a fixed perk, and the definition the old id). From stage B2 a perk of the instance in a place
  replaces the fixed perk of the definition in that place (a charm that the player slotted: the Boltcaster has Lightning Harnessing II
  in place 4, where the definition has `Slottable_Common_DmgBasic`; research `bow-build.md` section 3.2 and the tooltip of the video; a
  decision of an agent; test case BC10). Before stage B2 the two counted: each light or heavy arrow of the build had +0.023 too much
  (live run of stage B2). A loot item has its fixed perks in its instance (the roll), thus it loses none. A perk with the condition OnActive or OnUnsheathed counts only for the weapon in the hand
  (and its off hand item). A perk that the item cannot have counts nothing.
- The passive values (the cells of the affixes, the ability rows with no event, and from stage 3.3b-S5 the fixed node rows
  of the tree of the weapon in the hand: `Passives::fixed`; the status row
  that an affix keeps is a status of the status system with the source "gear"), each one times
  the potency: MOD<attribute> (the bonus, a whole
  number), MaxHealthMod and MaxHealth (a part of the health of the base, the level, and the
  constitution), BaseDamage, DMG<type>, ABS<type>, PhysicalArmor, ElementalArmor,
  BaseDamageReduction, and the regeneration of a status with TickRate and HealthModifierPercent.
  The perk tool requires the ABS and DMG columns of each damage type (and Standard) in the
  affix table: the table of the game has no PhysicalArmor, ElementalArmor, and
  BaseDamageReduction column, so they are optional there (code review of stage 3.3b-S1, finding
  F12, test case RS12).
  An ability row with no event and the condition EquipLoadCategory counts only in the load classes of its cell (code review of
  stage 3.4f-B, finding CS-R6, test case RZ6: `GlobalPerk_Artifact_Set2_HeavyLegs`, `Fast,Normal`, gives its MaxHealth 0.2 at the
  light and the medium load, not at the heavy load). The gear system computes the equip load of the equipment first, then the
  fixed values (`load_allows` in `world/gear.cpp`).
  An ability row counts IsStackableMax times at most for one character (the largest potency
  first). The statuses on the character (a food, a dust, the status of the heart rune, the status
  of an affix) count through the status system.
- The default of the stage (not a decision of the user): the ordinary attribute perks of an
  item (Soldier, Sentry, ...) give 0. The cells MOD<attribute> of their affixes are 0 in the
  game data (the 411 level points of a character are the source of its attributes); the client
  swaps them to a different perk (Acutely Empowered, Wellness), which has its own value.
- The equip load: the load of each armor item (WeightOverride, in whole tenths: the client
  shows 231.57 as 22.9), the class by the percents of the row Player of the encumbrance table
  (light below 26, medium below 46, heavy to 100, overburdened above), the damage part of the
  class (+0.15, 0, -0.15), and the heal factor of the class (1.3, 1.0, 0.7). The client computes
  its own equip load, average gear score, and armor from the paperdoll items. From stage
  3.3b-S6 the heal factor less 1 is a part of the outgoing heal of the character (an ability
  heal of a light character: +0.3), and the heal factor multiplies the potions and the life
  steal of the character ([Combat and health](combat-and-health.md)).
- The heal values of the gear (stage 3.3b-S6; `GearSummary`): the HealScalingValueMultiplier,
  HealMod, ConsumableHealMod, NonConsumableHealMod, and DmgPctToHealthHealMod of the statuses
  of the gear and of the effects (`Statuses::values`), for the outgoing and the received heal
  parts. A node change refreshes the gear values at once, also the fixed stats of the nodes
  (code review of stage 3.3b-S5, finding F5, test case RV5).
- The damage of a hit of a character (`Combat::damage`): raw times (1 + the part of the gear:
  the passive BaseDamage, the BaseDamage of each perk of the hit whose conditions pass, the
  equip load); a gem of the weapon of the hit (an affix with DamagePercentage and DamageType)
  converts that part of raw to its type; each part times (1 + DMG<type>) and the defense of the
  creature for its type; the mitigation of the creature times (1 + the armor part of its
  effects). The damage number has two entries for a conversion. A row of the type of the gem
  has one part.
- The damage of a creature on a character (`Combat::creature_damage`): the armor times (1 +
  the armor part of the character for the category), then times (1 - ABS<type>) and (1 -
  BaseDamageReduction). From stage B4 the BaseDamageReduction has the part of the rules of a hit taken with the attack type of the
  creature attack too (Enchanted Ward: `Passives::taken_reduction`, [Passive rules](passive-rules.md)).
- The perks of a hit. From stage 3.3b-S5 the evaluator of the passive rules ([Passive rules](passive-rules.md),
  `world/passives.h`) evaluates each ability row of a perk and each learned node of the tree of the weapon of the hit: one
  list of conditions, one comparator (`world/data_rules.h`), and one list of effects for the two. `Combat::strike` takes the
  BaseDamage part of the rules of the hit from `Passives::before_hit` (`Gear::hit_damage` gives the same sum), and
  `Gear::on_hit` gives each accepted hit to `Passives::after_hit`. The gear system keeps the effects that only the gear has: a rend of the armor of the creature, a
  damage over time of HealthModifierDamageBased of the raw damage of the hit with its ticks, a chain (CastSpell: the next
  creatures near the target, MaxChainNum, 8 at most, with sight) (`Gear::perk_status`), the burst of a perk
  (`Gear::perk_burst_of`), and the status on the caster (`Gear::caster_effect`, with the time of
  SelfApplyStatusEffectDurations: code review of stage 3.3b-S1, finding F10). A blank comparison kind is LessThanOrEqual
  (the AbilityData parser of the client, image RVA 0x43B6390; research abilities-shared.md K15; before stage 3.3b-S1 the
  backend used Equal). A row with a column that the evaluator does not have has no effect, and `tools/world passives NAME`
  names it with the column (a rule of this project).
- A tick and a chain hit go through `Combat::strike_amount`: the defense of the creature for
  the type, the creature system, the damage number. No perk of the attacker counts for them.
  A caster that left the world or died does no damage. A tick or a chain hit with a change of
  the health gives its rune charge (SourceRuneChargeOnApply, OnTick).
- The build perks (the user build, `~/Games/soulwarden/work/builds/medium-bruiser-mikars.md`):

  | Perk | Effect in the backend |
  | --- | --- |
  | Sundering Shockwave | A hit of the rows of Shockwave gives `Status_Perk_Hammer_Shockwave_Rend` (7 s, armor -0.05 times the potency); the icon shows at the name plate. |
  | Insatiable Gravity Well | A hit of the burst of Gravity Well heals 15 percent times the potency of its damage, one time in 1 s, and casts the burst of the perk (`InsatiableGravityWell`, 4 m, row `GreatAxe_GravityWell_Burst_Perk`) around the character in the next tick: the client text of the perk; the data has no link from the perk to the spell. |
  | Refreshing Move | A light or heavy hit takes 0.8 percent times the potency of the cooldown of each ability of the bar of the weapon in the hand (0.4 s activation cooldown). |
  | Abyssal Attunement, Abyssal Gambit, Abyssal Cruel | The void damage over time of the hit; the BaseDamage parts need stamina below 100 (from stage 3.4f-B the stamina of the server) or a crowd control effect on the creature. |
  | Shirking Fortification, Stamina Recovery, Gambled Energy, the stamina runeglass (stage 3.4f-B) | A creature hit in the immune window of a dodge gives the fortify of the armor (`Status_Perk_Armor_DodgeSuccess_Fortify`); a hit taken below 50 percent health and 100 percent stamina gives 20 stamina (60 s activation cooldown); a hit with less than 100 percent stamina gets the BaseDamage part; a hit with damage gives 1 or 4 stamina, one time in 3 s ([Stamina and dodge](stamina-and-dodge.md), [Passive rules](passive-rules.md)). |
  | Chain Void | A light or heavy hit casts the chain (3 creatures, 15 percent of the raw damage, 0.3 s apart). |
  | The runeglass and ward gems | ABS and DMG parts (passive). |
  | Health, Wellness, Acutely Empowered, Slash Protection, Slash Damage, Regenerating, Refreshing Toast | Passive values; Refreshing Toast shortens the cooldown of a consumable. |
  | Thwarting Strikes, Resilient, Freedom, Purifying Toast, the PvP ring perk | No effect: the backend has no grit, no crowd control on a player, no debuff on a player, and no player target (the critical hits came in stage 3.3b-S5: [Combat and health](combat-and-health.md)). |

- The cost of the rules: a hit looks at the perks of one character (55 at most), the chain at
  the position index near its target; the effects of a creature are in the creature system
  (test case IZ11 has a time limit).
- The ApplicationCooldown of a status of a perk on a creature (`Gear::perk_status`: a rend, a damage over time, a chain): from stage B2
  one application for each creature in that time, of each source (`Creatures::application_ready`; before: for each attacker). Research
  `bow-build-2.md` Q4: the cooldown of a status is on its target.
- The conversion and the damage types of a hit (stage B2, `HitContext::types`): the type of the row when a part keeps it, and the type of
  the conversion; the condition DamageTypes of a rule reads them ([Passive rules](passive-rules.md)).
- The rules of the Boltcaster of the bow build (stage B2: Electrified, Bolt and its bolt, Shirking Shock II, and Surprise Attack of the
  tree): [Passive rules](passive-rules.md).
- The haste perks of the bow build (stage B3): Speedy of the Winged Leather Shoes (`PerkID_Artifact_Set1_MediumFeet`: +0.5 in the time
  factor of each haste on the character, +0.1 BaseDamage while hasted) and Alacrity II of the charm of the Boltcaster
  (`PerkID_Slottable_Common_DurHaste`: +0.094 x potency in the time factor; 1.274 at 800). The status system applies the time factor
  ([Status effects](status-effects.md)); the evaluator has the rules ([Passive rules](passive-rules.md)). A haste of the build lasts 1.62 times
  its time. The statuses of a perk of a hit (`Gear::perk_status`) obey the removal window of the target (code review of stage B2, finding
  B2-R4; [Creatures](creatures.md)).
- The stat lines of the jewelry (stage B4; item catalog format 4, `trinkets`, [Game data](../development/game-data.md)): an item of the
  slots 0 to 7 with a row of the trinket table gives `gear score x Scale...PerGS x TrinketBonus x 0.01` for each line: ScaleHealthPerGS
  (max health), ScaleDamagePerGS (base damage), ScaleCDRPerGS (cooldown reduction) [client code 0x3636A50, the consumer of the
  tooltip lines; live check L1: "+4.53% Max Health" of Gorgon's Amulet at 725 = 725 x 0.625 x 0.01 x 0.01, "+5.00% Cooldown Reduction" of
  the earring at 800]. The max health and the base damage join the parts of the perks: one MaxHealth multiplier and one BaseDamage part
  (the client has one stat multiplier of each kind, enum 0x3FF0810) [agent decision; the sum agrees with the live sheet: 5,330]. The
  cooldown reduction joins the fixed cooldown rules in one sum (`Passives::cooldown_factor`; live check L1: the tooltip 19.7 s), and the
  stat multiplier state of the owner has it as the key CooldownReduction (0x4F, the value 1 + the line; protocol notes 4.49).
- The maximum health of the bow build (stage B4, live check L1 and the sheet of run L2): (856 base + 3,450 of level 70 + 560 of
  constitution 19 = 4,866) x (1 + 0.05 Wellness + 0.0453 the amulet line) = 5,330. The video shows 10,676: the game data and the client
  give no other source (no threshold below constitution 100, no other MaxHealth row of the build). The client of the video has
  different data (its tooltip of Enchanted Ward shows 3.5 percent; this client shows 1.8 percent). The difference stays open: no
  value of this client gives it [agent decision: the backend follows the data of this client].
- The threshold abilities of the attributes (stage B4): a fixed row of a threshold (`Con_Bonus_100_1`: MaxHealth 0.03, `Dex_Bonus_25_1`,
  `Str_Bonus_25_1`) is a passive value of the gear values with no mana part (the progress system has the MaxMana and ManaRate of the
  thresholds since stage 3.5b.2). A change of the attribute bonus (a food, a perk) that adds or takes a threshold makes the values again
  in the same refresh. The other threshold rows are rules of the evaluator ([Passive rules](passive-rules.md)).
- The received heal parts of a fixed perk row (stage B4): HealMod, ConsumableHealMod, NonConsumableHealMod, and DmgPctToHealthHealMod of
  an ability row with no event, x potency (Alchemist Reprieve `Global_Amulet_PotionHeal`: 0.02 x 4.0; The Thirst
  `GlobalPerk_Artifact_Set1_Earring1`: 0.33). They join the parts of the statuses in the gear summary (one pool,
  [Combat and health](combat-and-health.md)). Before stage B4 only the statuses gave them.
- The statuses of a perk of a hit (`Gear::perk_status`: a rend, a damage over time) last the time of the row x the outgoing time factor of
  the attacker (stage B4: Battle Precision, `Dex_Bonus_150_1`), then DurationMax; a damage over time has the ticks of that time.
- The damage of a tick of a creature status (stage B1, `Gear::tick_damage`): the creature system gives each damage tick of a status
  with a forced damage row (the burn of Flame Arrow) to the path of the damage of a perk (`perk_damage`, `Combat::strike_amount`: a
  fixed amount, the defense of the creature, no reaction, the number for the source) [agent decision: one path for each damage of a
  fixed amount].

## Commands

```sh
tools/world gear NAME                 # the perks with an effect (slot, item, perk, the perk before the swap, potency), the parts, the armor, the equip load, the effects, the counts
tools/world give NAME 2hGreatAxeT5 1 --gs 600 --perks PerkID_Stat_TwoHandSoldier,PerkID_Weapon_CDRBasic,PerkID_Gem_VoidDMG4
tools/world perks NAME 24 PerkID_Weapon_ChainVoid,,PerkID_Gem_VoidDMG4 --paperdoll   # the perks of an equipped item
tools/gamedata/perks.sh               # write perk-data.json to the game data folder
tools/gamedata/perks.sh --find Gravity   # the perks, buckets, and items with a text
```

- The route of the gear: `GET /v1/dev/gear?character=ID`.
- The log has one line for each hit with its converted part, and the lines `world: heal:`,
  `world: effect:` (a rend), `world: ability: the burst of a perk`.

## Traps

- The client does its own perk swap: a give of the Soldier perk on a weapon shows "Acutely
  Empowered". The backend stores the perk after the swap (`tools/world items` prints it). Look
  for the swapped perk in a test.
- The client computes the armor of the character sheet from its items and the statuses of the
  status state (stage 3.3b-S1: a dust shows its armor after a new open of the inventory), and
  the attribute values from the attribute state (the bonus map): the status state alone does
  not change an attribute of the sheet.
- A perk of the weapon that is not in the hand does nothing: select the weapon (key 1 or 2)
  before a test of an OnActive perk.
- The potency of a perk scales with the gear score of its item: a test item with no `--gs`
  has the lowest gear score of its range.
- The weapon damage of the character sheet is a number of the client, not of the server (stage B4, client GetCoreDamage 0x14368D200 with
  the item callback 0x1436A57A0). With a gem whose affix has UseWeaponAttributeScaling (+0x13d of the affix; loader 0x1443B3A40) the sheet
  shows core x (1 + p x (1 - p)), p the DamagePercentage of the gem: the rapier of the bow build (p 0.5, core 1,459.6) shows 1,824, the
  Boltcaster (p 0.99, core 2,480.9) 2,505. Without the flag the sheet shows (1 - p) x core + p x the core with no attribute: 1,012 for the
  rapier, the number of the video. The data of this client has the flag; the client of the video does not. The server damage of a hit
  does not use this number.
