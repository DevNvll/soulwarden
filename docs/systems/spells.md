# Spell entities

Read this document before a change of a spell entity (the vortex of Gravity Well, stage 3.3b-S2a; the Pylon of the ice
gauntlet, the trap of the musket, and the Beacon of the life staff, stage 3.3b-S2b), before a new spell entity, and before
a live test of a spell. The wire contracts are in the protocol notes (section 4.25). The cast that asks for a spell is in
[Abilities](abilities.md); the projectile of a spell is in [Projectiles](projectiles.md); the interest table, the records,
and the stop are in [World and sessions](world-and-sessions.md).

## Start here

| Task | Start here |
| --- | --- |
| The entity of a spell: its id, caster, role, place, row, times, and end | `apps/backend/src/world/spells.h` (`world::Spells`, `SpellRequest`, `SpellEnd`, `SpellRole`), `world/entity.h` (`SpellBody`), `World::make_spell`, test cases SP2, SB2 |
| The hits of the vortex: the phases of the cast at the place of the spell | `Spells::tick`, `Abilities::spell_phase`, `Abilities::throw_projectile`, test case SP3 |
| The Pylon (a summon): the spawn, the payment, the held cooldown, the shots, the threat, the death chain | `Abilities::spawn_summon`, `Spells::summon_shot`, `Spells::creature_hit`, `Spells::start_end`, test cases SB2, SB3, SB4 |
| The trap: the toss, the arm time, the trigger, the end of its life | `Abilities::throw_spell`, `Abilities::spell_landed`, `Spells::trap_step`, test case SB5 |
| The Beacon (an aura): the attachment, the area that follows, the statuses, the ends | `Abilities::spell_landed`, `Spells::aura_step`, `Spells::target_left`, `World::friendly`, test case SB6 |
| A cancel: the leave and the death of the caster, the leave of the target, a command | `Spells::caster_left`, `Spells::caster_died`, `Spells::target_left`, `Spells::end`, test cases SP4, SP9, SB4, SB6 |
| The record of a spell: the metadata, the Spell state, the PositionInTheWorld state, the vitals, the delayed events | `server/rep/states/spell_state.cpp`, `server/rep/states/state_codecs.cpp` (`spell_codecs`), `RepSession::add_observer_record`, test cases SP2, SP5, SB2, SB5 |
| The interest of a spell: a late observer, a leave and a return, two sessions | `World::interest_enter`, `RepSession::update_interest`, test cases SP5, SP6, SB7 |
| The spell data of the game (the slices, their slots, their events, the values of format 2) | `tools/gamedata/spells.sh` (`spells.py`), `gamedata/spell_data.h`, [Game data](../development/game-data.md), test cases SP1, SP8, SB1, SB10 |
| The cost of the spells | the due list of `Spells`, test cases SP7, SB8 |
| A live check of a slice record before its code: the probe entity | `tools/world probe`, `World::make_probe`, `server/http/dev_routes.cpp` |
| The spells of stage 3.3b-M2: the shield of Maelstrom, the ground of Mighty Gavel, the fortify of Clear Out, the fortify in the well of Unyielding | `Abilities::cast_spell_at`, `Spells::absorbs`, `Spells::aura_step`, `SpellRequest::areaStatuses`, test cases GH8, GH10, GH11, GH12 |

## Rules

### All spell entities

- The spell system (`world/spells.h`, `world::Spells`) makes, changes, and removes the spell entities. A spell entity is
  an entity of the world with the kind `spell` (`Entity::spell`, `SpellBody`): its id from the id allocator, its caster
  (the entity, the actor, and the character id), its role, its position, its spell row, and its start and end on the world
  clock. It has no character, no account, and no session (rules MP-1 to MP-4).
- The role (`SpellRole`) selects the rules of the spell: `area` (the vortex of Gravity Well), `summon` (the Pylon), `trap`
  (the trap of the musket), `aura` (the Beacon), and from stage 3.3b-M2 `shield` (the projectile shield of Maelstrom) and
  `visual` (a picture with no rule of the server: the ground of Mighty Gavel). The abilities system gives the role from the
  plan of the cast, or from the spell row for a spell of an action, a fragment, or a rule (a decision of an agent: the data
  has the components, and each role uses the values of its components).
- The end of an area with no ClearOnEnd takes its rows from its friends and leaves their statuses (code review of stage 3.3b-M2,
  finding M2-R3, `Spells::entity_removed`, case RH3): a later area gives the status again (a second Clear Out with Power Cleaner).
- The statuses of an area leave a friend at its exit only with ClearOnExit of the spell row, and each friend at the end only
  with ClearOnEnd (stage 3.3b-M2; game data: the Beacon and Sacred Ground have both; the fortify of Clear Out has neither,
  and its status keeps its 4 s). An area of a slice component (an AreaStatusEffectComponent with m_removeOnExit) takes its
  statuses at the exit and at the end. A status row with no duration (BaseDuration -1) stays while the friend is in the
  area (`Spells::give_member`).
- A summon, a trap, and an aura need the values of their slice (spell data of format 2). With no spell data, or with a file
  of format 1 (a summon with no AutoSpell, a trap with no detection volume), the cast makes no entity: the summon pays
  nothing, and it holds no cooldown (test case SB10). The vortex works with no data (the times of the ability
  rule, no record).
- The key of the row on the wire is the CRC-32 of the lower-case SpellID (client code 0x683DE00, live check L4 of stage
  3.3b-S2a: `B5715A91` for the vortex).
- A delay of the data of 0 or less is the start of the spell (the data has -6 s and 0.0001 s in some slices); a positive
  delay is 1 ms at least (finding F9 of the review of stage 3.3b-S1).
- The interest. A session observes a spell when its character is not more than the `m_sliceSpawnRadius` of the slice
  from it (game data: 50 m for the vortex, the Pylon, and the Beacon; 125 m for the trap), and not more than the interest
  distance of a character. The session stops it at that distance plus the margin of a creature (20 m). A spell with no
  slice data has no record.
- The record (`server/rep/states/spell_state.cpp`; one encoder for each state). A spell has only the observer view: the
  session of the caster observes its own spell as each other session does (test case SB7: the same bytes for two
  sessions).
  - The metadata state (slot 0: the slice asset and the sub id).
  - The Spell state (type 2912) at the slot of the Spell component of the slice (1 for the vortex and the Beacon, 4 for the
    Pylon). Field 6 (the attachment) names the entity that an aura follows (its actor and its group id): the client moves
    the picture with it (live check of stage 3.3b-S2b). No attachment: the null actor and the group `00000000FFFFFFFF`.
  - The PositionInTheWorld state (type 13) at its slot (4 for the trap): the place of a slice with no Spell component.
    With no such state the client does not show the trap (live check of stage 3.3b-S2b).
  - The Vitals state (type 15) of a slice with a Vitals component and a row of the base vitals tables (the Pylon, the
    trap): the health, the maximum health, and the vitals id (the CRC-32 of the lower-case row id). The client shows the
    name plate and the health bar of the Pylon (live check of stage 3.3b-S2b).
  - One delayed event state (type 3139) for each delayed event that started, with the time that is left. An event of the
    init starts at the start of the spell. An event that starts at a trigger (`m_startDelayOnTrigger`, no
    `m_startDelayOnInit`) starts when the server triggers its owner entity (`SpellBody::triggers`): before that it has no
    state. An event of the init whose target the server triggered is completed: the client triggers that target at once
    (the death chain of the Pylon, the activation of the trap: live check of stage 3.3b-S2b). An event whose time passed: a
    deactivation or a trigger is completed; an activation is not completed and waits until 1 s after the end of the spell
    (a completed activation plays the burst again: live check L4 of stage 3.3b-S2a).
- No Ownership state and no TemporaryAffiliation state: the records show each spell with no such state, and their values
  are not known. Do not make them up.
- The stop. At its end, at a cancel, and at a command, the world removes the entity (`World::remove_spell`), and each
  observer gets the stop in the control block of a bundle. The client does not remove the entity by itself.
- The slots and the delays come from the data of each slice. Do not use constant slots.
- The load. A spell costs work only at its times: the tick takes only the due entries (a phase, an end, a shot, a step of
  a trap, a step of an aura), and each step looks only at the entities near its spell (the position index). No rule looks
  at each spell, creature, or character of the world (test case SP7: 5,000 far vortexes and 10 near ones; test case SB8:
  3,003 spells of the three roles, 60 ticks in less than 2 s, and a far crowd of 4,000 creatures adds no look).
- A new spell entity needs, in this sequence: a live check of its record with a probe (the smallest record, the fields,
  the delayed events, the stop), its rows in the spell data, its request from its system, a test case with two sessions,
  and a live video.

### The spells of stage 3.3b-M2

- The shield of Maelstrom with Absorb (`Spell_Upgrade_GreatAxe_Maelstrom_ProjectileConsume`, role `shield`; the slice
  `spell_projectile_block_maelstrom`): the action of Maelstrom casts it at its start (a CastSpell of the action with the node
  condition). It follows its caster for its Duration (1,125 ms). A projectile whose shooter is an enemy of the caster (a
  creature: `World::hostile`) ends at the first point of its segment in the sphere of the shield (Radius 2 m, its center 1 m
  above the feet of the caster: CastHeight 1) with the end `absorbed` ([Projectiles](projectiles.md); `Spells::absorbs` looks
  only at the shields near the segment). The client shows only the spin of the action (live check I1): the shield is an entity
  of the server. Open: no creature of the backend shoots, thus no removal shows live.
- The ground of Mighty Gavel (`WarHammer_Spell_MightyGavel`, role `visual`): the fragment of the action casts it at 0.638 s
  (CAGE-CastSpell) at the caster, on the ground. It has no rule of the server: its slice has no offense, and the hit of the
  ability is the strike of the fragment. The slice ends it (8 s in the live run).
- The fortify of Clear Out (`WarHammer_Spell_ClearOut_Fortify`, role `aura`): the rule Power Cleaner casts it at a hit of
  Clear Out ([Passive rules](passive-rules.md)). It follows its caster for 100 ms and gives `WarHammer_ClearOut_FortifyBuff`
  (4 s) to each friend in 6 m; the status keeps its time after the end of the spell (no ClearOnExit, no ClearOnEnd).
- The fortify in the well (Unyielding): the slice of the upgraded vortex has an AreaStatusEffectComponent (spell data of format
  3: `areaStatuses`; the friends, 4 m, `Status_Upgrade_Greataxe_GravityWell_ArmorInWell`, m_removeOnExit). The vortex gives the
  status to each friend in 4 m (also the caster), takes it at the exit, and takes it from each friend at its end (test case
  GH12 with two sessions).

### The vortex of Gravity Well (area, stage 3.3b-S2a)

- The make. The abilities system asks for a spell at the landing of the projectile of Gravity Well (`Spells::make`,
  from `Abilities::throw_projectile`), with the phases of the cast that the spell runs (the vortex at its start, and the
  burst). The spell row is `Spell_GreatAxe_GravityWell_Vortex`, or `Spell_GreatAxe_GravityWell_Vortex_Upgrade` with the
  upgrade node.
- The times come from the slice: the burst at the activation (event type 2) of the child entity with the damage row of
  the burst (`GreatAxe_GravityWell_Burst`: 3.0 s), the end at the trigger of the child entity with a
  `DestroyGDEComponent` (3.5 s). With no spell data: the times of the ability rule, and the end at the last phase.
- The phases. At the time of each phase the system gives it to the abilities system (`Abilities::spell_phase`), which
  does the hits of the phase at the place of the spell. Each phase runs one time, while the spell is in the world.
- The cancel. The leave of the caster ends each of its spells: the phases that did not come do not run. The death of the
  caster ends a spell only when its row has `EndOnCasterDeath` (game data: 912 of 1,777 spell rows). The two vortex rows
  do not have it (a decision of the user is open: the brief of stage 3.3b-S2a asked for a cancel at the death).
- The pull of the vortex ([Forced movement](forced-movement.md)): its start gives each creature of its sphere the impact
  of the row of the child entity VortexPull, and the burst pushes 0.5 m.
- No limit of the count of vortexes: the cooldown of the cast (30 s) limits them (a decision of an agent).

### The Pylon of the ice gauntlet (summon, stage 3.3b-S2b)

- The spawn (game data): the cast makes the prefab of `IceMagic_Spell_Pylon` at 0.6 s of the cast fragment
  (`player_magicgauntlet_ice.adb`), `CharRelPosOffsetY` (2 m) in front of the caster in the direction of the press, on the
  ground (`NoTargetSpawnFromRaycast`; the navigation data gives the floor, a floor more than 3 m from the feet is not used).
- The payment (game data, research abilities-magic.md Q3): the action `IceMagic_IcePylon_Cooldown` pays the cost of
  `IceMagic_Spell_Pylon_UI` (ManaCost `IceMagic_IcePylon`: 15) at the spawn, through the one mana path. The press pays
  nothing. A press with less mana than that cost makes no cast (`noMana`, before the payment; the overload rule is an
  open decision for the user). Less mana at the spawn, or a spawn that fails: no Pylon, no payment, and no cooldown (the
  held cooldown ends with no countdown; research abilities-magic.md: a failed placement starts no summon and no fixed
  countdown).
- The held cooldown (game data and client code 0x6923930): from the press to the end of the Pylon the cooldown of the
  ability is indefinite (`AbilityCooldown::indefinite`): a second press is refused, and the owner gets the end all ones
  and the minimum end 0. At the end of the Pylon the cooldown of the data (10 s) starts. One Pylon for each character
  follows from this rule.
- The health (game data): the row of the Vitals component of the slice (`IceMagicPylon`):
  `HealthOverridePercentSpellOwnerMaxHealth` (0.2) of the maximum health of the caster, else `HealthBaseMax`; its armor:
  the armor overrides of the row. A creature hit uses that armor and the level of the caster (`Combat::creature_damage`).
  A summon or a trap whose slice names a vitals row that the spell data does not have is not made, and the Pylon pays
  nothing (code review of stage 3.3b-S2b, finding F8; case RK8). The loader refuses a slice whose vitals row is neither in
  the list `vitals` nor in the list `vitalsNotInData` (the rows of a different table that the tool names: the seed of the
  Goth elephant).
- A Vitals component with no row (code review of stage 1.1b, finding F2, test case RD2): the spell tool stops when a
  Vitals component of a slice names no row. The loader refuses a file of format 2 with a slice that has a slot of the
  Vitals state (type 15) and no vitals values. The spell system makes a summon or a trap only when its vitals row is
  resolved, also when the slice has no Vitals component at all (code review of stage 3.3b-S5, finding F11, test case
  RV11): else no spell, and no payment.
- The life (game data): `m_lifetime` of its AutoSpell (15 s). Then the expire event triggers the entity of the event
  (`DestroyTrigger`), and the entity goes away at the destroy event of that entity (1.3 s later).
- The shots (game data): from `m_initialCastDelay` (1 s), then each `m_cooldown` (2 s), the Pylon shoots the spell of
  its AutoSpell (`IceMagic_Pylon_Projectile`, the row `IceMagic_Pylon`) through the projectile system
  (`Projectiles::launch_spell`) at a creature of its detection volume (`Ice_Pylon`, 20 m). The target: the nearest
  creature that lives, that is not on its way back, and that the Pylon sees (a decision of an agent: the data has no
  order of the targets). The shot starts 1.2 m above the feet of the Pylon (a decision of an agent from the height of its
  detectable object). With no target the Pylon looks again after 250 ms (a decision of an agent). Each shot triggers the
  cast event (`FireTrigger`). The hits belong to the caster: the credit and the damage number.
- The threat (game data: `OverrideTargetForAiThreat` of the spell row): a creature that a shot hurts takes the Pylon as
  its target (`HitWeapon::threat`, `Behavior::hurt_by`), walks to it, and attacks it (`Spells::creature_hit`). A creature
  with a target keeps it (the one-target rule of the behavior).
- The death (game data): at the health 0 the death event of its Vitals component triggers `DestroyTrigger`: each observer
  plays the death chain, and the entity goes away 1.3 s later. A Pylon in its end chain takes no damage and does not shoot.
- The cancel: the leave of the caster removes the Pylon at once.
- The affiliation (game data and a live check): the slice applies no affiliation at its start, and the backend sends no
  TemporaryAffiliation state. The name plate of the own Pylon then has a red bar; a made-up entry of the state gives a blue
  bar (live check of the final run). The values of the original server are not known: a decision for the user.
- The HUD (live check): the button of the Pylon shows no countdown while the Pylon lives (the client starts its own hold at
  the message `Pylon_Spawn`, which the backend does not send). A second press plays the cast animation of the client; the
  server refuses it (the cooldown).
- Not in this stage: the Pylon spells of the nodes (`_Regen`, `_Refresh`, `_Ultimate`, the burst of `PylonBurst`), and the
  frost volume.

### The trap of the musket (trap, stage 3.3b-S2b)

- The toss (game data): `Musket_Spell_Trap` tosses `Musket_Trap_Toss` (`_DMG` with the node `Upgrade_Musket_TrapRend`)
  through the projectile system (the slice `trap_bear_projectile`: 20 m/s, gravity 20 m/s²). The shot item of the client
  (the attack `Traps`) comes 8 input steps after the press (live check): the direction of the item. With no item (a
  character with no client) the server throws it 0.567 s after the press in the direction of the press.
- The landing makes the trap (`Musket_Trap_Set`, the slice `Trap_Bear`) on the ground below the end of the flight.
- The arm time (game data): the detection volume (0.5 m) is active 0.1 s after the start. From then on each 100 ms (a
  decision of an agent: a creature at 5 m/s moves 0.5 m in it) the trap looks for a creature whose capsule touches the
  volume.
- The trigger (game data): the first creature triggers it one time. Each creature in the sphere of the offense
  (`TrapActivate`, the row `Musket_Trap`, 1.1 m) with no wall between takes one hit with the weapon of the cast, and the
  effects of the plan (`Musket_Trap_Root`, 3 s; with the node also `Musket_Trap_Rend`). The detection events of the slice
  trigger `Trigger` and `Dissolve`; the entity goes away at the destroy of the timeline of `Trigger` (0.508 s).
- The life (game data): the event of the entity `Lifetime` triggers `Trigger` at 30 s: the trap goes away 0.508 s later
  with no hit.
- The health: the row `Trap_Bear` (17). No creature takes a trap as a target in this stage.
- Not in this stage: the interaction option of the trap (stage 2.5).

### The Beacon of the life staff (aura, stage 3.3b-S2b)

- The projectile (game data): `Spell_Ability_Lifestaff_Beacon_Projectile` (ManaCost `LifeStaff_Beacon`: 16, paid at the
  press; the row `LifeStaff_Beacon`). The shot item of the client (the attack `Heavy` of the life staff) comes 4 input
  steps after the press (live check); with no item the server throws it 0.433 s after the press. The projectile hits a
  creature or a character (`TargetTypes All`; not its caster, not a dead one). The hit on a creature hurts it with the
  coefficient 1.46 of the row ("146% damage on impact" of the description). A hit on a character does no damage.
- The area (game data): the `SecondarySpellID` of the projectile (`Spell_Ability_Lifestaff_Beacon_AOE`), on the entity
  that the projectile hit (`AttachToOwner`, `StickWhenAttached`), or on the ground at the end of the flight. Its radius
  (3 m) and its life (10 s) are the Radius and the Duration of the spell row.
- The nodes: the chain Infused Light, Radiance's Blessing, Speed of Light selects `_Upgrade`, `_Upgrade2`, `_Upgrade3`
  of the projectile (a decision of an agent from the spell rows: the radius 4 m, then 15 s, then the haste). A node with no
  node before it in the chain does not count.
- The follow: each 250 ms (a decision of an agent) the area takes the place of its target (the client moves the picture
  with field 6 of the Spell state).
- The statuses (game data): each 250 ms the area looks for the friends of the caster in its radius (the position index;
  3 m up or down at most) with the sight from 1.5 m above the center of the area to the chest of the friend
  (`Navigation::sees`; code review of stage 3.3b-S6, finding S6-R4, case RW4: a friend behind a wall is not in the area, and a
  friend that goes behind a wall leaves it; the heights are a decision of an agent, the same as the sight test of a direct heal).
  A friend that comes in gets the statuses of the spell row (`Status_Ability_Lifestaff_Beacon`,
  `Status_Ability_Lifestaff_Beacon_SG_Potency`; the source `spell`); a friend that leaves loses them (`ClearOnExit`); at
  the end each friend loses them (`ClearOnEnd`). The press resolves the status rows of the area (`SpellPlan::statuses`): a row that the
  data does not have refuses the press before the payment (finding F9; case RK9).
- Two auras with one status row (code review of stage 3.3b-S2b, findings F4 to F6; project rules): a friend in two Beacons
  has one status of each row, with the source of the first Beacon (`Spells::AuraRow`: the auras of each row of each
  character); the second Beacon gives no new application and no new tick. The end of the Beacon of the source gives the
  status the source of the other Beacon (`Statuses::change_source`), and the heal source of that aura too: its healer gets the
  notices and its heal base heals (code review of stage 3.3b-S6, finding S6-R5, case RW5); the end of the last Beacon ends it
  (cases RK4). A row
  of the data that is shorter than the Beacon (10 s in the Beacon of 15 s of Radiance's Blessing) comes again on a friend in
  it when it ended, with one new application (`SpellCounts::auraRenewed`; RK5). A friend that leaves the world loses the
  statuses of each Beacon before its entity goes away (`Spells::member_left`; the status of a character stays with its
  character id; RK6).
- The friends (a decision of an agent; the decision of the user is open): the backend has no group. The friends of a
  character are the character itself (a group of one). A test fixture can add a pair (`World::set_test_friends`; in a
  development run `tools/world friends A B`, stage 3.3b-S6). The second character of a live run is not a friend when no
  fixture made the pair: it gets no status.
- The pulses (game data): the status `Status_Ability_Lifestaff_Beacon` has `TickRate` 1 s and `HealthModifierDamageBased`
  0.16: each second the status heals its character through the heal path (stage 3.3b-S6): 0.16 times the heal base of
  the caster (its weapon at its gear score with the Focus scaling), the potency of the statuses with EffectPotencyMods,
  the outgoing part of the caster, and the received part of the friend ([Combat and health](combat-and-health.md); test
  case HE5; live run `heal-live2`: 10 pulses of 55.1 with the heal base 344.1). The area keeps its heal source
  (`SpellRequest::heal`), and gives it to each friend that comes in. A pulse with no heal source gives no health
  (`StatusCounts::pulsesWithNoHeal`).
- The ends: its life (10 s), the leave of its target (`SpellEnd::targetLeft`), the death of its target
  (`SpellEnd::targetDied`: the death of a creature or of a character ends each Beacon on it; code review of stage
  3.3b-S2b, finding F3; case RK3), and the leave of the caster. A projectile whose impact killed its target makes no
  Beacon.
- The record (finding F7; case RK7): a session observes a Beacon that follows an entity only while it observes that entity
  (field 6 of the Spell state names its actor); the Beacon goes out of the interest with it.

### Sacred Ground of the life staff (aura on the ground, stage 3.3b-S6)

- The spell (game data): the self spell `Spell_LifeStaff_SacredGround_Self` (Holy Ground: `_Self_Upgrade`, Anointed:
  `_Self_Upgrade2`, the conditions of the action list) at the caster, on the ground, at 0.333 s of the cast. Its radius
  (3 m) and its life (12 s) are the Radius and the Duration of the row. The cost (ManaCost `LifeStaff_HealingWave`: 15) is
  paid at the spawn, through the one mana path (`Abilities::spawn_summon`, the aura branch). Less mana at the spawn: no
  area and no payment.
- The statuses (game data): the StatusEffects of the spell row (`Status_Ability_Lifestaff_SacredGround`:
  HealthModifierDamageBased 0.16 each 1 s; `Status_Ability_Lifestaff_SacradGround_Beacon_Potency`; Anointed adds
  NonConsumableHealMod 0.5). The area gives them to its friends as the Beacon does (the same aura rules, `ClearOnExit`,
  `ClearOnEnd`), with its heal source.
- The place: the caster, also for a confirm with a click. The client casts `Spell_LifeStaff_SacredGround` at the point of
  its raycast then; the backend does not decode that point (open; a decision of an agent).
- Live run `heal-live2`: the area at the feet of the caster, a number "+55" each second, the health to its maximum
  (`videos/live2-b-sacred-ground.mp4` in the evidence of the stage).

## Commands

```sh
tools/world cast NAME PLACE [--yaw DEGREES]   # press place 0 (Q), 1 (R), 2 (F) of the bar for a character (also a remote test character)
tools/world spells                         # the spell entities: role, age, health, attachment, the friends in an aura
tools/world spells end NAME                # each spell entity of a character ends (each observer gets the stop)
tools/world spells hurt ENTITY AMOUNT      # a summon or a trap takes damage; the health 0 starts its death chain
tools/world probe NAME ASSET [DX DY DZ] [--sub N] [--radius M] [--fragment SLOT:TYPE:HEX]...   # a probe for a live check
tools/world probe fragment ENTITY SLOT TYPE HEX   # one more fragment for a probe
tools/world probe remove ENTITY            # remove a probe: each observer gets the stop
tools/gamedata/spells.sh [OUTPUT.json]     # write spell-data.json (default: the game data folder)
tools/gamedata/spells.sh --find TEXT...    # print the spells with a text and their slice
```

- The routes: `POST /v1/dev/abilities/cast`, `POST /v1/dev/spells/end`, `POST /v1/dev/spells/hurt`, `POST
  /v1/dev/world/probe`, `POST /v1/dev/world/probe/remove`; `GET /v1/dev/world` has the member `spells`. The admin interface
  has `cast_ability` and `end_spells`. The probe, the fragments of a probe, and the hurt of a spell are development
  commands: the backend has them only with `--dev-commands` (the harness gate `SOULWARDEN_HARNESS=1`).
- `tools/world cast` gives the press the checks of a press of a client (the bar, the node, the cooldown, the mana). For a
  character with no client the server throws the projectile of Gravity Well, of the trap, and of the Beacon in the
  direction `--yaw` (degrees from the north to the east).
- `tools/gamedata/spells.sh` writes `spell-data.json` of format 2 (1,703 spells, 1,058 slices, the base vitals rows that
  the slices name). A spell row with no slice file, and a slice with no Spell component and no PositionInTheWorld
  component, have no record: the counts are in the file. With no file the backend starts: the vortex has its hits and no
  record, and the Pylon, the trap, and the Beacon make no entity.

## Traps

- The field masks of a state are chained: each mask byte comes before the values of its 7 fields. A record with both
  mask bytes first gives "Failure during read of ReplicatedStateBundle" in the game log.
- With no delayed event state the client starts the delays of the slice at the record: a late observer then sees the
  full vortex and a burst 3 s after its entry.
- A completed delayed event state executes its event at once, also an activation: a late observer after the burst then
  sees a second burst.
- The trap slice has no Spell component: with only the metadata state the client makes the entity but does not show it.
  It needs the PositionInTheWorld state.
- The projectile of a Pylon shot shows no picture with a Spell state for its slice (live check): the client shows the
  charge of the Pylon (`FireTrigger`) and the damage number, not the flight. The flight of a projectile of the server for
  an observer is open (as for a projectile of stage 3.3b-S3).
- The second character of a live run (`tools/world spawn`) is not a friend: a Beacon on it gives no status to it, and its
  name plate has a red health bar. Put the caster in 3 m of the target to see the statuses.
- A Beacon on a creature hurts the creature (1.46 of the weapon damage): a test that is not a fight puts the Beacon on the
  second character, not on a creature.
- A wolf of a test at 14 m to 16 m that looks away from the character took no target (the vision rule); a wolf 9.5 m in
  front of the character looking at it works.
- A video of a vortex or a Pylon: the caster stands between the camera and the spell. Turn the camera to the side, and
  crop the frames near the spell. The client has a day and a night with no world time of the server.
- A late observer must be more than 70 m away before the cast (50 m and the margin of 20 m): at 64 m it stays in the
  interest.
- A remote test character keeps its cooldown: `tools/world cooldowns NAME reset` before the next cast.
- The name plate of the Pylon of a different character does not show for an observer (its body and its light show). A
  test of the health of a Pylon needs the view of its caster.
- With `tools/world ignore NAME on` only that character is ignored: a wolf of a test can kill a remote test character
  (`tools/world heal NAME` gives it its life again).
- The creatures of a test stay where they are: remove them (`tools/world creature remove ID`) before a walk in the same
  street, or they block the character.
