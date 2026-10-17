# Forced movement

Read this document before a change of the pull or the push of a hit on a creature, of the dash of the caster of an
ability, or of an action that the server starts (the counter of Riposte), and before a live test of them (stage 3.3b-S4).
The wire contracts and the client code are in the protocol notes (section 4.26). The abilities that cause them are in
[Abilities](abilities.md), the creature in [Creatures](creatures.md) and [Creature behavior](creature-behavior.md), the line
of a movement in [Navigation](navigation.md), and the position check in [Position](position.md).

## Start here

| Task | Start here |
| --- | --- |
| The pull and the push of a hit on a creature: the impact of a damage row, the two rules of the client, the steps, the stop at a wall | `apps/backend/src/world/forced_motion.h` (`world::ForcedMotion`, `forced_rules`), `world/entity.h` (`ForcedMove`, `Creature::forced`), `Creatures::impact`, `Combat::strike` (the parameter `source`), test cases FM2 to FM5 |
| The impact values of the game data | `tools/gamedata/combat.sh` (`combat_tables.py`, the member `impacts`), `gamedata/combat_tables.h` (`DamageImpact`, `DamageRow::impact`), test case FM1 |
| The line of a forced movement on the navigation data | `Navigation::slide` (`world/navigation.h`), test case FM4 |
| The pull of the vortex of Gravity Well and the push of its burst; Path of Destiny | `PhasePlan::pull`, `Abilities::area_hit`, test case FM6 |
| The dash of the caster (Charge): the envelope of the position check, the dash of the server for a character with no client, the observers | `CasterMotion` (`world/abilities.h`), `Abilities::motion_allowance`, `Abilities::start_motion`, `MotionAllowance` (`world/position_report.h`), `ComponentKey::motion` and `encode_observed_motion_state` (`server/rep/states/movement_state.cpp`), test cases CM1, CM2, FM7 |
| The counter of Riposte: the window, the counter that the server starts, its hit | `CastPlan::window`, `PhaseKind::counter`, `Abilities::countered`, `Health::creature_hit`, test cases RC1, RC2 |
| The load of the forced movement | `ForcedMotion::tick`, `ForcedCounts`, test case FM8 |

## Rules

- The forced movement is a part of the creature system (`world/forced_motion.h`): only that system moves a creature. A hit
  that a creature takes and lives after (`Combat::strike`) moves it by the impact of its damage row
  (`gamedata::DamageImpact`: the columns ImpactDistanceX, Y, Z and ImpactDecayRate of the game data, and the same columns
  with "_AI"). The client does not move a creature of the server by itself (live check L7-A): the server moves it, and
  each observer gets its movement state with each new place.
- The values for a creature: the "_AI" distances of the row when the row has one that is not 0, and the "_AI" decay rate
  when it is above 0; else the normal values (client code 0x683D910 takes such a value when the row has one; game data:
  26 of 414 rows have an "_AI" distance).
- The direction: from the source of the hit to the creature, in the plane (the point of the hit for a projectile, stage
  3.3b-S3, [Projectiles](projectiles.md)). The source is the attacker for a melee hit and
  for a burst around the caster, the center for an area or a spell (`Combat::strike`, parameter `source`), and the
  attacker for a row with UseAttackerPosForReaction (Path of Destiny). The displacement is direction * Y + side * X, side
  = direction x up (client code 0x48A0690). A creature at the source (nearer than 0.05 m) has the direction north (the
  default of that code). Z is not a place: the creature stays on its floor.
- A decay rate above 0 (client code 0x48A0D30, `ReactionMotion::OnUpdate`): the travel at the time t of the reaction is
  |displacement| * (1 - decay^(30 t)). Maelstrom (Y -1.5, decay 0.8) pulls 1.5 m to the caster: 49 % in 0.1 s, 74 % in
  0.2 s, 97 % in 0.5 s. A decay rate of 0 or less (client code 0x488B8F0): |displacement| meters in each second to the
  source point for a negative Y (a pull), away from it for a positive Y; a pull ends 0.1 m from the point. The vortex
  of Gravity Well (Y -7) pulls at 7 m/s to its center.
- The length (user test 2, UT2-4; game data and client code): the client runs the push in the activity ReactionMotion of the action
  `Reaction` until the end of its fragment (IfFragmentDone), or of the action `Pushable` (ActiveTime 0.4 s) for a row with
  NoReaction. The mode comes only from the decay of the row (client code 0x48A0690: mode 1 for a decay of 0 or less; UsePushNoDecay
  of Pushable does not change it there). Thus the push of a hit lasts the reaction that the hit started (`HitResult::push`), or 0.4 s
  for a row with NoReaction (`kPushableTime`). From stage U3 the length of a full reaction is the rule of the clip HitStun
  ([Creatures](creatures.md), `Creatures::plan_reaction`): a fragment with that clip ends at the hit stun of the row (HitStun_AI / 30 s,
  in the limits of the play speed of the rest of the fragment); a fragment with no clip keeps its length (1.5 s with no length). A hit
  with no full reaction (a light hit on an Elite, the reaction limit) plays the additive reaction and pushes with its push scale (the
  rule of the additive push below); a promoted elite takes nothing in an additive reaction. A pull of an ability that is no hit (the
  vortex of Gravity Well, the pull of Reap) keeps 1.5 s. A heavy arrow (BowAttack2: 1 m/s, no decay, power 3, 22 hit stun frames) pushes
  the Inquisitor Cleric 0.73 m in 0.73 s (its `r_R3_F` with the clip HitStun; before stage U3: 1.33 m in 1.33 s). Code review of stage
  U3, finding U3-R9: this text and the trap of the slow walk had the rules from before stage U3 (corrected in stage B2).
- The end of a decay travel (a decision of an agent, user test 2): when less than 2 cm of it is left (`forced_rules::kRest`), the last
  step goes the rest; GreatAxe_PrimaryAttack (1 m, decay 0.85) ends after 0.8 s (before: the end at 1 mm held the creature for 1.47 s).
- The steps: one new place at most each 50 ms of the world clock (`forced_rules::kStep`, a decision of an agent: 20
  movement states in a second; the walk of a creature has 5). Each step goes on the straight line of the navigation
  (`Navigation::slide`): a wall, a drop (a floor more than a step lower), or a cell with no free floor stops the creature
  at the last place of the line, and the movement ends there. With no navigation data for the place: the line on the
  terrain, and nothing stops it.
- A creature on its way back (immune) and a dead creature do not move; a death ends a movement. A new impact replaces the
  movement that runs (the client starts its reaction motion again). A root, a stun, and a knockdown do not stop a forced
  movement (the vortex roots the creatures that it pulls; a decision of an agent from the design of the data).
- The behavior. While a force moves a creature, the behavior does not move it and starts no attack (`Behavior::think`,
  `go_home`, `roam`; `Creature::held_by_force`). At the end the creature goes on with its path (user test 2, UT2-4: the navigation
  keeps a path that is 1 m or less away, `navigation_rules::kOffPath`, and takes the free straight line at once for one that is
  farther), and a creature in a fight keeps the new place in its trail (`Behavior::moved_by_force`).
- The push scales (stage U3, research Q5; game data: the activity ReactionMotion of the action of the hit; client code 0x48A0690): the
  displacement is multiplied by Scale, and by PushBackScale when it goes away from the source (a push; a pull keeps its length). The
  entry of the action is the first that holds of `ReactionLimiter_Active` (the additive table in the reaction limit), `PromotedElite`
  (a kind with the status AI_Elite_Unbreakable_Grit_Buff in its buff buckets), and `Default`. A promoted elite moves 0.15 of a push in a
  full reaction and nothing in an additive one. The log line of the reaction gives the scale (`push scale 0.15`).
- The push of an additive reaction (stage U3; decision (c) of the orchestrator, and live check LC-Q5-2 for the run): a hit with no full
  reaction plays the additive reaction ([Creatures](creatures.md)) and pushes with its push scale (a light arrow on a Solo+ wolf: 0.5
  m/s for 0.7 s, 0.35 m). This push does not hold the behavior (`ForcedMove::additive`): the run, the attack, and the homing go on, and
  the two movements add. The push and the velocity rule of a homing take their step from the place of the creature; the time rule of a
  homing adds the change of its curve, which a push does not move (code review of stage U3, finding U3-R2, test case UR2: before the
  correction the curve took back most of the push). Live check LC-Q5-2 showed the reason: a push that held
  the behavior stopped the run of a wolf for the 0.7 s of the push, and the client showed the stop. A push of an additive reaction does
  not replace the push of a full reaction that runs (a rule of an agent; `ForcedCounts::keptFull`). A full push stops the homing of an
  attack; an additive push does not. Test case CF12.
- The action of the creature during the movement: the reaction of the hit (stage 3.2a: `Reaction` for a creature that
  plays no attack). The movement keeps the action (`Gait::place`): the client plays the reaction while the creature
  slides on the line.
- The abilities (game data): Maelstrom and Maelstrom with Storm's Reach pull (Y -1.5, -2.0; the second spin, NoReprieve,
  has no impact); the start of the vortex of Gravity Well pulls each creature of its sphere (the child entity VortexPull of
  the slice: the row GreatAxe_GravityWell_Vortex, damage factor 0, `PhasePlan::pull`); the burst pushes 0.5 m (decay 0.3);
  Path of Destiny pushes 2 m from the caster (decay 0.9); Shockwave pushes 0.25 m; Charge 0.25 m; the swing of Gravity
  Well 0.5 m. Wrecking Ball and its node Pushback have no impact distance: they move nothing. The basic attacks of the
  great axe and the war hammer push too (GreatAxe_PrimaryAttack: 0.5 m, 1.0 m for a creature).
- The abilities of stage 3.3b-M2 (game data; [Abilities](abilities.md)): the pull of Reap (the rows of its second strike:
  `GreatAxe_JudgementsReach_Pull` Y -2.0, `_upgrade` -4.0, `_upgrade2` -6.0, decay 0.8; the `_reduced` rows of Fatal
  Attraction -1.25, -3.0, -4.5; the `_RP2` rows of Gravity the same distances), with the row of the shape nearest to the
  creature. The client reports no contact for it: the server tests the shapes (`Abilities::follow_strike`). The push of Clear
  Out (`Warhammer_Damage_ClearOut`: Y 3.8, decay 0.85; live run of stage 3.3b-M2: 3.80 m in 1.5 s). Whirlwind 0.25 m, Execute
  1.0 m, Armor Breaker and Mighty Gavel 0.5 m from the caster.
- The dash of the caster (Charge, `CasterMotion`). The client moves its character itself with the root motion of the
  action (live check L7-C: 13.77 m in 1.0 s with no creature in front, 14.73 m in 1.6 s with the swing; a step of 0.602 m
  in 1/30 s at most). The server gives no second displacement: the owner gets no movement state with a position. The
  position check accepts the dash in the envelope of the accepted cast (`Abilities::motion_allowance`): the window gets
  14.73 m x 1.3 more and a step can have 18.07 m/s x 1.3 (1.3: the largest MotionMultiplier of the action
  GreatAxe_Engage, game data), from the press for 1.6 s and one window (1 s) after it; the fast step only for the 1.6 s.
  Without it the check refused 7 reports of one dash (L7-C). A refused press gives no envelope. One cast gives its extra
  distance one time: in the envelope the distance from the place at the press is not more than the normal travel of the
  time since the press and the extra distance (code review of stage 3.3b-S4, finding F1, test case RM1).
- The observers of a dash get the state `motion` (`ComponentKey::motion`, the observer view of the movement state: the
  place, the direction of the dash, and the actions of layer 0 of the client: the state 7 with the sequence 59 for the
  dash, 2312 for the swing, then 30/1080 at rest; live check L7-D). The world marks it at an accepted report of the client
  at most each 50 ms. A character with no client (a remote test character, `tools/world cast`) gets the dash from the
  server: 13.77 m in 1 s on the line of the press, or to 1 m in front of the first creature on the line, through the
  navigation (`World::move_character`).
- The counter of Riposte (`CastPlan::window`, `PhaseKind::counter`). An accepted press puts the window on the caster: the
  status Rapier_Riposte_Timer of the data (1 s; its absorption of 2 takes the hit). A creature hit in the window
  (`Health::creature_hit`, after its damage) is a counter, one time for each cast: the window ends, the server starts the
  counter, and at 0.433 s (the capsule of the fragment Ability_Rapier_Riposte_Out) it hits the attacker one time with the
  row 1H_Rapier_Riposte_Stun (its StatusEffect Rapier_Riposte_Stun: 2 s; the _Enhanced row and stun with the node
  Rapier_Riposte_Mod3), when the attacker is still in the reach of its hit plus 0.3 m with no wall. The row pushes it 1.5
  m. A hit after the window, or with no window (a press in the cooldown), is not a counter.
- The counter needs the weapon of its cast in the hand (code review of stage 3.3b-S4, finding F2, test case RM2): a change
  of the weapon ends the window, and a hit with a different weapon starts no counter ([Abilities](abilities.md)).
- The client action of the counter is not started (open, the rapier part): Ability_Rapier_Riposte_Out has ServerStarted
  and the activation `Not True`; the client starts such an action only from the action layers of a movement state of the
  server (client code 0x2A66E50), and the sequence number of that action is not known (live check L8). The client plays
  its loop and then its idle pose; the hit, the stun, and the push of the counter show.
- The load: the step looks only at the creatures that a force moves now, each one at most each 50 ms, and at the cells of
  its short line. Test case FM8: 200 movements in a world of 4,200 creatures, 60 ticks in less than 2 s.

## Commands

```sh
tools/world list                     # the lines "abilities" (with the counters) and "force" (the movements of the creatures)
tools/world creatures                # the mark MOVED BY A HIT (ROW) for a creature that a force moves now
tools/world track ID 3 --tsv FILE    # the places of a creature in a pull or a push
tools/world cast Remote 0 --yaw 270  # Charge of a remote test character (a dash of the server), to the west
tools/gamedata/combat.sh [OUT.json]  # the combat tables with the member impacts (234 rows with an impact)
```

- `tools/world list` prints `force: N creatures move by a pull or a push now, N movements started (N replaced, N not
  taken), N steps, N stopped by a wall or a drop, the largest number at one time N`.

## Traps

- A creature with a slow walk and heavy arrows (a result from before stage U3: user test 2, live run `u2-after`): the Inquisitor Cleric
  walks 1.25 m/s, and each heavy arrow pushed it 1.33 m in 1.33 s; with a heavy arrow each 2 s it went back. From stage U3 the push of a
  heavy arrow on it is 0.73 m in 0.73 s (the clip HitStun), and the Cleric comes nearer between two arrows. A melee grunt (5.5 m/s) comes
  in 15 s to 20 s from 24 m.
- The log line of a push gives its longest time (`for 1.33 s at most`), and the line of the reaction its power and its hit stun
  (`world: reaction: creature ... plays the reaction of power 3 (fragment 26, front 0 degrees) for 0.73 s (the HitStun clip), a hit stun
  of 0.73 s, the chain 1`). The push of an additive reaction has the end `(the push of an additive reaction: the behavior goes on)`,
  and its stop line gives the travel of the push only, not the distance from the start (the creature also ran).
- A combat tables file of a tool from before stage 3.3b-S4 has no member `impacts`: the backend starts and no hit moves a
  creature. The start log then has the line `the combat tables have no impacts` (else `234 damage rows with an
  impact`). Make the file again with `tools/gamedata/combat.sh`.
- With `tools/world ignore NAME on` a pulled creature at rest does not fight; a creature of a slice that wanders walks
  to its home and wanders again after the movement (and after the root of the vortex).
- The dash of Charge goes in the direction of the camera of the client: turn the camera before a press (a dash of a
  test went into a wall after 5.8 m).
- The projectile of Gravity Well flies in the direction of the camera of the client (its shot), not in the direction of
  the body.
- A creature in the pull of the vortex stops 0.1 m from the center: three wolves stand on one place. The crowd rule of
  the behavior separates them only when they go to a target.
