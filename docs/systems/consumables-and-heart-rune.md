# Consumables and the heart rune

Read this document before a change of the use of a potion, a food, or a dust, of the effects on a
character, or of the heart rune (its charge, its key, and its explosion). The wire contracts are in
the protocol notes (section 4.23). The values that an effect gives a character are in
[Gear and perks](gear-and-perks.md).

## Start here

| Task | Start here |
| --- | --- |
| The use of a consumable: the quick slot keys, the message of the bag, the checks, the cooldown, the effects | `apps/backend/src/world/consumables.h`, `server/rep/client_input.h` (`InputStep::quickKey`), `server/rep/rep_session.cpp` (`handle_use_item`), `Inventory::take_one`, test case IZ7 |
| The countdown of a consumable on the HUD | `server/rep/states/cooldown_state.cpp` (field 3), `Entity::itemCooldowns` |
| The heart rune: the charge, the key, the variant status, the explosion | `world/runes.h`, `server/rep/states/charge_state.cpp`, `server/rep/client_input.h` (`InputStep::runeKey`), test case IZ6 |
| The heal and the mana of an effect | `Health::heal_amount`, `Abilities::change_mana` |

## Rules

- The consumable system (`world/consumables.h`) owns the use of a consumable and the effects on
  a character. The effects and the cooldowns are values of the character with the character id
  as their key: a new session and a new entity of the character keep them. They are not in the
  database: a restart of the backend ends them. The tick looks only at the characters with an
  effect.
- A use comes from a quick slot key (the bits 404, 117, 140, 513 for the paperdoll slots 14 to
  17; no message) or from the message 2917 of the bag (the entry "Use" of the action menu, the
  bag slot). The world finds the character from the session. The item of the slot must be a
  consumable of the data; an item of the class SiegeWarOnly is refused; a row with
  DisableUseInCombat is refused in a fight; the cooldown of the item must have ended. The
  inventory takes one item in one transaction. A use that a check refuses changes nothing.
- The effects (AddStatusEffects, with DurationOverrides): an effect with no duration gives one
  heal (Health + HealthModifierPercent of the maximum, times the heal factor of the equip load)
  and one mana change (Mana; the one mana path of the abilities). An effect with a duration stays
  for its time: its values count in the gear system (MODConstitution of a food, the armor of a
  dust), a TickRate gives its heal and mana at each tick, and StopOnHitCount ends it after hits.
  RemoveStatusEffects and RemoveStatusEffectCategories end the older effects first (a new food
  ends the old one). The same effect again has the new times. A death ends the effects with
  RemoveOnDeath.
- The cooldown: CooldownDuration for the item, times 1 + the CooldownTimer of each perk with
  OnUsedConsumable (Refreshing Toast). The other items of the same CooldownId get 80 percent of
  it when the row has a SharedCooldownLocString (the client text: "Using this will put all other
  survival potions on 80% cooldown"; a project value, the data has no cell for it), else all of
  it. The cooldown state of the owner (field 3) has an entry for each item of the cooldown id in
  the quick slots and in the bag: the key is the CRC-32 of the lower-case item id. The client
  shows the countdown on each one.
- The heart rune system (`world/runes.h`) owns the charge of a character (a value of the
  character, 0 to 60) and the activation. A character with a heart rune in slot 45 gets charge:
  AttackRuneCharge of each accepted hit, HitRuneCharge of each hit of a creature, and
  SourceRuneChargeOnApply and OnTick of a status of a perk with a change of the health; one
  gain is 6 at most. The owner gets the charge state (slot 48, the percent); only the owner.
- The key G (bit 450): with the ability Ability_Rune_Detonate (the EquipAbility of the weapon row
  of the rune) and a full charge, the charge goes to 0, and the character gets one status of the
  action Rune_Detonate: the first variant whose ability row a perk of the character has, in the
  sequence Dominator, Greater, Major, or the base status (a rule of this project: one status,
  thus one explosion). The status counts in the gear system (Dominator: armor -50 percent). At
  its end (3 s) the explosion: each creature in Radius 5 and Height 5 of the spell
  Rune_Detonate_AoE around the character that the character sees, 32 at most, the row
  Rune_Detonate_AoE with the weapon of the rune, through `Combat::strike` (the perk of
  Escalating Explosion adds 20 percent there). A character that died or left the world before
  the end makes no explosion. A key in the status or with a charge that is not full changes
  nothing. The client empties its icon itself and shows no explosion: the damage numbers show.
- The base damage of the rune on its tooltip (2,325 at the gear score 625 and level 27) is the
  damage of the weapon of the rune with the level of the character: the damage rule of the
  backend gives the same value.

## Commands

```sh
tools/world use NAME 3                  # use the consumable of bag slot 3
tools/world use NAME 14 --paperdoll     # use the consumable of quick slot 14 (key 3)
tools/world rune NAME charge 100        # set the heart rune charge (percent)
tools/world rune NAME activate          # the key of the heart rune
tools/world gear NAME                   # the effects, the item cooldowns, the rune charge
tools/hx 'key 0x33'                     # the first quick slot key (3) in a live client
tools/hx 'key 0x47'                     # the key of the heart rune (G)
```

- The routes: `POST /v1/dev/items/use`, `POST /v1/dev/rune/charge`, `POST /v1/dev/rune/activate`,
  `GET /v1/dev/gear`. The admin interface has `use_item`, `set_rune_charge`, `activate_rune`.
- The log lines: `world: consumable:`, `world: heal:`, `world: mana:`, `rep: quick slot key N`,
  `rep: use item request`, `world: heart rune:`, `rep: heart rune key`.

## Traps

- The client plays the drink of a quick slot key itself, also in a cooldown of the server: a
  drink with no heal is a use that the server refused (the log line has the cause).
- The client uses no item with a level requirement above the level of the character: it sends
  no bit (a level 27 character and a potion of level 55). FoodCONT5 needs the level 65.
- The backend sends no status effect of a player: the client shows no icon of a food, a
  potion, or the status of the heart rune, and its character sheet shows no attribute of a
  food (the backend counts it: `tools/world gear`).
- The action menu of an item of the bag did not open with `tools/hx 'rclick'` in the live check
  I12; the use of the bag is proved at the message (I7) and in test case IZ7.
- A rune explosion through a wall: CheckObstructions; a creature behind a wall takes nothing.
- The creatures ignore a character with `tools/world ignore NAME on`, and a kill then gives no
  reward: turn it off for a test of the loot of an explosion.
