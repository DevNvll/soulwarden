# Abilities

Read this document before a change of the abilities of a player: the press of a key of the bar, the
cast, the cooldown, the hits of an ability, and the effects of an ability on a creature, and before
a live test of an ability. The wire contracts are in the protocol notes (section 4.22, and 4.7,
4.13, 4.15; the cost, the mana, and the perks of an ability in 4.23). The nodes and the bars of a character are in
[Level, attributes, and skills](progression-and-skills.md), and the effects on a creature also in
[Creatures](creatures.md) and [Creature behavior](creature-behavior.md).

## Start here

| Task | Start here |
| --- | --- |
| The press of a key and the cast: the bits of the input report, the checks, the refusals | Protocol notes 4.22, `apps/backend/src/world/abilities.h` (`Abilities::press`, `CastVerdict`), `server/rep/client_input.h` (`InputStep::abilityKey`), `server/rep/rep_session.cpp` (`press_ability`), test cases AB1, AB3 |
| The cooldown: the value of the character, the countdown on the HUD | Protocol notes 4.22, `world/abilities.h`, `server/rep/states/cooldown_state.cpp`, `world/entity.h` (`Entity::cooldowns`), test case AB2 |
| The rule of one ability: its phases, damage rows, spells, effects, and the nodes that change them | `Abilities::plan` and `ability_rules` in `world/abilities.h` and `abilities.cpp`, test case AB12 |
| A melee hit of an ability (Charge, Maelstrom, Wrecking Ball, the swing of Gravity Well) | `Abilities::contact_phase`, `Combat::claim`, `Combat::strike` ([Combat and health](combat-and-health.md)), test case AB4 |
| An area of an ability (Shockwave, Path of Destiny, the vortex and the burst of Gravity Well) | `Abilities::run_area`, `throw_projectile`, `World::for_each_near`, `Navigation::sees`, test cases AB5 to AB7, AB10 |
| The vortex of Gravity Well as a spell entity: its record, its times, its cancel | [Spell entities](spells.md), `Abilities::spell_phase`, test cases SP2 to SP6 |
| The Pylon, the trap, the Beacon (stage 3.3b-S2b): the plan, the spawn, the payment, the held cooldown, the shot of the cast | `Abilities::plan` (`SpellPlan`), `Abilities::spawn_summon`, `Abilities::throw_spell`, `Abilities::spell_landed`, `Abilities::start_held_cooldown`, [Spell entities](spells.md), test cases SB2 to SB10 |
| A cast for a character with no client (a remote test character) | `Abilities::press_character`, `commands::cast_ability`, test case SP9 |
| The effects on a creature: stun, root, slow, weaken, knockdown, stagger, the icons | `Creatures::apply_effect`, `Creatures::knock`, `world/behavior.cpp`, `server/rep/states/status_state.cpp`, test cases AB8, AB9 |
| The ability data of the game | `tools/gamedata/abilities.sh` (`abilities.py`), `gamedata/ability_data.h`, [Game data](../development/game-data.md) |
| The cost of a cast and the mana: the one mana path, the regeneration, the mana state | `Abilities::mana_cost`, `Abilities::change_mana`, `Abilities::tick`, `server/rep/states/mana_state.cpp`, test case IZ8 |
| The passive rules of the nodes and the critical hits of an ability (stage 3.3b-S5): the evaluator, the spell selections of the action data | [Passive rules](passive-rules.md), `world/passives.h`, `Abilities::plan` (`selected_spell`, `data_rule`), `gamedata/ability_data.h` (`action_spells`), test cases PS1, CH7 |
| The perks of an ability (Sundering Shockwave, Insatiable Gravity Well, Refreshing Move) | [Gear and perks](gear-and-perks.md), `Abilities::perk_burst`, `Abilities::reduce_weapon_cooldowns`, test case IZ5 |
| The pull and the push of an ability, the dash of Charge, the counter of Riposte (stage 3.3b-S4) | [Forced movement](forced-movement.md), `CasterMotion`, `Abilities::motion_allowance`, `Abilities::countered`, test cases FM6, CM1, CM2, RC1, RC2 |
| The heals of the life staff (stage 3.3b-S6): Divine Embrace, Sacred Ground, the aim and its confirm, the target of a heal | `Abilities::plan` (`HealPlan`), `Abilities::confirm`, `Abilities::heal_target`, `Abilities::cast_heal`, `Abilities::heal_source`, [Combat and health](combat-and-health.md) (the heal path), test cases HE1 to HE4, HE11 |
| The fragment of an ability in the sequence (stage 3.3b-M2): Reap, Execute, Whirlwind, Mighty Gavel, Armor Breaker, Clear Out, and the other abilities of the great axe and the war hammer | `Abilities::press_entity` (`CastPlan::sequence`), `AttackSequences::start_ability`, `Abilities::strike_hit`, `Abilities::fragment_event`, `Abilities::follow_strike`, `Abilities::row_effects`, [Combat and health](combat-and-health.md) (the sequence), test cases GH1 to GH9 |
| The spells of an action, of a fragment, and of a rule (stage 3.3b-M2): the shield of Maelstrom, the ground of Mighty Gavel, the fortify of Clear Out | `Abilities::cast_spell_at`, `Abilities::rule_spell`, [Spell entities](spells.md), test cases GH8, GH10, GH11 |
| The hits of a cast for the client (IfAbilityNumHits; the exit of Whirlwind) | `Abilities::count_hit`, `CombatState::abilityHits`, `server/rep/states/skill_states.cpp` (slot 46), protocol notes 4.22, test case GH19 |

## Rules

- The abilities system (`world/abilities.h`, `world::Abilities`) owns the casts and the
  cooldowns. They are values of the character with the character id as their key (rule
  MP-1): a new session and a new entity of the character keep them (`Abilities::entered`
  copies them into `Entity::cooldowns`). Only this system changes them. The cooldowns are not
  in the database: a restart of the backend ends each cooldown (a project rule).
- The cast. The session gives each press bit of a new input step to the world (one cast for
  each input sequence: a client sends an entry in five reports). The world finds the
  character from the session, the weapon in its hand, the ability table of the weapon, and
  the ability on the place of the key. It refuses the press when the character is dead, the
  input is locked (a travel that the client did not confirm), the weapon has no ability
  table, the place is empty, the character does not have the node, the ability has no rule
  or the data has no row for it, the cooldown runs, or a different cast plays. A refused
  press changes nothing: no cast, no cost, no hit. The backend sends no refusal and no
  correction: the client has no message for them (live check L2). The session writes one log
  line for the first refusal of each cause, and `tools/world list` counts all of them.
- The cooldown starts at an accepted press, with the time of the data (`Time`, and the
  minimum `MinTime`) on the world clock. The owner gets the cooldown state (slot 45, type
  2932): one map for each place of the bar. Only the owner gets it. A cooldown is per ability,
  not per place. A change of the bar sends the cooldown state again (code review of stage
  3.3a, finding F8): the countdown is on the new key.
- The cooldown of the client (stage 1.3b, I8). Each cooldown has a second end: the end of the
  cooldown of the client (`AbilityCooldown::clientEnd`). The client starts its own cooldown at
  the press, and a reset of the server does not end it (live check I8-05). A press before the
  end of the client cooldown is refused: the client does not play it. The reset command ends
  only the cooldown of the server. A cooldown reduction (Stimulated Reduction, Refreshing
  Move) changes the two ends.
- The cost (stage 1.3b, I14; protocol notes 4.23). The mana cost of a cast is the `ManaCost` of
  the first spell of the plan whose cost id the mana table of the players has, times 1 + the
  `ManaCostMult` of each learned node whose `ManaCostList` has the id or "All", not less than
  0. A cost id with no row costs nothing: the client pays nothing for it (static proof). A
  press with less mana than the cost is refused (`noMana`): no cast, no cooldown. An accepted
  cast pays through the one mana path (`Abilities::change_mana`), which the mana potion uses
  too. The abilities of the great axe and the war hammer have no cost: the spells of the war hammer name
  `SpecialAttack`, which the mana table does not have, and the spells of the great axe name no
  cost id. The stamina has no model in the backend (the stamina rows of the two weapons are 0).
- The mana regeneration (stage 1.3b): a spend starts it after `ManaRegenDelay` (0 s), or after
  `ManaRegenDelayOnEmpty` (2 s) at 0 mana, at `ManaBaseRegenRate` (2 mana in each second; the
  row Player of the base vitals). The mana state (slot 47) has the delay that is left and the
  rate while the mana is not full: the client predicts the same gain. The world sends the
  state again at each change and when the mana is full.
- A melee phase is a window of input steps after the press (and the same time on the world
  clock, plus 0.5 s). A contact of the session of the caster in the window is a hit of that
  phase, with its damage row. A creature takes one hit of each phase. The hit check of a melee
  attack stays (the observer, the immune mark, the weapon, the distance, the point, the rate).
  Code review of stage 3.3a: a contact that comes more than 250 ms before the first time of
  the phase on the world clock is not a hit (finding F1, `ability_rules::kPhaseEarly`); a
  later press of the session, accepted or refused, ends the windows of the earlier cast, and
  a contact of an attack input that started after the press is a hit of the attack (F2). The
  close comes before each refusal, also for an empty place of the bar (code review of stage
  1.3b, finding F9).
- An area phase runs at its time on the world clock. It takes the creatures of its circle
  from the position index, not more than 3 m higher or lower (or the `Height` of the spell),
  with no wall between the center and the creature (`Navigation::sees`), 32 at most, the
  nearest first (for the whole phase, also across the circles of a path: finding F4 of the
  review of stage 3.3a; a project value, because the spell rows and the ability rows of the
  data have no column for a count of targets). A creature takes one hit of each phase. A
  creature on its way back takes nothing. A caster that left the world or died does nothing,
  but for the phases of a spell entity: they run at the place of the spell while the spell is
  in the world, also after the death of the caster (the vortex row has no `EndOnCasterDeath`;
  [Spell entities](spells.md)).
- The shot of Gravity Well: the origin of a shot must be near the character with no wall
  between the launch place of the server (1.5 m above the character) and it (finding F3);
  else the server throws the projectile from the character.
- Each status row of a plan, also of a delayed effect, must be in the data: an ability with a
  part of its data is refused (`noRule`; finding F9).
- Each hit of an ability goes through `Combat::strike`: the damage of the row, the creature
  system with the attacker (the target, the credit, the rewards of a kill), and the damage
  number to the owner. The cast keeps the slot of its weapon: a hit uses the gem of that
  instance (code review of stage 1.3b, finding F1). The cast keeps the damage conversion of the
  gem of the weapon at the press (`Cast::conversionType`, `conversionPart`): a different
  weapon in the slot later does not change the hits of the cast (code review of stage 3.3b-S1,
  finding F3, test case RS3). The hit has the ability of the cast for
  the condition InSequence of a caster effect (stage 3.3b-S1: Safety Measures puts the fortify
  WarHammer_WreckingBall_Fortify on the caster at a hit of Wrecking Ball; [Status
  effects](status-effects.md)).
- The effects on a creature go only through the creature system (`Creatures::apply_effect`
  for a status row, `Creatures::knock` for a knockdown or a stagger). The values come from
  the status row of the data (the time, the stun, the root, the slow, the weaken, the end at
  the next hit). The same effect again gets the later end. A creature on its way back takes
  no effect. The behavior obeys each effect at each step.
- The observers. Each session that observes the creature gets its status state (the icons
  at the name plate) and its movement state (the action of the effect). Only the session of
  the caster gets the cooldown state and the damage number (test case AB9).
- The cost of the rules. A phase looks only at the entities near its center. The end of the
  effects looks only at the creatures with an effect. A cast that has no phase to come goes
  away 5 s after its end (test case AB10 has a time limit).
- The vortex of Gravity Well is a spell entity (stage 3.3b-S2a, [Spell entities](spells.md)):
  at the landing of the projectile the abilities system asks the spell system for the entity,
  and the spell runs the vortex phase at its start and the burst at the time of the slice
  (3.0 s). Each session near it sees the vortex and the burst. The projectile of Gravity Well
  comes from the shot of the client (the attack "gravitywell"), or the server throws it 0.83 s
  after the press when no shot came (also for a character with no client: `tools/world
  cast`). Each other shot item goes to the projectile system (stage 3.3b-S3,
  [Projectiles](projectiles.md)): the basic attacks of the ranged and magic weapons.
- The spell entity of a cast (stage 3.3b-S2b, `CastPlan::spell`, [Spell entities](spells.md)). Each row and each spell of
  the plan must be in the data (the spell rows, the ability shot of the projectile data, the status rows of the effects, the
  cost id of the payment), or the press is refused (`noRule`).
  - Ice Pylon of the ice gauntlet (`IceMagic_IcePylon`): the spawn of `IceMagic_Spell_Pylon` at 0.6 s in front of the
    caster; the payment of 15 mana at the spawn (the press pays nothing; a press with less mana than that cost is refused:
    `noMana`); an indefinite cooldown from the press to the end of the Pylon, then the cooldown of the data (10 s).
  - Traps of the musket (`Ability_Musket_Traps`): the toss `Musket_Trap_Toss` (`_DMG` with `Upgrade_Musket_TrapRend`), whose
    landing makes the trap; the effects of the trap hit: `Musket_Trap_Root` (and `Musket_Trap_Rend` with the node).
  - Beacon of the life staff (`Ability_Lifestaff_Beacon`): the projectile `Spell_Ability_Lifestaff_Beacon_Projectile`
    (`_Upgrade`, `_Upgrade2`, `_Upgrade3` by the chain of the nodes Infused Light, Radiance's Blessing, Speed of Light; a
    decision of an agent), whose landing makes the area of its `SecondarySpellID` on the entity that it hit. The press pays
    16 mana (the cost of the projectile spell).
  - The projectile of the trap and of the Beacon comes from the shot item of the client (the trap: the attack "traps", 8
    input steps after the press; the Beacon: the attack "heavy", 4 input steps after the press; live check of stage
    3.3b-S2b), or the server throws it 0.3 s after the time of the cast fragment (0.267 s and 0.133 s) when no item came. The
    Pylon has no shot item (live check: the client sends no item and no message for the Pylon). The direction of such an item is
    valid when its length in three dimensions is 1, also straight down (code review of stage 3.3b-S2b, finding F10; case
    RK10); the direction of the press is the fallback only for a direction that is not finite or has the length 0.
  - The status rows of the area of the Beacon (StatusEffects of its spell row) are resolved at the press: a row that the
    data does not have refuses the press (`noRule`) before the payment (finding F9; case RK9).
- The impact of a hit (stage 3.3b-S4, [Forced movement](forced-movement.md)): each hit of an ability goes through
  `Combat::strike` with its source (the center of an area or of a spell; the caster for a melee phase), and a creature
  that lives after it moves by the impact of the row. The start of the vortex gives the impact of its pull row
  (`PhasePlan::pull`: GreatAxe_GravityWell_Vortex, the child entity VortexPull of the slice) with no damage. The pull row
  must be in the data, as each row of a plan.
- The movement of the caster (stage 3.3b-S4, `CastPlan::motion`): Charge has a dash that its client makes (the root
  motion of GreatAxe_Engage). The position check accepts it in the envelope of the accepted cast
  (`Abilities::motion_allowance`); the owner gets no position of the server; each observer gets the state `motion` with
  the place and the actions 7/59 (the dash) and 7/2312 (the swing), then the action at rest. For a character with no
  client the server makes the dash (13.77 m in 1 s, or to 1 m in front of a creature on the line).
- The counter (stage 3.3b-S4, `CastPlan::window`, `PhaseKind::counter`): Riposte of the rapier puts its window
  (Rapier_Riposte_Timer, 1 s) on the caster through the status system; a creature hit in the window starts the counter one
  time, which hits the attacker at 0.433 s with its row and its stun (`Abilities::countered`). The client does not play the
  counter action (open: its action numbers are not known). The counter needs the weapon of the cast in the hand (the
  condition Riposte_Attack of the action data: the mannequin tag Rapier and the timer; code review of stage 3.3b-S4,
  finding F2, test case RM2): a change of the weapon ends the window (`Abilities::weapon_changed`), a hit with a different
  weapon in the hand starts no counter, and the counter hit needs the weapon too.
- The rules of the nodes in a plan (stage 3.3b-S5, [Passive rules](passive-rules.md)). A node changes an ability in one of
  two ways, and the data says which one:
  - A rule row of the node (an ability row with a cell of a rule): the evaluator of the passive rules applies it at each hit
    of the ability (the hit has the row, the attack type Ability, the ability of the cast, the spell of the area, and the
    count of the creatures in the area). The plan does not apply such a row a second time: Aftershock
    (`Ult_Passive_WarHammer_SlowOnCC`), Frailty (`Upgrade_WarHammer_Shockwave_Weaken`), Crowded Well
    (`Upgrade_Greataxe_GravityWell_DmgPerFoe`), and Stimulated Reduction (`Upgrade_WarHammer_PathOfDestiny_CDRPerHit`) have
    their rule of stage 3.3a in the plan only for an ability file with no rule row (`data_rule`: a file of an older tool).
  - A spell selection of the action data (`actionSpells` of `ability-data.json` format 4: a CastSpell of the action list of
    the weapon in an IfThenElse with a test IfAbilityActive of a node): the plan takes the spell of the first selection
    whose nodes the character has (`selected_spell`). Shockwave: `WarHammer_Spell_Shockwave_L2` with Meteoric Crater
    (`Upgrade_WarHammer_Shockwave_SizeIncrease`); Path of Destiny: `_L2` with Upgrade_WarHammer_PathOfDestiny_Stagger, `_L3`
    with Upgrade_WarHammer_PathOfDestiny_CDRPerHit; Gravity Well: the projectile spell (its SecondarySpellID is the vortex);
    the Beacon: its projectile. An action with no selection in the file keeps the rule of stage 3.3a. A selection with a
    test that the tool cannot write as a list of nodes (an Or of a state; two casts of the ice storm) is not exported: the
    tool lists it in `actionSpellsNotExported`.
- A critical hit of an ability (stage 3.3b-S5, [Combat and health](combat-and-health.md)): each hit of a phase rolls, as a
  basic attack does; an area has its center as the source of a hit from behind (no direction for a creature at the
  center); a tick of a status does not roll. Each phase hit has its attack: the cast (`kAttackCast`), for the hit counts of
  the rules.
- The heals of the life staff (stage 3.3b-S6; `CastPlan::heal`, `HealPlan`). Divine Embrace: a direct heal at 1.0 s of
  its cast (the cast bar of the client, live check), its spell `Spell_LifeStaff_DivineEmbrace` (Privilege:
  `_ReducedCost`, the selection of the action data), and the status of its spell row
  (`Status_Ability_Lifestaff_DivineEmbrace`: HealthModifierDamageBased 1.2, one tick). The cost of the spell (ManaCost:
  25, Privilege 20) is paid with the heal, not at the press. Sacred Ground: the area of its self spell
  (`Spell_LifeStaff_SacredGround_Self`, Holy Ground `_Self_Upgrade`, Anointed `_Self_Upgrade2`) at the caster at
  0.333 s, with its cost (15) at the spawn; its statuses heal each 1 s for 12 s. Source: game data (the spell rows, the
  action list `playeractions_ability_lifemagic`); the times of the cast: live check.
- The aim (stage 3.3b-S6; live check of the run `heal-live1`, and the action list of the data). The key of Divine
  Embrace or Sacred Ground starts only the aim of the client ("LCTRL Target Self"): no cast, no cooldown, no cost
  (`AbilityCounts::aims`). The confirm of the same session starts the cast with all the checks of a press
  (`Abilities::confirm`): the self cast input `self_target` (action bit 430, LCTRL) heals the caster; a click
  (`attack_primary`) heals the target choice. The key with `self_target` held casts at once on the caster (the actions
  `*_SelfCast` and `*_SelfLoop` of the data). An aim ends at its confirm, at a press of a key, at a change of the weapon,
  at a change of the bar or of the nodes of the character, at a travel (code review of stage 3.3b-S6, findings S6-R2 and S6-R8;
  `Abilities::cancel_aim`), and after 10 s (`ability_rules::kAimKeep`, a decision of an agent). The confirm needs the ability of
  the aim on its place of the bar now (`AbilityPress::expected`; else the refusal `aimChanged`, "the place of the bar has a
  different ability than the aim", case RW2) and uses the lock of the session now: a confirm in the loading screen of a travel is
  refused (`locked`, case RW8). A click of a different session confirms nothing (rule MP-10). A press with no session
  (`tools/world cast`, a character with no client) casts at once.
- The target of a direct heal (`Abilities::heal_target`). The self cast: the caster. A click: the nearest friend
  (`World::friendly`) that lives, can gain health (code review of stage 3.3b-S6, finding S6-R3: its health is below its
  maximum; code review of stage 3.3b-M1, finding M1-R7, case RB7: the heal base, the outgoing factor, and the received factor of
  the target are above 0, thus Disease and the arena debuff can make a friend no target), is in the range of the spell
  (CastDistance 25 m), and in sight (chest to chest); with no such friend the caster (the self form of the spell). A caster at its
  full health is no target: no heal and no payment. The data excludes a target that has the status of the heal
  (TargetStatusEffect with NotEqual of the ability row). No target: no heal and no payment. The point of the click is
  not decoded: the nearest friend, not the friend under the aim (a decision of an agent).
- The heal source of a cast (`Abilities::heal_source`): the healer, its weapon and its gear score, the spell, and the
  ability table. The heal path computes the amount from it ([Combat and health](combat-and-health.md)).
- An accepted press of an ability with no fragment in the sequence (a cast, or the aim of an aimed ability) ends the basic
  attack of the character (stage 3.3b-M1, `Combat::end_sequence`): the client leaves the fragment of its attack for the
  action of the ability.
- The fragment of an ability (stage 3.3b-M2; game data: `weapon-actions.json` of format 2, [Game data](../development/game-data.md)).
  An ability of the great axe or the war hammer whose weapon has its action in the file plays the fragment of that action in
  the sequence of the character (`CastPlan::sequence`; world/attack_sequence.h, [Combat and health](combat-and-health.md)):
  - The hits are the strikes of the fragment: a contact in the window of a strike (one input step early, two late, and the
    due time on the world clock) is a hit with the row of the clip for the nodes of the character. A creature takes one hit of
    each strike. The contact phases of the plan go; their effects (the knockdown of Wrecking Ball) come at each hit of a
    strike. The area phases, the shot, and the spell of the plan stay (Shockwave, Path of Destiny, Gravity Well).
  - The reach of a strike: the extent of the shape of the row (the offset, the half height, and the radius of a capsule; half
    the diagonal of a box) and 1 m of the arm, not less than 3.5 m, and the tolerance 1.5 m (`Combat::strike_reach`; a decision
    of an agent). Reap: 3.5 m; with The Collector 6.25 m (game text: 5 m and 8 m, with the dash of the action).
  - The fragment: the fragment of the action, or the last selection of the action (ChangeActionFragment) whose nodes and perks
    the character has: Fatal Attraction (`Ability_GreatAxe_JudgementsReach_Upgrade`), Unending Winds
    (`Ability_GreatAxe_Whirlwind_Upgrade`), the artifact perk of Whirlwind (`_Upgrade_Artifact`). Executioner plays its base
    fragment: no action selects `Ability_GreatAxe_Executioner_Upgrade` (live check I1).
  - The start of the fragment (`AttackSequences::start_ability`): at rest at the press. In a fragment that plays, at the first
    step in the next 20 steps at which the action of the ability can start: in an instant window of a transition to it at once,
    in a buffered window at the end of the window, at the mark CanTransition, or at the end of the fragment. With none, the
    press is refused (`casting`): the client does not play it. The timed phases of the plan come that much later (`CastPlan::delay`).
  - The transitions inside an ability have the conditions of the action lists as terms: a learned node, a perk ability, the
    attack input, the block press (`block_press_virtual`, bit 484: the swing of Charge with Unpredictable Strike, live check of
    stage 3.3b-M2), the hits of the cast (IfAbilityNumHits: the exits of Whirlwind with Unending Winds), and the fragment that
    plays. A collision of the client (`character_collided`: the uppercut of Charge) is not visible to the server: a contact that
    fits a strike of the next fragment shows it, and the fragment starts at that step (a hit with the row `GreatAxe_Engage1`,
    live run of stage 3.3b-M2).
  - The abilities of the other weapons (Riposte of the rapier) and an ability whose action is not in the file keep the
    contact phases of their plans. The six abilities of this stage (Reap, Execute, Whirlwind, Mighty Gavel, Armor Breaker,
    Clear Out) have no plan of their own: with no file of format 2 their press is refused (`noRule`).
- The pull of Reap (stage 3.3b-M2): a strike whose usable rows do no damage (`_Pull`, `_upgrade`, `_upgrade2` with the factor
  0; `_RP2` with 0.0001) gets no contact from the client (live check I1). The server tests its shapes at the time of the
  strike (`Abilities::follow_strike`): each creature that a strike of the cast hit, and each creature in a shape of a usable row
  in front of the direction of the press (the capsule from its offset minus its half height and its radius to its offset
  plus them, its radius to each side, and 0.5 m for the body, 2 m up or down) in sight of the caster. Each creature gets the
  row of the shape nearest to it (1.2 m, 2.7 m, 4.3 m): the impact of the row (1 m, 2 m, 3 m of the test data; 2 m, 4 m, 6 m
  in the game data) and the status of the row. A decision of an agent from the data and the live checks.
- The status of a damage row (the column StatusEffect of the combat tables, format 3; `Abilities::row_effects`): the root of
  Gravity on the pull rows `_RP2` (`Status_Passive_GreatAxe_Gravity_Root`, 0.33 s, then its OnEndStatusEffect
  `_Root_Hold`, a root of 0.4 s). EffectOnlyOnDamage: only after damage above 0. ApplicationCooldown of the status row: one
  application for each creature in that time (1 s). The live run of stage 3.3b-M2 showed the two statuses on a wolf.
- The hits of a cast for the client (stage 3.3b-M2; `Abilities::count_hit`, `CombatState::abilityHits`). For an ability whose
  row has TrackHitCount (Whirlwind), the press sets the count to 0 and each accepted hit of a strike adds 1. The owner gets
  them in the ability state (slot 46, fields `hitDataNumHits` and `hitDataAbilityIds`; protocol notes 4.22). The client tests
  them for IfAbilityNumHits: with no count it leaves the spin of Unending Winds at 1.933 s also after four hits; with the
  count it plays the seven spins (live proof of stage 3.3b-M2). A refused contact adds nothing.
- The spells of a cast (stage 3.3b-M2; `Abilities::cast_spell_at`). The action casts a spell with no mark at its start (the
  shield of Maelstrom with Absorb, `Spell_Upgrade_GreatAxe_Maelstrom_ProjectileConsume`). A fragment casts a spell at its
  time (CAGE-CastSpell: the ground of Mighty Gavel, `WarHammer_Spell_MightyGavel` at 0.638 s), and an action casts a spell at
  a mark; both only for the six abilities of this stage (the plans of stage 3.3a make the areas of their spells). The role of
  the spell comes from its row: a friend area with statuses is an aura, a friend spell at its owner is a shield, each other
  spell is a picture with no rule of the server ([Spell entities](spells.md)).
- A rule of a node can cast a spell (CastSpell; [Passive rules](passive-rules.md)): Power Cleaner casts
  `WarHammer_Spell_ClearOut_Fortify` at a hit of Clear Out (`Abilities::rule_spell`): the fortify of the friends in 6 m.
- The rows of the abilities (stage 3.3b-M2; [Passive rules](passive-rules.md)): Execute gives BaseDamage 0.5 against a target
  below 50 % health (its ability row); Armor Breaker gives ArmorPenetration 0.35 at each hit of its fragment (its row has
  InAction `Ability_WarHammer_ArmorBreaker` and InSequence `Ability_WarHammer_BlockBreaker`, the fragment of its action).
- A new ability needs, in this sequence: a live check of its keys, its contacts, and its
  shots (protocol notes 4.22: L1, L3, L7), its rule in `Abilities::plan` with the rows and the
  spells of the data, a test case, and a live video. Do not make a message of the server for
  an ability that is not proved.

## Commands

```sh
tools/world cooldowns NAME           # the cooldowns that run, with the seconds to their end
tools/world cooldowns NAME reset     # each cooldown of the character ends
tools/world node NAME greataxe Ability_Greataxe_Charge Upgrade_Greataxe_Maelstorm_Rng   # assign nodes (one point each)
tools/world node NAME greataxe Upgrade_Greataxe_Charge_DmgPerDistance Upgrade_Greataxe_Charge_SwingAttack   # the swing of Charge
tools/world bar NAME greataxe 0 Ability_Greataxe_Charge   # put an ability on place 0 (Q) of the bar of a weapon
tools/world creature effect 2c WarHammer_Shockwave_Stun 5   # give a creature a status effect of the data for 5 s
tools/world creature effect 2c knockdown                    # a knockdown (2 s) or a stagger (1 s)
tools/world list                     # the line "abilities": the casts, the refusals by cause, the hits, the effects
tools/world cast NAME 2 --yaw 90     # press place 2 (F) for a character (also with no client); the yaw from the north
tools/world spells                   # the spell entities (the vortex) with their age
tools/gamedata/abilities.sh          # write ability-data.json to the game data folder
```

- The routes: `GET /v1/dev/abilities?character=ID`, `POST /v1/dev/abilities/reset`, `POST
  /v1/dev/progress/node`, `POST /v1/dev/progress/bar`, `POST /v1/dev/creatures/effect`, `POST
  /v1/dev/abilities/cast`. The admin interface has `assign_node`, `set_ability_bar`,
  `reset_cooldowns`, and `cast_ability`.
- The build of the user (`~/Games/soulwarden/work/builds/medium-bruiser-mikars.md`) on the
  development character: give the mastery levels first (`tools/world mastery NAME greataxe
  20`, the same for `warhammer`), then the 19 nodes of each tree with `tools/world node`, in
  the sequence of the tree (a node of a row needs the points of the rows before it), then the
  three places of each bar with `tools/world bar`. The character window shows the result
  (K key, tab "Weapon Mastery").
- `tools/gamedata/abilities.sh` writes `ability-data.json` (98 cooldowns, 4,516 status effects
  of 4,517 rows, 1,777 spells; 0.4 MB) to the game data folder, from the same sources as the
  creature data tool. `--find TEXT...` prints the rows with a text. With no file the backend
  starts, and it refuses each press (`no data`). `tools/gamedata/creatures.sh` writes the
  reaction fragments of each slice (the member `reactions`) for the knockdown and the
  stagger.

## Traps

- The client plays an ability with no answer of the server, and it does not play a press in
  its own cooldown. Thus the client and the server agree in a normal game. A cooldown that
  only the server has (a command, or a different client) shows a countdown, but the client
  plays a press in it: the server then refuses it, and the animation plays with no damage.
- A cooldown stays in the state of the character until its end and the end of the client
  cooldown passed (code review of stage 1.3b, finding F11: a cast of a different ability erased
  the end of the client); the map of the owner has only the cooldowns of the server that run.
- A press in the local cooldown of the client sends the key bits, and the client does not
  play it (stage 1.3b, I8: this is the cause of the "first press after a weapon change" of
  stage 3.3a, whose tries reset the cooldowns of the server between presses). The backend
  refuses such a press (the cooldown of the client). The reset command does not end the
  cooldown of the client: after a reset, wait for the countdown of the HUD before the next
  press.
- After a travel the body of the character faces north, also when the camera faces a
  different direction. Charge and Wrecking Ball go in the direction of the body: walk forward
  for a short time (`tools/hx 'keydown 0x57' 'wait 250' 'keyup 0x57'`) before such an ability.
- The ability uses the weapon in the hand. A world entry selects the first weapon slot: key
  `1` or `2` (`tools/hx 'key 0x32'`) selects a weapon slot. A weapon with no node on its bar
  refuses each press (`the place of the bar is empty`).
- The stun of Shockwave ends at the next hit (`StopOnHitCount`). To see the stun in a video,
  do not hit the creature in the 2 s.
- The root and the slow of the vortex show only as the icon at the name plate (the root only)
  and as a creature that does not move. Show them with the track of the creature (`tools/world
  track ID SECONDS --tsv FILE`). The vortex and the burst show in the client from stage
  3.3b-S2a; with no `spell-data.json` the vortex has no picture.
- With `tools/world ignore NAME on` no creature fights the character, also after a hit: a
  creature with a slow then does not run. Turn the ignore value off for a video of a slow or
  a root (the creature must run to the character), and put a second creature with no effect
  beside it for the comparison.
- Charge and the shot of Gravity Well go in the direction of the camera of the client, not of the body: turn the camera
  (`tools/hx 'look DX 0'`) before the press.
- Riposte needs the hit of a creature in its window of 1 s: press the key just after the creature starts its attack (the
  backend log line `starts the attack`; the bite of a wolf comes 0.5 s later).
- Divine Embrace and Sacred Ground need a confirm after the key: `tools/hx 'key 0x51' 'wait 700' 'key 0xA2'` (Q, then
  LCTRL: the self cast), or `'click X Y'` on the friend. A key alone gives only the aim: the backend log has the line
  "aims", and no cast.
- Sacred Ground with a click puts the area at the caster, not at the point of the aim (the backend does not decode the
  point of the client: the spell `Spell_LifeStaff_SacredGround` with its raycast of 25 m is not used).
- `tools/gamedata/abilities.sh` with no argument writes into the common game data folder: the
  next start of the user then has the file. For a test, give it an output path and give the
  run its folder with `SOULWARDEN_GAMEDATA_DIR`.
- The pull of Reap has no contact in the input of the client (stage 3.3b-M2): look for the line "moves creature" of the
  backend log, not for a hit claim. With Fatal Attraction the first strike reaches 3.65 m (its shape is shorter than the
  shape of the pull): a wolf at 4.3 m gets the pull with no hit of the first strike.
- The exit of Whirlwind is a test of the client (IfAbilityNumHits): with no TrackHitCount in the row of Whirlwind (an
  ability data file of an older tool), the client leaves the spin at 1.933 s although the server accepts each hit.
- The swing of Charge (Unpredictable Strike) needs the block press between 0.1 s and 0.4 s of the charge:
  `tools/hx 'key 0x52' 'wait 230' 'mousedown right' 'wait 80' 'mouseup right'` (the key of Charge on place R). The node
  needs Frenzied Momentum first (`Upgrade_Greataxe_Charge_DmgPerDistance`).
- Wrecking Ball gave no contact in the street of the test place (the setup of stage 3.3b-M2, a wolf 4 m in front, the body
  turned to the camera), also with the backend of stage 3.3b-M1: the leap lands at the wolf, and the client reports no
  contact. Open; stage 3.3a had a contact at 0.8 s in a different place.
- The time of day of the game changes fast: the street of the test place is dark at some times. Make a video again when it
  is day.
