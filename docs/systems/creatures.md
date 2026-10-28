# Creatures

Read this document before a change of the creatures of the world: the spawn points, the record of a
creature for a session, its health and death, its actions, and the creature data of the whole world.
The behavior of a creature is in [Creature behavior](creature-behavior.md), and its path in
[Navigation](navigation.md). The wire contracts are in the protocol notes (section 4.14).

## Start here

| Task | Start here |
| --- | --- |
| Creatures: the spawn points, the kinds, the record of a creature, health and death, the action of a creature (the scope fields, the locomotion of a walk), the commands | Protocol notes 4.14, `apps/backend/src/world/creatures.h`, `gamedata/creature_data.h`, `server/rep/states` (movement, vitals, marker; `creature_scope`), `server/http/creature_commands.cpp`, `tools/world creatures`, `tools/gamedata/creatures.sh`, `tools/research/creature_actions.py`, test cases UT2 and UT3 |
| The end of an action and the idle record, the record of a timed action in each state, the reaction to a hit (the rule of the action lists, the power, the length, the hit stun, the limit), the push of a hit (user test 2) | Protocol notes 4.14 and 4.26, `world/creatures.h` (`Creatures::rest`, `hurt`, `reaction_allowed`, `reaction_length`), `world/hit_reaction.h`, `world/entity.h` (`Creature::idle`, `hitStunUntil`, `reactionChain`, `reactionLimitUntil`), `gamedata/creature_data.h` (`CreatureSlice::idleAction`, `reactionLengths`), `gamedata/combat_tables.h` (`DamageReaction`), `server/rep/states/movement_state.cpp`, test cases UX2 to UX6, UX8, UX9 |
| The reaction of stage U3: the row of the reaction table for the side and the power, the length with the HitStun clip, the chain and its resets, the additive reaction in its own scope, the push scales, the kind marks | `Creatures::react`, `plan_reaction` (`world/creatures.cpp`: `hit_angle`, `select_row`, `hit_stun_length`, `push_scale`), `gamedata/creature_data.h` (`ReactionTable`, `ReactionRow`, `AdditiveReaction`, `CreatureKind::promotedElite`), `world/entity.h` (`CreatureAdditive`), `server/rep/states/movement_state.cpp` (the additive scope record), test cases CF2 to CF5, CF10, CF11 |
| The creature data of the whole world: which entries are in, which slices are out, the spawn action of a slice, the picture check of the slices | Protocol notes 4.14, `tools/gamedata/creature_data.py`, `tools/gamedata/creature_slices.json`, `tools/research/creature_tour.py` |
| The status effects of an ability on a creature: the stun, the root, the slow, the weaken, the knockdown and the stagger, the status state (the icons at the name plate) | Protocol notes 4.22, [Abilities](abilities.md), `world/creatures.h` (`Creatures::apply_effect`, `knock`), `world/entity.h` (`CreatureEffect`, `Creature::held`, `rooted`, `speed_factor`, `damage_factor`), `server/rep/states/status_state.cpp`, `tools/world creature effect`, test cases AB6 to AB8 |
| The pull and the push of a hit (stage 3.3b-S4) | [Forced movement](forced-movement.md), `world/forced_motion.h`, `Creatures::impact`, test cases FM2 to FM8 |
| The heals and the buffs of a creature (stage 3.4f-D): the one path of a status on a creature, the heal of its ticks, the DMG part and the haste, the dispel, the due list of the effects, the status actions and the low-health part of the data | Protocol notes 4.40, `world/creatures.h` (`Creatures::give_status`, `heal`, `end_effects`, `sweep`, `effect_tick`), `world/entity.h` (`CreatureEffect`, `Creature::damage_factor`, `speed_factor`), `gamedata/creature_data.h` (`CreatureStatusAction`, `StatusMark`, `lowHealth`), [Creature behavior](creature-behavior.md), `tools/world creature effect`, `tools/world creature status-action`, test cases CRH1 to CRH9 |
| The stacks of a status on a creature (stage B2): StackMax, the end of the rule of a character, the count at the name plate, the ApplicationCooldown on the target, the removal window, the one-time damage of a row with no duration | `Creatures::give_status`, `apply_effect`, `application_ready`, `stacks_of`, `remove_statuses`, `server/rep/states/status_state.cpp` (`encode_creature_status_state`), protocol notes 4.47, test cases BC1, BC2, BC6, BC7 |
| The load of the world: the cost of the spawn rule, the time of a tick | `apps/backend/src/world/creatures.h` (`SpawnGrid`), `world/load_meter.h`, `tools/world list` (the line `load:`), test case WS2 |
| The stationary targets (stage QG5c1: a totem, a nest): the place slot, the record with no movement state, the hit with no reaction, the kill and the respawn; the kept-out causes of the creature data | Protocol notes 4.14, `gamedata/creature_data.h` (`CreatureSlice::placeSlot`, `stationary`, `CreatureData::kept_out`), `server/rep/states/object_states.cpp` (`encode_target_place_state`), `world/creatures.cpp` (`hurt`, `knock`), `world/forced_motion.cpp`, `world/behavior.cpp` (`bind`), `tools/gamedata/creature_data.py` (`is_target`, `read_target_list`, `keep_out`), the list `targets` of `tools/gamedata/creature_slices.json`, test cases QG5C1A to QG5C1C |

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
  that lives after it plays the action `Reaction` of its slice by the reaction rule below (user test 2), and at its end its
  observers get the idle record of the slice. A creature
  that plays an attack plays no reaction, and a step of a way ends a reaction. The creature gets the character that
  hurt it as its target (`Creatures::hurt` has the attacker). A creature on its way back is
  immune for each source: a hit, the health command, and the kill command do not lower its
  health (`Creatures::set_health` gives `CreatureError::immune`; a health that goes up is not a
  damage; test case RP7).
- The reaction to a hit (user test 2, UT2-9; game data: the action `Reaction` of `baseai_reaction.actionlist` and its reaction
  table `BaseAI_ReactionTableHarness`; protocol notes 4.14). `Combat::strike` gives `Creatures::hurt` the reaction cells of the
  damage row (`hit_reaction`: NoReaction, PowerLevel_AI, HitStun_AI; a blank "_AI" cell takes the normal cell). The rules and the
  source of each one:
  - No reaction (game data): a tick of a damage over time (no damage row), a row with NoReaction, a Boss type (Raid10+, Raid20+,
    Boss, Boss+, Boss-, EliteBoss). A light attack (and an attack of the type Ability of a power below 1) plays one only on the types
    Solo, Solo-, Solo_StarterBeach, Solo-_StarterBeach; a heavy attack also on Solo+, Solo+_StarterBeach, Named_Solo, Named_Solo+; an
    attack of a different type (Ability, Magic, MagicSpell, ...) of a power of 1 or more on each type that is not a boss. A Magic or a
    MagicSpell row of a power below 1 plays no reaction and gives no hit stun: the branch below power 1 of the action list names only
    the type Ability (code review of user test 2, finding U2-R2; game data: Firestaff_PrimaryAttackC3, IceMagic_Pylon,
    IceMagic_PylonBurst have the power 0; test case RT2). The creature type is the type of the kind (`CreatureKind::creatureType`).
  - The rules of the backend from before (decisions of stage 3.2a and 3.3a): no reaction while an attack, a shot, a cast, or a status
    action plays (the grit of the action list: an attack of a creature of the backend always has grit), and none while a stun or a
    knockdown holds the creature.
  - The fragment (stage U3, research Q2; game data: the reaction table of the action `Reaction` of each slice, `CreatureSlice::
    reactionTable`; client code 0x2B9F1C0): the first row of the table whose power is the power of the hit (or -1: each power; a power
    above the largest power of the table takes the largest) and whose angle condition holds for the angle of the source. The angle is
    from the front of the creature, positive to its right (`hit_angle`; the sign is the sign of damaged_right, live check LC-Q2-1); a
    condition holds for min - tolerance < angle < max + tolerance (0.01 in the data). No row: the action keeps its own fragment. A row
    whose fragment name the slice does not have gives the fragment of the action. A row with AlignToDirection turns the creature so that
    the hit comes from AlignOffsetAngle (each wolf row: the offset 45 or -45 for the powers 0 to 3, 90 or -90 for the powers 4 and 5;
    a light arrow from the right turns the wolf 45 degrees to the source).
    The family tables differ: the wolf table gives power 0 to 2 the fragments `r_R2_*` (the rule of user test 2 gave `r_R1_F`). A slice
    with no table (a file of a tool from before stage U3) keeps the rule of user test 2: power 0 and 1 `r_R1_F`, power 2 to 4 `r_R2_F`
    to `r_R4_F`, power 5 `r_R4_F`, the front only. A side fragment with no variant in the slice: the front fragment of the same power
    (a project rule; live check LC-Q2-2 is open).
  - The length (stage U3, research Q3; client code 0x2559640; a decision of the orchestrator): a fragment with the clip HitStun
    (Procedural type HitStun at the time s of the fragment of the length L) ends at the hit stun of the row: T = HitStun_AI / 30 s,
    held between s + (L - s) / 3 and s + (L - s) / 0.001 (the play speed of the rest of the fragment is from 0.001 to 3.0), and s + (L
    - s) / 3 when HitStun / 30 is not above s. The Inquisitor Cleric under a heavy arrow (22 frames): 0.73 s, and its push 0.73 m (before:
    1.33 s, 1.33 m). The client does this only for its own character; the server of the game did it for a creature (an inference of
    the research). A fragment with no clip keeps its length (UT2-7: the end blend, else the end of its animation; with no length
    `Creatures::kReactionTime`, 1.5 s). The action ends at IfFragmentDone with the ExitOffsetSeconds of the table.
  - The hit stun (a decision of an agent: the unit of HitStun_AI is not proved; frames of 1/30 s, as the other frame values of the
    data): HitStun_AI frames from the hit (`Creature::hitStunUntil`; 18 frames, 0.6 s, for a light arrow); in it the behavior does not
    step, turn, or attack, and the reaction plays (before, the next step ended it after 200 ms at most).
  - The limit (game data: Chained_Reaction_Count 3, the status AI_Reaction_Limiter of 2 s, the tag ReactionLimiter_Active): the third
    chained reaction of a Solo type (of a heavy attack for a Solo+ or a named type), or the first one of DungeonMiniBoss, Dungeon+,
    Elite+, EliteMiniBoss, CatacombsMiniBoss, starts the limit until 2 s after its end; in it a light or heavy attack plays no reaction
    (`Creature::reactionLimitUntil`). The 2 s start at the real exit of that reaction (game data: AI_Reaction_Limiter is in
    EffectsToApplyOnExit of the action `Reaction`): its natural end, a step, a new action, a stun, or the death
    (`Creatures::action_exits`, `Creature::reactionLimitAction`; code review of user test 2, finding U2-R3; test case RT3). Before
    the exit the limit holds to the natural end plus 2 s.
  - The chain (stage U3, research Q3; a decision of the orchestrator: the reading of the data): each full reaction since the last reset
    counts (`Creature::reactionChain`; before, a reaction counted only when it started while the reaction before it played). The resets
    (game data: ResetReactionCount): the end of the limit (OnExit of ReactionLimiter_Active), and a hit in an attack of the creature with
    grit and no limit (OnEnter of the additive reaction). Three light arrows 0.8 s apart on a Solo wolf start the limit at the third.
    The log line of a reaction gives the count (`the chain 2`).
  - The additive reaction (stage U3, research Q5; game data: the action `Reaction_Additive` of `baseai_reaction.actionlist`, its table
    of the fragments `r_R0_*` in the scope "Additive" of the slice; a decision of the orchestrator after live check LC-Q5-2): a hit
    with a reaction row that plays no full reaction by the rule (a light hit on a Solo+ or an Elite type, the limit), or that comes in an
    attack of the creature (grit) with a power above 0, plays the additive reaction: the row of its table for the side, in its own
    scope (`Creature::additive`, `CreatureSlice::additive`: the scope of the controller, 4 for the wolf, 5 for the Cleric). The action,
    the attack, or the run of the creature goes on; the client plays the flinch over it (live check LC-Q5-2: the client accepted the
    record, the wolf at rest lowered its head and shoulders for 0.7 s, and the run of a promoted wolf went on with no restart). It plays
    to the end of its fragment less ExitOffsetSeconds (0.9 - 0.2 s for the wolf). Its push: [Forced movement](forced-movement.md) (it
    does not hold the behavior). In the limit a melee hit on a type that is not an elite type gives the limit again (the
    EffectsToApplyOnEnter of the action: AI_Reaction_Limiter for a hit that is not ranged). No additive reaction for a kind with the
    status AI_NoReactions, a dead creature, and a creature that a stun or a knockdown holds (a project rule: the backend has no
    breakout).
  - The kind marks (stage U3; game data: the buff buckets of the kind, `BuffBucketData`): a kind whose buckets give the status
    AI_Elite_Unbreakable_Grit_Buff is a promoted elite (`CreatureKind::promotedElite`: the push scale PromotedElite, 0.15 of a push in a
    full reaction and 0 in an additive one), a kind with AI_NoReactions plays no reaction (`noReactions`). The tool reads a bucket of the
    type AND as the union of its statuses and a bucket of the type OR as their intersection (a mark only when each choice gives it).
    The supported build: 96 promoted elite kinds (221 spawn points), 0 kinds with AI_NoReactions.
  - The push of the impact of the row is an activity of the reaction, or of the action Pushable (0.4 s, game data) for a row with
    NoReaction: a hit with neither moves nothing ([Forced movement](forced-movement.md)).
- The end of an action (user test 2, UT2-2; client code 0x2A5D630, 0x6D899DD, 0x4838EE0, 0x2A2BC20; protocol notes 4.14). The client
  does not end an action of an action list by itself (NoAutoBlendOut): the body keeps the last pose. And the scope fields with no
  record make the client end its own idle at each tick while states come. Thus the end of an action of the server with an end, the end
  of a walk (`Creatures::stop`, also at the anchor), and the end of a stun give the creature the idle record of its slice
  (`Creatures::rest`, `Creature::idle`: the action `Idle`, `CreatureSlice::idleAction`, one record with a new start count, its
  fragment loops), and each observer gets its movement state. A later state (a turn) has no scope fields: the client keeps the idle. A
  session that observes the creature later gets the idle record in its first record. A slice with no action `Idle` keeps the rule from
  before (no state at the end of an action; the fields with no record at a stop). An action and a step clear the idle record.
- The record of a timed action (user test 2, UT2-3; client code 0x48709A0, 0x2A222E0, 0x2A2ACD0, 0x2A2BC20 with the constant -0.5 at
  0x7F4016C; protocol notes 4.14): while a one-shot action of the server with an end plays (a reaction, a knockdown, an attack, an area
  attack, a status action, a timed task), each movement state has its record again, with the same start count and the time of the
  state. Before, the push of a heavy arrow (a state each 50 ms) made the client start the reaction again at each half of its length
  (three reactions of a Cleric for one arrow). The locomotion, the idle record, and the actions with no end keep one record.
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
  `Creatures::apply_effect` (the same status again: one more stack and the end of the rule of a character, from stage B2) and
  `Creatures::knock`. From stage 3.4f-D a status row comes on a creature through one path
  (`Creatures::give_status`: an ability, a passive rule, a spell, a status action of the creature, a command):
  - the effect keeps its row: the part of the damage of a hit of the creature is the DMG<type> of the row for the type of the hit
    (the rule of the damage of a character; the Howl of the hound: 0.1 of each type), or DMGStandard for a row with no typed DMG part;
    the speed is the strongest slow times 1 plus the strongest haste (MoveSpeedMod 0.2 of the Howl; a decision of an agent);
  - a heal at each tick (TickRate) of a row with a positive HealthModifierPercent or Health: that part of the maximum health, through
    the one heal path of a creature (`Creatures::heal`: to the maximum, the vitals state for each observer; no heal for a dead
    creature); the first tick at the next tick of the world (DelayInitialTick: one TickRate later), then each TickRate before the end
    (the rule of a character, [Status effects](status-effects.md)); the same row again gets the end of the stacks rule below and no second
    tick, and the ticks keep their cadence: a tick that the old end stopped comes at its time when the new end is later (code review
    of stage 3.4f-D, finding SU-R1); at its end an effect first gets its ticks that are due before the end, also when a late world step
    takes an earlier entry of the creature first (finding SU-R2); the OnTickStatusEffect of the row comes at each tick, and it ends with the effect when the row has no KeepOnTickEffectOnEnd
    (game data). A row with no duration is one application now. A negative heal part of a row (a damage over time on a creature) has no
    rule of its own;
  - the damage of a tick (stage B1, `StatusDamage`, `CreatureEffect::tickDamage`): a status that a rule gives with a forced damage row
    (Flame Arrow: `Status_ExplosiveShot_Burn`, [Passive rules](passive-rules.md)) keeps the damage of one tick, its damage type, and
    its rune charge. At each tick (the cadence of the heal ticks: TickRate 1 s, DelayInitialTick, the ticks that are due before the end)
    the world hurts the creature through the gear system (`Gear::tick_damage`, the path of the damage of a perk: the defense of the
    creature, its reaction rule (no reaction: a damage over time), the damage number for the source, its death) one step later
    (`World::after(0)`: a tick that kills the creature does not change the effect list in its loop). The source is the character of
    the rule; a source that left the world gives no tick. The count `damageTicks`. The icon: the compact status map of the creature
    (slot 8, type 4236) for each observer, as each creature effect (live: the burn icon at the name plate of the direct target only);
  - the dispel (game data): a row with RemoveStatusEffects or RemoveStatusEffectCategories ends those effects of the creature first
    (the Null Chamber of the void gauntlet: the category Buff; the ability is not in the backend yet, a command applies the row;
    stage B2: the bolt of the Boltcaster ends the charges; `Creatures::remove_statuses`, also for RemoveTargetStatusEffectsList of a
    passive rule);
  - the stacks (stage B2, `CreatureEffect::stacks`; the rule of a character, [Status effects](status-effects.md)): a new effect has
    InitialStackSize stacks (1 to StackMax); the same row again adds one stack to StackMax (a value below 1 is 1: client loader
    0x44B65F5), and at StackMax with DontReplaceStack changes nothing. The end of the same row again: `Statuses::renewed_end`
    (RefreshDuration: now plus the time, not before the old end; a DurationMax above the duration: the old end plus the time, not after
    now plus DurationMax; else the later end; RefreshDuration "TRUE" as a text counts: `CellValues::flag`). All stacks end together at
    the end (RemoveUnappliedStacks and a row with no such cell: the backend has one end for each effect; the client has no reader of the
    cell, research bow-build-2.md K3: a decision of an agent). The DMG parts and the speed part of the row count for each stack. Each
    observer gets the stack count in the compact map (the packed byte: bits 0 to 6 the stacks; live check L1 of stage B2: the name plate
    shows "x2", "x3"); a new stack sends the list, a renewal with the same stacks sends nothing (the compact map has no end). Test case BC1.
    An entry whose end came and that the sweep of the due list did not take yet (a hit of a world task or of an item of a client before the
    sweep) is not renewed: the application sweeps the creature first (the ticks due before the end come, review SU-R2), and the new entry
    starts with its initial stacks and a new serial (code review of stage B2, finding B2-R2, test case BV2: a hit after the end of 2 charges
    gave 3 charges, and two more hits gave a bolt). Inside a sweep or a tick the old entry only ends;
  - the ApplicationCooldown of a row (stage B2, `Creatures::application_ready`): one application of the row for each creature in that
    time, of each source (the key is the creature, as for a character; research bow-build-2.md Q4: the bolt of the Boltcaster at most once
    in 3 s on each target). Only an application that changes the creature starts it. The status of a damage row (`Abilities::row_effects`)
    and the rend and the damage over time of a perk (`Gear::perk_status`; before stage B2 its key was the attacker) use the same time.
    Test cases BC2, BC7;
  - the removal window (stage B2; game data, a decision of an agent): a running effect whose row removes rows or categories
    (RemoveStatusEffects, RemoveStatusEffectCategories with a duration) refuses those rows while it runs (the count `statusesBlocked`):
    `Status_Perk_Artifact_Set1_Bow_StaticCharge_Removal` (0.2 s) takes the charge that the hit of the bolt adds;
    `Status_SetBonusInfix_Focused_CCEffectImmunity` (1 s, the category CC) has the same form. Test case BC2. The statuses of a perk
    (`Gear::perk_status`: a rend, a damage over time, a chain) have the same test before their cooldown, their effect, and their damage
    (`Creatures::removal_blocks`; code review of stage B2, finding B2-R4, test case BV4: the rend of a perk came in the window);
  - a row with no duration and a damage of its source (stage B2: the bolt, `Status_Perk_Artifact_Set1_Bow_StaticDischarge`,
    [Passive rules](passive-rules.md)) is one damage now through the path of a tick (`Gear::tick_damage`, the count `instantDamage`), and
    a row with ShowInNameplates and a DurationMax stays for the DurationMax (0.1 s) with no tick: each observer gets its icon and the
    client plays its visual effect (live check L2 of stage B2: the compact entry of the bolt plays the lightning strike of its
    FxScriptOn; with no entry the client shows nothing). Test cases BC2, BC6;
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
  a stun the next movement state has the idle record of the slice (user test 2; a slice with no idle action: the scope fields with
  no record). The behavior obeys
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
- The attack type of a creature attack (stage B4; research `bow-build-2.md` Q5, game data): the column AttackType of the damage row of the
  attack (Light, Heavy, Magic, or blank) is the 10th value of a contact (`CreatureContact::attackType`) and the member `attackType` of a
  ranged release and of an area strike. The health system gives it to the rules of a hit taken (Enchanted Ward: AttackType Light,Heavy,
  [Passive rules](passive-rules.md)), and the log line of a creature hit names it. A contact of 8 or 9 values (a file of an older tool)
  has no attack type: a rule with AttackType does not act for it, and the backend works.
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

- The vitals categories of a creature for the quests (stage QG2a, [Quests](quests.md); `LootTables::kill_categories`) [client 0x3B19F40,
  0x5C75830; data]: the VitalsCategories of its base row (`loot-tables.json`), the category of the kind (the VitalsCategoryId of the spawn
  entry of the level metadata: `CreatureKind::category`), and the direct parent of each (`categoryGroups` of `loot-tables.json`,
  GroupVitalsCategoryId of `javelindata_vitalscategories`). A kind of the creature data is one vitals row, one spawn category, and one set of
  bodies: two spawn points of one vitals row with two spawn categories are two kinds (`Dryad_Archer_31` and `Dryad_Archer_31:2`).

### The stationary targets of stage QG5c1

The evidence is in `~/Games/soulwarden/work/evidence/quests/qg5c1/implement/` (`RESULT.md`, the pictures `shots/I4-*`, the videos
`videos/I4-*`).

- A stationary target is a creature of the same table whose slice has a PositionInTheWorld component and no movement component
  (`CreatureSlice::stationary`: `movementSlot` 0 and `placeSlot` not 0) [data: the slots of the template; live I4]. The tool takes only
  the slices of the list `targets` of `tools/gamedata/creature_slices.json` (each entry names its live check) [project rule: the record of
  each slice needs a picture, as the list `notShown`]: the cursed pirate banners (`totems/totem_pirate_cursed`, the siren variant), the
  scorpion egg clutch (`pois/brimstone/totem_scorpion_nest_sm`), and the corruption barb (`totem_corrupted`). The supported build has 100
  spawn points of 6 kinds. The kind keeps the family of its base row (no family for a totem, Wildlife for the nest) [data]; the backend gives
  it no family, no roam, and no attack.
- The record [live I4]: the metadata state (the asset of the slice), the PositionInTheWorld state at the place slot (type 13: the place of
  the spawn point and the rotation of its yaw; `encode_target_place_state`, `ComponentKey::targetPlace`), the vitals state, the marker
  state, and the status state at their slots; no movement state. The client shows the body and the name plate with this record
  ("Mistywater Cursed Banner" level 70, "Putrid Egg Clutch", "Sirensong Harmonic Resonator", "Corruption Barb").
- The hits and the kill [live I4; tests QG5C1B]: a hit goes through `Creatures::hurt` (the credit entry of `Rewards::hit`, the death at
  health 0, one `Rewards::killed`): the quest kills, the experience, and the loot of the vitals row are the rules of a kill of a creature
  (the kind category of the spawn point counts: `Corrupted_Totem_07SM05` of the kind `Lost_Totem_60`). The target takes no reaction, no
  target, no push (`ForcedMotion::impact` refuses it), and no knockdown or stagger (`Creatures::knock` refuses it) [project rule: the slice
  has no action table]. Its behavior binding is "a stationary target" (no tree): it never senses, roams, or attacks. A status effect can
  come on it (the icons of its StatusEffects slot, the ticks of a damage over time).
- The respawn [data; live I4]: the death gives its spawn point the respawn time of its spawner, as for a creature; the respawn removes the
  body and makes a new target (a new entity). The point spawners of the totems have 90 or 360 s; the area spawner of the nest has
  `m_minRespawnRange` and `m_maxRespawnRange` 360 [agent: the two values of an area spawner are its respawn time, the tool reads them for a
  spawner with no point spawner cooldown; client serializer 0x3C32830].
- The picture tour of stage QG5c1 (`tools/research/creature_tour.py`, 50 slices of the list `notShown`, the current record and the DLC of
  stage 1.1b): 14 slices show a body and a name plate now and leave the list [live: `shots/tour/sheet-00.jpg`, `sheet-01.jpg`]: for example
  `anubianguardian_horus` (the named Horus of `1605_0701_brokenkey`, live I3: the kill counts for its task, and its loot table gives an item),
  `corruptionheavy_dt5` (Ser Ballard of `99B_MSQ_02_meetgalahad`), `dynastyheavy`, `sand_elemental_heavy`, `evil_knight_heavy`, the season
  slices `season_02_herikai`, `season_04_frost_king`, `season_02_sulfur_elemental_heavy_n1`. The cause of the change is not proved (the
  stages after the tours of stage 3.4c changed the record: the idle record of user test 2, the status state, the target bits). 36 slices stay
  (the chameleon, the armored dragon, the hercyne creatures, the upyr creatures, the swarmers, the wardens, and others).
- The kept-out causes (the member `keptOut` of the tool, `CreatureData::kept_out`): the names of the vitals rows and of the spawn
  categories of the entries that the file does not have, with the cause (the slice that the client does not show, an encounter entry, an
  override entry, an entry that the level data does not spawn). The support manifest names the cause in the reason of a kill task. 736
  names in the supported build.


### The members of the encounters (stage QG5c2)

The source of each rule is in brackets, as above ([live] here: the live checks of stage QG5c2; [Encounters](encounters.md)).

- A member of an encounter recipe is a kind of `creatures.json` with no spawn point: `member` names its member slice, and the list
  `members` of the file gives each member slice its kind (or -1 and the cause of a member that the tool leaves out: 22) [data].
  `CreatureData::member_kind` finds it; a loop over the spawn points does not [agent]. Its level is `m_vitalsLevel` of the member slice,
  else the level of its variant, else the most common level of the IsEncounter entries of its vitals row [data].
- Only the encounter system makes and removes a member (`Creatures::spawn_member`, `remove_member`); the creature keeps the placement and
  the generation of its instance (`Creature::encounter`), and its death calls `Encounters::member_died` [agent]. A member has no spawn
  point: it never respawns by itself, and the reset of the spawn points does not touch it [agent].
- `Behavior::provoke` gives a member the nearest participant that lives as its target at its spawn, after the aggro delay of the manager
  [data: `m_overrideAggroDelay`, `m_aggroDelayOverride`; live L2: with no provoke a member 3 m from the character did not see it].
- Code review of stage QG5c1, finding QG5C1R-R1: a hit ends the next-hit stun of a stationary target too; only the reaction and the
  target of the hurt stay behind `!stationary()` [agent; test case QGR5C1A].
- Stage QG5c3: a direct member (a creature slice itself in a spawn timeline: a CharacterComponent and a Vitals component with
  `m_rowReference`, no AIVariantProvider) has its vitals row from `m_rowReference` and its body from the slice; the creature tool makes
  its kind with the rules of a spawn point [data; [Encounters](encounters.md)]. In the supported build: the inquisitor cleric and the
  markswoman of the corrupted tree (the flamekeeper stays out: the client does not show its slice), the named anubian brute of the tomb of
  1650, the tendrils of Overseer Zane, and members of seasonal events. The build has 141 member kinds (133 before), and 18 members stay out
  with their cause.
- Stage QG5c3: the starved ghost of Torment (`ghost_starved_22.dynamicslice`) and the boss ghost of the siphon fight of 09A_S18
  (`worldevent_ai_09a_s18_ghost_shade_boss.dynamicslice`) are in `AREA_EXCEPTIONS` of `creature_behavior.py`: they have the shared action
  list of the ghosts (74 actions, the same as `ghost_charred`) with the plague nova, and their damage table has no row of the spell, as
  for the 14 ghost slices of the list [data]. Their behavior data is complete now, and their members have kinds (the four ghosts of
  Torment, the boss ghost "Scourge of the Storm": live QG5c3 L1, its name plate and its health bar).

## Open points

- Stage QG5c1: a stationary target has no server part of its slice: the area status of a corruption barb on the players near it
  (`Player_Status_Effect`), its self heal, and the scorpion child spawner of the nest are not applied; the placement tilt of a totem is not in
  the creature data (the place state has only its yaw).

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
  points of the whole world (stage QG5c1: 33,023 spawn points, 3,901 kinds, 521 slices, 25 regions, with 100 spawn points of stationary
  targets and 736 kept-out names; 32,840, 3,848, and 503 before; a file
  of 15 MB with the 19,814 patrol routes of format 3 and the time constraints of format 4 (stage 2.2: 405 points); 60 s with
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
  knockdown plays the fragment of the hit reaction. From user test 2 each slice also has `reactionLengths` (the length of
  the variant of each reaction fragment for the tags of its weapons and its default tags: the end blend, else the end of its
  animation from the CAF file; -1: not known), and the length of an attack with no end blend is the end of its animation (else its
  last contact plus 0.9 s): the counts `attackLengths` and `reactionLengths` (the supported build: 560 attacks from an end blend,
  1,411 from the animation, 338 guessed; 2,144 reaction fragments with a length, 621 with none). From stage 3.3b-S5 each slice also has `behind` and `head` (the hit
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
- The members of stage U3 (`creature_behavior.py`, `creature_data.py`; the check cases `u3-*` of `check_creature_tool.py`, test case
  CF11). An attack with Homing clips has a fourth member: one row of 17 values for each clip (start, duration or -1 to the end of the
  attack, MoveToTarget, MoveToDistance, MoveToDuration, MoveToVelocity, MaxMoveDistance, TurnRate, UseExponential, Radius, MaxAngle,
  Height, Lock, AllowMoveAfter, Upwards, UseRadius, RotateAfter; [Creature behavior](creature-behavior.md)). A slice has
  `reactionTable` (the rows of the table of its action `Reaction`: power, the angle ranges, the fragment index, the length of its
  variant, the time of its HitStun clip or -1, AlignToDirection, AlignOffsetAngle; `motion`, the entries of ReactionMotion with Scale
  and PushBackScale, `Default` first; `exit`, ExitOffsetSeconds) and `additive` (the same for `Reaction_Additive`, with its action and
  its scope, 2 to 15). A kind has `promotedElite` and `noReactions` (from `buffBuckets`). The counts of the supported build: `homing`
  (2,075 attacks, 4,658 clips, 2,055 clips that move), `reactionTables` (502 slices, 13,477 rows, 3 missing tables), `slicesWithReactionTable`
  (463), `slicesWithAdditive` (319), `kindsPromotedElite` (96), `spawnPointsPromotedElite` (221). A reaction table file that is not in
  the game copy (3 slices, for example the withered beetle) gives the slice no table: it keeps the rule of user test 2. A file of a tool
  from before stage U3 loads, with the rules of user test 2 and no homing.
- `tools/research/creature_tour.py` is the picture check of the creature slices: `prepare
  GAMEDATA TOUR` makes a game data folder with one kind for each slice, `run TOUR OUT`
  makes one creature of each slice in front of the character of a live run and takes one
  picture (`--list FILE` for some slices, `--wait SECONDS` for the time before the
  picture), `check OUT` removes a picture that is not a picture of the game, and `sheets
  OUT` puts 25 pictures on one sheet. A person must look at each sheet.

## Traps

- Stage QG5c1: a totem has no CharacterComponent: the server tests a shot against the default capsule (`projectile_rules`), and an arrow that
  the client aims at the side of a banner passes it (live: 0.6 m to the right at 5 m). Aim at the middle of the totem.
- Stage QG5c1: the client removes the body of a dead totem itself (its DelayedEvent `DestroyDelay`, 2.5 s, and a DestroyGDE component): the
  burst plays and the body goes away, while the entity stays in the world of the backend until the respawn or the distance rule.
- Stage QG5c1, a live kill with the behavior on: `tools/world ignore NAME on` after a hit makes the creature lose its target, go back to its
  anchor, and get its full health. Keep the ignore value off for the whole fight of a test that needs the credit, or start the run with
  `SOULWARDEN_CREATURE_BEHAVIOR=0`.

- The homing values of the data are not small: TurnRate 900400 and a clip duration of 66329 s (clips with no end), MoveToDuration 75 s,
  clips that start after the end of the attack, and TurnRate 1080 or 3.0 with UseExponential (a rate for each frame above 1 in 169
  slices). The loader takes each finite value of 0 or more; the world limits an exponential rate to 1 and ends a clip at the end of
  the attack (a backend that refused such a file did not start in the live run of stage U3).
- A reaction from the side of a creature turns it (AlignToDirection): a wolf hit from its right by a heavy arrow turns to the source.
  For a picture of a side reaction, take the picture from the side of the camera before the hit.
- A light hit on a Solo+ creature (an alpha wolf) at rest plays the additive reaction: a short flinch of the head and the shoulders,
  and a push of 0.35 m. At 10 pictures each second it is easy to miss: take the pictures from the side, near the creature.

- The yaw of `tools/world creature spawn` turns to the west: `--yaw 270` looks east, `--yaw 90` west. A creature that looks away does
  not see the character (user test 2: a ghost with `--yaw 105` 3 m west of the character took no target for 40 s).
- The light and the smoke of a ghost change in each frame: a measure of the change between frames does not show a frozen pose. Compare
  the pose at the same times after the start of an attack (user test 2: `UT2-2-before-after-ghost-attack1-end-side-by-side.png`).
- An Elite takes no reaction and no push from a light or a heavy attack (the reaction rule): use one as the target of a long test of
  the attacks of a player (`Ghost_Frozen_65_Elite`), so that it stays in the reach.
- From stage U3 an Elite+ that is not a promoted elite takes the additive push of a light arrow (0.35 m away from the source): after
  three arrows `Wolf_Elemental_Earth_ELITE_05WC10` was 0.6 m beside the line of the shots, and the next arrows passed it (stage B2). With
  `tools/world ignore NAME on` and the behavior on, a pushed creature also walks back to its place, immune. For a series of bow hits on
  one creature use a large body (`CorruptedLegion_Cyclops_ELITE`, 238,670 health) in a run with `SOULWARDEN_CREATURE_BEHAVIOR=0`.
- The icon of a status at the name plate of a creature is small (about 12 pixels): crop the frames near the name plate and scale them 4
  times to read the stack count ("x2", "x3"). The client shows no count for one stack.
- A slice of the tests with only `Idle_No_Task` has no idle action: its creature keeps the rule from before user test 2.
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
