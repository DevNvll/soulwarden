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
  removed has no respawn time. At the respawn time, when a player is 150 m or less from the
  point, the point first removes the body of its dead creature and then makes its new
  creature; with no player that near, the body stays (the distance rule removes it at 200 m)
  and the point waits. The respawn times are values of the world, not of the database. A
  creature of a command has no spawn point: the distance rule does not remove it.
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
  (`PartsRequest::keepScope`, test case RP3). A
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
- The record of a creature has the metadata state (the asset of its slice), the movement
  state, the vitals state, and the marker state, with the slot of each component from the
  slice (game data: `CreatureSlice::movementSlot`, `vitalsSlot`, `markerSlot`). A creature
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

## Commands

```sh
tools/world creatures NAME 60        # the creatures that are 60 m or less from a character (no name: each creature)
tools/world kinds wolf               # the kinds of the creature data with a text in their id, name, or family
tools/world creature spawn Wolf_Grey_7 NAME 0 4 0   # a creature of a kind 4 m to the north of a character
tools/world creature health 2c 50%   # set the health of the creature whose id ends with 2c
tools/world creature kill 2c         # kill it (the death action, the dead mark)
tools/world creature remove 2c       # take it out of the world
tools/world creature reset           # each spawn point that waits makes its creature again
tools/world creature action 2c Spawn 3   # start an action of the action table of its slice (3 s; no time: no end)
```

- `tools/gamedata/creatures.sh` writes `creatures.json` to the game data folder: the spawn
  points of the whole world (32,840 spawn points, 3,848 kinds, 503 slices, 25 regions; a file
  of 12.4 MB with the 19,814 patrol routes of format 3; 60 s with `SOULWARDEN_OODLE_LIB`). The
  tool prints the counts of the routes, the policies, the waypoints, the tasks, and the kinds
  that wander. `--radius METERS` (with `--center X Y`) writes a
  smaller file, `--report FILE` writes one line for each entry that is left out, and
  `--all-slices` also writes the slices that the client does not show (a file for a
  picture tour, not for the game data folder). The tool reads the two slice lists of
  `tools/gamedata/creature_slices.json`, and it stops when that file is not correct or when
  a slice of the list `spawnAction` has no action `Spawn` with a fragment.
- `tools/gamedata/creatures.sh --no-attack FILE` writes one line for each kind that does not
  attack, with the cause (859 kinds of the supported build).
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
- The world of the client has a day and a night with no world time of the server: a run
  can be dark. The street at the default spawn has lamps: use it for a screenshot of a
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
- The places of the world are dark in many runs (no world time of the server): a name
  plate shows also in the dark.
