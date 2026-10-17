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
  `ability-data.json` format 4: 2,025 rows).
- The events that the backend has a source for: OnHit, OnCrit, OnKill, OnHitTaken, OnHeadShot, OnHitBehind, OnHealed (only
  its HealScalingValueMultiplier, stage 3.3b-S6). A row with no
  event and a condition of a hit (DamageTableRow, AttackType, RemoteDamageTableRow) is a rule of that hit (Crowded Well:
  `Upgrade_Greataxe_GravityWell_DmgPerFoe` on the burst of the vortex). A row with no event, no condition, and a value is a
  fixed stat. A row with no event, NumAroundMe, and SelfApplyStatusEffect is a status while the condition holds. A row
  with OnBlockedHit and OnHit applies its OnHit part (the backend has no block).
- A row with a column that is not a condition, an effect, or a mark of the evaluator, or with an event that the backend does
  not have (a block, a weapon swap, a dodge with no hit), has no effect. A row with OnHealed and an effect after the heal (a status, a
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
  - A fixed stat: its CritChance and CritDamage count at each hit; its other values (PhysicalArmor, MaxHealth, the
    attributes) are in the gear values of the weapon in the hand (`Gear::values`).
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
| 12 elemental weapon perks (`Global_Weapon_DodgeSuccess_*DMG`, `Ability_Perk_Weapon_DodgeSuccess_*DMG`) | Not applied: the damage over time of the next hit needs StatusEffect (a condition of a status of the character), RemoveStatusEffects, and ForceStatusEffectDamageTableRow. |
| 3 cleanse amulets (`Global_Amulet_DodgeSuccess_CleanseDoT`, `_CleanseDebilitate`, `Ability_Perk_Amulet_DodgeSuccess_Cleanse`), `GlobalPerk_Artifact_Set2_HeavyChest`, `Ability_Spear_CDROnHitAfterDodge` | Not applied: RemoveStatusEffects (the removal of statuses of a category) is not in the backend. |
| 3 block perks (`Global_Chest_OnBlockedHitTaken_StaminaRegen`, its `Ability_Perk_*` form, `Passive_Flail_Eldritch_Passive2`), `AbilityID_Catacombs_Revitalize_Ability` | Not applied: the backend has no block (OnBlockedHitTaken). |
| 22 rows after a dodge (`CDROnDodgePerk1` to `3`, `Global_Common_CDRonDodge`, `Global_Earring_ThreatSubOnDodge`, the 10 `Mut_Curse1_*` rows, `Passive_Lifestaff_Protection_HealingOnDodge`, `Passive_Musket_Trapper_*`, `Passive_VoidGauntlet_HealOnDodgeMaxMana`, `Passive_Greatsword_Off_8`, `Ultimate_Bow_Skirmish`, `AbilityTable_SetBonus_Infix_Stifling_Legs`) | Not applied: AfterAction, AfterSequence, and Duration (a rule after the end of an action) are not in the evaluator. |
| 13 rows of an equipped ability or a trigger (`RequiredEquippedAbilityId`, `AbilityTrigger`, `AbilityList`: the helpers of the flail, the sword, the dagger, and the greatsword, the greatsword counter, `Passive_Flail_Crusader_Passive4`) | Not applied: the evaluator has no such condition. |
| 5 rows of an ability with no plan (`Ability_Greatsword_DrainAttack`, `Ability_Spear_Cyclone_BonusStaminaOnMultiHit`, `Rapier_Evasion_Mod2`, `_Mod4`, `Upgrade_Sword_WhirlingBlade_Rend`) | Not applied yet: the weapon part of their ability comes later. |
| `Global_Conqueror_Longsword_ShieldlessAgility_HitStaminaCostTracker`, `Global_Conqueror_Musket_LimbDebilitation_Legshot`, `GlobalPerk_Artifact_Set2_MediumLegs`, `Passive_Firestaff_Pyro_ManaOnblock` | Not applied: OnEquipStatusEffect, OnLegShot, DamageTypes, UseDefenderWeaponOnApplyOtherSE are not in the evaluator. |

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

A condition EquipLoadCategory does not stop a row (the gear values apply it). The two rules are not in the backend: a later stage of
the bow build implements them (the roadmap, stages B1 to B5).

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
- The list says "not applied yet" for each node of an ability with no plan, also when the evaluator has each column of the
  row: the hit of that ability does not exist in the backend.
- The box of Blood Lust follows the look of the camera of the client, not the body: a creature behind the camera does not
  count also when the body faces it.
- A status of a rule near the character shows in the HUD of the owner with no time: it has no end while the condition holds.
- The ability file of a tool from before stage 3.3b-S5 (format 3) has only a part of the rows (497 of 2,025) and no spell
  selections of the action data: the evaluator applies only the rows of the file, and the plans keep the node rules of
  stage 3.3a. Write the file again with `tools/gamedata/abilities.sh`.
- The icon of `Warhammer_Pass_ExhaustOnHit` at the name plate of a creature is the one that ends 5 s after the hit
  (Shockwave also gives the weaken for 10 s, the slow for 4 s, and the stun for 2 s): count the time in a loop of pictures.
