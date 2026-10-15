# Projectiles

Read this document before a change of the shot of a ranged or magic basic attack (the bow, the musket, the fire staff,
the ice gauntlet, the life staff, the void gauntlet), its flight, or its hit, and before a live test of a shot (stage
3.3b-S3). The wire contracts and the client code are in the protocol notes (section 4.27, and 4.7 for the input entry).
The damage is in [Combat and health](combat-and-health.md), the capsule of a creature in [Creatures](creatures.md), the
line test of a wall in [Navigation](navigation.md), and the projectile of Gravity Well (an ability) in
[Abilities](abilities.md).

## Start here

| Task | Start here |
| --- | --- |
| The shot item of the client: its fields, the session | Protocol notes 4.7 and 4.27, `apps/backend/src/server/rep/client_input.h` (`InputShot`), `server/rep/rep_session.cpp` (`RepSession::shoot`), `tools/research/input_packet.py --contacts` |
| The launch and its rules, the ledger, the flight, the hit, the hit near the muzzle | `world/projectiles.h` (`world::Projectiles`, `projectile_rules`, `ShotVerdict`, `ProjectileEnd`), test cases PJ3 to PJ8 |
| The capsule of a creature and the sweep of a segment | `world::capsule_of`, `world::sweep`, `gamedata/creature_data.h` (`CreatureCapsule`), test case PJ2 |
| The wall test of a segment | `Navigation::line_stop` (`world/navigation.h`), test case PJ4 |
| The projectile data of the game: the profiles, the slices, the ammunition, the weapons, the shots | `tools/gamedata/projectiles.sh` (`projectiles.py`, `check_projectile_tool.py`), `gamedata/projectile_data.h`, [Game data](../development/game-data.md), test cases PJ1, PJ11 |
| Two sessions, the load | Test cases PJ9, PJ10 |
| A shot of the server for a character with no client | `Projectiles::launch_character`, `commands::shoot`, `tools/world shoot` |

## Rules

- The projectile system (`world/projectiles.h`, `world::Projectiles`) owns the shots and the projectiles in the air. They
  are values of the world with the character as their key (rule MP-1): no session has a flight timer. A session gives each
  shot item of a new input step to the world (`RepSession::shoot`); the item of Gravity Well (the attack "gravitywell")
  goes to the abilities system. The world finds the character from the session (rule MP-5).
- The client reports no later hit (live checks L2 and L5: no item and no message after a shot that hits a creature, a
  wall, or nothing). Thus the server follows each projectile itself and decides its hit. The one hit that the client
  reports is the hit near the muzzle: the receiver of the shot item.
- The launch (game data where it has the value, `projectile_rules` for the rest). The weapon in the hand gives the
  profile (WeaponItemDefinitions: RangedAttackProfile). The attack of the item (the CRC-32 of the lower-case
  RangedAttackName) must be an attack of the profile with no hit scan and no grouped spread. A shot rule of the data must
  have the attack, the row (the CRC-32 of the lower-case damage row), and the charge of the item; a magic shot has the row 0
  and the charge 0 on the wire, and its row is the row of its spell (the spell table: RangedAttackProfile,
  RangedAttackName, DamageTableRow). The ammunition key must be the ammunition of an empty slot of the ammunition type of
  the weapon (DefaultArrow, DefaultShot: client code): the backend has no equipped ammunition (the slots 12 and 13 take no
  item), and the ammunition-free setting of the client is on (live check L1). A magic shot has no ammunition key. The
  origin is 5 m or less from the character, with no wall from 1.5 m above its feet; the direction is a unit vector. A
  refused shot changes nothing: no projectile, no ledger entry, no cost, no damage, and no message (the client has none).
- The press. The newest press of the attack input (bit 555) at or before the input step of the item fits the shot. A
  released press (bit 567) fits for 30 steps after its release. A heavy shot needs a hold from the press to the release
  (or to the shot): 14 input steps for the bow (the marker SetChargedAim at 0.5 s of the game data, less one step; live
  check L2: 14 steps light, 16 steps charged) and 4 steps for a magic weapon (the condition of 0.1667 s of the fragment
  Attack_Start_Charge, less one step). A ranged weapon keeps no hold bit: the attack track keeps the release of each press
  (`AttackTrack::Press::released`).
- The ledger. Each accepted launch has its entry with the character, the session, the input sequence, and the number of
  its press as the key: one projectile for each press. A second item of one press (a repeated datagram, two items in one
  entry) is refused (`repeated`). The entry stays for the life time of the projectile and 10 s after it. The 32 local
  projectiles of the client are visuals: they are not in the item, and they are not a limit of the server. A character
  has 16 projectiles in the air at most (a decision of an agent: a limit of the cost).
- The flight. On the world clock, in steps of 1/30 s at most: the speed of the slice (`m_baseSpeed`), its acceleration
  (`m_accel`, from the distance `m_projectileAccelDelayDist` of the profile on: client code 0x695CCC0), and the steps
  `p += (v + a dt / 2) dt`, `v += a dt` of the client. The projectile ends at the range of the profile (`m_maxRange`) or at
  the life time of the slice (`m_flyingLifetime`), whichever comes first: a miss. The bow: 100 m/s, gravity after 5 m, 100
  m; the fire ball: 100 m/s, no gravity, 0.5 s (50 m).
- The hit. Each segment looks at the creatures near it (the position index: the half length of the segment and the reach
  of the largest capsule of the creature data) and at the cells of the navigation data on it (`Navigation::line_stop`).
  The first creature that lives and is not on its way back, whose capsule the segment touches with the radius of the
  projectile (the profile: 0.15 m for a light arrow, 0.275 m for a charged one) and 0.25 m (`kHitTolerance`, a decision
  for the user: the network delay), takes one hit through `Combat::strike`: the row of the shot, the weapon and the gem of
  the launch, the damage modifier of the ammunition (1.2 for each arrow and shot of the game data), and the point of the
  hit as the source of its impact. The projectile ends: no pierce, no bounce (the profiles of the basic attacks have no
  bounce; the penetration depth of 0.2 m of the bow has no known use). A wall or the ground first: the projectile ends with
  no hit. The damage number goes to the client of the caster; each observer of the creature gets its health and its
  reaction (the common hit path).
- The hit near the muzzle. A shot item with a receiver: the receiver must be a creature of the world with a reference of
  its slice (its actor, its entity, the facet target of a hit volume), the session must observe it, the profile must have
  a near range, and the capsule must be in the near range (with the radius of the projectile and 0.25 m) of the origin,
  with the point 4 m or less from the creature and no wall between. The hit is at the launch, and no projectile flies. A
  dead creature or one on its way back is passed: the server flies the projectile.
- The capsule of a creature (game data and a decision of an agent): the capsule of the CharacterComponent of its slice
  (`CreatureCapsule`: the radius, the length of the segment, the axis in the frame of the creature, the offset), turned by
  the direction of the creature, with its lowest point at the feet of the creature (the game files do not say where it is).
  An axis of 0 is a sphere. A slice with no capsule (2 of 503) and a creature data file of an older tool have the default
  capsule: the agent of the navigation data (radius 0.35 m, height 1.8 m).
- The cost. The ManaCost of the spell of a magic shot goes through the one mana path (`Abilities::mana_cost_of`,
  `Abilities::change_mana`) at the launch; not enough mana: refused. The basic attacks of the fire staff cost 0 (their
  spells name no cost id; the mana table has FireStaff_Primary 0 and FireStaff_Heavy 0).
- A caster that leaves the world ends its projectiles with no hit; a dead caster does no damage (its projectiles end).
- The cost of the rules. The tick looks only at the projectiles in the air, each segment only at the creatures near it and
  at the cells of its line (test case PJ10: 100 shots in a world of 4,040 creatures, the same looks as with 80 creatures).
- With no projectile data (no file, a file that is not complete) the backend starts, and each shot is refused (`no
  projectile data`).

## Commands

```sh
tools/world shoot NAME [--yaw DEGREES] [--pitch DEGREES] [--heavy]   # a shot of the server for a character (also with no client)
tools/world projectiles              # the projectiles in the air, the shots by verdict, the ends by cause, the work
tools/world list                     # the line "projectiles"
tools/gamedata/projectiles.sh [OUT.json]   # write projectile-data.json (default: the game data folder)
tools/gamedata/projectiles.sh --find TEXT...   # print the attacks, the shots, and the weapons with a text
```

- The routes: `POST /v1/dev/projectiles/shoot`; `GET /v1/dev/world` has the member `projectiles`. The admin interface has
  `shoot`.
- `tools/world shoot` takes the yaw from north to the east and the pitch up, from 1.5 m above the feet of the character: to
  hit a wolf 12 m away, give a pitch of about −4 degrees. A heavy shot takes the attack named "..._Heavy" of the data
  first.
- The backend log has one line for each launch (`world: projectile N: NAME launches ...`), each hit (`world: hit: ...`),
  and each end (`... ends after M m and T ms at X Y Z: CAUSE`), one line for the first refusal of each cause of a session
  (`rep: shot refused (CAUSE)`), and the counts at the end of the session (`rep: shots of the session`).

## Traps

- The capture folder of a session is full after about 6 minutes in the world: a shot after that is not in the captures.
  Leave the world and enter it again for a new folder (`tools/scenarios/leave.sh`, then the Play button).
- The fire staff of the client makes no heavy fire ball with the input of the harness: the hold bit 191 is in the input
  only in the steps 1 and 3 after the press, and the client takes the light branch (also with the graphics quality 1). The
  server rule has its test (PJ8); a live heavy fire ball comes from `tools/world shoot NAME --heavy`.
- The aim of the bow (the right button) moves the camera over the shoulder: the crosshair is the line of the shot. In the
  aim, `'look DX DY'` moves the crosshair by about 2 pixels for each unit. Take a picture in the aim before a shot.
- The camera of the staff (no aim) is behind the character: the character hides a creature on the line of the shot. Put
  the creature on the line of the camera (the direction of the shot of the last item, in the backend log), and crop the
  frames near its name plate.
- A creature on its way back is immune: a projectile passes it (no hit). With `tools/world ignore NAME on` a creature that
  a hit pushed can walk back to its spawn place.
- The damage number shows for about 1 s: take the pictures in a loop from the shot on.
- A projectile data file of the game data folder that the tool did not finish is not used: the tool writes, then renames.
  A file from before stage 3.3b-S3 does not exist: with no file each shot is refused.
