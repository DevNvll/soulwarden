# Status effects of a player

Read this document before a change of the status effects of a character: a food, a potion, a dust, the status of a
heart rune, the status of a perk of an equipped item, a caster effect of an ability, the values of a status, the status
state of a player (the HUD icons, the character sheet, the name plate of a remote player), and before a live test of a
status. The wire contract is in the protocol notes (section 4.24). The status effects of a creature are in
[Creatures](creatures.md) and [Abilities](abilities.md).

## Start here

| Task | Start here |
| --- | --- |
| The one owner of the statuses of a character: apply, end, ticks, values | `apps/backend/src/world/statuses.h` (`world::Statuses`), `world/entity.h` (`ActiveStatus`, `StatusSource`), test cases ST2 to ST5 |
| The status state of a player (slot 44, type 4236) and the wall clock pair of the Player state | Protocol notes 4.24, `server/rep/states/status_state.cpp` (`encode_player_status_state`), `states/player_state.cpp`, test cases ST6, ST7 |
| The values of a status in the gear summary, the limits of the categories | `Statuses::values`, `Gear::refresh` ([Gear and perks](gear-and-perks.md)), test case ST4 |
| A caster effect of a learned node or of a perk (SelfApplyStatusEffect) | `Gear::on_hit`, `Gear::caster_effect`, test case ST8 |
| The speed of a status: the speed table of the player (slot 43, type 1525), the position check | Protocol notes 4.24, `server/rep/states/stat_multiplier_state.cpp`, `Gear::refresh` (`Entity::moveMultiplier`, `sprintMultiplier`), `PositionTrack::set_client_speed`, `client_speed`, test cases SV1 to SV3, RS1, ST9 |
| The corrections of the code review of stage 3.3b-S1 (F1 to F13) | `apps/backend/tests/review_status_tests.cpp`, test cases RS1 to RS13 |
| The status categories and the caster effect rows of the game data | `tools/gamedata/abilities.sh` (format 3), `gamedata/ability_data.h` (`StatusCategory`, `AbilityRow`), test case ST1 |

## Rules

- The status system (`world/statuses.h`, `world::Statuses`) is the one owner of the status effects of a character. A
  status is a row of the status tables of the game data on the character. The statuses are values of the character
  with the character id as their key (roadmap, rule MP-1): a new session and a new entity of the character keep them,
  and their time runs while the character is not in the world. Only this system changes them. The entity has a copy
  (`Entity::statuses`) for the encoders and the rules of the other systems; the copy does not end on its own. The
  consumable system (a food, a potion, a dust), the rune system (the status of an activation), the gear system (the
  status that an affix of an equipped item keeps; the caster effects), and the development commands use it.
- One status of a row for each character: the maps of the client have one entry for each status number (client code
  0x4038E80). A new application of a row that the character has:
  - one more stack, to StackMax (a value below 1 is 1: client loader 0x44B65F5); a new status has InitialStackSize
    stacks (1 to 25);
  - the end: RefreshDuration: now plus the duration, but not before the old end (a shorter refresh keeps the later end:
    code review of stage 3.3b-S1, finding F6, test case RS6); a DurationMax above the duration: the old end plus the
    duration, not more than now plus DurationMax; else the later end of the two;
  - the larger potency stays; at StackMax with DontReplaceStack the application changes nothing.
  The meaning of RefreshDuration, DurationMax, and DontReplaceStack is a reading of the names of the columns of the
  game data (a decision of an agent): the add code of the server is not in the client (the static search found only
  the loader 0x44B4430).
- The duration: the duration of the source (DurationOverrides of a consumable), else BaseDuration; not more than
  DurationMax. A row with no duration is one application now: a heal (Health and HealthModifierPercent of the maximum
  health, times the potency and the heal factor of the equip load), a mana change (Mana, ManaModifierPercent), and its
  CastSpell. A status of the gear has no end.
- ApplicationCooldown: one application of a row for each character in that time. Only an application that changes the
  character starts it: an application that DontReplaceStack refuses does not (finding F7, test case RS7).
- A positive time of a status row (BaseDuration, TickRate, DurationMax, ApplicationCooldown) is 1 ms at least: the
  loader refuses a file with a positive time below 1 ms, and the world counts each positive time as 1 ms at least. Thus
  each due entry is later than the tick that makes it, and a tick of the world always returns (finding F9, test case
  RS9).
- Each OnEndStatusEffect and each CastSpell of a status row names a row of the data: the loader refuses a file with a
  link that does not resolve, but for the one link of the game with no row (`AI_IceDragon_EmpoweringLink_Stack_Apply`
  to `AI_IceDragon_EmpoweringLink_Stack`, `kMissingEndLinks`; finding F8, test case RS8).
- A TickRate with Health, HealthModifierPercent, Mana, or ManaModifierPercent: a heal and a mana change at each tick
  (times the potency and the stacks); the first tick comes at the next tick of the world after the application
  (DelayInitialTick: one TickRate later). The ticks of a character that is not in the world wait for its next entry.
- A TickRate with HealthModifierDamageBased (a heal of a part of the damage of the source: the Beacon of the life staff,
  stage 3.3b-S2b): a pulse at each tick with no heal (`StatusCounts::pulsesWithNoHeal`). The amount needs the heal base of
  stage 3.3b-S6 (open). A negative HealthModifierDamageBased (a damage over time) has no rule here.
- The source "spell" (stage 3.3b-S2b, [Spell entities](spells.md)): the area of a spell gives its statuses to a friend in it
  and takes them back when the friend leaves the area and at its end (`Statuses::remove_source`, the end cause "its source
  went away").
- StopOnHitCount: the status ends at that hit that the character takes. RemoveOnDeath: it ends at the death of the
  character (the health system calls `Statuses::died`). A dead character takes no status (but a row with
  AllowAddIfDead). The gear refresh also gives a dead character no status of the gear whose row a dead character does
  not take; the revival and the respawn give it again (finding F5, test case RS5).
- The end: at its end on the world clock the status goes away, and its OnEndStatusEffect comes on the character. A
  removal before the end (a death, a newer status of the source, the leave of a rune activation, a command) gives the
  end status too, but not for a row with DontApplyOnEndEffectOnRemove.
- CastSpell of a row (at its application): the spell Rune_Detonate_AoE is the explosion of the heart rune
  (`Runes::status_spell`). Thus the data makes the explosion: Rune_Detonate ends, its end status
  Rune_Detonate_CastAoE comes, and its CastSpell is the explosion. A different spell is not in this stage (a count in
  `tools/world status`).
- The values (`Statuses::values`): MOD<attribute>, MaxHealthMod, PhysicalArmor, ElementalArmor, BaseDamageReduction,
  DMG<type> and DMGStandard, ABS<type>, MoveSpeedMod, and SprintSpeedMod, each times the potency and the stacks. The
  sum of a value over the statuses of a category is not above the positive ValueLimits of the category and not below
  the negative one (StatusEffectCategoryData: Haste MoveSpeedMod 0.4, Slow -0.5, ArmorFortify PhysicalArmor 2.0,
  Fortify ABS 0.5, ...; client code 0x40242C0 sums the speed values by category). A status with two categories with a
  limit for one value counts in the first one (a decision of an agent). The gear system adds the values to the gear
  summary of the character (`Gear::refresh`): each value counts one time (not in the gear values and in the statuses).
- The gear: each status that an affix of an equipped item keeps (StatusEffect of AffixStatData: a ward amulet, the
  regeneration of an earring) is a status with the source "gear", with no end and the potency of the perk. It is not on
  the wire (the client has the items). The gear refresh gives the list again; a status of an item that the character
  took off ends (its source went away). The leave of the world ends them; the next entry gives them again. An
  application of a different source (a command, a consumable) on a status of the gear keeps the source "gear": the
  status ends with its item (finding F4, test case RS4). A status of a different source stays through a leave and comes
  in the first record of the next entry (finding F2, test case RS2).
- The caster effects (stage 3.3b-S1): a hit of a character (`Gear::on_hit`) evaluates the ability rows of the perks and
  of the learned nodes of the ability table of the weapon of the hit; a row with OnHit, a SelfApplyStatusEffect, and
  conditions that pass puts the status on the caster (source "ability"). The conditions are the conditions of a perk
  row ([Gear and perks](gear-and-perks.md)), and InSequence: the hit is a hit of the cast of an ability of the list (a
  rule of this project: InSequenceTime is not read). The build: Safety Measures (a hit of Wrecking Ball: the fortify
  WarHammer_WreckingBall_Fortify), Greed (a light hit of the great axe: three stacks of its empower), Hammer Time (a heavy
  hit of the war hammer). A row with OnHitTaken (Power Through Pain) or a condition that the backend does not evaluate
  (NumAroundMe: Crowded Protection) has no effect in this stage. The time of a caster effect is the
  SelfApplyStatusEffectDurations of its ability row (a number, or a list in the sequence of the statuses), else the
  duration of the status row (finding F10, test case RS10).
- The status state of a player (protocol notes 4.24). The owner gets the full map (group 1, field 0) with each status
  on the wire, an empty lightweight map, and an empty tray. An entry of the full map alone gives the HUD icon of a row
  with ShowInUITray, its countdown, its stack count (×2), its visual effect, and its values in the character sheet
  (live checks L1 and L2, live run s1-live2); a tray entry gives a second icon of the same status. An observer gets the
  public statuses (ShowInNameplates, AlwaysReplicate, ForceReplicateToRemotes) in the full map and the compact map of
  group 3; the compact map has the statuses with ShowInNameplates (the icon at the name plate). The end of an entry is a
  time of the synced wall clock of the client: the Player state of the owner has the wall clock pair from one sample
  (group 1, fields 6 and 7). A first record of a character with no status on the wire has no status fragment.
- The speed (stage 3.3b-S2a, criterion S2a-I10; protocol notes 4.24). The client does not apply the speed values of the
  status map (live checks L1 and s1-live2). It runs with the speed table of the player (slot 43, type 1525, owner only;
  `server/rep/states/stat_multiplier_state.cpp`). The gear refresh computes `Entity::moveMultiplier` and
  `sprintMultiplier`: 1 plus the MoveSpeedMod and the SprintSpeedMod part of the statuses, in the limits of
  StatMultiplierData (0 to 1.5). An ability file with no limits (a tool from before stage 3.3b-S2a) gets the limits of
  the supported client, 0 to 1.5 (`gear_rules::kClientSpeedMin`, `kClientSpeedMax`; code review of stage 3.3b-S2a, finding
  F2, test case RG2): the client bounds the value with them in each case. The encoder sends the keys 0x34 (MoveSpeedMod) and 0x35 (SprintSpeedMod) with the
  multipliers; both at 1 give the empty table, and a first record with both at 1 has no fragment. In the live walks the
  key 0x35 changes the run and the sprint (for 1.4: the run x1.43, the sprint x1.31), and the key 0x34 has no effect. The
  status rows of the game have their speed in SprintSpeedMod (the haste, the heart rune, the slow of the vortex).
- The position check takes the multiplier that the client has: the window speed (8 m/s) times the larger multiplier of
  the last speed table that the session sent (`PositionTrack::client_speed`), not less than 1; after a change the
  multiplier before it counts for 1 s more (the client gets the table some time after the send). Each older value keeps
  its own end: a later lower table does not move it (code review of stage 3.3b-S2a, finding F1, test case RG1). A haste
  that the client did not get does not permit a faster run (code review of stage 3.3b-S1, finding F1, test cases RS1,
  SV2).
- The statuses are not in the database: a restart of the backend ends them (a rule of this project from stage 1.3b,
  also for the cooldowns). The research lists the options for a later decision of the user (persist long statuses with
  wall ends, or not).
- The cost: the expiry and the ticks look only at the due list (one entry for each end and each tick that is due); an
  old entry (a renewed status) is taken and dropped. No rule looks at each character, creature, or spawn point (test
  case ST11 has a time limit).

## Commands

```sh
tools/world status NAME                              # the statuses (seconds, stacks, potency, source), the speed and armor parts, the counts
tools/world status NAME UncategorizedHaste_40pct 30  # a status row of the game data for 30 s (0: the duration of the row)
tools/world status NAME Status_Passive_Greataxe_Reaper_DmgOnLight 0 3   # three applications: three stacks
tools/world status NAME remove FoodCONT4             # end a status (all: each status but the gear)
tools/world gear NAME                                # the gear values with the status values in them
```

- The routes: `GET /v1/dev/status?character=ID`, `POST /v1/dev/status` (`status`, `seconds`, `potency`, `count`),
  `POST /v1/dev/status/remove` (`status`). The admin interface has `apply_status` and `remove_status`.
- The log lines: `world: status: NAME gets STATUS (SOURCE), N s, potency P`, `world: status: STATUS of NAME ends: CAUSE`.

## Traps

- The DurationMax of a row also limits a command: `tools/world status NAME Rune_Detonate_Major 60` gives 3 s (its
  DurationMax is 3). Use a row with a longer time for a test of a value (UncategorizedHaste_40pct: 30 s).
- The tray of the owner stays empty: a tray entry and a full map entry of one status show two icons (live run s1-live).
- A tray entry that comes before the wall clock pair keeps a wrong time ("20737d"): the HUD computes the time of an
  entry when it applies it. The Player state (slot 9) is in the first record before the status state (slot 44).
- A countdown at 0 does not remove an icon (the HUD shows "–"): the server sends the list again at the end.
- `tools/hx` runs its calls one after the other: a key press in a second call during a hold of a first call comes
  after the end of the hold. Put the key into the same call (`tools/hx 'keydown 0x57' 'wait 1000' 'key 0x47' 'wait 4000'
  'keyup 0x57'`).
- `tools/world status NAME STATUS` refuses a character that is not in the world. For a check of the first record,
  apply the status before the leave: it stays through the leave.
- The heart rune of the development character is Detonate with the status Rune_Detonate_Dominator (Haste+ArmorRend,
  SprintSpeedMod 0.2, 3 s), not Rune_Detonate_Major.
- A walk for a speed check needs a free street: a creature of an earlier test in the way stops the character (a walk of
  6.9 m in place of 11 m).
- In a live check with raw fragments (`tools/world fragment`), a full map entry of a heart rune status
  (Rune_Detonate_Major) did not go away after its omission (live check L2); in the real flow of the key G the icon goes
  away at its end (live run s1-live2). Do not test the rune with raw map entries.
- A food of level 65 (FoodCONT5) is refused for a character of level 60 (RequiredLevel, review finding F5); use
  FoodCONT4 (level 35) for a test of the development character.
