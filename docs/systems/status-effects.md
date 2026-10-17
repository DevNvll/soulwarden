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
| A caster effect of a learned node or of a perk (SelfApplyStatusEffect), a status while creatures are near | [Passive rules](passive-rules.md), `Passives::after_hit`, `Passives::tick`, `Gear::caster_effect`, test cases ST8, PS3 |
| The speed of a status: the speed table of the player (slot 43, type 1525), the position check | Protocol notes 4.24, `server/rep/states/stat_multiplier_state.cpp`, `Gear::refresh` (`Entity::moveMultiplier`, `sprintMultiplier`), `PositionTrack::set_client_speed`, `client_speed`, test cases SV1 to SV3, RS1, ST9 |
| The corrections of the code review of stage 3.3b-S1 (F1 to F13) | `apps/backend/tests/review_status_tests.cpp`, test cases RS1 to RS13 |
| The heal of a status: a fixed heal, a heal over time, a heal of an ability (HealthModifierDamageBased), the heal values (stage 3.3b-S6) | `Statuses::give` (`StatusApply::heal`, `ActiveStatus::heal`), `Statuses::potency_factor`, `Health::heal` ([Combat and health](combat-and-health.md)), test cases HE1, HE4, HE5, HE7 |
| The damage of a status: the poison of a creature area, its damage source, the ticks, the dodge (stage 3.4f-C) | `Statuses::give` (`StatusApply::damage`, `ActiveStatus::damage`, `DamageSource`), `Health::status_damage`, [Spell entities](spells.md), test cases CRA2, CRA5, CRA8 |
| The corrections AR-R1 and AR-R2 of the review of stage 3.4f-C: the dodge at the due time of a tick, the exit from an area before a tick | `Statuses::tick`, `Statuses::give` (`at`), `Health::status_damage` (`at`), `Spells::holds`, test cases RAR1, RAR2 |
| The statuses of a creature (stage 3.4f-D: its heals and buffs; stage B2: the stacks, the ApplicationCooldown on the target, the removal window) | [Creatures](creatures.md) (`Creatures::give_status`) |
| The end of a status at a new application (one rule for a character and a creature, stage B2) | `Statuses::renewed_end`, `CellValues::flag` (a mark as a number or the text TRUE), test cases ST2, BC1 |
| The status categories and the caster effect rows of the game data | `tools/gamedata/abilities.sh` (format 4), `gamedata/ability_data.h` (`StatusCategory`, `AbilityRow`), test case ST1 |
| The time factor of a status on the character (stage B3: Speedy, Alacrity II), OnStackStatusEffect (the timer of Archer's Speed), the end of a status when the weapon of its tag leaves the hand | `Statuses::apply` (`StatusApply::fixedTime`, `on_stack`), `Passives::duration_factor`, `Passives::weapon_changed`, test cases HS1 to HS3 |

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
  the loader 0x44B4430; research bow-build-2.md Q4: no client reader of RefreshDuration, bit 28, and of RemoveUnappliedStacks, bit 26).
  From stage B2 one function has the end rule (`Statuses::renewed_end`), for a character and for a creature, and a mark in the text
  form "TRUE" counts (`CellValues::flag`: RefreshDuration "TRUE" of `Status_ExplosiveShot_Burn`). At the end all stacks go together
  (RemoveUnappliedStacks: the backend has one end for each status; a decision of an agent).
- The duration: the duration of the source (DurationOverrides of a consumable), else BaseDuration, times the time factor of the
  character (stage B3), not more than DurationMax. The time factor is 1 plus the sum of the StatusEffectDurationReduction parts of the rules of
  the character for the status (client getter 0x68308E0 sums them; [Passive rules](passive-rules.md), `Passives::duration_factor`): Speedy
  (0.5) and Alacrity II (0.094 x 1.274) give a haste 1.62 times its time: Dodge and Weave 2 x 1.62 = 3.24 s, Archer's Speed 5 x 1.62 = 8.1 s
  (live checks B3-1, B3-3). The use of the sum (`time x (1 + sum)`) is an agent decision (research bow-build-2.md K1: the server code is not in
  the client; a product of the two factors would give 3.36 s and 8.4 s). A time of a development command with seconds stays
  (`StatusApply::fixedTime`); with 0 seconds the time of the row has the factor. A row with no duration is one application now: a heal (Health and HealthModifierPercent of the maximum
  health, times the potency, through the heal path of stage 3.3b-S6 with its kind), a mana change (Mana, ManaModifierPercent), a
  stamina change (Stamina, stage 3.4f-B), and its CastSpell. A status of the gear has no end.
- OnStackStatusEffect (stage B3): after each application, the stacks of the status against AddOnStackSize with AddOnStackSizeComparison (the
  comparator of the data; a row with no size: each application) give the named statuses to the character, with the status as their source
  [game data: 46 rows; the haste of Archer's Speed gives its timer `Status_Passive_Bow_Skirmish_HasteOnSwap_Timer` (10 s, the category Timer:
  no time factor of a haste) at its first stack]. A chain of such rows stops at the depth 4.
- The end when the weapon of a tag leaves the hand (stage B3): a tag removal action of the data ends its statuses at a change of the weapon in
  the hand ([Passive rules](passive-rules.md); the end cause "the weapon of its tag left the hand", `StatusEnd::weapon`).
- ApplicationCooldown: one application of a row for each character in that time. Only an application that changes the
  character starts it: an application that DontReplaceStack refuses does not (finding F7, test case RS7).
- A positive time of a status row (BaseDuration, TickRate, DurationMax, ApplicationCooldown) is 1 ms at least: the
  loader refuses a file with a positive time below 1 ms, and the world counts each positive time as 1 ms at least. Thus
  each due entry is later than the tick that makes it, and a tick of the world always returns (finding F9, test case
  RS9).
- Each OnEndStatusEffect and each CastSpell of a status row names a row of the data: the loader refuses a file with a
  link that does not resolve, but for the one link of the game with no row (`AI_IceDragon_EmpoweringLink_Stack_Apply`
  to `AI_IceDragon_EmpoweringLink_Stack`, `kMissingEndLinks`; finding F8, test case RS8).
- A TickRate with Health, HealthModifierPercent, Mana, ManaModifierPercent, or Stamina (stage 3.4f-B): a heal, a mana change, and a
  stamina change at each tick (times the potency and the stacks); the first tick comes at the next tick of the world after the
  application (DelayInitialTick: one TickRate later). The ticks of a character that is not in the world wait for its next entry. The
  stamina change goes through the stamina path ([Stamina and dodge](stamina-and-dodge.md)): Stamina Recovery (`Status_Perk_Amulet_StamOnLow`,
  0.1 s, TickRate 0.25: 20 one time), Mauler's Stamina (`Status_Passive_Greataxe_Mauler_StamVsLowHp`: 40 one time); test cases SM3, SM4.
  Before stage 3.4f-B a row with only Stamina did not tick (no gain).
- The damage of a status (stage 3.4f-C; the poison `AI_Poison_55` of the mushroom of a Dryad Shaman). A negative
  HealthModifierPercent and a negative HealthModifierDamageBased with no UseHealScalingValue are damage (client code 0x449D740:
  a negative value of the three health terms is damage, a positive value is a heal). One tick takes, as one loss of health
  (`Health::status_damage`): the part of the maximum health (`-HealthModifierPercent × maximum health`) plus the part of the
  core damage of the source (`-HealthModifierDamageBased × core damage`), times the potency and the stacks. Example of the
  live proof: `0.02 × 4,869 + 0.04 × 1,169.9 = 144.2` for a Dryad Shaman of level 30. Sources:
  - The core damage of the source: game data and client code. The producer 0x5C3C930 (`m_cachedCoreDamage`, computed one
    time) gives `(attribute part + 1) × base damage` for an attacker that is not a player; the area copies the damage of its
    caster into the source (0x3FE6500). The backend uses the damage of the kind and the level of the creature
    (`CreatureData::damage`) at the cast, in the `DamageSource` of the area. A status with no damage source (a command) gets
    no damage-based part.
  - The sum, the potency, the stacks, and no armor: a project rule (a decision of an agent). The tick code of the original
    server is not in the client (static study STATIC-AREAS.md, Q1.4); the client test 0x6892D40 adds the damage-based part and
    Health with no potency. A status with DamageType and no IsTrueDamage could go through the armor of the target; that is not
    proved, and the backend uses no armor for a tick.
  - The type of the damage number: the DamageType of the row (`Nature` for the poison) on the wire of the damage number of
    the owner.
- A tick in the immune window of a dodge does no damage, but for a row with IgnoreInvulnerable (a decision of an agent with
  the client code: the damage receiver 0x692E4A0 refuses an attack on an invulnerable target with no IgnoreInvulnerable;
  the status row has its own flag IgnoreInvulnerable, loader 0x44B4430). The status itself stays: the add test of the client
  (0x401FB90) has no test of the invulnerability (test case CRA8). The dodge decides a tick at its due time, not at the time of the
  world tick that processes it (code review of stage 3.4f-C, finding AR-R1, case RAR1: a tick processed 30 ms late came after the end
  of the immune window and did damage).
- A tick of a status of a status area of a creature: the source area looks at the character first (`Spells::holds`; finding AR-R2, case
  RAR2): a character that left the volume leaves the area now, its status goes (m_removeOnExit), and no tick comes. The world runs the
  statuses before the spells: before this rule a character that walked out took one more tick.
- The status of an area has the area as its source (`StatusSourceKind::spell`, the entity of the area). The exit of the
  character and the end of the area take it at once, also with time left ([Spell entities](spells.md)). A second area that
  gives the same row gives it again at its next application. The death of the character ends it (RemoveOnDeath).
- A heal of an ability (stage 3.3b-S6): a status with HealthModifierDamageBased (Divine Embrace 1.2 with one tick, Sacred
  Ground and the Beacon 0.16 each 1 s) heals the heal base of its source (`ActiveStatus::heal`, the `HealSource` of the
  cast or of the area: the healer, its weapon, its gear score) times the value, the potency, the stacks, and the potency
  factor of the character (`Statuses::potency_factor`: 1 + the EffectPotencyMods of the statuses on the character for
  this row, for example `Status_Ability_Lifestaff_SacradGround_Beacon_Potency`). The heal path adds the outgoing part of
  the healer and the received part of the character. A status with HealthModifierDamageBased and no heal source gives no
  health (`StatusCounts::pulsesWithNoHeal`). A negative HealthModifierDamageBased (a damage over time) has no rule here.
- The heal kind and the number (stage 3.3b-S6): a status of an ability heals with the kind ability and shows a number; a
  status of a consumable shows a number at its application, and at a tick only with ShowUiDamageNumbersOnHeal of the row
  (`MajorPotionRegen`); a status of the gear, of a rest, or of a command shows no number (a decision of an agent: the
  client reads the column into bit 1 of byte +0x368 at 0x44B6EFF; its use is not found).
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
  DMG<type> and DMGStandard, ABS<type>, MoveSpeedMod, SprintSpeedMod, from stage 3.3b-S6 the heal values
  (HealScalingValueMultiplier: the outgoing part; HealMod, ConsumableHealMod, NonConsumableHealMod, DmgPctToHealthHealMod:
  the received parts; `Disease` HealMod -0.5), and from stage 3.3b-S5 CritChanceModifier and
  CritDamageModifier (the critical parts of the gear summary: [Combat and health](combat-and-health.md)), each times the
  potency and the stacks. The
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
  hit of the war hammer). From stage 3.3b-S5 the evaluator of the passive rules does this ([Passive rules](passive-rules.md)):
  a row with OnHitTaken (Power Through Pain) puts its status on the caster after a hit that a creature gives it, and a row
  with NumAroundMe and no event (Crowded Protection, Center of Attention) is a status with no end while the creatures near
  the character meet the condition (source "ability" with the rule id; it ends when the condition does not hold and at the
  leave of the world). The time of a caster effect is the
  SelfApplyStatusEffectDurations of its ability row (a number, or a list in the sequence of the statuses), else the
  duration of the status row (finding F10, test case RS10).
- Stage 3.3b-M2: a status action of the action lists (Guarded Sprint: `WarHammer_Pass_SprintDmgReduction` while the character
  moves with the war hammer in its hand) and the box of Blood Lust are statuses with no end while their condition holds (the
  same source "ability"; [Passive rules](passive-rules.md)). A status of an area with no duration (BaseDuration -1: the fortify
  in the well of Unyielding) has no end while the friend is in the area, and the area takes it ([Spell entities](spells.md)).
  The fortify of Clear Out (`WarHammer_ClearOut_FortifyBuff`, 4 s) comes from the spell of the rule Power Cleaner.
- A status with EquipAbility (stage B2): while it runs, the ability row that it names is a rule of the hits of the character, with the
  potency of the status (the tracker of Shirking Shock II equips `Ability_Perk_Weapon_DodgeSuccess_LightningDMG`; [Passive
  rules](passive-rules.md)). The heart rune keeps its own rule (the EquipAbility of its weapon row).
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
- The regeneration of the stamina (stage 3.4f-B): the StaminaRate part of the statuses (`StatusValues::staminaRate`, summed with the
  potency; Holy Ground +0.25, an earring affix) is `GearSummary::staminaRate`. The stamina system regenerates 1 + that part times its
  rate, and the speed table of the owner has the key 0x0B (StaminaRate) with that value when it is not 1 (client code 0x6888790;
  test case SM3). The value of a creature changes nothing (a creature has no stamina).
- The hastes of the bow build (stage B3): each one has MoveSpeedMod and SprintSpeedMod 0.07; two hastes give 1.14 (the limit of the category
  Haste: 0.4). Live check L1 of stage B3 (before the code, a haste of a command): a run of 4 s went 22.0 m with no haste and 23.4 m with 1.07
  (1.065, the start and the stop are the same); live check B3-4 (the haste of Archer's Speed): 19.05 m and 20.37 m in 3.5 s (1.069), no report
  of the position refused. The HUD icon of the owner comes from the full map entry: "3s" for 3.24 s, "8s" for 8.1 s (the HUD rounds up),
  and the timer of Archer's Speed shows its own icon (live checks L1, B3-1, B3-3). The client predicts no haste of Dodge and Weave (live
  check L1: a dodge of the client with the node and full stamina showed no icon with the backend of stage B2): the icon comes only from the
  server, about 0.2 s after the end of the dodge on the server.
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
tools/world status NAME AI_Gravedigger_TakeMinStamDMG 0 1 0.4   # potency 0.4: -2 stamina (a dodge at 98 stamina; stage B3)
tools/world gear NAME                                # the gear values with the status values in them
```

- The routes: `GET /v1/dev/status?character=ID`, `POST /v1/dev/status` (`status`, `seconds`, `potency`, `count`),
  `POST /v1/dev/status/remove` (`status`). The admin interface has `apply_status` and `remove_status`.
- The log lines: `world: status: NAME gets STATUS (SOURCE), N s, potency P`, `world: status: STATUS of NAME ends: CAUSE`.

## Traps

- The poison of a mushroom continues after the death of its caster: the area stays to its end of 26 s. A character that stays
  in the area of a level 30 shaman dies in about 70 s. A test that is not a fight takes the character out of the area.

- A command with seconds keeps its time (no time factor of Speedy or Alacrity); a command with 0 seconds gives the time of the row with the
  factor. Use 0 for a check of the time factor.
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
