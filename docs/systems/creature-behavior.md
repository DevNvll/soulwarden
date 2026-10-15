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
  bodies on one vertical line too). A kind with no attack that the data tool can use (859
  of the 3,848 kinds) takes no target and takes damage; it patrols or wanders with the roam
  data of its spawn point and of its slice, as each other creature (test case RP9).
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
