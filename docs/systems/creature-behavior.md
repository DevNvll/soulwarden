# Creature behavior

Read this document before a change of the behavior of a creature (the sense, the fight, the way back,
patrol and wander, the rules of a fight that a player can play) and before a live test of a fight.
The places of a creature come only from [Navigation](navigation.md). The wire contracts are in the
protocol notes (sections 4.14, 4.17, and 4.19).

## Start here

| Task | Start here |
| --- | --- |
| The behavior of a creature: the sense, the way to a player, the attack and its contact test, the way back, the behavior data of a slice, the two switches | Protocol notes 4.17 and 4.19, `apps/backend/src/world/behavior.h`, `world/navigation.h`, `world/creatures.h`, `gamedata/creature_data.h`, `tools/gamedata/creature_behavior.py`, `tools/world ignore`, test cases FT1 to FT14 |
| Patrol and wander of a creature: the routes of the spawn points, the wander values, the walk out of a fight, which creatures roam, the anchor of a fight, the reset (immune, the trail back), the wait at the place nearest to a target with no way | Protocol notes 4.14, 4.17, and 4.5, `apps/backend/src/world/behavior.h` (`behavior_rules`), `world/navigation.h` (`next_step` with `nearest`), `gamedata/creature_data.h` (`PatrolRoute`), `server/rep/states/vitals_state.cpp`, `tools/gamedata/creature_data.py` (`read_patrol`, `world_route`, `check_creature_tool.py`), `tools/world creatures`, `tools/world creature anchor`, test cases PT1 to PT10 and RP1 to RP9 |
| A fight that a player can play: the 10 s with no target after a travel, the health that comes back, light and heavy attacks of a creature and their pause, creatures that do not stand at one place | Protocol notes 4.19, `apps/backend/src/world/behavior.h` (`behavior_rules`), `world/health.h`, `gamedata/creature_data.h` (`kContactGap`, `kHeavyWeight`), test cases PL1 to PL4 |
| The shot of an archer (stage 3.4f-A): the band, the choice of melee or shot, the windup and the release, the aim and the lead, the repeat time, the walk back, the cancels | Protocol notes 4.37, `apps/backend/src/world/behavior.h` (the rule Shoot, `behavior_rules::kShot*`, `Behavior::aim`, `command_shot`), `world/projectiles.h` (`launch_creature`), `gamedata/creature_data.h` (`RangedRelease`, `kBasicShotAction`), `tools/world creature shoot`, test cases CRG2 to CRG5 |
| The cast of a caster (stage 3.4f-B): the cast as the basic shot, the band of a caster, its repeat time; the corrections CR-R1 and CR-R2 of the review of stage 3.4f-A | Protocol notes 4.38, `world/behavior.h` (`behavior_rules::kCast*`, `kSpawnLead`, `Behavior::band_of`), `gamedata/creature_data.h` (`kBasicCastAction`, `CreatureSlice::caster`), test cases CRB1, CRG9, CRG10, RR1, RR2 |
| The area attack of a creature (stage 3.4f-C): the choice, the range, the repeat, the walk of a creature with only area attacks, the casts and their cancels, the place; the correction CS-R4 (the spawn lead for a new observer) | Protocol notes 4.39, `world/behavior.h` (`behavior_rules::kArea*`, `Behavior::think_area`, `start_area`, `cast`, `area_place`, `command_area`, `spawn_lead_over`), `gamedata/creature_data.h` (`CreatureAreaAttack`, `AreaCast`), [Spell entities](spells.md), `tools/world creature area`, test cases CRA6, RZ4 |
| The behavior trees (stage BT1): the format of a tree file, the loader and the compiler, the runtime, the leaves, the default tree, the blackboard, the folder of a development run, the reload, the view of a tree | This document, section "Behavior trees" below, `apps/backend/behavior/*.jsonc` and `tree.schema.json`, `world/behavior_tree.h`, `world/behavior.h` (`decide`, `run_tree`, `run_node`), `tools/world creature tree`, test cases BT1 to BT5 and BTD1, research `bt0-ai-data.md` |
| The binding of a tree to a creature (stage BT2): the binding file and the order of its keys, the server tree of a slice, the subtrees of the default tree, the refusal of a tree that names an action that the slice does not have, the tree command, the tree in the tab "Creatures"; the corrections of the review of BT1 | This document, section "Behavior trees" below, `apps/backend/behavior/bindings.jsonc` and `bindings.schema.json`, `world/tree_binding.h`, `world/behavior.h` (`bind`, `fits`, `set_tree`), `tools/gamedata/creature_behavior.py` (`read_server_trees`), `tools/world creature tree ID --set NAME`, test cases BTB1 to BTB6, RBT1 to RBT7, BT5 |
| The status actions of a creature (stage 3.4f-D): its heal at low health, its buff, the marks and their cancels, the repeat; the place rule of the client for a cast (at the caster, at the target, at the aim point) | Protocol notes 4.40, `world/behavior.h` (`behavior_rules::kStatusRepeat`, `Behavior::think_status`, `wants`, `start_status_action`, `status_mark`, `command_status_action`, `place_rule`, `area_range`), `gamedata/creature_data.h` (`CreatureStatusAction`, `StatusMark`, `CreatureSlice::lowHealth`), [Creatures](creatures.md), `tools/world creature status-action`, test cases CRH1 to CRH5 |

## Rules

- The homing of an attack (stage U3, research Q1; game data: the Procedural clips of the type Homing in the ProcLayer of each attack
  fragment; client code 0x255A140, 0x2564160, 0x2565210, 0x25666A0; `world/attack_motion.h`, `Behavior::start_attack`). The client
  applies a Homing clip only to its own character and puts a creature at the place of the server (live checks LC-Q1-1 and LC-Q1-2):
  thus the server moves and turns the creature in the clips of its attack. A clip starts at the sum of the Blend ExitTime before it
  and lasts to the ExitTime of the next Blend (-1: to the end of the attack). At its start the target must be in the wedge of the clip
  (Radius, Height, MaxAngle), else the clip does nothing. The turn: TurnRate degrees each second, or 1 - (1 - TurnRate)^(30 dt) of the
  angle with UseExponential (a rate above 1 counts as 1). The move with MoveToTarget: to MoveToDistance from the target, at most
  MaxMoveDistance from the place at the start; u = min(t / MoveToDuration, 1), w = u^2 (3 - 2u), each step at most MaxMoveDistance /
  MoveToDuration x dt. Steps of 50 ms on the line of the navigation (a wall ends the move); the record of the attack stays. The ends: the
  clip, the end or the replacement of the attack, the death, the way back, a stun or a knockdown, and the push of a full reaction (the
  push of an additive reaction adds to the homing). The log line at the end: `world: homing: creature ... moved 0.85 m in the attack
  Attack_Bite (2 clip(s)): its clips ended`. Live (stage U3): before, a wolf bit from 1.6 m and its body came back to its place after
  the bite, and a ghost swiped from 1.6 m and came back the same way; after, the wolf ends its bite near the character and the ghost
  follows a side step of 1 m (homing 0.47 m to 1.36 m). The root motion of the animations (phase B of the research) is not done: the
  client plays a remote lunge in place (LC-Q1-1). Test cases CF1 (the time rule, two sessions) and CF9 (the load: 200 homings in a world
  of 4,200 creatures).
- The corrections of the code review of stage U3 (stage B2; `world/attack_motion.cpp`, `HomingClipRun`):
  - A root (`Creature::rooted`) holds the place of a creature in the homing of its attack, as in each step of the behavior: the turn and
    the attack go on, and the curve of the time rule goes on with no travel, so the blocked travel does not come back at the end of the
    root (finding U3-R1, test case UR1).
  - The time rule takes the change of its curve as the step (the curve place is apart from the place of the creature; the line to the
    target goes from the curve place): a push of an additive reaction between two steps stays (finding U3-R2, test case UR2). The
    velocity rule steps from the place of the creature.
  - Each clip starts at its own time, also while an earlier clip runs: the clips of one attack run at the same time, each with its own
    origin, target place, turn, and move (finding U3-R6, test case UR6; the exporter collects the clips of each procedural layer, and 32
    attacks of the common data have clips that overlap, for example `Attack_Rapier_Flurry` of `lost_pirate_spectral_1hsword`). Two
    clips that turn at the same time both turn the creature (a decision of an agent: the data has no rule for an overlap).
- The behavior of a creature (protocol notes 4.17; project rules, in `world/behavior.h`, with
  each constant in the namespace `world::behavior_rules`). The game files give the values of
  a slice (the vision, the leash, the speeds, the attacks) and no rule. A creature at rest
  gets a character as its target when the character is in its vision (the distance of its
  slice, and the angle of its slice to each side of its direction), or when the character
  hurts it. It goes to its target with the run speed of its slice, in steps of 200 ms of the
  world clock, and it starts an attack of its slice when the target is in the reach of the
  attack. An attack is an action of the server with one contact or more: the world tests
  each contact at its time on the world clock (the target is in the world, lives, is in the
  reach and in the angle), and one contact that passes is one damage result
  (`Health::creature_hit`). A creature loses its target when the target is dead, left the
  world, is ignored, or is farther than 60 m, and at the end of its leash: it then goes back
  to its anchor (the reset, below). A creature whose target has no way to it goes to the
  place nearest to the target (`next_step` with `nearest`), waits there with its target for
  12 s (`kNoWayHold`; the time starts at that place, not on the way to it, test case RP4;
  each attack starts the time again), attacks it in its reach, and then
  goes back. A creature has one target. From stage 3.4d a wall stops the vision, the attack, each contact of an
  attack, and the way (the navigation data, protocol notes 4.19; a ceiling between two
  bodies on one vertical line too). A kind with no attack that the data tool can use (747
  of the 3,848 kinds from stage 3.4f-D; 789 in stage 3.4f-C, 814 in stages 3.4f-A and 3.4f-B, 859 before the basic shot) takes no target and takes damage; it patrols or wanders with the roam
  data of its spawn point and of its slice, as each other creature (test case RP9).
- A summon as a target (stage 3.3b-S2b, [Spell entities](spells.md)). A creature that a shot of a summon hurts takes the
  summon as its target (the Pylon: `OverrideTargetForAiThreat` of its spell row; `Behavior::hurt_by` with the threat of the
  hit), when it has no target. It walks to the summon and attacks it; a contact on the summon goes to the spell system
  (`Spells::creature_hit`: the damage of the creature rule with the armor of the vitals row of the summon). A summon in its
  end chain, with the health 0, or that left the world is not a target: the creature loses it ("the summon ends") and goes
  back. A trap and an aura are not targets.
- Stage QG5c5: a member of an encounter has no spawn point; a part of a group member with a patrol path of the data has its route in its
  entity (`Creature::memberRoute`: the waypoints of its AIPathComponent in world coordinates, turned by the yaw of its spawn location), and
  `Behavior::route_of` returns it. The rules below apply to it: at rest it walks its route (live QG5c5 L3: the navigator of the boat landing
  walks from the boat along its path); a target of the encounter (the aggro of the manager) comes first.
- Patrol, wander, and the reset (stage 3.4e, protocol notes 4.14 and 4.17; project rules,
  each constant in `behavior_rules`). The routes of the spawn points and the wander values of
  the slices are game data (`creatures.json` of format 3: the member `routes`, value 11 of a
  spawn point, and the members `ambient` and `wander` of the behavior of a slice). A creature
  at rest roams only while a character is in the leave distance of its interest
  (`World::interest_leave`, 120 m at most); a different creature stands and needs no time. It
  walks with the walk action of its slice (`Move`, `CreatureSlice::walkAction`, the gait
  `Gait::walk` of `Creatures::move`) and its ambient speed, to the next waypoint of its route
  (the policy: a loop, back and forth, at random) or to a random place of its wander range
  (the generator of the world: `World::random_unit`, `WorldOptions::randomSeed`; no other
  random source in the world). At a waypoint it waits for a random time of its range and
  plays its task only when the action table of the slice has an action with that exact name;
  a negative wait is no wait, but the waypoint of a route of one waypoint and a waypoint whose
  task ends with `_LOOP` keep the creature there for ever (after a fight it goes there again
  and plays the task again, test case RP8). A creature is at a waypoint, at a place of its
  trail, and at its anchor only on the floor of that place (`behavior_rules::kFloorTolerance`,
  1.0 m): a place on a different floor is no arrival, and the navigation decides (test cases
  RP1, RP2). The anchor of a fight is the place
  where the creature took its target (`CreatureMind::anchor`): the leash is measured from it,
  the way back ends there, and the patrol or the wander goes on from it. On the way back
  (the mode `home`) a creature is immune: it takes no damage (`Creatures::hurt` gives
  `CreatureError::immune`, `Combat::claim` the verdict `immune`) and no target by a hit or by
  its vision, and its vitals state has `invulnerability` and `displayImmuneWhenInvulnerable`
  1 for each observer (`Creature::immune`, `immuneChanged`). It has its full health at the
  anchor. A way back with no path walks the places of the way out back (the trail, one place
  for each meter, 512 at most), and a creature is at its anchor in one step only when no
  session can observe it (`Behavior::observed`): never a jump in front of a client. This is
  true also for a creature that cannot walk (no free floor at its place): it is not at its
  anchor because it cannot walk, and while a session observes it, it waits (review of stage
  3.5a, finding F6, case RL6).
- The effects of an ability (stage 3.3a, protocol notes 4.22, [Abilities](abilities.md)). The
  behavior obeys the effects of `Creature::effects` and `Creature::knockedUntil` at each step:
  a stun, a knockdown, and a stagger (`Creature::held`) stop each step of the creature (no
  movement, no attack, no turn; it keeps its target); a root (`Creature::rooted`) stops each
  movement (on the way to a target the creature turns to it and attacks it in the reach of
  its attack, as a creature that does not walk); a slow (`Creature::speed_factor`, the
  strongest slow) lowers the speed of each step: the way to a target, the way back, the
  trail, and a roam. A creature with a root or a stun on its way back waits at its place: it
  is not at its anchor. A creature on its way back takes no new effect.
- A forced movement (stage 3.3b-S4, [Forced movement](forced-movement.md)): while the pull or the push of a hit moves a
  creature (`Creature::forced`), the behavior does not move it and starts no attack, also on its way back and on a roam.
  At its end the creature goes on with its path (user test 2, UT2-4: no new search while it is 1 m or less from its path; the step
  comes in the next step of the behavior; case UX5), but only when the straight line from its new place to the next corner of the
  path, or to its next cell, is free, also on the real segment from its new place (code review of user test 2, finding U2-R1, and of
  stage B0, finding B0-R1; [Navigation](navigation.md)), and a creature in a fight
  keeps the new place in its trail (`Behavior::moved_by_force`).
- The hit stun (user test 2, UT2-9, [Creatures](creatures.md)): for HitStun_AI frames after a hit that started a reaction
  (`Creature::hitStunUntil`) the behavior does not step, turn, or attack: the reaction plays, and the walk starts again after it (one
  new locomotion record, not one after each hit of a walk that the next step cut after 200 ms). A hit during an attack starts no
  reaction and no hit stun (the grit of an attack). The rule is also for a roam (a wander or a patrol of a passive creature, which
  takes no target) and for the way back (`Behavior::hit_stunned`; code review of user test 2, finding U2-R5; test case RT5).
- The length of an attack (user test 2, UT2-7, game data): the end blend of its fragment, else the end of its animation (the CAF file
  of the clip), else its last contact plus 0.9 s. At its end each observer gets the idle record ([Creatures](creatures.md)), and the
  pause of the attack starts.
- A knockdown and a stagger (code review of stage 3.3a). A slice with no reaction fragment for
  the power still gets the hold of the knockdown: no movement and no attack for its time (finding
  F5). A shorter or weaker knock does not end an earlier one: the later end and the stronger
  reaction stay (F6). At the end of a knock while a stun runs, the creature plays its action
  `Stun` again (F7).
- A creature with no way to its target (stage 3.4e) holds 12 s at the nearest place. A step
  on a new path ends the hold, also a step on a partial path to a new nearest place: a new
  hold starts at the end of that path (review of stage 3.5a, finding F7, case RL7).
- The rules for a fight that a player can play (stage 3.4d, protocol notes 4.19; project
  rules, each constant in `behavior_rules`, `world/health.h`, or `gamedata/creature_data.h`).
  The shelter: for 10 s after a character came to a place (a world entry, a travel, a
  respawn, a revive) the vision of no creature takes it as a target; the time starts at the
  first position that its client reports from the place and ends 60 s after the arrival at
  the latest; a creature that the character hurts takes it in this time too; a creature
  that has the character as its target loses it at a travel (`Behavior::shelter`, the
  values are in `Entity::life`; `WorldOptions::shelterTime`, which the test host sets to 0
  for the cases of the other rules). The rest: a character that took no damage and did no
  damage for 6 s gets 3 percent of its maximum health each second (`Health::tick`; a
  creature that takes the character as its target, and each attack, start the 6 s again).
  The attacks: two contacts of one attack that are 0.15 s or less from each other are one
  hit (`CreatureContact::repeat`); a creature makes its light attacks in the sequence of the
  file and one heavy attack after each three of them (`CreatureAttack::heavy`); the pause
  after an attack is 1.5 s, and 2.5 s after a heavy attack. The crowd: a creature does not
  end a step of its way to its target 0.5 m or less from a different creature that lives
  (it makes a shorter step or waits, `Navigation::take_back`), and it attacks from its
  place when the target is in the reach of its attack; creatures with the same target wait
  for each other, and a creature goes past a creature with a different target (or with
  none) after 10 steps of waiting, also in the last steps of its way. A new rule of the
  behavior keeps the cost rule: no scan of each creature
  of the world.
- The shot of an archer (stage 3.4f-A, protocol notes 4.37; research `creature-ranged.md`). A creature whose slice has the basic shot
  (the ranged attack of the action FireBow of its action table, `gamedata::kBasicShotAction`; 20 slices: the Evil Knight, Lost Knight,
  and Lost Mercenary archers, the Skeleton Archers, the Dryad Archers, the Damned Bowmen, and their variants) fights a character with
  it. The rules and the source of each one:
  - Which attack (a decision of an agent, the scope of the stage): the basic shot only; the other ranged attacks of the data (a charged,
    a fast, a triple shot, a cast) are in the data and wait for later parts. A creature with no melee attack and the basic shot fights
    too (the Dryad Archers); one with neither takes no target. The shot is only at a character: a creature with no melee attack that
    takes a summon as its target (the rule of the summon above) turns to it, does not attack it, and loses it at the end of the summon.
  - The band (a project rule, research Q3 option 1: the game files have no keep-distance rule and no behavior tree): a target in
    `kShotFar` (20 m, horizontal) with a free line (the vision line of the navigation data) and in `kShotHeight` (12 m) gets the shot; a
    creature farther, or with no free line, walks on its path to `kShotHold` (18 m: 2 m of the band for a target that moves some steps);
    a creature nearer than `kShotNear` (8 m) walks back to `kShotMiddle` (14 m) on a path, when the place is in its leash, at most one
    time in `kRetreatPause` (6 s); with no such path it shoots from its place. Reason: a fight that the player can drive (an archer that
    stands still at each distance cannot be pushed back), with no endless kite (the pause), in the 100 m range of the profile.
  - Melee or shot (a project rule, research Q3 option 1): the melee attack of the slice wins when the target is in the reach of its next
    melee attack (the reach of the data and `kContactTolerance`); the shot in the band. The choice holds for one action: an action plays
    to its end with no step, no turn, and no other attack (a hit plays no reaction while a shot plays, as for an attack).
  - The windup and the release (game data): the action of the shot (FireBow with the tags of the slice, its time from the animation data:
    2.47 s for the Evil Knight archer) and the release at the time of its CAGE-RangedAttack event (1.333 s; 0.65 s for the Dryad Archer).
    The world keeps each release on the world clock with the generation of its action (`PendingRelease`). The release flies only when the
    action is still the action of the shot (a stun, a knockdown, or a command replaced it: no shot, research A5), the creature lives (its
    death before the release: no shot), and the target is there (its death or its leave: no shot). These ends are rules of this project
    (research A6).
  - The aim (a decision of an agent with the values of the data): the origin `kMuzzleHeight` (1.5 m) above the feet and `kMuzzleForward`
    (0.6 m) in front of the creature (live check L2: the arrow leaves the bow); the point: the middle of the capsule of the target (the
    capsule of the player slice, game data). The lead (research Q3 option 1, with the values of the clip): with `AILeadTarget`, a
    constant-velocity intercept of the velocity of the target at the release (`World::velocity_of`: the accepted positions of the last
    60 ms or more; 0 after 300 ms with no change) with the aim speed of the clip (`ProjectileSpeed`, 50 m/s), at most
    `AILeadTargetMaxAngle` (10 degrees) from the direct line. Then the fall: the aim is higher by the fall of the acceleration of the
    slice in the time of the flight (live check L2b: the client flies the arrow with the acceleration of the slice). At most
    `AIAimMaxAngle` (45 degrees) to each side of the front of the creature (the creature looks at the target at the start of the action).
    The direction stays: the shot does not follow the target. No deliberate miss: the data has `AIMissMinDistance` and
    `AIMissMaxDistance` (1.5 m to 3 m) with no chance of a miss, thus the backend keeps the values and uses none (a miss with each shot
    would hit nothing).
  - The repeat (a project rule, research Q3 option 1): the next shot `kShotRepeat` (3 s) after the start of the last shot, or at the end
    of its action when later; a melee attack keeps its pause (1.5 s, 2.5 s), and a shot waits for that pause after a melee attack.
  - The flight and the hit: [Projectiles](projectiles.md) (the creature shots). The shot hits the first character on its line, not only
    the target.
  - The command `tools/world creature shoot CREATURE CHARACTER` (`Behavior::command_shot`): the creature takes the character as its target
    and starts its shot now, with no band and no repeat time (a test of a wall, a far target, an observer).
  - The walk into the band (stage 3.4f-B, the rule of the research Q3): a creature that walks into its band from farther walks on to
    `kShotHold` (`kCastHold` for a caster) before it stands (`CreatureMind::closing`); the code of stage 3.4f-A stopped at the edge of
    the band (test case CRB1: a mage at 18 m stands at 14 m, an archer at 18 m in its band shoots from its place).
  - A required spawn action (code review of stage 3.4f-A, finding CR-R1; test case RR1): a creature whose slice needs the spawn action
    starts no shot before `kSpawnLead` (1.5 s) after it was made: each session that saw it come shows the action that plays (the
    windup), not only the arrow.
  - The pending release (finding CR-R2; test case RR2): a release flies only when the creature still fights the same target. A travel
    of the target (its shelter), a lost target, or a different target cancels it (`releasesCancelled`): the shot does not follow the
    target to its new place.
- The cast of a caster (stage 3.4f-B, protocol notes 4.38; research `creature-ranged.md` part B). A slice with no ranged attack of
  the action FireBow and a ranged attack of the action `Cast_One_Projectile` (`gamedata::kBasicCastAction`) has the cast as its basic
  shot (`CreatureSlice::caster`; 10 slices of 88 kinds: the Skeleton Mage, the Ancient Guardian mage, the Dynasty summoner, the Dryad
  shaman, the Damned priest). The rules of the shot above hold, with these values:
  - The cast (game data): the action `Cast_One_Projectile` (1.4 s for the Skeleton Mage), the release at its CAGE-RangedAttack event
    (0.5 s), the profile, the attack index (125 for `AI_Mage_Sphere/Basic`), the ammunition (none for the Skeleton Mage: the slice
    override of the profile), the projectile slice (`mage_projectile_slow`, 20 m/s, no fall, a flight of 3 s), and the row of its
    damage table (live check L6, live proof P1).
  - The band and the repeat (a project rule: a decision of an agent with an option of the research Q3; the game files have no
    keep-distance rule; a caster stands nearer than an archer and casts more often): a cast in `kCastFar` (16 m) with a free line; a walk
    to `kCastHold` (14 m) from farther; a walk back to `kCastMiddle` (11 m) from nearer than `kCastNear` (6 m); the next cast
    `kCastRepeat` (2.5 s) after the start of the last one, or at the end of its action when later.
- The status actions of a creature (stage 3.4f-D, protocol notes 4.40; research `creature-ranged.md` part D). A slice with a status
  action of the creature data (an action of the list `statusActions` of `tools/gamedata/creature_slices.json` whose action list puts
  statuses on the creature at a mark of its fragment: the self heal `InqCleric_Rekindle_Self` of the Inquisitor Cleric, the `Howl` of the
  Evil Knight hound) uses it in its fight. The rules and the source of each one:
  - The action and the marks (game data): the action of the data with the tags of the slice, and at the time of each mark (the
    CAGE-Named event of the fragment: 0.633 s for the self heal, 1.2 s for the Howl) the statuses of the mark come on the creature itself
    (`Creatures::give_status` with the creature as the source; [Creatures](creatures.md)). The world keeps each mark on the world clock
    with the generation of the action (`PendingStatus`).
  - The cancels (project rules, the same as a cast): a mark gives nothing when a different action replaced the status action (a stun, a
    knockdown, a command) or when the creature is dead or left (`statusMarksCancelled`). The target of the creature does not matter.
  - When (project rules, decisions of an agent: the task of the behavior tree that starts the action is not in the game files): before
    the area attack, the shot, and the melee attack, when the creature is ready and has a target of a fight (a character or a summon:
    code review of stage 3.4f-D, finding SU-R6; the area attack and the shot keep their player target): its heal (a status of the action with a positive
    HealthModifierPercent or Health) when its health is not above the low-health part of its slice (game data:
    m_lowHealthPercentThreshold of the Vitals component, 0.15 for the Cleric; the input LowHealth of the target curve `healally` uses
    that value), its buff (no heal part) when it does not have the buff; and not when it has a status of the action. Not before
    `kStatusRepeat` (20 s) after the start of its last status action, and after the pause of its last attack (the rule of an area
    attack). The action plays to its end as a shot (no step, no turn, no other attack).
  - The command `tools/world creature status-action CREATURE [ACTION]` (`Behavior::command_status_action`, route `POST
    /v1/dev/creatures/status-action`): the creature starts its status action now, with no condition and no repeat time.
- The area attack of a creature (stage 3.4f-C, protocol notes 4.39; [Spell entities](spells.md) has the areas). A slice with an area
  attack of the creature data (an action with a CAGE-CastSpell event of a spell of the list `areaSpells`: the Dryad Shaman, the Undead
  Shaman, the Ancient Guardian mage, the Inquisitor Cleric, the Damned Priest, the Lost Soul Harvester) casts it at a character. The
  rules and the source of each one:
  - The casts (game data): the action of the area attack (`Shaman_Bomb`, 4.03 s, with the tags of the slice) and one cast at the time
    of each CAGE-CastSpell event of its fragment (the mushroom: 1.2 s, 2.2 s, 3.13 s; the lightning: 0.93 s). The world keeps each cast
    on the world clock with the generation of the action (`PendingCast`).
  - The cancels (project rules, the same as a release of a shot): a cast makes no area when a different action replaced the area
    attack (a stun, a knockdown, a command), when the creature is dead or left, when it lost the target or has a different target,
    or when the target is not there (`castsCancelled`).
  - The targets (client code 0x679DBB0 with the clip values `AIAimAtTarget` and `UseAllAvailableTargets`): one spell for each
    target of the AI. A creature of the backend has one target: one spell for each cast.
  - The place (client code 0x5C35450, 0x5C09A20, with the values of the clip): the place of the target at the cast; with `AILeadTarget`
    the lead of its velocity at `ProjectileSpeed` (100 m/s), at most `AILeadTargetMaxAngle` from the direct line; at most
    `AIAimMaxAngle` (45 degrees) from the front of the creature. Then at most the CastDistance of the spell row from the creature (a
    decision of an agent: the clamp of the server is not in the client), on the floor of the navigation data. No deliberate miss: the
    clip has `AIMissMinDistance` and `AIMissMaxDistance` and no chance of a miss (the rule of the shot).
  - The place rule (stage 3.4f-D, client code 0x679DBB0 read again; `Behavior::place_rule`): the aim point above only for a cast with
    `AIAimAtTarget`, `UseAllAvailableTargets`, and the ranged attack data of the spell row (RangedAttackProfile and RangedAttackName:
    the six spells of stage 3.4f-C have `AI_Ranged_AOE`). With no ranged attack data the target loop keeps the place of the caster, and
    with `AIAimAtTarget` false the place is the caster: the plague nova, the corrupted blast, the anchor slam, and the shockwave (their
    CastDistance is 0). `AIAimAtTarget` alone: the position of the target, no lead. `AIAimAtTarget` false with ranged attack data is a
    shot of the ranged attack data (0x5BFBAF0): the backend has no such rule, and the area attack is not usable. The offset
    CharRelPosOffset of the row is turned by the direction of the creature (0x676AAF0; the spells of the data have none). A cast at the
    caster has the Radius of its row as its distance (`Behavior::area_range`, a decision of an agent: the target must be in the sphere
    of the spell; the CastDistance of the data is 0).
  - Which attack and when (project rules, decisions of an agent: the game files have the casts and the CastDistance, and no rule for
    the choice or the repeat). The area attack comes before the shot and the melee attack when it is ready: the target is in the
    CastDistance of the row of its first cast (horizontal; `kShotHeight` up or down) with a free line; `kAreaRepeat` (10 s) passed
    since the start of the last area attack of a creature with a different attack (the end of its action when later), or
    `kAreaOnlyRepeat` (5 s) for a creature with only area attacks (the Undead Shaman); and the pause after its last attack passed (the
    pause of a melee attack, or the repeat of its basic shot when that ends first). A slice with two area attacks uses them in turn
    (the Dryad Shaman: `Shaman_Bomb` with three mushrooms, then `AOE_Ranged_Spell` with one). Live proof P4: a Dryad Shaman casts each
    11.6 s and shoots between.
  - The walk (a project rule): a creature with only area attacks walks on its path to `kAreaHoldMargin` (2 m) inside the CastDistance
    (the Undead Shaman: 12 m, to 10 m), and stands and turns to the target there. A creature with a different attack fights with that
    attack out of the CastDistance.
  - The required spawn action (code review of stage 3.4f-B, finding CS-R4; test case RZ4): no shot and no area attack before
    `kSpawnLead` (1.5 s) after the later of the make of the creature and its first record for a session
    (`Creatures::first_record`, `Creature::recordedAt`). A new observer of an old creature with a spawn action thus sees the windup.
  - The command `tools/world creature area CREATURE CHARACTER` (`Behavior::command_area`, route `POST /v1/dev/creatures/area`): the
    creature takes the character as its target and starts its next area attack now, with no range and no repeat time.
- Behavior trees (stage BT1; the plan `behavior-tree-plan.md`, research BT0 `bt0-ai-data.md`). A tree decides what a creature does
  next; BT1 changed no behavior (the differential test BTD1). The rules and the source of each one:
  - The engine and the tree (a decision of the plan): the engine of `world/behavior.h` keeps the rules that no tree can change. In a
    fight it holds the creature and runs no tree while a stun, a knockdown, a stagger, or a forced movement holds it, in the hit stun of
    the last hit, and while an action of the creature plays (`Behavior::engine_holds`); it runs the way back (`go_home`: immune, the
    trail, the jump only with no observer); it keeps the vision rule, the shelter, the contacts, the releases, the casts, and the marks
    with their cancels, the world clock, and the one random source. In each other step of a creature in a fight or at rest (the step of
    200 ms, only the creatures of `m_active` and `m_roaming`) the tree of the creature runs (`run_tree`). `CreatureBrain::cause` says
    which one decided (`tree`, `held`, `hit_stun`, `action`, `way_back`).
  - The leaves (BT1): each one is one rule of the behavior before the trees, with the values of its node. `keep_target` (the target
    checks: gone, the end of the leash, farther than `lose_distance`; a creature that does not walk: the combat vision distance of its
    slice; failure: the creature lost its target and goes back), `status_action`, `area_attack`, `basic_shot`, `melee` (the way, the
    attack, the crowd, the wait with no way), `turn_to_target`, `roam`. A leaf fails when the slice has no such ability; `area_attack` and
    `basic_shot` fail for a target that is not a character (the rule of stage 3.4f-A). The other nodes: `selector`, `sequence`,
    `invert`, `subtree`, and the conditions `in_fight`, `target_is`, `check` and the action `set` (the blackboard).
  - The default tree (`apps/backend/behavior/default.jsonc`, `default-fight.jsonc`, and from BT2 its subtree files, a project rule with the order of
    `Behavior::think` before the trees): a creature in a fight runs the subtree `default/fight` (`keep_target`, then the status action,
    the area attack, the shot, the melee attack, the turn), a creature at rest roams. The decision constants of `behavior_rules` are node
    parameters with the same values (the bands `kShot*` and `kCast*`, `kRetreatPause`, `kArea*`, `kStatusRepeat`, `kLightAttacks`,
    `kLoseDistance`); the pause after an action is the `pause` of the node that started it (`heavy_pause` for a heavy melee attack;
    `CreatureMind::actionPause`). `kAttackPause` and `kHeavyPause` stay in the code as names of the values of the default tree (case BT1
    checks them). A development command (`shoot`, `area`, `status-action`) takes the values of the first node of its kind in the tree of
    the creature, else of the default tree (no such node in either tree: the command does nothing). From stage BT2 the roam has its
    parameters, and the default tree is a set of subtrees (below).
  - The format (a decision of the plan): one tree in one file of JSON with comments (`//`, `/* */`). The vendored JSON library 3.12.0 has
    no option for a trailing comma: a trailing comma is an error. `{"format": 1, "tree": NAME, "blackboard": {KEY: {"type": "int" | "float"
    | "bool", "default": V}}, "root": NODE}`, a node `{"node": KIND, "name": TEXT, ...}` with `children` (a list), `child` (one), or `tree`
    (a subtree). `apps/backend/behavior/tree.schema.json` is the JSON Schema for an editor; the loader of the backend is the authority.
  - The loader and the compiler (`world/behavior_tree.cpp`, decisions of an agent for the limits): each node kind, each member, each
    parameter and its range (times 0 to 600 s, pauses 0 to 60 s, distances 0 to 200 m, `lose_distance` 1 to 1000 m, `light_before_heavy`
    a whole number 1 to 20, a band with near <= middle <= hold <= far), the keys of a `check` and a `set` (declared in the file, the value
    fits the type), and the subtrees (no cycle, each one in the set). The compiler puts a tree and its subtrees into one flat pre-order
    table (at most 1,024 nodes, 32 levels, 64 keys); the keys of a file are in the sequence of their names. A file that is not correct
    is refused with one log line `world: behavior tree refused: FILE: PLACE: CAUSE`, and the tree of its name stays the tree of the
    program.
  - The files (a decision of the plan; the brief: the backend starts and works with no file): the build puts the text of
    `apps/backend/behavior/*.jsonc` into the program (`cmake/embed_behavior.cmake`, a generated source file; from BT2 also the binding file
    `bindings.jsonc`). No file of the game data folder, nothing for `tools/release.sh`. The tree `default` must be in the set: with no such tree that compiles (a fault of the build)
    the trees of the world do not change, and with none at the start the creatures do nothing (`StepCause::no_tree`).
  - The state of a tree is in the creature (rule MP-1): `CreatureMind::brain` (the compiled tree, the result of each node in the last
    step, the value of each key). The behavior binds a creature to the tree `default` when it makes the creature and again at a reload:
    a step allocates nothing and looks up no text. The leaves of one step look up the target one time (`Behavior::step_target`).
  - The cost (case BT4): 400 creatures in a fight need 0.05 us to 0.08 us more for each step of a creature than the code before the
    trees (about 7 ns for each node; 6.4 nodes for each step of a creature on average), with the same steps and attacks.
  - A development run (`--dev-commands`, the harness gate): `SOULWARDEN_BEHAVIOR_DIR` names a folder whose `*.jsonc` files replace the
    trees of the program of the same name (`WorldOptions::behaviorDirectory`; `tools/dev-run.sh` gives the variable to the backend). The
    backend does not read it without `--dev-commands` (one log line). `tools/world creature tree reload` (route `POST
    /v1/dev/creatures/tree/reload`, needs `SOULWARDEN_HARNESS=1`: 403 without it) reads the program and the folder again and binds each
    creature to its new tree. `tools/world creature tree CREATURE` (route `GET /v1/dev/creatures/tree`) prints the last step, each node
    with its result and parameters, and the blackboard: the declared keys and the facts of the creature (the names of the auto-evaluated
    keys of the client where the meaning is the same: `Has_Target`, `Target_Vitals_State`; research BT0).
  - The server trees of the original are not in the client files (research BT0: each slice names its tree, for example
    `SharedAssets/AI/BehaviorTrees/Wolf/Wolf_Base.behaviortree`, 128 names; the pak files have no such file). Each tree of this project
    is a project rule.
- Behavior trees, stage BT2 (the plan `behavior-tree-plan.md`, row BT2). BT2 changes no behavior of a creature with the default binding
  (BTD1 and its stored trace pass with no change). The rules and the source of each one:
  - The subtrees of the default tree (a decision of the plan): `default` is a selector of `sequence[in_fight, subtree default/fight]`
    and `subtree default/rest`; `default/fight` is `keep_target`, then a selector of the subtrees `default/status`, `default/area`,
    `default/shot`, `default/cast`, `default/melee`, and `turn_to_target`. One file for each subtree (`default-*.jsonc`), with the values of
    BT1. The default tree has 20 nodes after the compiler (13 in BT1); a subtree node only runs its child.
  - The shot and the cast (a decision of an agent, BT2): a `basic_shot` node has the band of an archer (`archer`), of a caster (`caster`),
    or both (the format of BT1). A node with no band of the slice fails with no change of the creature: `default/shot` has only the band
    of an archer, `default/cast` only the band of a caster. For each slice exactly one of the two runs, thus the behavior is the one of
    BT1. A development command (`shoot`) takes the first `basic_shot` node with the band of the slice.
  - The waits of the roam (the open point of BT1): the node `roam` has `wait_shortest` and `wait_longest` (the wait after a walk of the
    wander, a random time between them) and `no_way_pause` (the pause after a goal with no way), with the values of stage 3.4e (4 s, 12 s,
    3 s; before, `kWanderWaitShortest`, `kWanderWaitLongest`, `kRoamNoWayPause`). The three parameters are optional (a decision of an
    agent): a `roam` with no parameter (the files of BT1) has these values. The spread of the first roam of a new creature takes
    `wait_shortest` of the tree of the creature.
  - The binding file (`apps/backend/behavior/bindings.jsonc`, project data in the program like the trees; `bindings.schema.json` for an
    editor; `world/tree_binding.h`): `{"format": 1, "bindings": [{KEY: TEXT or [TEXT, ...], "tree": NAME}, ..., {"tree": NAME}]}`. One key
    for each binding; the binding with no key is the binding of each other creature (at most one). The keys: `kind` (the id of a kind),
    `vitals` (its vitals row), `slice` (the path of its slice), `serverTree` (a server tree of its slice), `creatureType`, `family`. The
    file of BT2 has one binding: `{"tree": "default"}`.
  - The selection (rules of this project, BT2): the binding of the narrowest key that matches wins, in the order kind, vitals, slice,
    serverTree, creatureType, family, then the binding with no key, then the tree `default`. Why: a kind is one row of the data, a vitals row
    has one or more kinds, a slice is one body, a server tree is the AI archetype of some slices (family and weapon), a creature type is a
    role across the families (a mini boss of each family), and a family is the widest group. The place of a binding in the file does not
    count: a new line at the end cannot lose to a wide line, and a wide line cannot hide a narrow one. A text matches with no difference of
    upper and lower case. A value in two bindings of one key is an error of the file (a tie: the file is refused). A slice with two server
    trees (the 3 bombers) takes the first one of the slice that has a binding.
  - The server tree of a slice (game data, research BT0): `creatures.json` has the member `serverTrees` of each slice (the asset paths of
    `m_behaviorTreeAssets` of its `BehaviorTreeComponent`, lower case, with no folder `sharedassets/ai/behaviortrees/` and no suffix:
    `wolf/wolf_base`; 503 slices, 128 names; `tools/gamedata/creatures.sh`). A file of an older tool has no member: a `serverTree`
    binding then matches no creature, and the other keys select.
  - The load (decisions of an agent, BT2): the binding file of the folder of a development run (`SOULWARDEN_BEHAVIOR_DIR/bindings.jsonc`)
    replaces the file of the program as a whole. The loader checks the form, each key and value, the tie, and the tree of each binding (a
    tree of the set). A file that is not correct is refused with one log line `world: behavior tree refused: bindings.jsonc: PLACE: CAUSE`,
    and the binding file of the program stays (a fault of the program file: no binding, each creature has the tree default). The log line
    `world: behavior tree bindings: SOURCE, N binding(s)` names the file that the world uses.
  - The named action and the refusal (the plan; the form is a decision of an agent): a `status_action` and an `area_attack` can name the
    action of the slice that they use (`"action": "Howl"`): the node then uses only that status action or area attack. When the behavior
    binds a creature (its make, a reload, a command) it checks that the slice has each action that the tree names (`Behavior::fits`, in the
    status actions or the area attacks of the slice, by the name of the action). A tree that names an action that the slice does not have
    is refused for that creature: the creature has the tree `default`, `CreatureBrain::refusal` has the cause, and the log has one line
    for each tree and slice (`world: behavior tree refused for creature ID (KIND, BINDING): the tree "T" names the status action "A" (node N):
    the slice PATH has no status action of this name; the creature has the tree default`). The count `treeRefusals` counts each one.
  - The binding is a value of the creature (rule MP-1): `CreatureBrain::binding` (the text of the binding: `family Wolf (binding 3)`,
    `each creature (binding 1)`, `a command`), `refusal`, and `actions` (the place of each named action in the slice). The behavior sets them
    when it binds the creature, not in a step: a step looks up no text. The cost of a step does not change (the selection runs at the make
    of a creature and at a reload only).
  - The tree command (a development command, the harness gate): `tools/world creature tree ID --set NAME` (route `POST
    /v1/dev/creatures/tree/set`, `Behavior::set_tree`) gives the creature the tree NAME now, with a new state of the tree, until a reload or
    the removal of the creature. A tree that names an action that the slice does not have: 409, and the creature keeps its tree; a name that
    is not a tree: 404; no `SOULWARDEN_HARNESS=1`: 403. `--set default` gives the default tree.
  - The view: `tools/world creature tree ID` prints the binding and a refusal; the route tree has `binding` and `refused`; the creature rows
    of the admin state (`creatures_view`) have `tree`, `treeBinding`, and `treeRefused`, and the tool window shows the tree in the tab
    "Creatures" ([Tool window and admin](tool-window-and-admin.md)). `tools/world creatures` marks a creature whose tree is not the default
    tree (`TREE NAME`) and a refused tree of a binding.
  - The corrections of the code review of BT1 (BT2): a JSON number that is too large (`1e400`) is a refusal of the file, not an exception
    (R1); the set compiles after the last file, so a subtree can be in a later file (R2; a tree that does not compile by a fault of its own
    file is refused, and the tree of the program of its name comes back); a command (`shoot`, `area`, `status-action`) after a reload of a
    tree with no node of its kind does nothing (R4); a file of a folder is read with a bound of 1 MiB + 1 byte (R5); an int key of the
    blackboard takes values from -16777216 to 16777216 (exact in its 32-bit float, R6); a format number is compared before a conversion (R7);
    case BT5 decodes the creature records of both fights in both sessions (R8). A link in the folder of a development run is read (R3 was
    not accepted: the folder is a folder of the developer, behind the harness gate, and the program of the user reads no folder).
- Two switches for the behavior. `SOULWARDEN_CREATURE_BEHAVIOR=0` (backend option
  `--no-creature-behavior`) turns it off for a start: the creatures stand at their spawn
  points. The command `set_creatures_ignore` (`tools/world ignore`, the mark of the tab
  "Character") makes the creatures ignore one character: a value of the character in the
  memory of the world, with the character id as its key (`Behavior::set_ignored`), which
  stays when its session ends, until the backend stops.

## Commands

```sh
tools/world ignore NAME on           # the creatures ignore a character (off: they attack it again)
tools/world track 2c 20 --tsv FILE   # the place of a creature each 200 ms for 20 s
tools/world creature anchor 2c 8585.5 4243.1 173.26   # the anchor of a creature in a fight (a test of a way back with no path)
tools/world creature area 5e Soulwarden               # the creature casts its next area attack at the character now (stage 3.4f-C)
tools/world creature status-action 5e Howl            # the creature starts its status action now (stage 3.4f-D: its heal or its buff)
tools/world creature tree 5e                          # the behavior tree of the creature: its last step, each node, the blackboard (stage BT1)
tools/world creature tree reload                      # the trees and the binding file again from the program and SOULWARDEN_BEHAVIOR_DIR
tools/world creature tree 5e --set test/still         # the creature gets the tree test/still now (stage BT2; default: the default tree)
tools/test-soulwarden-backend.sh --write-trace BTD1   # the stored trace of the decisions again: only for an intended change of the behavior
```

- `tools/world` has the commands of stage 3.4b (routes `/v1/dev/world/ignore`, `heal`,
  `hurt`, and `message`): `ignore CHARACTER on|off` (the creatures ignore a character:
  turn it on before a test that is not a test of a fight, because each creature that sees
  the character attacks it), `heal CHARACTER` (the full health; a dead character lives
  again at its place), `hurt CHARACTER AMOUNT` (the health goes down; 0 is the death, with
  the death screen of the client), and `message CHARACTER TYPE HEX` (one message with your
  bytes to the session of a character: a test of a message in a live client before its
  code). `tools/world creature action CREATURE NAME [SECONDS] --tags HEX` starts an action
  with your tag bytes (24 hexadecimal digits). `tools/world list` prints the health of each
  character with the mark `DEAD`, and the line `behavior` (on or OFF, the time of the
  steps, the creatures in the step, the targets, the attacks, the contacts). `tools/world
  creatures` prints a mark for a creature that fights (`FIGHT`), that goes back (`GOES BACK
  (immune)`, stage 3.4e), that has no attack (`no attack`), and that does not leave its place
  (`stays at its place`).
- `tools/world` has the marks and the command of stage 3.4e: `tools/world creatures` prints
  `PATROL (waypoint N of M)` (with `, stays there` for a creature that stays at its waypoint
  for ever, and `, stands: CAUSE` for a creature that does not roam now: no character near,
  no navigation data of its place yet, no free floor at its place, no behavior data of its
  slice), `WANDER`, `ON ITS TRAIL` (a way back with no path), and `WAITS (no way to its
  target)`. `tools/world list` prints the line `roam` (the creatures that roam now, the
  steps, the waypoints, the places of a wander, the ways back, the waits, the ways on the
  trail, the jumps, and the refused hits on an immune creature). `tools/world creature
  anchor CREATURE X Y Z` (route `/v1/dev/creatures/anchor`) moves the anchor of a creature in
  a fight to a place: a test of a way back with no path (a place with no way to it, for
  example a ledge).

## Traps

- A tree file with a trailing comma is refused (the vendored JSON library reads comments, not trailing commas): the log line names the
  byte. The keys of a blackboard are in the sequence of their names, not of the file.
- A change of the default tree is a change of the behavior of each creature: case BTD1 then fails (the stored trace of the decisions,
  `tests/golden/behavior_trace.txt`). Write the trace again (`--write-trace`) only for an intended change, with its source and a live
  video.
- The view of `tools/world creature tree` shows the last step of the tree. While an action plays, the engine holds the creature: the
  view then says `an action plays` and no node has a result. Look again after the end of the action.
- `SOULWARDEN_BEHAVIOR_DIR` replaces a tree by its name (the member `tree` of the file), not by the name of the file. A folder with a
  file `default.jsonc` whose tree is `default/fight` replaces the subtree. The file `bindings.jsonc` of the folder is the binding file, not a
  tree: it replaces the binding file of the program as a whole (a folder with no binding file keeps the binding of the program).
- A binding selects a tree by the narrowest key, not by the place in the file: a `family` binding at the start of the file does not hide a
  `kind` binding at its end. Two bindings with one value of one key (also with a different case: `Wolf`, `wolf`) refuse the whole file.
- A tree of a binding that names an action of a slice (`"action": "Howl"`) is refused for each creature whose slice does not have it: the
  creature fights with the tree default. The log has one line for each tree and slice, not one for each creature; `tools/world creature
  tree ID` prints `REFUSED` with the cause.
- The `serverTree` bindings need a `creatures.json` of stage BT2 or later (`tools/gamedata/creatures.sh`): with an older file no creature has
  a server tree, and these bindings select nothing (no log line).
- After the end of its attack a creature stands in the idle of its slice (user test 2): a ghost floats up and down, a wolf crouches.
  Before, it kept the last pose of the attack until its next state. A video of the agent desktop shows it; a still picture does not.
- In the hit stun of a hit a creature does not turn to its target: a fast series of light hits keeps it in its reactions until the
  reaction limit (the third chained reaction of a Solo kind), then it attacks again.
- A Cleric heals itself only at 15 percent of its health or below (the low-health part of its slice): set its health
  (`tools/world creature health ID 12%`) for a test, and turn the ignore value off: the heal is a part of its fight.
- An Evil Knight hound howls when it takes its target, and again 20 s later when its buff ended; the Grey Wolf has the action `Howl` with a
  damage event and no buff (the variant of its tags).
- An undead officer of the data with no melee attack (`Undead_Officer_16`) has only the anchor slam from stage 3.4f-D: it walks to 1.5 m
  of its target and slams each 5 s.
- The sight of the Dryad Shaman is 12 m (75 degrees to each side): a shaman spawned 12.1 m from the character takes no target. Use
  10 m (`-10 -1.5 0 --yaw 270` at the place of the default spawn).
- A creature with a basic shot always had the shot ready before the pause of a melee attack: before the correction of the rest rule
  (the earlier of the two pauses) a Dryad Shaman cast its area one time and then only shot (live run of stage 3.4f-C; test case
  CRA6).

- An archer sees a character at the vision distance of its slice (12 m for the Evil Knight and Lost Knight archers, 18 m for the
  Skeleton Archer), and it shoots in 20 m: a character at 15 m in front of a Lost Knight archer is not seen. Hit it, or come nearer.
- A creature of a spawn point near the archer (a wolf, a Risen) attacks the character too: for a clean video remove them
  (`tools/world creature remove ID`; the points wait until `tools/world creature reset`).
- A Skeleton Mage (`Skeleton_Mage:2`, Celestial Mage) sees 12 m too and casts in 16 m. For a video of a cast or a dodge (stage
  3.4f-B) the street of the default spawn is clear at 8879 4252.2 138.4 with the view to the west (`tools/world travel ... --screen
  off`, then `tools/hx 'look -900 0'`): put the creature 10 m to the west and 1.5 m to the south (`-10 -1.5 0 --yaw 270`), so that
  the character does not hide it.

- Each creature that sees the character attacks it, also in a test of a different system:
  the character can die while a test waits. Turn the ignore value on first
  (`tools/world ignore Soulwarden on`: it stays until the backend stops), or start the run
  with `SOULWARDEN_CREATURE_BEHAVIOR=0`. A creature of `tools/world creature spawn` attacks
  too: a Grey Wolf does 119 damage with a bite and 417 with a lunge, and the character
  has 2,186 health.
- A fight for a test: `tools/world ignore Soulwarden on`, `tools/hx 'look -900 0'`,
  `tools/world creature spawn Wolf_Grey_7 Soulwarden -9 0 0 --yaw 270` (9 m to the west,
  with its face to the character: the vision of a wolf is 12 m and 75 degrees to each
  side), then `tools/world ignore Soulwarden off`. The backend log has one line for each
  step of the fight (`world: creature ID (KIND) has a target`, `starts the attack`,
  `world: hit: creature ... hurts NAME`, `world: miss`, `lost its target`, `is at its
  home`).
- The pictures of a fight: the wolf runs 9 m in 1 s. Take the screenshots in a loop
  directly after the command (`for i in 01 02 ... 24; do tools/shot NAME-$i; done`: 24
  calls need 4 s) and put them on one sheet (`magick montage ... -tile 4x3`). The camera
  is behind the character: a creature in front of the character can be behind its body
  in the picture. Turn the camera to the side (`'look 900 0'`) for a side view.
- A creature of a spawn command stands on the floor of the navigation data at its own
  place that is nearest to the height of the character. With no direction (the tab
  "Creatures" sends none) it looks at the character: it sees the character after the
  shelter time.
- The client moves the body of a creature forward in some attacks (the lunge of a wolf)
  and puts it back at the end: the position of the backend did not change.
- A creature with a weapon plays no attack when the tag bytes of its scope record are 0,
  with no error and no log line. The tags of a slice are game data
  (`tools/gamedata/creature_behavior.py`): after a change of the tool, look at one attack
  of a creature with a weapon in the live client.
- The 10 s of shelter: after a world entry, a travel, a respawn, and a revive, no creature
  takes the character for 10 s after the first position report from the place. A test of
  a fight waits for that time, or the character hits the creature first. A travel ends
  each fight of the character: use a walk (`'keydown 0x57'`) to lead a creature to a
  different place.
- A creature that waits for a different creature does not move: a queue of creatures on
  stairs is the crowd rule, not a fault of the path. Only the creatures in the reach
  attack.
- A creature with no way to a character goes to the place nearest to it and waits there
  12 s (stage 3.4e; before, it went home at once), then it goes back and does not look at
  the character for 8 s: a stone higher than a step of 0.35 m (the Tomb of the Old
  Warrior) is enough. Keep a test of a walk on one floor.
- Only a creature near a character roams: `tools/world creatures` shows `stands: no
  character near` for a creature far from each character, and a patrol starts again at the
  next scan (1 s) when a character comes near.
- The start wolf waits 10 s to 40 s at each waypoint: a video of its patrol needs 90 s or
  more. Its track (`tools/world track ID 90 --tsv FILE`) shows the waypoints.
- With the ignore value on, a creature on its patrol does not see the character: turn it
  off for a test of a fight from a patrol, and on again at the end (a lead script that
  stops early leaves it off, and the creature takes the character again).
- A fight to the end of the leash: the character must walk faster than the creature can
  attack it, and a Grey Wolf does 119 and 417 damage. Send `tools/world heal` each 2 s while
  the character walks, and strafe (`'keydown 0x44'`) with the camera to the creature: a
  walk forward hides the creature behind the character, and a walk backward ends at the
  objects behind it.
- The Tomb of the Old Warrior has two places with no way for a creature: a raised stone at
  `8586.16 4240.15 167.15` (1.4 m above the floor; a wolf attacks from the place nearest to
  it) and a ledge at `8585.5 4243.1 173.26` (6.7 m above the floor; a wolf waits below it,
  out of its reach). `python3 -B tools/gamedata/nav_file.py at DATA X Y` prints the floors
  of a cell, and `tools/world path` says if a place has a way.
- The camera on the ledge cannot see the place below it. Spawn the creature where the
  camera sees its anchor: the video shows the run to the ledge and the run back.
- A wolf of `tools/world creature spawn` stands on the floor that is nearest to the height
  of the character at its place: a blocked floor below the free one makes it stand (`stays
  at its place`). Give it a height (`DZ` 0.5) to put it on the free floor.
- The faminegeists of Mosswater Bourg walk at 0.07 m/s (the ambient speed of their slice):
  use an alligator of that place (1.81 m/s, a route of 56 m in the water) for a video.
