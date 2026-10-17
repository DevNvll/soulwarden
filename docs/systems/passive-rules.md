# Passive rules

Read this document before a change of a rule of a learned node of a weapon tree or of an ability row of a perk, and
before a live test of such a rule. The evaluator of the passive rules is the one place that reads the cells of these
rows (stage 3.3b-S5). The critical hit itself is in [Combat and health](combat-and-health.md); the wire contracts are in
the protocol notes (section 4.30; the status state in 4.24, the status of a creature in 4.22).

## Start here

| Task | Start here |
| --- | --- |
| The rules of a hit: the events, the conditions, the effects | `apps/backend/src/world/passives.h` (`Passives::before_hit`, `after_hit`, `conditions_pass`, `counts_pass`), `world/combat.cpp` (`Combat::strike`), test cases PS1, PS2, PS7 |
| The comparator of the data (the blank comparator, the band of an equal value) | `world/data_rules.h` (`compare`), test case PS2 |
| A status while entities are near a character | `Passives::tick`, `Passives::around_count`, `gamedata/ability_data.h` (`tracking_spell`), test case PS3 |
| A rule of a hit that a character takes | `Passives::hit_taken`, `world/health.cpp` (`Health::creature_hit`), test case PS7 |
| The list of the rules: what the evaluator applies, and what it does not apply with the cause | `Passives::describe`, `describe_table`, `describe_row`, `server/http/passive_commands.cpp`, `tools/world passives`, test case PS4 |
| The rule rows of the data and the spell selections of the action data | `tools/gamedata/abilities.py` (format 4), `gamedata/ability_data.h`, [Game data](../development/game-data.md) |
| The fixed stats of the nodes in the gear values | `Gear::values` ([Gear and perks](gear-and-perks.md)) |
| The rules of stage 3.3b-M2: a spell of a rule (CastSpell), the box of Blood Lust, the status actions (Guarded Sprint), the row of an ability with InSequence, the uses of a node in the weapon actions | `Passives::apply_effects`, `Passives::around_count` (`tracking_supported`), `action_holds`, `ability_value_rule`, `Passives::describe_row` (`data_use`), test cases GH9, GH10, GH13, GH14, RV9 |
| The rules of stage B2: the conditions of a status of the target and of the attacker, DamageTypes, the helper rows of a node, the rows that a status equips, the end of statuses, the Boltcaster, Shirking Shock II, Surprise Attack | `Passives::target_status_pass`, `own_status_pass`, `status_rules`, `rules_of`, `apply_effects`, `target_status`, `HitContext::types`, `targetEffects` (`world/gear.h`), `AbilityData::helpers_of`, test cases BC3 to BC8 |
| The rules of stage B3 (the haste chain of the bow build): the weapon swap (OnWeaponSwap), the end of a dodge (the dodge status actions, AfterAction and AfterSequence), the tag removal actions, the time factor of a status (StatusEffectDurationReduction), the condition StatusEffectCategories | `Passives::weapon_changed`, `dodge_started`, `dodge_ended`, `duration_factor`, `self_pass`, `own_status_pass`, `describe_row`, `gamedata/ability_data.h` (`ActionDodgeStatus`, `ActionTagRemoval`), test cases HS1 to HS10 |
| The rules of stage B4 (the other rules of the bow build): the threshold abilities of the attributes, the cap of the sources of a row, DistFromDefender, a target condition with no event, the outgoing time factor, the attack type of a creature hit, the fixed cooldown rules, the consumable rules, the perk script of Vital Synergy | `Passives::attribute_rules`, `merged`, `conditions_pass`, `target_value_rule`, `outgoing_factor`, `taken_reduction`, `cooldown_factor`, `consumable_cooldown_factor`, `consumable_used`, `tick` (`kPerkScripts`), test cases BR1 to BR14, BY2 |

## Rules

- One evaluator (`world/passives.h`, `world::Passives`, world thread only). The combat system asks it before the damage of
  each hit of a character on a creature (`before_hit`: the damage part, the critical parts, the penetration) and gives it
  the result after the hit (`after_hit`: the statuses, the heals, the cooldown changes). The health system gives it each
  hit that a character takes (`hit_taken`), and asks it for the outgoing part of each heal of a character (`heal_bonus`,
  stage 3.3b-S6). The world tick runs the rules of the entities near a character (`tick`). No
  other code reads a cell of a rule: the gear system has no condition check of its own now (stage 3.3b-S5), and it takes
  the fixed stats of the nodes from the evaluator (`Passives::fixed`).
- The rules of a hit (`Passives::rules_of`): the learned nodes (level above 0) of the ability table of the weapon of the hit
  (the WeaponMastery of the weapon row), each with its row of the ability data; the active abilities of that table (their
  rows have the rules of their own hits); and the ability rows of the perks of the equipped items, with the potency of the
  perk. A character has no rule of a different weapon tree in a hit. Source: game data (the ability tables of the players,
  `ability-data.json` format 4: 2,025 rows). From stage B2 two more sources (game data):
  - the helper rows of a learned node (`AbilityData::helpers_of`: the rows whose RequiredEquippedAbilityId is the node, with
    EquipWhenUnsheathed; 78 rows of the data): the tracker of Surprise Attack, `Passive_Bow_Hunter_DmgOnFirst_Tracker`. The list names
    them with the origin "helper of a node". RequiredEquippedAbilityId is also a condition: the character has the node;
  - the rows that the statuses of the character equip (`Passives::status_rules`: EquipAbility of the status row, with the potency of the
    status; 378 status rows of the data have EquipAbility), for a hit of each weapon: the tracker of Shirking Shock II
    (`Status_ItemPerk_Weapon_DodgeSuccess_LightningDMG_Tracker`)
    equips `Ability_Perk_Weapon_DodgeSuccess_LightningDMG` while it runs. The list names them with the origin "row of a status".
- The events that the backend has a source for: OnHit, OnCrit, OnKill, OnHitTaken, OnHeadShot, OnHitBehind, OnHealed (only
  its HealScalingValueMultiplier, stage 3.3b-S6), and from stage B3 OnWeaponSwap and the end of a dodge (below). A row with no
  event and a condition of a hit (DamageTableRow, AttackType, RemoteDamageTableRow) is a rule of that hit (Crowded Well:
  `Upgrade_Greataxe_GravityWell_DmgPerFoe` on the burst of the vortex). A row with no event, no condition, and a value is a
  fixed stat. A row with no event, NumAroundMe, and SelfApplyStatusEffect is a status while the condition holds. A row
  with OnBlockedHit and OnHit applies its OnHit part (the backend has no block).
- A row with a column that is not a condition, an effect, or a mark of the evaluator, or with an event that the backend does
  not have (a block, a traversal), has no effect. A row with OnHealed and an effect after the heal (a status, a
  cooldown change, a heal of the healer: `Passive_Lifestaff_Healing_CDROnLow`) has no effect: the backend applies only the
  outgoing heal part. The list names it with the column (rule "no silent
  rule").
- The conditions and the source of each rule:
  - AttackType, DamageTableRow, RemoteDamageTableRow: lists of the data ("Light,Heavy"), no case.
  - TargetHealthPercent, MyHealthPercent, MyStaminaPercent, MyManaPercent with their comparison kinds: the comparator of
    the client (`data_rules::compare`). An empty comparison kind is LessThanOrEqual (client code 0x43B6390, protocol notes
    4.30). An equal value is a band of 0.001 percent: 600 of 2,000 is 30.000002 percent as a float, and "or equal" takes it
    (a decision of an agent; test case PS2: Executioner 50, `Upgrade_Greataxe_Executioner_CritVsLowHP` 30, Leaping Strike
    30, each below, equal, and above the limit).
  - The health of the target is its health before the hit, one value for each rule of the hit (a decision of an agent).
  - TargetStatusEffectCategory with TargetStatusEffectComparison: the EffectCategories of the status rows of the effects
    on the creature (NotEqual: the creature has none of them).
  - TargetStatusEffect, TargetStatusEffectStackSize, TargetStatusEffectComparison, CheckStatusEffectsOnTargetOwned (stage B2; client
    hit test 0x67A18F0, research bow-build-2.md Q4): with a stack size of 1 or more, the stacks of the status (or of the effects of the
    categories, when no status is named) with the comparison; with no size, the target has the status (NotEqual: it has none). The
    stacks of each source count (one entry for each status of an entity, client 0x4038E80; decision D10 of the orchestrator); with
    CheckStatusEffectsOnTargetOwned only an effect whose source is the attacker (a creature has one source for each effect: the last
    application). The conditions of a target status see the effects of the target before the hit (`HitContext::targetEffects`, a copy in
    `Combat::strike`): an earlier rule of the same hit does not change them (the bolt of the Boltcaster tests the 3 charges before the
    charge of its own hit). An effect with no row in the creature (the rend of a perk) gets its row from its status id in the copy (code
    review of stage B2, finding B2-R3, test case BV3: Mark lost its +0.1 on a creature with the rend of Sundering Shockwave).
  - StatusEffect, StatusEffectStackSize, StatusEffectComparison (stage B2; client self test 0x67A0130): with no size, the attacker has the
    status (each source; the comparison is not read); with a size, its stacks with the comparison. CheckOwnedStatusEffects is not in the
    evaluator (a row with it is not applied).
  - StatusEffectCategories (stage B3; client self test 0x67A0130, research bow-build-2.md Q1): the attacker has a status of a category of the
    list (each source); StatusEffectComparison NotEqual: none. Speedy (`GlobalPerk_Artifact_Set1_MediumFeet_2`: +0.1 BaseDamage while
    hasted). A rule with no hit (a swap, the end of a dodge, a time factor) has the same condition on its character. A row with no event,
    the condition StatusEffect or StatusEffectCategories, only the conditions of a rule with no hit, and only values of a hit (BaseDamage,
    CritChance, CritDamage, ArmorPenetration, HeadshotDamage, HitFromBehindDamage) is a value of each hit while the condition holds
    (`Passives::conditional_value`; the perks `BonusDamageSelfCCPerk1` to `4`: +0.12 to +0.24 while the character has a root or a slow)
    [agent decision with the client self test 0x67A0130]. The gear values do not take such a row (before stage B3 they gave it with no
    condition).
  - AfterAction, AfterSequence (stage B3): the end of an action. Only the end of a dodge is such an event in the backend (below); a hit does
    not pass a row with them (a row with an event in the window after an action, `PowerThrowBlockBreakPerk1`, `Rapier_Slash_Mod2`, is not
    applied, with the cause in the list).
  - DamageTypes (stage B2): the hit has a part of a type of the list (`HitContext::types`: the type of the row when a part keeps it, and
    the type of the conversion of a gem or an affix with a part above 0). The Boltcaster converts 99 percent: a light arrow has Thrust and
    Lightning, and passes DamageTypes Lightning (decision K6 of the research: a hit with a part of the type passes; the client reader of
    DamageTypes was not found).
  - InAction, InSequence: the hit is a hit of a cast of an ability of the list.
  - RequireReaction: the hit did damage above 0 (a decision of an agent).
  - DamageIsMelee: a melee contact; DamageIsRanged: a projectile. A hit of an area is neither (a decision of an agent).
  - HasGritActive, TargetHasGritActive: the backend has no grit; such a row does not pass (a project rule).
  - TargetVitalsCategory Player does not pass (the target of a hit is a creature); AttackerVitalsCategory must be Player.
  - EquipLoadCategory (stage 3.4f-B): a list of the load classes of the data (Fast, Normal, Slow, Overburdened); the class of the
    equip load of the character (`GearSummary::loadClass`: light, medium, heavy, overburdened). From stage 3.4f-C a fixed row (no
    event) with this condition counts only in its load classes too (finding CS-R6, [Gear and perks](gear-and-perks.md)).
  - MyStaminaPercent is the stamina of the server from stage 3.4f-B ([Stamina and dodge](stamina-and-dodge.md)); before it the
    stamina was always full (a rule with LessThan 100 never passed).
  - DontHaveStatusEffect: the attacker does not have the status.
  - TrackHitCount with NumberOfTrackedHits: the hits of one attack, compared with the limit (game text of Quick
    Recovery: "Max 5 reductions per attack").
  - A hit count (TrackHitCount, NumSuccessfulHits, NumConsecutiveHits) is a condition of the values before the damage too
    (`Passives::has_count_gate`, `counts_peek`: the same count on a copy, nothing spent). `Ability_Spear_BonusHvyDmgConsec`
    gives its BaseDamage only from the second heavy attack (code review of stage 3.3b-S5, finding F8; test case RV8).
    `after_hit` spends the count of the hit.
  - NumSuccessfulHits: the attack hit this number of targets; the rule fires one time for each attack (game text of
    Executioner's Speed: "Hitting at least 2 targets").
  - NumConsecutiveHits (with MaxConsecutiveHits, ResetConsecutiveOnSuccess): the hits of this number of attacks one after
    the other; a hit that the rule does not take ends the sequence.
  - An attack is one press of a basic attack, one cast of an ability, or one projectile (`HitContext::attack`,
    `passive_rules::kAttackPress`, `kAttackCast`, `kAttackShot`).
  - NumAroundMe (with NumAroundComparisonType): the creatures in the tracking spell of the rule (the spell row whose
    AbilityId is the rule and that has TrackNumInVolume: its radius, and its height or 3 m), only living creatures that are
    not on their way back. For a hit of an area whose spell is the tracking spell, the count is the creatures of that area
    (Crowded Well: the creatures in the vortex). Each spell row with the AbilityId of the rule and TrackNumInVolume is a
    tracking spell of that area: also the upgraded vortex of Unyielding (finding F4; test case RV4). A tracking spell for
    friends counts nothing: the backend has no friend relation in the world (a group of one). Game texts: Center of
    Attention 4 m (`Passive_Greataxe_Mauler_DmgWhenFoesNear`), Outnumbered 3 m.
  - A tracking spell of the shape Box (with a Length: Blood Lust, `Ultimate_Greataxe_Reaper`, 7 m long) is not applied:
    the backend counts the entities of a sphere only, and the list names the shape (finding F9; test case RV9).
- The effects:
  - Before the damage: BaseDamage (for each entity in the volume to MaxNumAroundMe, when the row has it), CritChance,
    CritDamage, ArmorPenetration, HeadshotDamage (a headshot), HitFromBehindDamage (a hit from behind). The potency of a perk
    multiplies each value.
  - A row with OnCrit and CritChance changes the roll of the hit (game text of Critical Condition: "+15% crit chance below
    30%"): its chance counts before the hit, and it has no effect after the hit. The CritDamage of a row with OnCrit joins
    the additive part of a critical hit only when the hit is critical (`HitBonus::critOnCrit`); with OnHeadShot, a headshot
    takes its HeadshotDamage instead (Vicious, `Global_Weapon_DmgCrit`; finding F3, test case RV3). The other OnCrit rows
    act after a critical hit (`Passive_Greataxe_Reaper_OnCritHeal`).
  - After the hit: SelfApplyStatusEffect (the status system, through `Gear::caster_effect`), OtherApplyStatusEffect (a rend,
    a damage over time, or a chain through the perk rules of the gear; each other status through the creature system with
    OtherApplyStatusEffectDurations), DmgPctToHealth (a life steal: the part of the damage of the hit, through `Health::heal` with the kind
    lifeSteal, thus with the outgoing and the received heal parts; [Combat and health](combat-and-health.md)), CooldownTimer with CDRImmediatelyOptions (ActiveWeapon: each ability of
    the bar of the weapon in the hand; AbilitySpecific: the ability of CooldownId; AllExcept: each other ability of the bar).
  - ActivationCooldown: the rule acts one time in that time, for each character (the key: the character id and the rule).
  - RemoveTargetStatusEffectsList (stage B2): the statuses of the list end on the target after the hit, of each source
    (`Creatures::remove_statuses`).
  - RemoveStatusEffects (stage B2): statuses of the attacker end after the hit (`Statuses::remove_rows`): the statuses of the list, or with
    the text AbilitySpecific the statuses of StatusEffectsList (Momentum of the rapier, `Rapier_BonusDMGAfterAbility`). A name that is not
    a status of the character ends nothing (`Global_Weapon_DodgeSuccess_*DMG` names its ability row `..._Tracker`, not the status: the
    game data; that tracker then ends at its time or at the next hit taken, its StopOnHitCount 1).
  - A status with no duration whose damage is a part of the damage of the hit (stage B2; a negative HealthModifierDamageBased with a
    DamageType, no rend, no CastSpell: the bolt `Status_Perk_Artifact_Set1_Bow_StaticDischarge`, and 12 rows of the set bonuses and of
    the infix perks): one damage of `-HealthModifierDamageBased x potency x the raw damage of the hit` of its type, through the one status
    path of a creature ([Creatures](creatures.md): its ApplicationCooldown on the target, its dispel, its icon). The damage base is the
    project rule of a damage-based status of a perk (research K5: the row has no ForceStatusEffectDamageTableRow). Before stage B2 the
    gear system gave the damage with an ApplicationCooldown for each attacker and no dispel.
  - A fixed stat: its CritChance and CritDamage count at each hit; its other values (PhysicalArmor, MaxHealth, the
    attributes) are in the gear values of the weapon in the hand (`Gear::values`).
- The weapon swap (stage B3, `Passives::weapon_changed`). The key of a weapon that changes the weapon slot in the hand (`Combat::change_weapon`)
  is the event OnWeaponSwap: the rows with OnWeaponSwap of the tree of the new weapon and of the perks, with the conditions of a rule with no
  hit (the values of the character, DontHaveStatusEffect, EquipLoadCategory, StatusEffect, StatusEffectCategories), the activation cooldown,
  and the effects SelfApplyStatusEffect, CooldownTimer with CDRImmediatelyOptions, and RemoveStatusEffects. Archer's Speed
  (`Passive_Bow_Skirmish_HasteOnSwap`: no timer, the haste; the haste gives its timer by OnStackStatusEffect, [Status effects](status-effects.md)).
  An equipment change that puts a different weapon into the hand (an equip, an unequip, a preset) is no swap [agent decision; research
  bow-build-2.md K2: the client does not test the bit of OnWeaponSwap, the event is server code]. Each change of the weapon in the hand (a swap
  and an equipment change) runs the tag removal actions of the data (`actionTagRemovals` of the ability data of format 7): a status of such an
  action ends when a tag of the weapon before goes (`Bow_Remove_StatusEffects_Sheathe`: the haste of Dodge and Weave ends when the bow leaves
  the hand; the end cause "the weapon of its tag left the hand"; client code 0x1E140A0 removes the same haste when the weapon in the hand is
  not a bow) [game data; client code].
- The end of a dodge (stage B3, `Passives::dodge_started`, `dodge_ended`; [Stamina and dodge](stamina-and-dodge.md): the dodge action). The
  dodge status actions of the data (`actionDodgeStatuses` of format 7: `Passive_Bow_Skirmish_HasteOnDodge` of `playeractions_ability_bow`)
  test at the start of the action the learned node, the tag of the weapon in the hand, the stamina before the payment as a part of the maximum
  (0.99 or more: "after dodging with full stamina"), and the statuses that the character must not have (of each source: no new haste while one
  runs); at the end of the action they give their statuses when the exit tags hold (the bow still in the hand), with the action as the source
  [game data; research bow-build-2.md Q1]. The rows with AfterAction (the dodge action of the stamina data) or AfterSequence (the fragment of
  the last dodge) and no event act one time at the end, with the conditions and the effects of a rule with no hit: Evasive Tactics
  (`Ultimate_Bow_Skirmish`: the empower `Status_Ultimate_Bow_Skirmish_DodgeDmg` for 5 s), the cooldown change after a dodge
  (`Global_Common_CDRonDodge`, `CDROnDodgePerk1` to `3`). The column Duration (the window after the action) is a mark: a rule with no event
  acts one time [agent decision: the reader of the column was not found].
- The time factor of a status on the character (stage B3, `Passives::duration_factor`; client getter 0x68308E0,
  `AbilityComponent::GetStatusEffectDurationMultipliers`, research bow-build-2.md Q1). A row with StatusEffectDurationReduction and no event
  is a time factor: for a status that its StatusEffect names or that has a category of its StatusEffectCategories, and whose own conditions
  pass, the value times the potency of the rule joins one sum (the client sums the parts). The status system applies `time x (1 + sum)`, then
  DurationMax ([Status effects](status-effects.md)). The new status counts as present for the condition StatusEffectCategories (else the first
  haste of a character with no haste is not longer; the text of each perk says that each haste is longer) [agent decision with the research
  recommendation; the use of the sum and the order are server code]. StatusEffectDurationCats is a mark: the getter does not read it. A time
  factor that names no status and no category (`Ability_Perk_Amulet_DurEmpower`, `Global_Amulet_DurEmpower`) matches no status and is not
  applied, with the cause. The outgoing part (TargetStatusEffectDurationCats, TargetStatusEffectDurationMult: "haste you apply" of Alacrity II,
  Battle Precision) is a different factor (stage B4, below): it acts only for a status that the character gives a different entity
  [agent decision: the outgoing part of Alacrity II on a haste that the character gives itself is open, research K1; applying both would give
  two times the text]. The outgoing part has a consumer only for a row with no event (`Passives::outgoing_factor`): a row with an event and
  these columns (`Global_Weapon_HercyneRaid_1`: OnHit, the DoT time of the target) is not applied for that part; with no other effect the
  list names the whole row as not applied, else its note names the part (code review of stage B4, finding B4-R5, test case BZ5).
- The giver of a status of a spell area (code review of stage B4, finding B4-R3, test case BZ3): the source of the status is the spell entity
  (its exit and its end need it), and the outgoing time factor is the factor of its caster (`SpellBody::caster`): Clear Out with Power Cleaner
  gives a friend 4.4 s of fortify with Foc_Bonus_200_1 (1.1); the caster gets no outgoing factor on itself.
- The sources of one row (code review of stage B3, finding B3-R2; stage B4, `Passives::merged`): the perks of each item with the same ability, a
  node and a status that equips the row, give one rule. The potency of the rule is the sum of the potencies of its sources, the largest
  first, to IsStackableMax sources (0: no limit); with IsStackableDiminishingModifier each next source counts that part less, and the count
  stops at a factor of 0 or less [client code: the potency 0x67E9A70, IsStackableMax at +0x944, IsStackableDiminishingModifier at +0x948;
  the order of the largest potencies first is the order of the gear values, an agent decision]. The time factors, the outgoing time factor,
  the reduction of a creature hit, the cooldown factors, the consumable rules, and the rules of a weapon swap and of a dodge use it. Example:
  Alacrity II (IsStackableMax 5) on six items counts five sources (test case BY2).
- A status while entities are near (`Passives::tick`, each 500 ms: `passive_rules::kAroundStep`, a decision of this
  project, because the action data starts the tracking spell and has no time). The status comes with no end while the
  condition holds, with the rule as its source, and it ends when the condition does not hold and when the character leaves
  the world (`Passives::left`). Only the characters with such a rule of the weapon in the hand look at the position index.
- A hit taken (`Passives::hit_taken`, after the damage of a creature): the rows with OnHitTaken and SelfApplyStatusEffect of
  the tree of the weapon in the hand and of the perks, with the hit counts (`Passive_WarHammer_Juggernaut_DmgAfterTakingDmg`).
  Its conditions: the health of the character after the hit, its stamina, its mana, DontHaveStatusEffect, RequireReaction
  (the damage above 0), and grit (none). A row with a different condition is not applied, with the column in the list
  (finding F6: `Global_Amulet_FortifyOnLow` gives its fortify only below 50 percent health; test case RV6). Stage 3.4f-B: Stamina
  Recovery (`Global_Amulet_StamOnLow`: below 50 percent health and below 100 percent stamina, 20 stamina) passes with the
  stamina of the server (test case SM4).
- A hit-taken action of the data (stage 3.4f-B; `actionHitStatuses` of `ability-data.json` format 6): an action of the player lists
  that applies statuses at a hit that the character takes (IfDamagedByPowerLevel) with tests of a learned node, the tags of the weapon,
  the health and the stamina as a part of the maximum, and statuses that the character must not have. Mauler's Stamina
  (`Passive_Greataxe_Mauler_StamVsLowHp`, the action `GreatAxe_Passive_StamWhenLowHP_Detect`): below 50 percent health and below 100
  percent stamina with the great axe, 40 stamina and a timer of 20 s (`Passives::hit_taken`; test case SM4). An action with an other
  activity (the HealingAura action of the life staff casts a spell) is not exported, with its cause (`actionHitStatusesNotExported`).
  The test IfDamagedByPowerLevel with FailOnNoReactionDamage needs a hit with damage above 0 (a decision of an agent).
- A hit in the immune window of a dodge (stage 3.4f-B, `Passives::hit_evaded`, [Stamina and dodge](stamina-and-dodge.md)): the rows
  with OnHitTakenWhileInvulnerable and SelfApplyStatusEffect of the tree of the weapon in the hand and of the perks. Its conditions:
  the values of the character, DontHaveStatusEffect, RequireReaction (the damage that the hit would do above 0), EquipLoadCategory,
  the activation cooldown, InAction (the action Dodge of the stamina data) and InSequence (the fragment of the dodge). A row of a hit
  between players (AttackerVitalsCategory or TargetVitalsCategory Player) does not pass: the attacker is a creature. Shirking
  Fortification (`Global_Armor_DodgeSuccess_Fortify`): the fortify of the armor perks (test case SM4).
- A heal (`Passives::heal_bonus`, stage 3.3b-S6): the rows with OnHealed and HealScalingValueMultiplier of the healer (the
  tree of the weapon of the heal and the perks) add their value to the outgoing heal part. Its conditions: TargetHealthPercent
  (the health of the target before the heal), the health, stamina, and mana of the healer, DamageTableRow (the row of the
  spell of the heal), TargetStatusEffectCategory, DontHaveStatusEffect, and grit (none). Divine Blessing
  (`Ultimate_Lifestaff_Healing`): +30 percent for a target at 50 percent health or less (test cases HE4, HE10).
- A rule of an action: a node with no cell of a rule whose node the action data tests (IfAbilityActive). The plan of the
  ability applies it when the backend has the ability ([Abilities](abilities.md): the spell selections of the action data).
  An active ability with no plan in this backend, and each node of such an ability, is "not applied yet": its weapon part
  comes later.
- The values of the evaluator are values of the character with the character id as their key (rule MP-1): the activation
  cooldowns, the hits of each attack, the consecutive hits, the statuses near. A session reads none of them. The status of
  a target goes to each observer through the normal path of the creature system (rule MP-4; test case PS5 with two
  sessions).
- The cost: a hit looks at the learned nodes of one weapon tree (40 at most) and at the perks of one character (55 at
  most), not at each row of the data. The rules near a character look at the position index in the radius of their
  tracking spell. The hits of an attack go 10 s after its last hit (`passive_rules::kAttackKeep`). Test case PS6: 50
  characters with their rules in a world of 4,200 creatures, with a time limit.

- A spell of a rule (stage 3.3b-M2): the cell CastSpell is an effect after a hit (`Passives::apply_effects`): the abilities
  system makes the spell at the character (`Abilities::rule_spell`; [Abilities](abilities.md)). Power Cleaner
  (`Upgrade_WarHammer_ClearOut_FortifyAOE`) casts `WarHammer_Spell_ClearOut_Fortify` at a hit of Clear Out: the fortify of
  the friends in 6 m (live run of stage 3.3b-M2: `WarHammer_ClearOut_FortifyBuff` for 4 s). DisableCastSpellDurability is a
  mark (the backend has no durability use).
- InSequence and InAction (stage 3.3b-M2): a hit passes when the list names the ability of its cast or the fragment of its
  strike (`HitContext::fragment`). The row of Armor Breaker names the fragment of its action (InSequence
  `Ability_WarHammer_BlockBreaker`). A row with no event, with InAction or InSequence, a value (ArmorPenetration), and no status
  gives its values at each hit of its ability (`ability_value_rule`: Armor Breaker ignores 35 % of the armor, the game text); a
  row with a status and no event stays a rule of its action (Shockwave: the plan gives its stun). Source: game data and game
  text; a decision of an agent for the form of the rule.
- The box of a tracking spell (stage 3.3b-M2, `tracking_supported`): Blood Lust (`Ultimate_Greataxe_Reaper`, tracking spell
  `Spell_Ultimate_Greataxe_Reaper`, ShapeTypes Box, Length 7, Radius 1.25, Height 5). The backend counts the creatures from the
  character forward to 2 times the Length (the game text "when looking at a foe within 14 m"), the Radius to each side, and the
  Height up and down, in front of the look of the character (`Entity::look`: the yaw of the camera of the last accepted
  position report). A decision of an agent from the game text (code review of stage 3.3b-S5, finding F9; test case RV9).
- The status actions of the action lists (stage 3.3b-M2; `actionStatuses` of `ability-data.json` format 5): an action that
  gives statuses while its condition holds. Guarded Sprint (`Passive_WarHammer_CrowdCrusher_RangeAbsorbSprint`): the action
  `Warhammer_Passve_SprintDmgRed` of `playeractions_ability_warhammer` gives `WarHammer_Pass_SprintDmgReduction` (10 percent
  absorption) while the character has the node, moves (the tags ForwardNav, BackNav, LeftNav, RightNav: `Entity::moving`), does
  not dodge (the tag Dodging: from stage 3.4f-B a dodge of the stamina system plays, from its start to its mark CanTransition), and
  has the war hammer in the hand (the tag Warhammer of the MannequinTag of the weapon). The world tick gives the status and takes it
  when the condition does not hold (`Passives::tick`; test case GH13). The start and the mark CanTransition of a dodge make the
  evaluator look at the character in its next tick (`Passives::recheck`; test case SM5).
- The list names the uses of a node in the data of the actions (stage 3.3b-M2, `describe_row`): a row of a strike, a transition
  inside an ability, a fragment that an action selects, or a spell cast of the weapon actions of format 2 (applied by the data);
  a grit of an action (ActivateGrit: not applied, the backend has no grit); a conditional of the movement of an action (applied
  by the client: the position check accepts its speed); a status action; a status that an area of a spell slice gives (the
  fortify in the well of Unyielding: [Spell entities](spells.md)).

### The rules of the great axe and the war hammer (stage 3.3b-M2)

The list of the backend (`tools/world passives --table greataxe`, `--table warhammer`; the run `m2-list` of stage 3.3b-M2)
names each node with what the backend does with it. The great axe: 33 of 38 nodes applied (with Unyielding), 34 from stage
3.4f-B (Mauler's Stamina). The war hammer: 34 of 38 nodes applied. The nodes that are not applied, and the cause (grit and the
block are not in these stages):

| Node | Cause |
| --- | --- |
| `Upgrade_Greataxe_Executioner_AddGrit`, `Upgrade_WarHammer_ArmBreak_Grit` | Grit: the action ActivateGrit of the fragment. The backend has no grit. |
| `Passive_Greataxe_Mauler_HeavyAddGrit`, `Passive_WarHammer_Juggernaut_HeavyGrit` | The backend has no grit. |
| `Passive_Greataxe_Reaper_DmgWhenBlocked`, `Passive_Greataxe_Mauler_DmgOnBlock`, `Passive_WarHammer_CrowdCrusher_LessStaminaOnBlock`, `Upgrade_WarHammer_Gavel_StaminaDmg` | No rule in the backend for a block (OnBlockedHit, OnBlockedHitTaken, BlockDamage, BlockDamageReduction). |
| `Passive_Greataxe_Mauler_StamVsLowHp` | Applied from stage 3.4f-B: the hit-taken action of the data gives its stamina through the stamina path. |

The nodes of stage 3.3b-M2 and how they act: The Collector (`Upgrade_Greataxe_Reap_RngInc`: the rows and the shapes of the
strikes of Reap), Fatal Attraction (`Upgrade_Greataxe_Reap_SpingAttack`: the fragment of Reap), Unending Winds
(`Upgrade_Greataxe_Whirlwind_MoreSpins`: the fragment of Whirlwind and its exits), Gusting Winds (`Upgrade_Greataxe_Whirlwind_Speed`:
the movement of the client), Frenzied Momentum and Unpredictable Strike (`Upgrade_Greataxe_Charge_DmgPerDistance`,
`_SwingAttack`: the transitions of Charge), Absorb (`Upgrade_Greataxe_Maelstorm_AbsorbProjectiles`: the shield of the action),
Gravity (`Passive_Greataxe_Mauler_PullDur`: the rows `_RP2` and their root), Mauler's Pull
(`Passive_Greataxe_Mauler_HeavyAddPull`: the strike of the heavy attack), Unyielding
(`Upgrade_Greataxe_GravityWell_ArmorInWell`: the area of the vortex), Power Cleaner (CastSpell), Guarded Sprint (the status
action), Blood Lust (the box), and the rows of the six abilities (Execute, Armor Breaker, and the upgrades with OnHit).

### The rules of the life staff (stage 3.3b-S6)

`tools/world passives --table lifemagic`: 11 of 38 nodes applied. Applied: Divine Embrace, Sacred Ground, and the Beacon
(their plans), Privilege (`Upgrade_Lifestaff_DivineEmbrace_ReduceCost`), Holy Ground and Anointed (the spell selections of
Sacred Ground), the two Beacon upgrades with a spell selection, Divine Blessing (OnHealed), and the two OnHit rules
`Passive_Lifestaff_Healing_HealingBuffonBasic` and `Passive_Lifestaff_Protection_FortifyOnBasic`. Not applied, and the cause:

| Node | Cause |
| --- | --- |
| `Ability_Lifestaff_SplashofLight`, `Ability_Lifestaff_OrbofProtection`, `Ability_Lifestaff_LightsEmbrace`, and the upgrades of these abilities | Not applied yet: the active ability has no plan in this backend. |
| `Upgrade_Lifestaff_DivineEmbrace_ChainOnLow`, `Upgrade_Lifestaff_DivineEmbrace_ChainOnLow2`, `Passive_Lifestaff_Healing_CDROnLow` | An effect after a heal (OnHealed with a status or a cooldown change). |
| `Passive_Lifestaff_Healing_NoManaOnBasic`, `Passive_Lifestaff_Healing_RemoveOnHeavy`, `Passive_Lifestaff_Healing_HealOnLight` | No rule in the backend for OnProjPassedThrough (a projectile that passes through a friend), Mana, or the removal of a status. |
| `Passive_Lifestaff_Healing_AoeHealWhenHit`, `Passive_Lifestaff_Protection_HasteWhenLow` | The action data applies it in an action of the client that this backend does not have. |
| `Passive_Lifestaff_Healing_GroupHealing`, `Passive_Lifestaff_Protection_GroupMana` | No rule in the backend for DisableCastSpellDurability and CastSpell. |
| The other passives of the Protection tree | No rule in the backend for their columns (Duration, AfterAction, StatusEffectCategories, the status durations, OnBlockedHitTaken, PerStatusEffectOnTarget). |

The special rules of the other weapons (Berserk, the channels, the stacks of the rapier and the spear, the stances of
the greatsword) are not in this stage: their rows show in the list with the cause.

### The rules of the stamina and the dodge (stage 3.4f-B)

The list of the backend (`tools/world passives --table stamina`; the final run of stage 3.4f-B with the files of `dist` and of the
common game data folder) names each row of the data (a node or a perk) with a part of the stamina or of the dodge: a column with
"Stamina", OnHitTakenWhileInvulnerable, EquipLoadCategory, or a text that names a dodge. 115 rows, 45 applied. The stamina model of
this stage gives the source of each applied row; the rows that are not applied need a different system, and the list names the cause.
From stage B2 the list has 61 of the 115 rows applied (the run `b2-live2` of stage B2 with the game data of the stage): the 12 elemental
weapon perks, `Ability_Spear_CDROnHitAfterDodge`, `GlobalPerk_Artifact_Set2_MediumLegs`, `Helper_Passive_Greatsword_Off_8`, and
`Passive_Dagger_Blade_LightFinish_Helper_Rend` came with the conditions and the effects of stage B2 (the rows of the table below). From
stage B3 the list has 67 of the 115 rows applied (the final run of stage B3 with the files of `dist` and of the common game data folder): the
six rows after the end of a dodge of the table below.

| Rows | What the backend does |
| --- | --- |
| 20 rows with OnHitTakenWhileInvulnerable and a status on the character: the fortify of the armor perks (`Global_Armor_DodgeSuccess_Fortify`, Shirking Fortification), the stamina of the legs perks (`Global_Legs_DodgeSuccess_Stamina`, Fast load), the trackers of the elemental weapon perks (`Global_Weapon_DodgeSuccess_*_Tracker`), `AbilityID_Catacombs_AddShirkingBlessing`, `Dex_Bonus_250_1`, `GlobalPerk_Artifact_Set1_LightFeet`, `Global_Conqueror_Longsword_ShieldlessAgility_SuccessfulDodge`, and the `Ability_Perk_*` forms of the same perks | Applied (`Passives::hit_evaded`): a creature hit in the immune window of a dodge gives the status. |
| `Global_Amulet_StamOnLow`, `Ability_Perk_Amulet_StamOnLow` (Stamina Recovery) | Applied: OnHitTaken below 50 percent health and 100 percent stamina, 20 stamina. |
| `Global_Ring_DmgLowStam` (Gambled Energy), `StaminaNotFullDamagePerk1` to `4`, `Rapier_BonusDmgLowStamina`, `Ability_Spear_BonusDmgMaxStamina`, `Dex_Bonus_100_1` | Applied: the BaseDamage part with MyStaminaPercent of the stamina of the server. |
| `Runeglass_Just_Stamina_Armor`, `Runeglass_Just_Stamina_Weapon`, `Runeglass_ThreatAdd_Stamina`, `Runeglass_ThreatSub_Stamina` | Applied: the cell Stamina of a hit rule (1 or 4 stamina at a hit with damage, one time in 3 s) through the stamina path. |
| `Ability_Spear_ReducedDodgeStamCost`, `Passive_Hatchet_ThrowingAxe_AbilityDodgeStamina` | Applied as a status on the character (OnHit); the conditional cost of the dodge that the status enables (the cost times 0.8 or 0.75) is in `costsNotApplied` of the stamina data: the stamina system does not apply it yet (the spear and the hatchet have no abilities in this backend). |
| `Passive_Greataxe_Mauler_StamVsLowHp` (not one of the 115 rows: its row has no stamina cell) | Applied: the hit-taken action of the data gives its stamina. |
| The other applied rows of the list (`AbilityTable_SetBonus_Infix_SecondWind_Gloves`, `GlobalPerk_Artifact_Set2_Warhammer2`, `GlobalPerk_Artifact_Set2_HeavyLegs`, `Global_Conqueror_MeleeWeapon_DodgeSuppressant`, `Heartgem_Rune_SpinningSwords_*`, `IceTempestPass3`, `Passive_Dagger_Blade_LightFinish_Weaken`, `Runeglass_Just_ElementalAdd`) | Applied by the rules of the earlier stages (a text of the row names a dodge or a stamina status; the row has no stamina condition). |
| 5 rows of a hit between players in a dodge (`Global_PvP_Amulet_DodgeSuccess_DMG`, `Global_PvP_Armor_DodgeSuccess_Heal`, `Global_Conqueror_DodgeCorruptionBall`, and two `Ability_Perk_PvP_*`) | Not applied: the attacker of a hit in a dodge is a creature. |
| 12 elemental weapon perks (`Global_Weapon_DodgeSuccess_*DMG`, `Ability_Perk_Weapon_DodgeSuccess_*DMG`) | Applied from stage B2: the tracker status equips the row (EquipAbility); the next hit gives the damage of its forced row (Shirking Shock II: 0.236 x potency of the base weapon damage as Lightning) and ends the tracker (RemoveStatusEffects). Before stage B2: not applied. |
| 3 cleanse amulets (`Global_Amulet_DodgeSuccess_CleanseDoT`, `_CleanseDebilitate`, `Ability_Perk_Amulet_DodgeSuccess_Cleanse`), `GlobalPerk_Artifact_Set2_HeavyChest` | Not applied: NumStatusEffectsToRemove and StatusEffectCategoriesList (the removal of a number of statuses of a category) are not in the backend. |
| `Ability_Spear_CDROnHitAfterDodge` | Applied from stage B2 (StatusEffect, RemoveStatusEffects); the spear has no ability in this backend yet. |
| 3 block perks (`Global_Chest_OnBlockedHitTaken_StaminaRegen`, its `Ability_Perk_*` form, `Passive_Flail_Eldritch_Passive2`), `AbilityID_Catacombs_Revitalize_Ability` | Not applied: the backend has no block (OnBlockedHitTaken). |
| 22 rows after a dodge (`CDROnDodgePerk1` to `3`, `Global_Common_CDRonDodge`, `Global_Earring_ThreatSubOnDodge`, the 10 `Mut_Curse1_*` rows, `Passive_Lifestaff_Protection_HealingOnDodge`, `Passive_Musket_Trapper_*`, `Passive_VoidGauntlet_HealOnDodgeMaxMana`, `Passive_Greatsword_Off_8`, `Ultimate_Bow_Skirmish`, `AbilityTable_SetBonus_Infix_Stifling_Legs`) | From stage B3: applied at the end of a dodge (`CDROnDodgePerk1` to `3`, `Global_Common_CDRonDodge`, `Passive_Lifestaff_Protection_HealingOnDodge`, `Ultimate_Bow_Skirmish`). Not applied, with the cause: ModifySelfApplyStatusEffectDuration and ThreatDamage (`Global_Earring_ThreatSubOnDodge`), OnEquipStatusEffect (the `Mut_Curse1_*` rows), ModifySelfApplyStatusEffectDuration (`Passive_Musket_Trapper_HasteOnDodge`), DisableApplyPerStatusEffectStack (`Passive_VoidGauntlet_HealOnDodgeMaxMana`), IsInCombatState (`Passive_Greatsword_Off_8`), a CastSpell after a dodge (`AbilityTable_SetBonus_Infix_Stifling_Legs`), and an action of the client that the backend does not have (`Passive_Musket_Trapper_DodgeReload`: its reload). Before stage B3 none was applied. |
| 13 rows of an equipped ability or a trigger (`RequiredEquippedAbilityId`, `AbilityTrigger`, `AbilityList`: the helpers of the flail, the sword, the dagger, and the greatsword, the greatsword counter, `Passive_Flail_Crusader_Passive4`) | From stage B2 RequiredEquippedAbilityId is a condition (the helper rows of a node): `Helper_Passive_Greatsword_Off_8` and `Passive_Dagger_Blade_LightFinish_Helper_Rend` are applied. The others keep a cause: AbilityTrigger, AbilityList, HealthDamageMitigation, ThreatDamage, or an ability with no plan. |
| 5 rows of an ability with no plan (`Ability_Greatsword_DrainAttack`, `Ability_Spear_Cyclone_BonusStaminaOnMultiHit`, `Rapier_Evasion_Mod2`, `_Mod4`, `Upgrade_Sword_WhirlingBlade_Rend`) | Not applied yet: the weapon part of their ability comes later. |
| `Global_Conqueror_Longsword_ShieldlessAgility_HitStaminaCostTracker`, `Global_Conqueror_Musket_LimbDebilitation_Legshot`, `Passive_Firestaff_Pyro_ManaOnblock` | Not applied: OnEquipStatusEffect, OnLegShot, UseDefenderWeaponOnApplyOtherSE are not in the evaluator. `GlobalPerk_Artifact_Set2_MediumLegs` is applied from stage B2 (DamageTypes). |

Grit and the block stay out of this stage (the roadmap): `Passive_Greataxe_Mauler_HeavyAddGrit`, `Passive_WarHammer_Juggernaut_HeavyGrit`,
and the block rows of the table of the great axe and the war hammer above keep their cause.

### The rows of the bow build with no event (stage B0)

The list must name each row that the evaluator does not apply (no silent rule). A row with no event that is not a rule of a hit
(no DamageTableRow, AttackType, or RemoteDamageTableRow), not the values of an ability, and not a rule of the entities near
(`describe_row`, step 4; test case BP9):

| Row | What the backend does |
| --- | --- |
| `Passive_Bow_Hunter_DmgVsLow` (Finishing Shot: TargetHealthPercent 50, BaseDamage 0.15) and the other node rows with a condition of a target and no event | Not applied: "the condition TargetHealthPercent with no event is not a rule of a hit in the evaluator". The evaluator takes such a row only as a rule of a hit (`before_hit`), and the gear values take a node row only with no condition (`Passives::fixed`). A perk row with such a condition keeps the rule of the earlier stages. |
| `Global_Common_CDR` (Refreshing, a perk of the bow build: CooldownTimer -0.02 with CDRImmediatelyOptions) and the other rows with no event and the effect CooldownTimer, CDRImmediatelyOptions, DmgPctToHealth, CastSpell, Stamina, or HealScalingValueMultiplier | Not applied: "the effect ... of a row with no event (the gear values take only the values of a fixed stat)". |

A condition EquipLoadCategory does not stop a row (the gear values apply it). Stage B4 applies the two rules (the section of stage B4
below): Finishing Shot is a rule of each hit (`target_value_rule`), and Refreshing is a fixed cooldown rule (`fixed_cooldown_rule`).

### The rules of the bow abilities (stage B1)

- The volley count of NumConsecutiveHits (Rapid Accuracy) [decision D11 of the orchestrator; build sheet Q3]. A hit with a volley
  (`HitContext::volley`: the cast number of the arrows of a bow ability) counts for the rule, the volley, and the target together
  (`volleyHits`): Rapid Accuracy (NumConsecutiveHits 3) fires when three arrows of one cast hit one target. A hit of a different
  cast, of a different target, or a miss does not add to the count, and a volley hit does not end the count of a different volley
  (no reset of the consecutive count). [Static proof: the hit test of the client `0x67A18F0` needs the target of the hit data for a
  row with NumConsecutiveHits.] The count of a volley ends after the time of the rule (the sweep), and with ResetConsecutiveOnSuccess
  at its success. IgnoreResetConsecutiveOnDeath is a mark (the death of the target does not end the count; the count is per target).
- InAction (stage B1): a hit passes also when the list names the ability action of its cast (`HitContext::action`: `Bow_RapidShot`,
  the actions of the projectile data). Final Blow (InAction `Bow_RapidShot`, DamageTableRow `Bow_RapidAttack_KnockBack`) and Rapid
  Accuracy use it.
- The forced damage row of a status (Flame Arrow) [game data]. A rule with ForceStatusEffectDamageTableRow gives its
  OtherApplyStatusEffect to the target with the damage of a tick: the HealthModifierDamageBased of the status row times the potency
  times the damage of the row of StatusEffectDamageTableRowOverride (`Common_DamageOverTime_Override`, DmgCoef 1) with the weapon
  and the gear score of the hit (`Combat::damage`), of the damage type of the status row. The creature status system runs the
  ticks ([Creatures](creatures.md)). Log line: `world: rule R gives S to creature X: N TYPE damage each tick`. Only a hit of the
  DamageTableRow of the rule gives it (Flame Arrow: the direct row of Explosive Arrow, not the explosion).
- DamageTableRowOverride (Blood Soaked Arrow) is an effect of the plan of the ability (`Abilities::resolve_volley` replaces the row of
  the clip): the list shows it as applied.
- The list of the passive rules (`describe_row`): a node that a cast of an ability action selects (Contained Detonation: the cast of
  `Spell_Ability_Bow_Explosive_Upgrade` with the node) is applied, with the note of the cast that needs the node; the projectile, the
  action, and the fragment names of a planned bow ability are names of a planned ability (the node rows of Rapid Shot are rules of
  its arrows).

### The rules of the Boltcaster and of the bow build (stage B2)

The rows of the game data (research `bow-build.md` section 4.3, `bow-build-2.md` Q4) and what the backend does (`tools/world passives
NAME` with the preset `bow-boltcaster`: each row below is "applied"; live checks B2-1 to B2-4 of stage B2):

| Row | What the backend does |
| --- | --- |
| `GlobalPerk_Artifact_Set1_Bow` (Electrified: OnHit, AttackType Light,Heavy, DamageTypes Lightning, BaseDamage 0.2) | +0.2 in the damage part of a light or heavy hit with a Lightning part (the conversion of 99 percent of its affix). An ability hit does not get it. |
| `GlobalPerk_Artifact_Set1_BowPerk` (Bolt: OnHit, RequireReaction, AttackType Light,Heavy, OtherApplyStatusEffect the charge) | One stack of `Status_Perk_Artifact_Set1_Bow_StaticCharge` (3 s, StackMax 3, RefreshDuration, the icon with its count at the name plate) for each light or heavy hit. An ability hit adds none. |
| `GlobalPerk_Artifact_Set1_BowPerk2` (the bolt: 21 bow rows, DamageTypes Lightning, TargetStatusEffect the charge, TargetStatusEffectStackSize 3 Equal, the bolt and the removal, RemoveTargetStatusEffectsList the charge, ActivationCooldown 1) | A hit of a row of the list with a Lightning part on a target with 3 charges before the hit: the bolt (0.8 x the raw damage of the hit, Lightning, one number), the end of the charges, and the removal of 0.2 s (it refuses the charge of the same hit: the 4th hit is the bolt, research K4). One bolt in 3 s for each target (ApplicationCooldown of the bolt status: a later rule in the 3 s takes the charges and gives no damage), one trigger in 1 s for each attacker. The ability hits of the list (Rapid Shot, Penetrating Shot, Explosive Arrow) give the bolt too (live check B2-2). |
| `Ability_Perk_Weapon_DodgeSuccess_LightningDMG_Tracker` and the equipped row `Ability_Perk_Weapon_DodgeSuccess_LightningDMG` (Shirking Shock II) | A creature hit in the immune window of a dodge gives the tracker (10 s, the potency of the perk: 1.274 at 800). The next hit with damage gives one tick of 0.236 x potency x the base weapon damage (`Common_DamageOverTime_Override`, DmgCoef 1, the weapon with the attributes and the level) as Lightning, and the tracker ends. ActivationCooldown 0.5. Live: 713.6 = 0.236 x 1.274 x 2,373.9. |
| `Passive_Bow_Hunter_DmgOnFirst` (Surprise Attack) and its helper `Passive_Bow_Hunter_DmgOnFirst_Tracker` | +0.2 (the data; the text says 15 percent: a literal number of an older text; decision of the orchestrator) on a hit of a target with no tracker of Surprise Attack of any source (decision D10); the tracker (10 s) goes on the target and the self tracker (15 s) on the attacker. The helper renews the tracker of the target at each later hit of an attacker with the self tracker. A second character gets no bonus while the tracker of the first is on the target. |
| `Rapier_BonusDMGAfterAbility` (Momentum) | Applied by the evaluator (StatusEffect, RemoveStatusEffects AbilitySpecific): +0.25 on a light or heavy hit while the character has the status, then the status ends. The status comes from an action of a rapier ability, which has no plan in this backend: only a command gives it now. |

### The haste chain of the bow build (stage B3)

The rows of the game data (research `bow-build.md` section 4, rows 24, 26 to 28, 36, 44, 45; `bow-build-2.md` Q1) and what the backend does
(`tools/world passives NAME` with the preset `bow-boltcaster`: each row below is "applied"; live checks B3-1 to B3-4 of stage B3):

| Row | What the backend does |
| --- | --- |
| `Passive_Bow_Skirmish_DodgeHaste` (Dodge and Weave: only WeaponTag; the action `Passive_Bow_Skirmish_HasteOnDodge` of the data) | At the end of a dodge that started with the node, the bow in the hand, 99 percent stamina or more, and no haste of Dodge and Weave: `Status_Passive_Bow_Skirmish_DodgeHaste` (2 s, haste 0.07; 3.24 s with Speedy and Alacrity II), when the bow is still in the hand. It ends when the bow leaves the hand. |
| `Passive_Bow_Skirmish_HasteOnSwap` (Archer's Speed: OnWeaponSwap, DontHaveStatusEffect the timer) | The key of the bow: `Status_Passive_Bow_Skirmish_HasteOnSwap` (5 s, haste 0.07; 8.1 s with Speedy and Alacrity II) and, by OnStackStatusEffect at its first stack, the timer (10 s). No haste while the timer runs. |
| `GlobalPerk_Artifact_Set1_MediumFeet` (Speedy: StatusEffectDurationReduction 0.5, StatusEffectCategories Haste) | +0.5 in the time factor of each haste on the character. |
| `GlobalPerk_Artifact_Set1_MediumFeet_2` (Speedy: OnHit, StatusEffectCategories Haste, BaseDamage 0.1) | +0.1 in the damage part of a hit while the character has a haste (live: 1.49 to 1.59 of the core of a hip shot). |
| `Ability_Perk_Amulet_DurHaste` (Alacrity II: StatusEffectDurationReduction 0.094, the outgoing part) | +0.094 x potency (1.274 at 800) in the time factor of each haste on the character. Stage B4: the outgoing part acts for a haste that the character gives a different entity (no ability of the build gives one). |
| `Passive_Bow_Skirmish_CDRVsLow` (Closing In: OnHit, TargetHealthPercent 50, Duration -1, CooldownTimer -0.05 ActiveWeapon) | A hit on a target at 50 percent health or less: the cooldowns of the abilities of the weapon in the hand -5 percent of their time. Duration is a mark. Before stage B3 the column Duration stopped the row. |
| `Ultimate_Bow_Skirmish` (Evasive Tactics, PvP tree: AfterAction Dodge, AfterSequence the dodge fragments, Duration 2) | At the end of a dodge: `Status_Ultimate_Bow_Skirmish_DodgeDmg` (empower 0.15 for 5 s). |
| `Passive_Bow_Skirmish_DebuffDur` (Battle Precision, PvP tree: TargetStatusEffectDurationCats, TargetStatusEffectDurationMult) | Applied from stage B4 (the outgoing time factor, the section below). |

### The other rules of the bow build (stage B4)

The rows of the game data (research `bow-build.md` section 4, `bow-build-2.md` Q5 to Q8, K9, K13) and what the backend does
(`tools/world passives NAME` with the preset `bow-boltcaster`: each row below is "applied"; live checks L1 and B4-1 to B4-6 of stage B4):

| Row | What the backend does |
| --- | --- |
| The threshold abilities of the attributes (`attributes.abilities` of `progression-tables.json`: `Dex_Bonus_25_1`, `Dex_Bonus_50_1`, `Dex_Bonus_150_1`, `Str_Bonus_25_1`, `Con_Bonus_100_1`, `Foc_Bonus_25_1`, and the others) | Rules of the character (`RuleOrigin::attribute`, `Passives::attribute_rules`): each threshold that the value of the attribute with its bonus reaches [client: the attribute screen tests the base value plus the bonus, `attributes.lua`]. A row with no event and no condition is a fixed stat of the gear values with no mana part (the progress system has the mana parts of the thresholds since stage 3.5b.2). The other rows go to the evaluator as the rows of a node. A new bonus (a food, a perk) makes the values again. A row with IsNonCombatAbility is a row of the gathering system (the list names it). |
| `Passive_Bow_Hunter_DmgVsDist` (Long Range: OnHit, DistFromDefender 10, DistComparisonType GreaterThanOrEqual, BaseDamage 0.1) | +0.1 in the damage part of a hit at 10 m or more. The distance: 3D, from the place of the attacker when the server takes the hit to the target [client hit test 0x67A18F0: the squared distance from the position +0x230 of the hit data, the loader squares the value: metres; the origin of +0x230 is not proved (research K9): an agent decision]. Live check B4-6: 9.915 m gave the raw damage 4,368.0, 10.103 m 4,616.1 (+0.1 of the core 2,480.9). |
| `Passive_Bow_Hunter_DmgVsLow` (Finishing Shot: TargetHealthPercent 50, BaseDamage 0.15, no event) | A rule of each hit (`target_value_rule`): a row with no event, a condition of the target (TargetHealthPercent, TargetStatusEffect, TargetStatusEffectCategory, DistFromDefender), only values of a hit, and no other effect [client: the hit test 0x67A18F0 tests the conditions of the target of such a row with no event]. +0.15 on a target at 50 percent health or less before the hit. |
| `Passive_Bow_Skirmish_DebuffDur` (Battle Precision, PvP tree: TargetStatusEffectDurationCats Debilitate,DoT, TargetStatusEffectDurationMult 0.2, TargetStatusEffectCategory Debuff, RequireReaction), `Dex_Bonus_150_1` (TargetStatusEffectDurationCats Dot, 0.05), the outgoing part of Alacrity II | The outgoing time factor (`Passives::outgoing_factor`): a status that the character gives a different entity (a creature or a different player) with a category of the list lasts `time x (1 + sum)`, then DurationMax. A condition of the target status counts the new status as present. The creature system (`Creatures::give_status`), the gear system (a rend and a damage over time of a perk), and the status system (a status of a player on a different player) use it. A status of the character on itself has no outgoing factor. [The client has no reader of the columns (research Q1): the sum and its use are agent decisions with the form of the incoming factor 0x68308E0.] |
| `Global_Armor_DefBasic` (Enchanted Ward: OnHitTaken, RequireReaction, AttackType Light,Heavy, BaseDamageReduction 0.01) and `Ability_Perk_Armor_DefBasic` (Enchanted Ward II: 0.016) | The reduction of a creature hit (`Passives::taken_reduction`) before its damage: each row with OnHitTaken and BaseDamageReduction whose AttackType has the attack type of the damage row of the creature attack (the creature data of stage B4; research Q5: the client reads the type of the damage row), x potency, the sources of each row with their cap (IsStackableMax 5). It joins the damage reduction of the gear. Live check B4-1: Light 0.1108 (4 plain and 2 II), Magic 0. |
| `Global_Common_CDR` (Refreshing: CooldownTimer -0.02, CDRImmediatelyOptions ActiveWeapon, IsStackableMax 3) and `Foc_Bonus_25_1` (-0.03; not of the bow build: focus 5) | A fixed cooldown rule (`fixed_cooldown_rule`, `Passives::cooldown_factor`): no event, a negative CooldownTimer, no action window, no condition of a hit, a scope that the backend has (none, ActiveWeapon, All, AbilitySpecific and AllExcept with CooldownId). The cooldown of a cast is `time x (1 + sum)`; the sum has the cooldown line of the jewelry too (one sum) [live check L1: the client tooltip of Explosive Arrow shows 22 x (1 - 2 x 0.0272) = 20.8 s with Refreshing, and 19.7 s with CooldownReduction 1.05 too; the HUD countdown matches the server, run L2 of stage B4]. |
| `Global_Chest_HealthLowStamRegen` (Vital Synergy) and `Ability_Perk_Chest_HealthLowStamRegen` | The perk script of the client (`kPerkScripts`; client code 0x238DEF0 from the perk tick 0x238B500, research Q6): the rows of the perk are empty. While the stamina is below half its maximum: `Status_Perk_Chest_StamLowHealthRegen` (HealMod, the icon "Anointed"); while the health is below half: `Status_Perk_Chest_HealthLowStamRegen` (StaminaRate). Each status has no end while its test holds, with the perk row as its source, and it ends when the test does not hold. The test runs each step of the tick of the rules near a character (500 ms) [the period of the client tick is not known: an agent decision]. Live check B4-3. |
| `Global_Amulet_PotionHeal` (Alchemist Reprieve: ConsumableHealMod 0.02), `GlobalPerk_Artifact_Set1_Earring1` (The Thirst: ConsumableHealMod 0.33) | Received heal parts of a fixed perk row (the gear values, [Gear and perks](gear-and-perks.md)): one pool with the parts of the statuses (Divine: HealMod; decision D9 of the orchestrator: Divine counts for the heal of a potion). Live check B4-5: the received factor of a potion 1.501 = 1 + 0.091 (Divine) + 0.08 (Alchemist Reprieve at the potency 4.0) + 0.33. |
| `GlobalPerk_Artifact_Set1_Earring1_2` to `_5` (The Thirst: OnUsedConsumable, CooldownTimer +0.25, CooldownId of one kind each), `Global_Earring_PotionCDR` (Refreshing Toast: -0.04, no id) | The consumable cooldown factor (`Passives::consumable_cooldown_factor`): the cooldown of the item x (1 + the sum of the rows of its cooldown id; a row with no id: each). A positive value is a longer cooldown (before stage B4 the backend read only a negative value with no id). Live check B4-5: the cooldown of the potion 31.2 s. |
| `Ability_Perk_Earring_OnPotionDmg`, `Ability_Perk_Earring_OnPotionFort` (the Toast perks: OnUsedConsumable, IsConsumableIds of the potions, SelfApplyStatusEffect, ActivationCooldown 8) | The consumable rules (`Passives::consumable_used`) after the effects of a use: the item in IsConsumableIds (the compare ignores the case: the list has `serumregent5`), not in IsNotConsumableIds, the cooldown id, the values of the character, the activation cooldown; then the status with the potency of the rule (live: the empower and the fortify for 8 s, potency 1.27). |
| The EquipAbility of a food status (Desert Sunrise: the status `FoodDOTT5` equips `Global_Consumable_FoodDoT_DurRed_T5`, StatusEffectDurationReduction -0.25 for the categories Burn, Bleed, Poison, Shock, and the other DoT kinds) | A rule of the status that equips it (stage B2, `status_rules`) in the time factor of stage B3: a status of one of these categories on the character lasts 0.75 of its time (live: the poison 20 s to 15.00 s, the HUD "15s"). Stage B4 adds only the test (BR11) and the note of the list: the rule worked from stage B3. |

The list of the rules (`describe_row`) names each new form: the target value rule, the outgoing time factor (with "the outgoing part acts
for a different entity"), the fixed cooldown rule, a consumable rule, the perk script, and the threshold rows with no combat use. With the
preset, the list of stage B4 has 76 rows "NOT APPLIED" (94 in stage B3), and 30 of the 40 rows of the bow table are applied (26 in stage B3).
The rows of the build that stay not applied: the rapier abilities, Bile Bomb, Hardcore Parkour, and Rain of Arrows (stage B5),
`Ultimate_Bow_Hunter_Refund` (a helper with a condition and no effect), Mortal Empowerment (`Global_PvP_Ring_OnKill_EmpowerStack`:
OnExecuted, a kill of a player), `Dex_Bonus_300_1` (StatusEffectCategoriesList) and `Dex_Bonus_300_2` (OnExecuted), and `Dex_Bonus_50_2`
(a non-combat row).

## Commands

```sh
tools/world passives NAME              # the rules of a character: each learned node and each perk row, applied or not, and why
tools/world passives --table greataxe  # the same for each node of a weapon tree (greataxe, warhammer, or a table id)
tools/world status NAME                # the statuses of the rules on the character, with their source
tools/world creature effect ID STATUS 5 # give a creature a status for 5 s (a test of TargetStatusEffectCategory)
```

- The first line of `tools/world passives NAME` is the critical summary of the weapon in the hand: the chance and the
  multiplier of the weapon, the fixed rules, the statuses, and the totals. The rules of a hit add their parts at the hit.
- The routes are `GET /v1/dev/passives?character=ID` and `GET /v1/dev/passives/table?table=GreatAxeAbilityTable`
  (development routes, `SOULWARDEN_HARNESS=1`).

## Traps

- A node acts only in a hit with the weapon of its tree: a war hammer node does nothing for a hit of the great axe.
- The value "before the defense" of a hit line is the raw damage with the damage parts: the core (the weapon with the attributes and the
  level) x the factor of the row x (1 + the parts). Two hits of one weapon on one target differ by the parts of their rules. Live run
  `b2-live3` of stage B2 (the core 2,373.9, both rows with the factor 1.0): a light arrow on the Cyclops with a charge (a debuff) had
  1.493 = 1 + 0.02 (Acutely Empowered) + 0.15 (the light load) + 0.2 (Electrified) + 0.1 (Mark) + 0.023 (`Slottable_Common_DmgBasic`, a
  fixed perk of the definition that the charm of the instance replaces: corrected in stage B2, case BC10); the first arrow of Rapid Shot
  had 1.370 = 1 + 0.17 + 0.2 (Surprise Attack). Gambled Energy adds 0.045 below full stamina (the first hit after a dodge).
- The list says "not applied yet" for each node of an ability with no plan, also when the evaluator has each column of the
  row: the hit of that ability does not exist in the backend.
- The box of Blood Lust follows the look of the camera of the client, not the body: a creature behind the camera does not
  count also when the body faces it.
- A status of a rule near the character shows in the HUD of the owner with no time: it has no end while the condition holds.
- The ability file of a tool from before stage 3.3b-S5 (format 3) has only a part of the rows (497 of 2,025) and no spell
  selections of the action data: the evaluator applies only the rows of the file, and the plans keep the node rules of
  stage 3.3a. Write the file again with `tools/gamedata/abilities.sh`.
- A compare of the damage of two arrows with and without a rule (Speedy) must keep the other parts equal: the charges of the Boltcaster are
  a debuff for 3 s only, so Mark (+0.1 with a debuff on the target) is gone when the second arrow comes later than 3 s after the first one
  (live run of stage B3: an arrow 5 s later had the same number with Speedy and without Mark). Shoot the arrows within 3 s.
- The first arrow on a target has Surprise Attack (+0.2) and no Mark; compare the second and the third arrow.
- The rules of a hit (`before_hit`, `after_hit`, `hit_taken`, `hit_evaded`) do not merge the sources of a row: each source is a rule
  (stage B4 merges them only in the new paths: the time factors, the reduction of a creature hit, the cooldown factors, the consumable rules).
  A rule of a hit on two items acts two times.
- The tooltip of a perk in the client takes the potency of the lowest gear score 100 (Refreshing 1.36: "2.72%"; Enchanted Ward 1.7515:
  "1.8%"; Alchemist Reprieve 4.0: "8%"). The backend takes the same potency. A video of a different client (Season 10) shows other numbers
  (Enchanted Ward "3.5%"): compare with a picture of this client.
- Long Range counts the distance at the time that the server takes the hit, from the place of the attacker then, not from the place of the
  shot. A run back after the shot changes the distance.
- The icon of `Warhammer_Pass_ExhaustOnHit` at the name plate of a creature is the one that ends 5 s after the hit
  (Shockwave also gives the weaken for 10 s, the slow for 4 s, and the stun for 2 s): count the time in a loop of pictures.
