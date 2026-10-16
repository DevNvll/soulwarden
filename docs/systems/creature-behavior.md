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
| The status actions of a creature (stage 3.4f-D): its heal at low health, its buff, the marks and their cancels, the repeat; the place rule of the client for a cast (at the caster, at the target, at the aim point) | Protocol notes 4.40, `world/behavior.h` (`behavior_rules::kStatusRepeat`, `Behavior::think_status`, `wants`, `start_status_action`, `status_mark`, `command_status_action`, `place_rule`, `area_range`), `gamedata/creature_data.h` (`CreatureStatusAction`, `StatusMark`, `CreatureSlice::lowHealth`), [Creatures](creatures.md), `tools/world creature status-action`, test cases CRH1 to CRH5 |

## Rules

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
  At its end the path of the creature is forgotten (the next step searches from the new place) and a creature in a fight
  keeps the new place in its trail (`Behavior::moved_by_force`).
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
    the area attack, the shot, and the melee attack, when the creature is ready: its heal (a status of the action with a positive
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
