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
- The direction: from the source of the hit to the creature, in the plane. The source is the attacker for a melee hit and
  for a burst around the caster, the center for an area or a spell (`Combat::strike`, parameter `source`), and the
  attacker for a row with UseAttackerPosForReaction (Path of Destiny). The displacement is direction * Y + side * X, side
  = direction x up (client code 0x48A0690). A creature at the source (nearer than 0.05 m) has the direction north (the
  default of that code). Z is not a place: the creature stays on its floor.
- A decay rate above 0 (client code 0x48A0D30, `ReactionMotion::OnUpdate`): the travel at the time t of the reaction is
  |displacement| * (1 - decay^(30 t)). Maelstrom (Y -1.5, decay 0.8) pulls 1.5 m to the caster: 49 % in 0.1 s, 74 % in
  0.2 s, 97 % in 0.5 s. A decay rate of 0 or less (client code 0x488B8F0): |displacement| meters in each second to the
  source point for a negative Y (a pull), away from it for a positive Y; a pull ends 0.1 m from the point. The vortex
  of Gravity Well (Y -7) pulls at 7 m/s to its center.
- The length: the length of the reaction of the creature (`Creatures::kReactionTime`, 1.5 s) at most. The client runs
  the activity in the action of the reaction until the end of its fragment; the backend has no length of a fragment.
- The steps: one new place at most each 50 ms of the world clock (`forced_rules::kStep`, a decision of an agent: 20
  movement states in a second; the walk of a creature has 5). Each step goes on the straight line of the navigation
  (`Navigation::slide`): a wall, a drop (a floor more than a step lower), or a cell with no free floor stops the creature
  at the last place of the line, and the movement ends there. With no navigation data for the place: the line on the
  terrain, and nothing stops it.
- A creature on its way back (immune) and a dead creature do not move; a death ends a movement. A new impact replaces the
  movement that runs (the client starts its reaction motion again). A root, a stun, and a knockdown do not stop a forced
  movement (the vortex roots the creatures that it pulls; a decision of an agent from the design of the data).
- The behavior. While a force moves a creature, the behavior does not move it and starts no attack (`Behavior::think`,
  `go_home`, `roam`). At the end the path of the creature is forgotten (its next step searches from its new place), and a
  creature in a fight keeps the new place in its trail (`Behavior::moved_by_force`).
- The action of the creature during the movement: the reaction of the hit (stage 3.2a: `Reaction` for a creature that
  plays no attack). The movement keeps the action (`Gait::place`): the client plays the reaction while the creature
  slides on the line.
- The abilities (game data): Maelstrom and Maelstrom with Storm's Reach pull (Y -1.5, -2.0; the second spin, NoReprieve,
  has no impact); the start of the vortex of Gravity Well pulls each creature of its sphere (the child entity VortexPull of
  the slice: the row GreatAxe_GravityWell_Vortex, damage factor 0, `PhasePlan::pull`); the burst pushes 0.5 m (decay 0.3);
  Path of Destiny pushes 2 m from the caster (decay 0.9); Shockwave pushes 0.25 m; Charge 0.25 m; the swing of Gravity
  Well 0.5 m. Wrecking Ball and its node Pushback have no impact distance: they move nothing. The basic attacks of the
  great axe and the war hammer push too (GreatAxe_PrimaryAttack: 0.5 m, 1.0 m for a creature).
- The dash of the caster (Charge, `CasterMotion`). The client moves its character itself with the root motion of the
  action (live check L7-C: 13.77 m in 1.0 s with no creature in front, 14.73 m in 1.6 s with the swing; a step of 0.602 m
  in 1/30 s at most). The server gives no second displacement: the owner gets no movement state with a position. The
  position check accepts the dash in the envelope of the accepted cast (`Abilities::motion_allowance`): the window gets
  14.73 m x 1.3 more and a step can have 18.07 m/s x 1.3 (1.3: the largest MotionMultiplier of the action
  GreatAxe_Engage, game data), from the press for 1.6 s and one window (1 s) after it. Without it the check refused 7
  reports of one dash (L7-C). A refused press gives no envelope.
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
