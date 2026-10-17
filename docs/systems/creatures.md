# Creatures

Read this document before a change of the creatures of the world: the spawn points, the record of a
creature for a session, its health and death, its actions, and the creature data of the whole world.
The behavior of a creature is in [Creature behavior](creature-behavior.md), and its path in
[Navigation](navigation.md). The wire contracts are in the protocol notes (section 4.14).

## Start here

| Task | Start here |
| --- | --- |
| Creatures: the spawn points, the kinds, the record of a creature, health and death, the action of a creature (the scope fields, the locomotion of a walk), the commands | Protocol notes 4.14, `apps/backend/src/world/creatures.h`, `gamedata/creature_data.h`, `server/rep/states` (movement, vitals, marker; `creature_scope`), `server/http/creature_commands.cpp`, `tools/world creatures`, `tools/gamedata/creatures.sh`, `tools/research/creature_actions.py`, test cases UT2 and UT3 |
| The creature data of the whole world: which entries are in, which slices are out, the spawn action of a slice, the picture check of the slices | Protocol notes 4.14, `tools/gamedata/creature_data.py`, `tools/gamedata/creature_slices.json`, `tools/research/creature_tour.py` |
| The status effects of an ability on a creature: the stun, the root, the slow, the weaken, the knockdown and the stagger, the status state (the icons at the name plate) | Protocol notes 4.22, [Abilities](abilities.md), `world/creatures.h` (`Creatures::apply_effect`, `knock`), `world/entity.h` (`CreatureEffect`, `Creature::held`, `rooted`, `speed_factor`, `damage_factor`), `server/rep/states/status_state.cpp`, `tools/world creature effect`, test cases AB6 to AB8 |
| The pull and the push of a hit (stage 3.3b-S4) | [Forced movement](forced-movement.md), `world/forced_motion.h`, `Creatures::impact`, test cases FM2 to FM8 |
| The heals and the buffs of a creature (stage 3.4f-D): the one path of a status on a creature, the heal of its ticks, the DMG part and the haste, the dispel, the due list of the effects, the status actions and the low-health part of the data | Protocol notes 4.40, `world/creatures.h` (`Creatures::give_status`, `heal`, `end_effects`, `sweep`, `effect_tick`), `world/entity.h` (`CreatureEffect`, `Creature::damage_factor`, `speed_factor`), `gamedata/creature_data.h` (`CreatureStatusAction`, `StatusMark`, `lowHealth`), [Creature behavior](creature-behavior.md), `tools/world creature effect`, `tools/world creature status-action`, test cases CRH1 to CRH9 |
| The load of the world: the cost of the spawn rule, the time of a tick | `apps/backend/src/world/creatures.h` (`SpawnGrid`), `world/load_meter.h`, `tools/world list` (the line `load:`), test case WS2 |

## Rules

- A creature is an entity of the same table, with the kind `creature` (`world::Creature` in
  `world/entity.h`): its kind and slice (game data), its level, its direction, its life state,
  the action that the server started, its spawn action, and the values of its behavior
  (`world::CreatureMind`: the mode, the target, the home) are in the entity; its health is
  in the vitals of the entity. It has no character, no account, and no session: a function for a character
  (`World::find_character`, each system with a character id) never finds a creature. Only
  the creature system (`world/creatures.h`) makes a creature, changes its health, moves it,
  and removes it (the behavior of the creatures, `world/behavior.h`, is a part of it). The health 0 is the death: the creature gets the dead mark and the death
  action of its slice, and its entity stays in the world (the body). A dead creature does
  not get health again. Damage goes through the same system (`Creatures::hurt`): a creature
  that lives after it plays the action `Reaction` of its slice, which has an end 1.5 s later
  (the creature then has no action of the server, and its observers get no record for it
  until its next movement state, which has the scope fields with no record). A creature
  that plays an attack plays no reaction, and a step of a way ends a reaction. The creature gets the character that
  hurt it as its target (`Creatures::hurt` has the attacker). A creature on its way back is
  immune for each source: a hit, the health command, and the kill command do not lower its
  health (`Creatures::set_health` gives `CreatureError::immune`; a health that goes up is not a
  damage; test case RP7).
- The target bits (stage 3.3b-M2b, client code, protocol notes 4.34): each movement state of a creature has `dataBits` 0x03. Bit 0
  makes the creature a camera lock target of the client, bit 1 a homing target (the Homing clips of the abilities of the players
  turn the character to it). With 0 the client finds no target. Each creature slice of the data has `m_notTargetableOnInit`
  false (the value from which the server of the game makes these bits). Test cases GH21, CR2.
- The spawn points (a project rule, protocol notes 4.14). The creature data has the places
  where the game has a creature. The world makes the creature of a spawn point when a
  player is 150 m or less from the point, and it removes the creature when no player is
  200 m or less from the point (`WorldOptions::creatureSpawnEnter`, `creatureSpawnLeave`; one
  scan for each 500 ms of the world clock). The two distances are distances of the spawn
  point, not of the creature: a creature that goes to a player and away from its point
  stays in the world (test case FT8). A spawn point has one creature at most. A creature of
  a point that died, or that a command removed, does not come back by this rule: the point
  waits until the command `reset`, or until its respawn time. The death of a creature gives
  its point the respawn time of its spawner (stage 3.5a, protocol notes 4.21,
  `world::respawn_time`: from the smallest to the largest value of the spawner with the
  generator of the world, the smallest value when the largest is not larger, 30 s for two
  values of 0; `Creatures::respawn_at` sets a different one); a point whose creature a command
  removed has no respawn time, also when the creature was dead (the command clears the
  respawn time: review of stage 3.5a, finding F8, case RL8). At the respawn time, when a player is 150 m or less from the
  point, the point first removes the body of its dead creature and then makes its new
  creature; with no player that near, the body stays (the distance rule removes it at 200 m)
  and the point waits. The respawn times are values of the world, not of the database. A
  creature of a command has no spawn point: the distance rule does not remove it.
- The time constraint of a spawn point (stage 2.2, game data, [World time and weather](world-time-and-weather.md)): a point whose
  spawner has a TimeOfDayConstraintComponent (405 points of the supported build) makes its creature only in its phases of the day
  (`SpawnPoint::phases`: night, dawn, day, dusk), and its creature leaves the world at the end of these phases when the data asks for
  it (m_despawnOnTimeEnd: each such point of the supported build). The leave is not a kill: no reward, and the point keeps its
  respawn time or the hold of a command. A phase change starts the normal scan in its tick. A creature of a command has no phase
  rule. Test cases WT5 to WT8.
- The cost of the spawn rule depends on the players and on the points near them, not on the
  size of the world. The creature data has the spawn points of the whole world, and no scan
  looks at each of them: the points are in a grid with cells of 64 m (`world::SpawnGrid`),
  a scan looks at the cells that are 200 m or less from each character
  (`World::for_each_character`) and at the creatures of the world (the creatures of the
  points near a player, and the creatures of commands), and the points that wait are in
  their own set (`Creatures::waiting_count`). The same for the behavior: a creature at rest
  needs no time. The step of the behavior looks only at the creatures of one set (a target,
  or the way back) and at the creatures that roam near a character (a set that the world
  makes from the characters with the position index each 1 s), and the vision rule runs from
  the characters with the position index (test cases FT9 and PT9, with a time limit). A new rule for a spawn point or for a creature keeps
  this: test case WS2 has a world of this size and a time limit that fails for a scan of
  all points. The world measures the real time of its ticks, of its scans, and of the steps
  of the behavior (`world/load_meter.h`, `World::load`, the lines `load:` and `behavior` of
  `tools/world list`): these values are a measurement only, and no rule of the world reads
  them.
- The spawn action (protocol notes 4.14). Some creature slices show no body and no name plate
  until the server starts the action that the slice names in `m_spawnAction` (the action
  `Spawn`). The creature data has the member `spawnAction` for each slice of the list
  `spawnAction` of `tools/gamedata/creature_slices.json`. The spawn action is a value of
  the creature for its whole time in the world (`Creature::spawnAction`), not the action
  that plays: the first movement state of each session for the creature has it, with the
  time 0 on the wire (`CreatureAction::started` keeps the time of the creation). When the
  creature plays a different action at that time (a walk, an attack), the session sends that
  action 1.5 s after the first record (`kSpawnLead` of `rep_session.cpp`; test case RU3); a
  movement state in the 1.5 s (a step of a walk) has no scope fields
  (`PartsRequest::keepScope`, test case RP3). At the end of the 1.5 s the session sends each
  scope that is different from the scope that it sent, also the fields with no record when
  the walk ended in the 1.5 s (review of stage 3.5a, finding F5, case RL5). A
  reaction, an attack, a walk, and the action of a command do not take the spawn action
  away, and the backend does not send it again at their end. A dead creature
  has its death action in the first state of a later session; a dead creature of a slice
  with no death action has its spawn action there, also when it died while its reaction
  played (test case WS4). A new action of a creature keeps this rule. The list
  `notShown` of the same file has the slices that the client does not show correctly: the
  tool leaves out their spawn points. A slice goes from one list to the other only with a
  picture of the live client (`tools/research/creature_tour.py`).
- The client plays no walk for a creature whose position changes with no action: the
  creature slides in its pose. A creature on its way has the locomotion action of its slice
  (`Move_Combat`, `CreatureSlice::moveAction`, `Creature::moving`) from its first step to an
  attack or to its anchor, and a creature on a roam has the walk action (`Move`,
  `CreatureSlice::walkAction`) from its first step to its waypoint or its place; a step
  replaces a reaction that plays, also for a slice with no locomotion action, and a death
  with no death action ends the locomotion record of each session (test cases RU4, RU2). An
  attack, a reaction, the death, and the locomotion are scope records with the tag bytes of the slice
  (`CreatureSlice::tags`: a creature with a weapon has no variant of an attack fragment with
  no tag, and its attack does not play with 12 bytes of 0). The client keeps the scope fields
  of a state that a later state does not have, and it starts a kept action again at each
  later state after its end; a record that comes again with a new time makes the body jump.
  Thus a session keeps the key of the scope that it sent for each creature
  (`Known::scope`, `creature_scope`), and a movement state has the scope fields only when
  that key changes: the record of the new action, or the fields with no record (the client
  plays its idle action). Protocol notes 4.14, user test 1.
- A session observes the creatures near its character in each start (not only with
  `--observe-players`, which is for the characters of other players). A creature comes into
  the interest table at the radius of its slice (`m_sliceSpawnRadius` of the game data, not
  less than 20 m and not more than 100 m), and it goes out 20 m farther. One scan takes 16
  new creatures at most, the nearest ones first. The records of creatures go together in
  common bundles below the size of one carrier message. The fragments of a creature make
  records by the rule of each entity: a second fragment for one slot, and a fragment that
  makes the record larger than one carrier message, go in a later record, and each later
  record of a creature starts a new bundle.
- The status effects (stage 3.3a, protocol notes 4.22). An ability gives a creature an effect
  of a status row of the game data (`CreatureEffect` in `Creature::effects`: the stun, the
  root, the slow, the weaken, the end at the next hit, the end on the world clock) or a
  knockdown or a stagger (`Creature::knockedUntil`). Only the creature system changes them:
  `Creatures::apply_effect` (the same status again gets the later end) and
  `Creatures::knock`. From stage 3.4f-D a status row comes on a creature through one path
  (`Creatures::give_status`: an ability, a passive rule, a spell, a status action of the creature, a command):
  - the effect keeps its row: the part of the damage of a hit of the creature is the DMG<type> of the row for the type of the hit
    (the rule of the damage of a character; the Howl of the hound: 0.1 of each type), or DMGStandard for a row with no typed DMG part;
    the speed is the strongest slow times 1 plus the strongest haste (MoveSpeedMod 0.2 of the Howl; a decision of an agent);
  - a heal at each tick (TickRate) of a row with a positive HealthModifierPercent or Health: that part of the maximum health, through
    the one heal path of a creature (`Creatures::heal`: to the maximum, the vitals state for each observer; no heal for a dead
    creature); the first tick at the next tick of the world (DelayInitialTick: one TickRate later), then each TickRate before the end
    (the rule of a character, [Status effects](status-effects.md)); the same row again (StackMax 1) gets the later end and no second
    tick, and the ticks keep their cadence: a tick that the old end stopped comes at its time when the new end is later (code review
    of stage 3.4f-D, finding SU-R1); at its end an effect first gets its ticks that are due before the end, also when a late world step
    takes an earlier entry of the creature first (finding SU-R2); the OnTickStatusEffect of the row comes at each tick, and it ends with the effect when the row has no KeepOnTickEffectOnEnd
    (game data). A row with no duration is one application now. A negative heal part of a row (a damage over time on a creature) has no
    rule yet;
  - the dispel (game data): a row with RemoveStatusEffects or RemoveStatusEffectCategories ends those effects of the creature first
    (the Null Chamber of the void gauntlet: the category Buff; the ability is not in the backend yet, a command applies the row);
  - the ends: the end of its time, the next hit for a stun of Shockwave, the dispel, and the death of the creature (each effect:
    RemoveOnDeath of the rows; the source of a status action is the creature itself). The way back gives no new effect (immune).
    ResetAI of the data (the reset of a creature removes the category ClearOnReset) is not applied: no effect of this stage has it;
  - the cost: a due list of the ends and the ticks (`end_effects`): a due creature has a look at its effects, a due tick at one
    effect; no look at each creature with an effect (test case CRH9: 1,000 creatures with a long effect add no look). A dead creature and a creature on its way back take no effect. A stun
  stops the attack that plays and starts the action `Stun` of the slice
  (`CreatureSlice::stunAction`); a knockdown (power 4) or a stagger (power 2) starts the
  action `Reaction` with the reaction fragment of its power (`CreatureSlice::reactions`, the
  fragments `r_R0_F` to `r_R4_F` of the slice). While a stun or a knockdown holds the
  creature (`Creature::held`), a hit starts no reaction; a hit ends a stun whose row has
  `StopOnHitCount`. The world ends each effect at its time at the start of a tick
  (`Creatures::end_effects`): it looks only at the creatures with an effect. At the end of
  a stun the next movement state has the scope fields with no record. The behavior obeys
  each effect ([Creature behavior](creature-behavior.md)), and the creature damage obeys a
  weaken (`Creature::damage_factor`, `Combat::creature_damage`).
- The record of a creature has the metadata state (the asset of its slice), the movement
  state, the vitals state, the marker state, and the status state, with the slot of each
  component from the slice (game data: `CreatureSlice::movementSlot`, `vitalsSlot`,
  `markerSlot`, `statusSlot`: the slot of the state type 4236 of the slot list of the
  slice). The status state has only the compact map of the effects whose row has
  `ShowInNameplates` (the icons at the name plate); a slice with no status slot gets no
  status state. A creature
  has only the observer view. The movement state of a creature at rest has the scope fields
  with no record: its client plays the idle action itself. An action that the server starts
  (the death action, the reaction to a hit, an attack, the spawn action of a slice that needs
  it, the locomotion of a way, the walk of a roam, the task of a waypoint, the action of a
  command) is in the state, as a scope record
  with the index of the action and of its fragment from the action table of the slice and
  with the tag bytes of the slice (game data). The spawn action of a first record has the
  time 0: with the time from the creation of the creature the client of a later session
  shows only its light and no body. Never put a state id of a
  player (the script layers) into the state of a creature. Each vitals state of a creature
  has `healthChangeFlags` with bit 0: without it the name plate of the client keeps its old
  bar. The first vitals state of a creature has `invulnerability` and
  `displayImmuneWhenInvulnerable` (1 on its way back, 0 if not), and a later one has them only
  when the immune mark changed after the last state of the session (protocol notes 4.5). The
  position of a creature is exact: the client does not move a creature to the
  ground, and the height of a spawn point can be a floor above the terrain. A creature that
  walks gets the height of each step from the navigation of the world.
- The capsule of a creature (stage 3.3b-S3): the creature data has the capsule of the CharacterComponent of its slice
  (`CreatureSlice::capsule`: the radius, the length of the segment, the axis in the frame of the creature, the offset;
  the wolf: 0.7 m, 0.9 m, the axis of its body). The projectile system turns it by the direction of the creature and puts
  its lowest point at the feet ([Projectiles](projectiles.md)). The posed hit volumes of the slice (the bones in an
  animation) are not in the data.
- The ranged attacks of a slice (stage 3.4f-A, protocol notes 4.37; game data): `CreatureSlice::ranged`, each one an action of the
  action table with its releases (`gamedata::RangedRelease`: the time of the CAGE-RangedAttack event, the profile and the attack with
  its index on the wire, the ammunition and its key, the projectile slice, the damage table key and the place of the row, the damage
  values of the row, and the aim values of the clip), and `CreatureSlice::basicShot`: the ranged attack of the action FireBow
  (`gamedata::kBasicShotAction`), the one that a creature uses ([Creature behavior](creature-behavior.md)). A slice with a basic shot and
  no melee attack fights (`CreatureSlice::fights`). The projectile slices are in `CreatureData::projectiles` (the asset, the root, the
  slot of the Projectile component, the flight values), and the capsule of the player slice in `CreatureData::player_capsule`. The
  loader refuses a file whose ranged attack names an action with no fragment, a release after the end of its action or with a value
  outside its range, or a projectile slice that the list does not have, and a projectile slice with no asset, no slot, or no speed. A
  file of an older tool has no such member: no creature shoots, and the backend works.
- The area attacks of a slice (stage 3.4f-C, protocol notes 4.39; game data): `CreatureSlice::areas`, each one an action of the action
  table with its length and its casts (`gamedata::AreaCast`: the time of the CAGE-CastSpell event, the SpellName, the shape `status`
  or `strike`, the aim values of the clip `AIAimAtTarget`, `AILeadTarget`, `ProjectileSpeed`, `AILeadTargetMaxAngle`,
  `AIAimMaxAngle`, `AIMissMinDistance`, `AIMissMaxDistance`, `UseAllAvailableTargets`, and for a strike the damage row of the spell
  row in the damage table of the slice). A slice with an area attack fights (`CreatureSlice::fights`; the Undead Shaman has no other
  attack). The loader refuses a file whose area attack names an action with no fragment, a cast after the end of its action or with
  a value outside its range, and a strike whose damage has no type or a value outside its range. A file of an older tool has no such member: no
  creature casts an area, and the backend works ([Creature behavior](creature-behavior.md), [Spell entities](spells.md)).
- The hit facts of a slice (stage 3.3b-S5, protocol notes 4.30): `CreatureSlice::behind`, the two angles of the
  DamageReceiver of the slice (m_isHitFromBehindMinAngle, m_isHitByCritFromBehindMinAngle; the wolf 2.0944 and 2.3562;
  503 slices; the tool stops for an angle field that is not exactly one 32-bit float: a cut field is a fault of the
  source, not the angle 0; code review of stage 3.3b-S5, finding F12, `check_hit_facts_tool.py` case cut-angle), and
  `CreatureSlice::head`, the head volume (the HitVolume with m_isHeadshot) as a capsule in the frame of the
  creature: the bone of the volume in the bind pose of the skeleton of the character of the slice (the `.chr` file of its
  `.cdf` file: the chunk CompiledBones 0x2000), with the center, the axis, the height, and the radius of its shape (374
  slices; the wolf: the bone `bind_neck_2_jnt`, radius 0.2 m). A slice with no such data has no hit from behind and no
  headshot ([Combat and health](combat-and-health.md)).
- The skinning of a body (stage 3.6a, [Gathering](gathering.md)): at the death of a creature whose slice has a skinning row of the
  gathering data, the creature system calls `Gathering::creature_died`: the body gets the node state Healthy in the slot of its
  GatherableController (type 12 in the creature codecs). The removal of the creature (at the respawn of its spawn point, by a command,
  or by the distance rule) removes the external object of its body in the interaction base (`Interactions::body_removed`), and a use
  of it ends. A respawn makes a new entity: a new body, a new node.

## Open points

These rules of the data have no code yet (protocol notes 4.40, "Not known, not done"; the code review of stage 3.4f-D). The status-action start rules
(the heal at the low-health part, the buff when the creature does not have it, at most each 20 s) and the speed rule (the strongest haste)
stay project rules: the behavior tree is not in the game files.

- The heal of a different creature: `InqCleric_Rekindle` of the Cleric with its beam (the self heal `InqCleric_Rekindle_Self` is done).
- The other actions of the data that put statuses on their creature (523 actions of the creature action lists; protocol notes 4.40):
  only the two actions of the list `statusActions` have a live check.
- The self-destructs and their rewards, the shield and the block of the officers, grit.
- The summons and the encounters (part E of stage 3.4f).

## Commands

```sh
tools/world creature shoot 2c NAME   # stage 3.4f-A: the creature starts its basic shot at a character now (the behavior rules)
tools/world creature area 2c NAME    # stage 3.4f-C: the creature starts its next area attack at a character now
tools/world creatures NAME 60        # the creatures that are 60 m or less from a character (no name: each creature)
tools/world kinds wolf               # the kinds of the creature data with a text in their id, name, or family
tools/world creature spawn Wolf_Grey_7 NAME 0 4 0   # a creature of a kind 4 m to the north of a character
tools/world creature health 2c 50%   # set the health of the creature whose id ends with 2c
tools/world creature kill 2c         # kill it (the death action, the dead mark)
tools/world creature remove 2c       # take it out of the world
tools/world creature reset           # each spawn point that waits makes its creature again
tools/world creature action 2c Spawn 3   # start an action of the action table of its slice (3 s; no time: no end)
tools/world creature effect 2c WarHammer_Shockwave_Stun 5   # a status effect of the game data for 5 s (no time: the time of its row)
tools/world creature effect 2c knockdown 2                  # a knockdown (or stagger) for 2 s
tools/world creature effect 2c AI_InqCleric_Rekindle         # a heal of the data: 4.5 percent of the maximum health each 1 s for 8 s (stage 3.4f-D)
tools/world creature effect 2c VoidGauntlet_NullChamber_Nullify_AoE   # a dispel of the category Buff (stage 3.4f-D)
tools/world creature status-action 2c InqCleric_Rekindle_Self   # the status action of the slice now (stage 3.4f-D)
```

- `tools/gamedata/creatures.sh` writes `creatures.json` to the game data folder: the spawn
  points of the whole world (32,840 spawn points, 3,848 kinds, 503 slices, 25 regions; a file
  of 13.1 MB with the 19,814 patrol routes of format 3 and the time constraints of format 4 (stage 2.2: 405 points); 60 s with
  `SOULWARDEN_OODLE_LIB`). The
  tool prints the counts of the routes, the policies, the waypoints, the tasks, and the kinds
  that wander. `--radius METERS` (with `--center X Y`) writes a
  smaller file, `--report FILE` writes one line for each entry that is left out, and
  `--all-slices` also writes the slices that the client does not show (a file for a
  picture tour, not for the game data folder). The tool reads the two slice lists of
  `tools/gamedata/creature_slices.json`, and it stops when that file is not correct or when
  a slice of the list `spawnAction` has no action `Spawn` with a fragment. Each slice has the
  member `reactions` (stage 3.3a): the fragment indexes of `r_R0_F` to `r_R4_F` of its
  action `Reaction` (-1: the slice has no such fragment). With a file from before, a
  knockdown plays the fragment of the hit reaction. From stage 3.3b-S5 each slice also has `behind` and `head` (the hit
  facts), and the counts `slicesWithBehindAngles`, `slicesWithHead`, `headsWithNoSkeleton` (140: the skeleton of the
  character is not in the game copy), and `headsWithOtherShape` (3: a shape with no radius). The tool reads the skeletons
  of `objects/characters/`; `tools/gamedata/check_hit_facts_tool.py` checks the reader with made-up sources (test case
  CH9).
- `tools/gamedata/creatures.sh --no-attack FILE` writes one line for each kind that does not
  attack, with the cause (747 kinds of the supported build, 7,997 spawn points from stage 3.4f-D; 789 in stage 3.4f-C; 859 before
  stage 3.4f-A: a kind with the basic shot attacks from then on, and from stage 3.4f-C a kind with an area attack).
- The ranged attacks (stage 3.4f-A, `creature_behavior.py`, `creature_data.py`): each action whose selected variant has a
  CAGE-RangedAttack event, with the joins of each release (the profile of the weapon of the main hand, the attack in the ranged attack
  data with its index: the profiles and then the attacks in the byte order of their names; the slice of the SliceOverride or of the
  ammunition in the AmmoSlot of the slice; the unique name of the damage table and the place of the row). The variant of a ranged attack
  has the tags of the weapons and the default tags of the slice (`m_defaultTags`). A release with a join that the data does not have makes
  no ranged attack of its action, with its cause in the count `rangedLeftOut`. The tool also writes `projectiles` (the projectile slices
  of the releases) and `playerCapsule`, and the counts `slicesWithRangedAttacks` (95), `slicesWithBasicShot` (20), `rangedAttacks` (225),
  `rangedReleases` (459), `projectileSlices` (69). The tool stops with no ranged attack data in the game copy.
- The area attacks (stage 3.4f-C, `creature_behavior.py`, `creature_data.py`, `spells.py`): each action whose selected variant has a
  CAGE-CastSpell event of a spell of the enemies (TargetTypes) with a supported shape (`spells.AreaSpells`: a place slice with an area
  status of the enemies and no offense, or a spell slice with one detection volume that is active between two events and a spell row
  with a sphere radius), whose spell is in the list `areaSpells` of `tools/gamedata/creature_slices.json` (the spells whose record
  has a live check; each entry names its check). A cast action of a chain, a reaction, a run, a throw, or a summon is not an area
  attack. A cast that the tool leaves out has its cause in the count `areasLeftOut`. The counts: `slicesWithAreaAttacks` (9),
  `areaAttacks` (11), `areaCasts` (15), `areaSpells` (6), `kindsWithAreaAttacks` (70), `spawnPointsWithAreaAttacks` (255). The
  tool prints the line `area attacks` and one line for each cause.
- The status actions (stage 3.4f-D, `creature_data.py`, `creature_behavior.py`): each action of the list `statusActions` of
  `tools/gamedata/creature_slices.json` (each entry names its live check: `InqCleric_Rekindle_Self`, `Howl`) whose action list has an
  IfThenElse with one IfMannequinMarker and ModifyStatusEffects with EffectsToApplyOnEnter (`marker_statuses`), and whose selected
  variant has the CAGE-Named event of that mark (its time). The slice has `statusActions` ([action, length, [[time, [status, ...]],
  ...]]) and `lowHealth` (m_lowHealthPercentThreshold of its Vitals component: 119 slices have the field; a field that is not a 32-bit
  float from 0 to 1 stops the tool). The counts: `slicesWithStatusActions` (3), `statusActions` (3), `statusActionStatuses`,
  `kindsWithStatusActions` (18), `spawnPointsWithStatusActions` (91), `slicesWithLowHealth` (119), `statusActionsLeftOut` (16: the
  slices whose variant of the action has no mark, the Grey Wolves with a Howl of damage). The loader refuses a status action whose action
  has no fragment, a mark after the end of its action or with no status, and a low-health part out of 0 to 1. A file of an older tool has
  no such member: no creature heals or buffs itself.
- `tools/research/creature_tour.py` is the picture check of the creature slices: `prepare
  GAMEDATA TOUR` makes a game data folder with one kind for each slice, `run TOUR OUT`
  makes one creature of each slice in front of the character of a live run and takes one
  picture (`--list FILE` for some slices, `--wait SECONDS` for the time before the
  picture), `check OUT` removes a picture that is not a picture of the game, and `sheets
  OUT` puts 25 pictures on one sheet. A person must look at each sheet.

## Traps

- The client keeps the scope fields of a creature: a state with no scope fields after an
  action makes the client start that action again. A new state of a creature must keep
  the rule of `creature_scope` (protocol notes 4.14).
- The spawn points of the start area are outside the wall of the settlement. From the
  default spawn place the character cannot walk to them through the wall (the wall is at
  x 8814, 36 m west of the street). Put the character outside with its row, with no run
  alive or after `tools/scenarios/leave.sh`: `sqlite3 ~/Games/soulwarden/work/dev-data/soulwarden.sqlite3
  "update characters set position_x=8800, position_y=4245, position_z=137.9 where
  name='Soulwarden'"` (7 m from a rabbit point; the wolf point `8815.25 4319.13` is 75 m to the
  north, and a Risen point is on the floor of a hut at `8873.61 4338.63`).
- The world time (stage 2.2) can be night: set the day for a picture (`tools/world time Soulwarden 12:00`, or the tab
  "World" of the tool window). The street at the default spawn has lamps: use it for a screenshot of a
  creature of a command (`'look -900 0'`, then `tools/world creature spawn KIND Soulwarden -5 0.9
  0 --yaw 270`: 5 m to the west on the street, with its face to the camera).
- An id of a kind can have a colon and a number (`Risen_L-R_Arm_6:2`): more than one kind
  has that vitals row. `tools/world kinds TEXT` prints the ids.
- The short form of a creature id (`tools/world creature kill 2c`) must find one creature:
  ids end with `2c` and with `12c` in a long run. Give more digits.
- A creature of a spawn point that died, or that a command removed, does not come back
  when the character goes away and comes back: `tools/world creature reset` (the button
  "Reset" of the tool window) makes each such point free again.
- When the character leaves the world, no player is near a spawn point: the world removes
  each creature of a spawn point, and it makes new ones (with new ids) at the next world
  entry.
- The client removes the body of a dead creature itself some minutes after the dead mark
  (the slice has a delayed event of 300 s). The entity stays in the world of the backend
  until a command or the distance rule removes it.
- A health change with no bit 0 in `healthChangeFlags` changes the value in the client and
  not the bar of the name plate: there is no error and no log line.
- A dead mark with no death action leaves the body in its idle pose, with the death effect
  and the white material. The death action is a scope record of the movement state: the
  index of the action and of its fragment come from the action table of the slice.
  `tools/research/creature_actions.py --compare FILE SLICE` compares the table of the file
  with the table of the live client.
- `tools/gamedata/creatures.sh` needs an Oodle source for some thousand pak entries. The
  extracted files of the work folder are not complete for it: set `SOULWARDEN_OODLE_LIB` to an
  Oodle library of the computer (a `liboo2core.so`), or `SOULWARDEN_OODLE_DLL` for the helper.
  The tool writes into the common game data folder: the next start of the user then has
  the creatures. For a test of a different file, give the tool an output path and give
  the run its folder with `SOULWARDEN_GAMEDATA_DIR`.
- The tool window covers the left half of the picture: a creature that the tab "Creatures"
  makes "to the north" can be behind the window. Hide the window (F8) to see it.
- The game log has no line for a creature record that the client accepts, and no line for
  a wrong action index: look at a screenshot.
- A picture of a creature with its name plate: travel with numbers to 3.5 m south of its
  position (`tools/world creatures NAME 150` prints the positions), with the height of
  the creature plus 0.3 m. The creature is then in front of the character. The place of a
  shrine can have a wall or the shrine between the character and a creature.
- A creature of a slice with a spawn action shows a light for 1 s to 4 s after its record
  (two slices: 5 s to 15 s), then its body. A screenshot directly after a travel can show
  the light.
- The picture of the game did not change for 1 minute one time in a tour (9 pictures in
  sequence were equal). The tour compares each picture with the one before it.
- `tools/research/creature_tour.py run` makes its creatures 5 m in front of the character.
  Turn the camera to the west one time before it (`'look -900 0'` at the default spawn,
  yaw 1.6 in `tools/research/player_position.py`): a second turn shows the wall of the
  house in each picture. Do not call `tools/hx` while a tour runs (one command file).
- A tour of 77 slices with a wait of 5 s needs 12 minutes, and a tour of all 553 slices
  needs more than 1 hour. The tour sends a small camera movement each 5 minutes, so that
  the client does not leave the world.
- The work file of a tour (`creature_tour.py prepare`) has each slice (`creatures.sh
  --all-slices`) and one kind `Tour_NNNN` for each slice. It is not a file for the game
  data folder of the user.
- The spawn action is a rule for the slices of one list. A slice that is in no list of
  `tools/gamedata/creature_slices.json` gets no spawn action: do not start the action for
  each slice without a new tour (476 slices have a picture with no action).
- A point with a time constraint (stage 2.2) makes no creature out of its phases: a creature of the data that is not there by day
  can be a night creature. `tools/world time` gives the phase, and `tools/world list` the counts `removedByPhase` and
  `outOfPhase` ([World time and weather](world-time-and-weather.md)). A name plate shows also in the dark.
