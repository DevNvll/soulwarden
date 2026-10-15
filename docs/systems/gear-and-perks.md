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
  the instance has the swapped id of a fixed perk, and the definition the old id). A perk with the condition OnActive or OnUnsheathed counts only for the weapon in the hand
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
  its own equip load, average gear score, and armor from the paperdoll items.
- The damage of a hit of a character (`Combat::damage`): raw times (1 + the part of the gear:
  the passive BaseDamage, the BaseDamage of each perk of the hit whose conditions pass, the
  equip load); a gem of the weapon of the hit (an affix with DamagePercentage and DamageType)
  converts that part of raw to its type; each part times (1 + DMG<type>) and the defense of the
  creature for its type; the mitigation of the creature times (1 + the armor part of its
  effects). The damage number has two entries for a conversion. A row of the type of the gem
  has one part.
- The damage of a creature on a character (`Combat::creature_damage`): the armor times (1 +
  the armor part of the character for the category), then times (1 - ABS<type>) and (1 -
  BaseDamageReduction).
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
  | Abyssal Attunement, Abyssal Gambit, Abyssal Cruel | The void damage over time of the hit; the BaseDamage parts need stamina below 100 or a crowd control effect on the creature. |
  | Chain Void | A light or heavy hit casts the chain (3 creatures, 15 percent of the raw damage, 0.3 s apart). |
  | The runeglass and ward gems | ABS and DMG parts (passive). |
  | Health, Wellness, Acutely Empowered, Slash Protection, Slash Damage, Regenerating, Refreshing Toast | Passive values; Refreshing Toast shortens the cooldown of a consumable. |
  | Thwarting Strikes, Resilient, Shirking Fortification, Freedom, Stamina Recovery, Gambled Energy, Purifying Toast, the PvP ring perk | No effect: the backend has no grit, no dodge, no crowd control on a player, no stamina, no debuff on a player, and no player target (the critical hits came in stage 3.3b-S5: [Combat and health](combat-and-health.md)). |

- The cost of the rules: a hit looks at the perks of one character (55 at most), the chain at
  the position index near its target; the effects of a creature are in the creature system
  (test case IZ11 has a time limit).

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
