# Consumables and the heart rune

Read this document before a change of the use of a potion, a food, or a dust, of the effects on a
character, or of the heart rune (its charge, its key, and its explosion). The wire contracts are in
the protocol notes (section 4.23). The status effects of a character (stage 3.3b-S1: the one owner, the HUD icons, the
values) are in [Status effects](status-effects.md).

## Start here

| Task | Start here |
| --- | --- |
| The use of a consumable: the quick slot keys, the message of the bag, the checks, the cooldown, the effects | `apps/backend/src/world/consumables.h`, `server/rep/client_input.h` (`InputStep::quickKey`), `server/rep/rep_session.cpp` (`handle_use_item`), `Inventory::take_one`, `world/statuses.h`, test cases IZ7, RI5 to RI8 |
| The countdown of a consumable on the HUD | `server/rep/states/cooldown_state.cpp` (field 3), `Entity::itemCooldowns` |
| The heart rune: the charge, the key, the variant status, the explosion | `world/runes.h` (`Runes::status_spell`), `server/rep/states/charge_state.cpp`, `server/rep/client_input.h` (`InputStep::runeKey`), test cases IZ6, RI4 |
| The heal and the mana of an effect | `Health::heal` (stage 3.3b-S6: the one heal path), `Abilities::change_mana` |

## Rules

- The consumable system (`world/consumables.h`) owns the use of a consumable and the item
  cooldowns. The statuses of a consumable go to the status system (`world/statuses.h`, stage
  3.3b-S1), with the source "consumable" and the item id. The cooldowns are values of the
  character with the character id as their key: a new session and a new entity of the character
  keep them. They are not in the database: a restart of the backend ends them.
- A use comes from a quick slot key (the bits 404, 117, 140, 513 for the paperdoll slots 14 to
  17; no message) or from the message 2917 of the bag (the entry "Use" of the action menu, the
  bag slot). The world finds the character from the session; message 2917 that names a
  different entity changes nothing (code review of stage 1.3b, finding F6). The item of the slot
  must be a consumable of the data; an item of the class SiegeWarOnly is refused; the level of
  the character must not be below the RequiredLevel of the item (finding F5: the client sends no
  key for it, and the server refuses a request); a row with DisableUseInCombat is refused in a
  fight; the cooldown of the item must have ended. The inventory takes one item in one
  transaction. A use that a check refuses changes nothing.
- The effects (AddStatusEffects, with DurationOverrides) go to the status system with its rules
  ([Status effects](status-effects.md)): a status with no duration gives one heal (Health +
  HealthModifierPercent of the maximum, times the heal factor of the equip load, times the
  received part of the kind consumable: HealMod and ConsumableHealMod; stage 3.3b-S6) with
  the green number of the owner, and one mana
  change (Mana; the one mana path of the abilities); a status with a duration stays for its
  time: its values count one time in the gear summary (MODConstitution of a food, the armor of a
  dust), a TickRate gives its heal and mana at each tick (a number only for a row with
  ShowUiDamageNumbersOnHeal: the regen potions; a food tick has no number), and StopOnHitCount
  ends it after hits.
  RemoveStatusEffects and RemoveStatusEffectCategories end the older statuses first (a new food
  ends the old one). A death ends the statuses with RemoveOnDeath. The client shows the icon
  and the countdown of each status with ShowInUITray (live run s1-live2: a food, a potion, a
  dust).
- The cooldown: CooldownDuration for the item, times 1 + the CooldownTimer of each perk with
  OnUsedConsumable (Refreshing Toast). The other items of the same CooldownId get 80 percent of
  it when the row has a SharedCooldownLocString (the client text: "Using this will put all other
  survival potions on 80% cooldown"; a project value, the data has no cell for it), else all of
  it. Each item keeps its own end, and a shared end does not make an end earlier (finding F7:
  the end of an item is the later of its own end and the shared end). The cooldown state of the
  owner (field 3) has an entry for each item of the cooldown id in the quick slots and in the
  bag: the key is the CRC-32 of the lower-case item id. The client shows the countdown on each
  one. A change of the bag or of the quick slots gives the map again: an item that comes during
  a cooldown of its id gets the countdown (finding F8).
- The heart rune system (`world/runes.h`) owns the charge of a character (a value of the
  character, 0 to 60) and the activation. A character with a heart rune in slot 45 gets charge:
  AttackRuneCharge of each accepted hit, HitRuneCharge of each hit of a creature, and
  SourceRuneChargeOnApply and OnTick of a status of a perk with a change of the health; one
  gain is 6 at most. The owner gets the charge state (slot 48, the percent); only the owner.
- The key G (bit 450): with the ability Ability_Rune_Detonate (the EquipAbility of the weapon row
  of the rune) and a full charge, the charge goes to 0, and the character gets one status of the
  action Rune_Detonate: the first variant whose ability row a perk of the character has, in the
  sequence Dominator, Greater, Major, or the base status (a rule of this project: one status,
  thus one explosion). The status is a status of the status system with the source "rune"
  (stage 3.3b-S1): its values count in the gear summary (Dominator: armor -50 percent, haste
  +0.2), the HUD shows its icon and its countdown, and the client plays its visual effect. The
  chain of the data makes the explosion: at the end of the status (3 s) its OnEndStatusEffect
  Rune_Detonate_CastAoE comes, and its CastSpell Rune_Detonate_AoE is the explosion
  (`Runes::status_spell`): each creature in Radius 5 and Height 5 of the spell around the
  character that the character sees, 32 at most, the row Rune_Detonate_AoE with the weapon of
  the rune (slot 45), through `Combat::strike` (the perk of Escalating Explosion adds 20 percent
  there). A death removes the status (RemoveOnDeath) and the leave of the world removes it (a
  rule of this project); the rows have DontApplyOnEndEffectOnRemove: no explosion, also when the
  character lives again or comes back before the end (code review of stage 1.3b, finding F4). A
  key in the status or with a charge that is not full changes nothing. The client empties its
  charge icon itself and shows no explosion: the damage numbers show. The haste of the status
  goes to the client in the speed table (stage 3.3b-S2a): the run is 1.2 times faster for the 3 s
  of the status (live walk of S2a-I10 with the key G; [Status effects](status-effects.md)).
- The activation checks the chain of the explosion before the payment
  (`Runes::chain_complete`): the spell Rune_Detonate_AoE, its damage row, the weapon of the
  rune, and an end status of the variant whose CastSpell is Rune_Detonate_AoE. With a part
  that the data does not have the key changes nothing, and the charge stays (code review of
  stage 3.3b-S1, finding F8, test case RS8).
- The base damage of the rune on its tooltip (2,325 at the gear score 625 and level 27) is the
  damage of the weapon of the rune with the level of the character: the damage rule of the
  backend gives the same value.

## Commands

```sh
tools/world use NAME 3                  # use the consumable of bag slot 3
tools/world use NAME 14 --paperdoll     # use the consumable of quick slot 14 (key 3)
tools/world rune NAME charge 100        # set the heart rune charge (percent)
tools/world rune NAME activate          # the key of the heart rune
tools/world gear NAME                   # the statuses, the item cooldowns, the rune charge
tools/world status NAME                 # the statuses of the character with their source (stage 3.3b-S1)
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
- A food of level 65 (FoodCONT5) is refused for a character below level 65 (finding F5). With the
  expansion of stage 1.1b the level 65 is a level of a player: `tools/world level NAME 65`, then the
  quick slot key (live proof of stage 1.1b). FoodCONT4 (level 35) is a food for a test at a lower
  level: `tools/world give NAME FoodCONT4 1`, then `tools/world use NAME SLOT`.
- The action menu of an item of the bag did not open with `tools/hx 'rclick'` in the live check
  I12; the use of the bag is proved at the message (I7) and in test case IZ7.
- A rune explosion through a wall: CheckObstructions; a creature behind a wall takes nothing.
- The creatures ignore a character with `tools/world ignore NAME on`, and a kill then gives no
  reward: turn it off for a test of the loot of an explosion.
