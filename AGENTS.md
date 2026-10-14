# Project Soulwarden agent context

Project Soulwarden supports the last retail New World client with a game-hook DLL and a detached C++
backend. Read only the documents relevant to the task.

## Task map

| Task | Start here |
| --- | --- |
| Plan, stage sequence, rules for more than one player | [Roadmap](docs/ROADMAP.md) |
| Build or start the game | [Reference](docs/REFERENCE.md) (section "Build and run"), [README](README.md), `linux-compat/run-soulwarden.sh` |
| Test the backend with no game process | `tools/test-soulwarden-backend.sh`, `apps/backend/tests` |
| Sessions of the game server: ids, end causes, capture folders | Protocol notes section 3.5, `apps/backend/src/service/rep_server.h` |
| The world object: clock, task queue, timers, entity table | `apps/backend/src/world/world.h`, `entity.h`, `ids.h`, protocol notes 4.9 |
| A component state for the owner and for an observer | `apps/backend/src/server/rep/states`, protocol notes 4.9 |
| A remote player, development commands of the world | `tools/world`, protocol notes 4.9 |
| Items, the bag, the coin of a character | Protocol notes 4.10, `apps/backend/src/world/inventory.h`, `tools/world give`, `tools/gamedata/items.sh` |
| Equipment: the paperdoll, equip and unequip, visible gear, the items of an archetype | Protocol notes 4.11, `apps/backend/src/world/inventory.h`, `server/rep/states/paperdoll_state.cpp`, `tools/world equip` |
| Level, experience, attributes of a character | Protocol notes 4.12, `apps/backend/src/world/progress.h`, `server/rep/states/attribute_state.cpp`, `tools/world level`, `tools/gamedata/progression.sh` |
| Weapon mastery, ability bar, trade skills | Protocol notes 4.13, `apps/backend/src/world/skills.h`, `server/rep/states/skill_states.cpp`, `tools/world mastery`, `tools/research/ability_tables.py` |
| Creatures: the spawn points, the kinds, the record of a creature, health and death, the action of a creature (the scope fields, the locomotion of a walk), the commands | Protocol notes 4.14, `apps/backend/src/world/creatures.h`, `gamedata/creature_data.h`, `server/rep/states` (movement, vitals, marker; `creature_scope`), `server/http/creature_commands.cpp`, `tools/world creatures`, `tools/gamedata/creatures.sh`, `tools/research/creature_actions.py`, test cases UT2 and UT3 |
| A melee attack of a player on a creature: the contact report of the client, the hit check, the damage rule, the damage number | Protocol notes 2.3, 4.7, and 4.15, `apps/backend/src/world/combat.h`, `gamedata/combat_tables.h`, `server/rep/client_input.h`, `server/rep/combat_messages.h`, `server/http/aws_routes.cpp`, `tools/gamedata/combat.sh`, `tools/research/input_packet.py --contacts` |
| The creature data of the whole world: which entries are in, which slices are out, the spawn action of a slice, the picture check of the slices | Protocol notes 4.14, `tools/gamedata/creature_data.py`, `tools/gamedata/creature_slices.json`, `tools/research/creature_tour.py` |
| The behavior of a creature: the sense, the way to a player, the attack and its contact test, the way back, the behavior data of a slice, the two switches | Protocol notes 4.17 and 4.19, `apps/backend/src/world/behavior.h`, `world/navigation.h`, `world/creatures.h`, `gamedata/creature_data.h`, `tools/gamedata/creature_behavior.py`, `tools/world ignore`, test cases FT1 to FT14 |
| The navigation data: where a creature can stand, its path around a wall and up stairs, what stops its sight, the tiles near the players, the tool that makes the data from the collision shapes of the game copy | Protocol notes 4.19, `apps/backend/src/world/navigation.h`, `gamedata/navigation_data.h`, `tools/gamedata/navigation.sh` (`navigation_data.py`, `collision_data.py`, `navcore.cpp`, `nav_file.py`), `tools/world path`, `tools/world track`, `tools/research/nav_draw.py`, test cases NV1 to NV12 and RN1 to RN10 (`tools/gamedata/check_navigation_tool.py`) |
| A fight that a player can play: the 10 s with no target after a travel, the health that comes back, light and heavy attacks of a creature and their pause, creatures that do not stand at one place | Protocol notes 4.19, `apps/backend/src/world/behavior.h` (`behavior_rules`), `world/health.h`, `gamedata/creature_data.h` (`kContactGap`, `kHeavyWeight`), test cases PL1 to PL4 |
| The terrain heights: the height of the ground at a place with no navigation data, and the terrain after the cookies of the placements | Protocol notes 4.17 and 4.19, `apps/backend/src/gamedata/height_maps.h`, `world/navigation.h`, `tools/gamedata/heights.sh`, `tools/gamedata/check_heights_tool.py` |
| A conversation with a person, a cutscene of the client, a character that the client keeps in place | Protocol notes 4.20, `apps/backend/src/server/rep/lock_messages.h`, `server/rep/rep_session.cpp` (`handle_cutscene_ended`, `end_input_lock`), `tools/world message`, test case UT1 |
| The mana of a character and the mana meter of the HUD (the blue bar below the crosshair) | Protocol notes 4.5, `apps/backend/src/server/rep/states/mana_state.cpp`, test case UT4 |
| The health and the death of a character: the damage of a creature, the damage mark, the death screen, the respawn | Protocol notes 4.18, `apps/backend/src/world/health.h`, `world/combat.h` (`Combat::creature_damage`), `server/rep/health_messages.h`, `server/rep/states/vitals_state.cpp`, `server/http/health_commands.cpp`, `tools/world heal` |
| The load of the world: the cost of the spawn rule, the time of a tick | `apps/backend/src/world/creatures.h` (`SpawnGrid`), `world/load_meter.h`, `tools/world list` (the line `load:`), test case WS2 |
| Travel: the server moves a character to a different place, the travel places, the tab "Travel" | Protocol notes 4.16, `apps/backend/src/world/world.h` (`World::travel`), `server/rep/travel_messages.h`, `gamedata/travel_places.h`, `server/http/travel_commands.cpp`, `tools/world travel`, `tools/world places`, `tools/gamedata/places.sh` |
| Ids of an entity, check of the client messages | Protocol notes 4.2, `tools/gamedata/slice_ids.sh` |
| The launcher in the Steam library | `tools/steam-shortcut.py` |
| The tool window (key F8): renderer hook, input, layout | Protocol notes section 6, `apps/game-hook/src/client/toolwindow`, `tools/hx 'toolwindow'` |
| The admin interface of the backend, a command for a character | Protocol notes 6.4, `apps/backend/src/server/http/admin_routes.cpp`, `character_commands.h` |
| Wire contracts and client code addresses | [Protocol notes](docs/PROTOCOL_REVERSE_ENGINEERING_NOTES.md) |
| Add a replicated state to the world | Protocol notes section 4, then `tools/st NAME_REGEX` |
| Static analysis | `tools/nw` (image queries), `tools/gh` (Ghidra MCP), `tools/ht` (message types) |
| Live analysis | `SOULWARDEN_TRACE`, `SOULWARDEN_PROBE`, `tools/hx`, `tools/shot`, `tools/research/player_position.py` |
| A screen of the client that is not complete (script errors) | `tools/research/script_errors.py`, protocol notes 4.10 |
| Movement and position of the player | Protocol notes sections 4.6 and 4.7, `apps/backend/src/world/position_report.h`, `server/rep/client_input.h`, `tools/research/input_packet.py` |
| The position report of the client (the change of the client code by the hook) | Protocol notes 4.7, `apps/game-hook/src/client/hooks/movement` |
| Player identity, accounts, the Steam id | Protocol notes sections 2.1 and 3.4, `tools/research/check_identity.py` |
| Character creation, deletion, the body in the menu | Protocol notes section 2.2, `tools/scenarios/create.sh`, `tools/research/creation_params.py` |
| Appearance of a character in the world | Protocol notes sections 2.2.5 and 4.5, `tools/gamedata/appearance.sh` |

## Stable contracts

- `apps/game-hook` owns the hook DLL. `apps/backend` owns the standalone C++ service and
  persistence. The DLL does not include or start the backend.
- Foundation for more than one player: the project has one client in the world now, but each
  change of the backend obeys the rules MP-1 to MP-10 of the roadmap (section 1.6). In short:
  the state of a character is in a world object with the character id as its key, not in a
  session object; one path sends a state change to each session that observes the entity; no
  constant id for a player; the backend finds the character of a request from the session;
  timers use the world clock.
- One thread owns the world: the listener thread of the game server, or the thread of the
  world object when the backend has no listener (`--no-rep-listener`). A different thread
  (an HTTP route) gives the world a task (`World::post`, `World::call`) and does not read a
  session. A task of `World::call` that did not start in its time limit is cancelled and
  does not run later (`CallStatus::notStarted`: nothing changed); a task that started is
  awaited. A route must not say "no change" for a task that started. The game server and each session read the time only from the world clock
  (`world/world_clock.h`), not from the clock of the computer. The stop of the backend waits
  for each HTTP connection thread before it destroys the world: `HttpServer::stop` joins
  each thread and has no time limit.
- The entity table of the world has one entity for each character that is in the world
  (`world/entity.h`). The name, the position, the appearance, the vitals, and each later
  state of a character are in its entity, not in `RepSession`. The world gives each entity
  its actor id (`world/ids.h`), keeps its position in the position index, and writes its
  position row. A system changes an entity and marks the component state (`World::touch`).
  The world refuses a position that is not finite or that is outside its limits
  (`valid_position`), for each source.
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
  hurt it as its target (`Creatures::hurt` has the attacker).
- The spawn points (a project rule, protocol notes 4.14). The creature data has the places
  where the game has a creature. The world makes the creature of a spawn point when a
  player is 150 m or less from the point, and it removes the creature when no player is
  200 m or less from the point (`WorldOptions::creatureSpawnEnter`, `creatureSpawnLeave`; one
  scan for each 500 ms of the world clock). The two distances are distances of the spawn
  point, not of the creature: a creature that goes to a player and away from its point
  stays in the world (test case FT8). A spawn point has one creature at most. A creature of
  a point that died, or that a command removed, does not come back by this rule: the point
  waits until the command `reset`, or until its respawn time (`Creatures::respawn_at`, the
  place of the respawn timer of stage 3.5a: no code sets a time yet). At the
  respawn time, when a player is 150 m or less from the point, the point first removes the
  body of its dead creature and then makes its new creature; with no player that near, the
  body stays and the point waits. A creature of a command has no spawn point: the distance
  rule does not remove it.
- The cost of the spawn rule depends on the players and on the points near them, not on the
  size of the world. The creature data has the spawn points of the whole world, and no scan
  looks at each of them: the points are in a grid with cells of 64 m (`world::SpawnGrid`),
  a scan looks at the cells that are 200 m or less from each character
  (`World::for_each_character`) and at the creatures of the world (the creatures of the
  points near a player, and the creatures of commands), and the points that wait are in
  their own set (`Creatures::waiting_count`). The same for the behavior: a creature at rest
  needs no time. The step of the behavior looks only at the creatures of one set (a target,
  or the way back), and the vision rule runs from the characters with the position index
  (test case FT9, with a time limit). A new rule for a spawn point or for a creature keeps
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
  start time of the creature. A reaction, an attack, a walk, and the action of a command do
  not take it away, and the backend does not send it again at their end. A dead creature
  has its death action in the first state of a later session; a dead creature of a slice
  with no death action has its spawn action there, also when it died while its reaction
  played (test case WS4). A new action of a creature keeps this rule. The list
  `notShown` of the same file has the slices that the client does not show correctly: the
  tool leaves out their spawn points. A slice goes from one list to the other only with a
  picture of the live client (`tools/research/creature_tour.py`).
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
  world, is ignored, or is farther than 60 m, at the end of its leash, and when its target
  has no way to it: it then goes to its home and has its full health there. A creature has
  one target. From stage 3.4d a wall stops the vision, the attack, each contact of an
  attack, and the way (the navigation data, protocol notes 4.19; a ceiling between two
  bodies on one vertical line too). A kind with no attack that the data tool can use
  stands and takes damage (859 of the 3,848 kinds).
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
- The functions of the navigation (`world/navigation.h`) are the only place where the
  behavior gets a place: `ground_height(position)` (the height of the floor that is nearest
  to the height of a position: one place can have more than one floor) and
  `next_step(creature, from, goal, distance)` (the place after one step on the path of a
  creature to a goal, or "the creature waits", "no way", "no ground"). `footing(position)`
  says if a creature walks, and `sees(from, to)` if a solid is between two places. Do not
  compute a place of a creature in a different function. With navigation data the path is
  a path of the data; with no data of a place (no folder, a folder that is not correct, a
  region with no file, a tile that is not correct) the two functions have the rule of stage
  3.4b for that place (the straight line on the terrain of the folder `heights`, and
  nothing stops the sight).
- The navigation data (protocol notes 4.19) is game data: the folder `navigation` of the game
  data folder, one file for each region (`r_+XX_+YY.swnav`, 32 by 32 tiles of 64 m with cells
  of 0.25 m, each tile one zlib stream) and `navigation.json`. The tool
  (`tools/gamedata/navigation.sh`) makes it from the collision shapes of the game copy, the
  terrain after the cookies of the placements, the water, and the navigation marks of the
  level designers. The size of a creature (the agent: radius 0.35 m, height 1.8 m, step
  0.35 m, slope 50 degrees, wade 1.2 m) is a project value with a live check for each
  number; each file has the values in its header, and the backend refuses a folder whose
  files do not have the same values, as a whole, as for each other file that is not
  correct. A tile with more than 16 floors in one cell, and a tile stream with a byte after
  its zlib stream, are not correct. The backend reads only the header and the directory of
  each file at its start. The tool stops (status 1, no region file) for a placed collider
  whose physics file does not decode.
- The world has only the tiles near its players (`navigation_rules`): each 500 ms it asks
  for the tiles that are 96 m or less from a character and 32 m or less from a creature on
  a path, and it removes a tile that no one asked for in 10 s. A thread of the navigation
  reads the tiles (`WorldOptions::navigationThread`; a test host reads them in the tick):
  the world thread reads no navigation file, and it takes the tiles that are ready at the
  start of a tick. The thread reads only the navigation data: it reads no entity. A
  creature whose tile is not there yet waits: the tile of its place, of its goal, of a link
  between two areas, and of each step (a path in the memory of a creature can go into a
  tile that the world removed). A tile that the world does not have is no proof of "no
  way". Do not read all tiles of a region, and do not
  read a tile in the world thread (the development command `tools/world path` is the one
  exception, for a test).
- The cost of the path search has limits (`navigation_rules`): all searches of one tick of
  the world make 4,000 steps at most (`kTickSteps`), one search makes 60,000 steps at most
  (`kSearchSteps`), the other searches wait in a queue, and a search continues in the next
  tick. A goal in a different area has no way with no search (the areas of the tiles, and
  the links over the tile edges, which the world makes only for a tile that comes or goes).
  First the straight line, then A* on the cells. A step of a creature ends at a corner of
  its path: the client moves a creature on the straight line between two movement states.
  Test cases NV6 and NV10 have the limits with a time limit: a new rule keeps them.
- Which creature walks: a creature walks when a free floor of the navigation data is 0.5 m
  or less from its place in the plane (to the area of its cell) and 0.6 m or less in the height
  (`Navigation::footing`). The world asks when the tile of the creature is there: until then
  the creature has no answer (`CreatureMind::groundKnown`), and it does not walk yet. A
  creature on no free floor does not leave its place: it turns to its target and attacks in
  its reach. 23,021 of the 24,228 spawn points of kinds that fight walk.
- The gates of a settlement are closed for a creature: the level designers put a navigation
  mark with no solid into each gate (three in the start settlement), and the tool makes no
  floor free in it. A creature outside a gate sees a character in the gate, finds no way,
  and loses its target. Do not remove these marks to make a test pass.
- The terrain heights are game data (`tools/gamedata/heights.sh`, one file for each region in
  the folder `heights` of the game data folder, `gamedata::HeightMaps`). The backend opens a
  region file only when a position in it is asked, reads one tile of 64 m, and keeps 512
  tiles: do not load the 49 regions at the start. The terrain is not each floor. From stage
  3.4d the heights are the terrain after the cookies of the placements (the client changes
  the height map near some placements, protocol notes 4.19; `tools/gamedata/navigation.sh`
  makes the folder again at its end), and the backend uses them only at a place with no
  navigation data. There a creature walks only when its spawn point is 1.0 m or less from
  the terrain height (`gamedata::kTerrainTolerance`; the creature data file has the terrain
  height of each spawn point): if not, it does not leave its place, turns to its target,
  and attacks in its reach. Without the height files and with no navigation data no
  creature leaves its place.
- The client plays no walk for a creature whose position changes with no action: the
  creature slides in its pose. A creature on its way has the locomotion action of its slice
  (`Move_Combat`, `CreatureSlice::moveAction`, `Creature::moving`) from its first step to an
  attack or to its home; a step replaces a reaction that plays. An attack, a reaction, the
  death, and the locomotion are scope records with the tag bytes of the slice
  (`CreatureSlice::tags`: a creature with a weapon has no variant of an attack fragment with
  no tag, and its attack does not play with 12 bytes of 0). The client keeps the scope fields
  of a state that a later state does not have, and it starts a kept action again at each
  later state after its end; a record that comes again with a new time makes the body jump.
  Thus a session keeps the key of the scope that it sent for each creature
  (`Known::scope`, `creature_scope`), and a movement state has the scope fields only when
  that key changes: the record of the new action, or the fields with no record (the client
  plays its idle action). Protocol notes 4.14, user test 1.
- The health and the death of a character (protocol notes 4.18). Only the health system
  (`world/health.h`) changes the health and the life state of a character
  (`Entity::vitals.health`, `Entity::life`); the progress system sets the maximum. The
  damage of a creature attack is computed in one function (`Combat::creature_damage`, a
  project rule: change it there and in the notes together): the damage of the level and of
  the kind of the creature, the row of the contact, the level difference, and the armor of
  the character. The health 0 is the death: the vitals state gets the dead bit (bit 0 of
  `vitalsData`) for the owner and for each observer, and the session of the owner sends the
  death notice (type 3950) with the time to the respawn screen (5 s). Only the session of
  the character that took damage gets the damage mark (type 3601,
  `SessionSink::damage_taken`). A dead character takes no damage, does no damage, and is
  the target of no creature. The respawn request of the client (type 1938) makes the
  character live again with its full health at the nearest travel place, or at the place
  of its death with no travel places: the change of the place is a travel
  (`World::travel`). The world finds the character from the session. The health and the
  death are not in the database: each world entry starts with the full health, and a dead
  character that leaves the world gets its respawn place in its row (`World::leave`).
- The first vitals state of a session for a character that lives has no `vitalsData` and no
  `healthChangeFlags` (the stored bytes of case T2). Each later vitals state of a character
  has the two fields.
- Two switches for the behavior. `SOULWARDEN_CREATURE_BEHAVIOR=0` (backend option
  `--no-creature-behavior`) turns it off for a start: the creatures stand at their spawn
  points. The command `set_creatures_ignore` (`tools/world ignore`, the mark of the tab
  "Character") makes the creatures ignore one character: a value of the character in the
  memory of the world, with the character id as its key (`Behavior::set_ignored`), which
  stays when its session ends, until the backend stops.
- Travel (protocol notes 4.16). `World::travel` moves a character that is in the world to a
  different place with no new login: it writes the position row first, puts the entity at
  the place, gives the entity a teleport number (1 to 15, `Entity::teleport`, the low 4 bits
  of the field `teleportAndMigrationId` of the movement state), marks the movement state,
  starts a scan of the spawn rule, and tells the session of the character
  (`SessionSink::traveled`). A travel whose row the database did not accept changes nothing
  and gives the error `TravelError::storage` (the reply 500 of a command). The teleport
  number is one above the number that the client of the owner has
  (`SessionSink::client_teleport`: the number of the last full movement state that the
  session sent), not one above the number of the entity: each travel before the next
  movement state of the owner has the same number, and 15 travels in one tick do not give
  the client its own number again. The client moves its character only for a movement
  state with a teleport number that is different from its own: the session then sends the
  full movement state to the owner and the message 3954 after it, and, for a distance above
  100 m, the message 3951 before it (the loading screen). An observer gets the movement
  state with the new position by the usual path. After the world entry, a travel is the only
  case in which a position of the server goes to the client of the owner. The position check starts again at the new
  place (`PositionTrack::traveled`): a report of the client from before the travel is
  refused with the cause `beforeTravel`, and it changes nothing. The session keeps the place
  of the travel until its client confirms it (an accepted position report, or a camera
  estimate near the place): until then the camera estimate does not follow a camera that is
  more than 15 m from the place in the horizontal plane or in the height
  (`RepSession::camera_before_travel`). The time alone does not end this rule: it adds only
  the distance of a movement with the limits of the position check. A travel of a character
  that is not in the world writes its row. The world refuses a travel to a place that is
  not a place for a character (`travel_position`: the limits of the world, and a height
  from -99 m to 999 m, the range of a height on the wire). The creatures of the new place
  come into the interest table of the owner when its client confirms the place, and at the
  latest 20 s after the travel (`kCreaturesAfterTravel`): the loading screen hides the start
  of a spawn action. A travel and a respawn end each lock of the input of the client: the
  session of the owner sends the messages 3746 and 6709 before the messages of the travel
  (protocol notes 4.20).
- The lock of the input of a client (protocol notes 4.20, `server/rep/lock_messages.h`). A
  conversation with a person (the key E, "Talk") is a player cutscene of the client: at its
  end the client sends 6705 and keeps the input of its character blocked until the answer
  6709, which the session sends at once (a travel does not free it). The client also sends
  2473 (the end of its committed interaction): the backend has no interaction state and
  changes nothing. A call that names a different entity gets nothing. Only the session of
  the owner gets 6709 and 3746. The backend has no conversation content, no lore content,
  and no interaction of an object: a new message of these systems keeps this answer.
- The mana of a character is in `Entity::vitals` and goes to its owner in two states with the
  same values: group 1 of the vitals state, and the mana state (slot 47, type 1652, owner
  only, `states/mana_state.cpp`). The mana meter of the HUD (the blue bar below the
  crosshair) reads only the mana state: with no mana state it shows always. A change of the
  mana marks the two states (`ComponentKey::vitals`, `ComponentKey::mana`). Protocol notes
  4.5.
- The travel places are game data (`travel-places.json`, `gamedata::TravelPlaces`): the
  spirit shrines and the inns of the territories, each with an id, a name, a territory, and
  a position. A command names a place by its id, or it gives the three numbers of a
  position. The commands are in the common layer (`server/http/travel_commands.cpp`): the
  development route (`tools/world travel`), the admin command `travel`, and the tab
  "Travel" of the tool window use it. With no file the list is empty, and a travel with
  numbers works.
- A melee attack of a player (protocol notes 4.15). The client finds each melee contact
  itself and reports it in the third list of its input entry (the contact report). The
  report is on in each start: the backend answers one remote configuration request of the
  client with the document that turns the client setting
  `javelin.enable-client-authoritative-melee` on (`server/http/aws_routes.cpp`; the exact
  request of protocol notes 2.3; each other request to that host keeps the reply 404: the
  route compares the full path text and the host with no port, `Router::add_exact`).
  `SOULWARDEN_MELEE_REPORT=0` (backend option `--no-melee-report`) turns it off: an attack then
  has no effect. The document has this one setting: do not add a setting without a live
  check.
- A contact is a claim of a client, not a fact. The session gives each claim to the combat
  system of the world (`world/combat.h`, `Combat::claim`), which finds the attacker from the
  session and checks the claim: the character lives, the target is a creature of the world
  that lives and that the session observes, the reference names a component of its slice, an attack input of
  the client fits the contact, the character has a melee weapon in its hand, the same attack
  did not hurt the creature, the exact position of the character is 5 m or less from the
  creature, and the client made 12 claims or less in 1 s. A refused claim changes nothing
  and gets no reply; the log counts the refusals with their cause. The values of one client
  for the check (`world::AttackTrack`: its attack inputs and the counts) are in its session,
  as the values of the position check. The two limits of a hit (the rate of the claims, and
  one hit of a creature for each attack) are values of the character, not of a session
  (`world::HitLimits`, `Combat::limits_of`, with the character id as the key): a new session
  of a character gets the limits that the character has, thus a client that connects again
  and again gets no more hits. The session takes each input step one time: a client sends
  an entry in 5 reports. An input entry that ends before the end of its body has no
  position of the character (`ClientInput::hasCharacter`).
- The damage of an attack is computed in one function (`Combat::damage`), from the combat
  tables (game data) and the entity of the attacker: the weapon of the item, its gear score,
  the damage row of the light or of the heavy attack, the attributes and the level of the
  attacker, and the mitigation, the weakness, and the absorption of the creature. The rule is
  a project rule (protocol notes 4.15): change it there and in the notes together. The health
  goes down through the creature system, each observer gets the vitals state, and only the
  session of the attacker gets the damage number (`SessionSink::damage_dealt`, message type
  2071 for the damage receiver facet of its own character). The client computes no damage.
- The weapon in the hand of a character is the item of the weapon slot that its client
  selected with a weapon key (`Entity::combat.selectedWeapon`), or the first weapon slot
  with an item. The sheathe bits and the active bits of the paperdoll state do not follow
  the keys yet (roadmap 3.1).
- The position of a character that a client plays is the position that its client reports
  (the position field of the input report, protocol notes 4.7). The session gives each report
  to the world (`World::report_position`), which finds the entity from the session, checks
  the report (`world/position_report.h`: a newer input number, finite numbers, the two limit
  values of the height, a peak speed, a speed of the height, a speed over a time window
  of 1 s, and a budget of time for the rises and a second one for the falls of the accepted
  reports: an accepted report takes the time of its rise from the first budget and the time
  of its fall from the second one (the sums of the rises and of the falls, not the
  difference of two heights), and the world clock fills each budget one time, thus the
  rises of a session are never more than 20 m/s times the time of the world clock plus
  0.25 s; a budget keeps the time that the client did not use, to the limit of 1 s, or of
  the time that the client has in store when that is more: the time of the world clock that
  its input numbers did not cover, 5.25 s at most, so that reports that come late and
  together pass; do not give a client the time of the world clock and the time of its input
  numbers for the same seconds; the first report of a session, and
  the first report after a travel, has the position of the entity as its reference, also
  for its height; the limits are constants from one live session, `position_limits`), and
  keeps an
  accepted position in the entity. A report that the world refuses changes no value of the world, and
  the session counts it in the log. The values of one client for the check (`PositionTrack`:
  the last input number, the accepted positions of the window with the two budgets of the
  height, the counts) are in its
  session, not in the entity. The camera estimate (`RepSession::track_position`,
  `World::set_position`) is the fallback: a session uses it when its client sends no position
  field (no hook change, the switch off, a different client build), and when no report had
  the field for 1 s. The log of a session names its source, and each change of it. A position
  of the client never goes back to its own client as a new position (the movement state is
  not marked).
- The heartbeat state of the movement component (each 200 ms, owner view) has the time anchor
  and the id of the rule (`movement_state_id`, protocol notes 4.6): 0x8000 or 0x0000,
  0x4000 or more from the newest input number of the client of the session. Never send a
  movement state for the owner with an id that can be an input number of its client: the
  client then moves its character to the position of that state.
- The position report of the client is a change of the client code by the hook
  (`apps/game-hook/src/client/hooks/movement`): 6 bytes at image address `0x2A242C1`, so that
  each input entry has flag bit 15. It is on in each start (it is a part of the product, not
  behind the harness gate), and `SOULWARDEN_EXACT_POSITION=0` turns it off. The hook compares the
  37 bytes from `0x2A242BB` with the code of the supported build before it changes them: with
  different bytes it changes nothing, the hook log has one line (`position report: OFF for
  this start of the game: CAUSE`), and the game continues. The hook checks the result of
  each call of the change (the page protection before and after the write, and the flush of
  the instruction cache): after a failure it puts the original bytes and the protection back
  where possible, and the log line names the step that failed. It is installed before the trace
  points: a trace point at the same address changes the bytes of the compare. The backend
  sends no housing state for this.
- The items and the coin of a character are in its entity (`world/item.h`: the bag is a
  vector of slots, as in the client) and in the tables `items` and `currencies`, with the
  character id as the owner key. Only the inventory system (`world/inventory.h`) changes
  them: it names a character, makes the change on a copy, writes all rows of the change in
  one transaction, and then changes the entity and marks the state. A request that is not
  correct changes nothing. A command for a character that is not in the world changes its
  rows. The database has the `ItemID` text of an item; the index on the wire comes from the
  item catalog at each load.
- The equipment of a character is in its entity too (`world::Equipment`: a map from the slot
  number of the paperdoll to a slot) and in the same table `items`: an equipped item is a
  row with the container 2 and the slot number of the paperdoll. The inventory system has
  equip, unequip, and the swap of two slots: a move between the bag and the paperdoll is one
  change, thus one transaction, and the item keeps its instance id. The slot of an equip
  must take the class of the item (`world::paperdoll::slot_accepts`, the rule of the
  client). A slot that loses its item stays in the map as an empty slot while the character
  is in the world: the client removes an item only when it gets the slot with no item.
- A request of a client for its equipment that the backend refuses gets the message "equip
  item fail" (type 3626) with the facet target and the entity of the request. A request
  that names the entity of a different character changes nothing and gets no reply.
- The lock of an item protects it from a request of a client that takes it away from the
  character (the drop). It does not stop an equip, and a development command has no lock rule.
- A new character gets the items of its archetype (the backstory table of the game, in the
  item catalog): the inventory module makes the rows (`world::start_items`), and the store
  writes them in the transaction that inserts the character. The backstory table also has
  rows of tests with many items: the create route makes a character only for a backstory of
  the character creation (the member `creationBackstories` of the catalog: the seven
  archetypes and the default backstory of the client), and it gives the reply 400 for a
  different one. A catalog with no such member gives no item to a new character.
- The level, the experience, and the attribute points of a character are in its entity
  (`world::Progression`, `world::Attributes`) and in one row of the table
  `character_progress`. Only the progress system (`world/progress.h`) changes them: set the
  level, give experience (the level goes up at the limit of a level), set an attribute, spend
  points, and reset them. The database has the points that the character spent on each
  attribute: the value of an attribute is the minimum of the game tables (5) plus these
  points, and the free points are the points of the level minus the spent points. The
  progress system computes the maximum health (the base of a player, the health of the
  level, and the health of the constitution) and marks the vitals state. The cost of a reset
  goes from the coin through the inventory system (`Inventory::pay`), in the transaction that
  writes the points. A system that gives experience later calls `Progress::add_experience`.
- The weapon masteries, the nodes of the weapon trees, the ability bars, and the trade skills
  of a character are in its entity (`world::SkillSet`) and in the tables `progressions`,
  `ability_nodes`, and `ability_bars`, with the id on the wire of each row (the CRC-32 of the
  lower-case id text of the game table) and its text in the column `name`. Only the skill
  system (`world/skills.h`) changes them, and it writes all rows of a change in one
  transaction. A weapon has one mastery point for each rank. A node that a character loses
  stays in its entity with the level 0 while the character is in the world: the client
  removes a node only when it gets it with the level 0.
- A request of a client for its attributes or its skills that the backend refuses changes
  nothing. The client has no failure message for these requests, and it changed its own
  values already: the session gets the state of the component again. Only the tree reset has
  an answer (type 2858, also for a refused reset, with the facet target and the entity of the
  request). A request that names the entity of a different character gets nothing.
- The ability state (slot 46) has one element for each of the 29 ability tables of the
  client, in the sequence of the client (`gamedata::kAbilityTableOrder`), and each element has
  three places. A fragment with fewer elements makes the client read memory that is not a
  bar. The sequence is a constant of the supported client build, read from the live client:
  the game files do not give it. `tools/research/ability_tables.py` compares it with a live
  client.
- A session is an observer: it has transport state, the camera estimate of its client, and
  the interest table (entity to interest number). One encoder for each component state
  (`server/rep/states`) makes the bytes for the owner view or for the observer view. No
  other code writes the bytes of a state. A new state type gets its encoder there, with the
  fields of each view in its file and in the protocol notes. A state that only the owner
  gets has the mark `ownerOnly` in its codec (the coin, the bag): an observer gets no
  fragment of it. A state that can be larger than one carrier message gives a list of
  fragments (`PartsEncoder`). The paperdoll encoder has the two views: the owner gets each
  item, and an observer gets the look of the items that show on the body (the armor slots 0
  to 4 and the weapon slots 24 to 29) and nothing of the other slots. A paperdoll fragment
  has only the slots that changed. The progression encoder has the two views too: the owner
  gets the level and the experience, and an observer gets the level (the name tag of a remote
  player shows it). The attribute state and the three skill states are for the owner only.
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
  it, the locomotion of a way, the action of a command) is in the state, as a scope record
  with the index of the action and of its fragment from the action table of the slice and
  with the tag bytes of the slice (game data). The spawn action of a first record has the
  time 0: with the time from the creation of the creature the client of a later session
  shows only its light and no body. Never put a state id of a
  player (the script layers) into the state of a creature. Each vitals state of a creature
  has `healthChangeFlags` with bit 0: without it the name plate of the client keeps its old
  bar. The position of a creature is exact: the client does not move a creature to the
  ground, and the height of a spawn point can be a floor above the terrain. A creature that
  walks gets the height of each step from the navigation of the world.
- A session sends each bundle below the size of one carrier message (1,024 bytes) when its
  fragments permit it: it cuts a record between two fragments, and it sends two fragments of
  one slot in two bundles. It sends no bundle that is larger than the limit of the client
  (256,000 bytes). The metadata state of an entity goes to its owner one time only: a second
  one makes the client of the owner close its connection.
- The point state (slot 67) is never cut into fragments: one fragment has each pool and each
  node of the character, for each size. The client replaces its node list with the list of
  a fragment (live proof, stage 1.7). A state encoder gives a list of fragments only for a
  state whose fragments the client adds to what it has (the bag, the paperdoll).
- A datagram of the carrier has 1,038 bytes at most (`kMaxDatagram` in
  `server/carrier/carrier.cpp`): the client closes its connection when it gets a larger one.
  A message with more than 1,024 data bytes goes in chunks.
- A session ends through one function (`RepServer::end_session`) with a cause, and the
  session leaves the world there (`World::leave`): the world saves the position of the
  character and removes its entity. The destructor of a session saves nothing.
- The world decides who plays a character (`World::enter`). It refuses a character that the
  store does not have and a character of a different account (registration result -160). A
  character is in one session at most: the newer session wins, and the older one gets the
  message "kicked" and ends. A route that removes a character removes its entity first, in
  one task of the world.
- `tools/test-soulwarden-backend.sh` must give status 0 after each change of the backend. Each
  stage adds a case with two sessions (rule MP-10). The stored bytes of case T2
  (`apps/backend/tests/golden/one_session.hex`) change only for a change of the wire bytes
  that is intended and that a live run proves (`--write-golden`). The tests have only
  made-up ids: no capture file and no id of a real account goes into the repository.
- A development run and a game of the user are separate. A development run
  (`tools/dev-run.sh`, `tools/scenarios`) has its own Proton prefix
  (`~/Games/soulwarden/proton-dev/1063730`), its own data directory
  (`~/Games/soulwarden/work/dev-data`), and the mark `SOULWARDEN_DEV_RUN=1` in the environment of each
  of its processes. `tools/dev-stop.sh` stops only processes with the mark. A start with no
  development variable (`linux-compat/run-soulwarden.sh`, the Steam library entry) uses the
  prefix `~/Games/soulwarden/proton/1063730` and the data directory `~/.local/share/soulwarden`.
  `tools/dev-env.sh` is the only place of the defaults of the development tools: a new tool
  reads the data directory from it (a Python tool with `tools/dev_env.py`).
- The tool window. The hook draws one Dear ImGui window on the picture of the game
  (`apps/game-hook/src/client/toolwindow`; key F8, `SOULWARDEN_TOOL_WINDOW_KEY`). It is on in each
  start, and `SOULWARDEN_TOOL_WINDOW=0` turns it off: the hook then installs no part of it. The
  tool window must never stop the game: each part of its start checks what it needs (the
  bytes of the client code, the import slots, the device, the queue, the swap chain), and a
  failure turns the tool window off for the start with one log line (`tool window: OFF for
  this start of the game: CAUSE`). Three threads: the submission thread of the renderer makes
  each Dear ImGui call and each Direct3D call (the hook is at the present call site
  `0x8D445C`); the window thread only puts input messages in a queue (the hook of the window
  procedure `0x7159680`); one worker thread makes the socket calls. Do not make a Dear ImGui
  call or a socket call on a different thread. While the tool window shows, the game gets
  each release of a pointer button and of a key, and no other input: a raw pointer packet
  with a release and other events (a movement, a different button, the wheel) goes to the
  game, and the game reads only the releases of it (the hook replaces the import slot of the
  client for `GetRawInputData`, `0x7ECA660`). The pointer capture of the hook ends on the
  window thread when the window closes and when the tool window stops. A command of the
  window keeps the character id of the snapshot that the window drew
  (`model::command(character, ...)`): do not read the selected character at send time. The
  tabs: "Items", "Character" (with the health of the character, the button "Heal", which
  has the text "Revive" for a dead character, and the mark "Creatures ignore this
  character"), "Skills", "Creatures", and "Travel" (the position of the
  character, the list of the travel places with a filter and the distance of each place, a
  button "Travel" for the selected place, and a form with three numbers). The tab
  asks the worker thread for the list of the places (`model::load_places`) until the
  backend gave it, one time each 5 s at most.
- The admin interface of the backend (routes `/admin/v1`, `server/http/admin_routes.cpp`) is
  for the tool window. It needs no harness gate: each start has it when the launcher gave a
  token. A request needs the token of the start in the header `X-Soulwarden-Admin`. The token is
  for the account of the game of the start: the backend finds the account from the token,
  each command names a character id, and a character of a different account gives the reply
  404. The token goes to no log line, to no capture file, to no query, and to no command
  line. The query of a request with a path below `/admin` goes to no log line and to no
  capture file (`capture::recorded_target`): a sender can put the token there by mistake. An
  argument of a command that is not necessary (`quantity`, `gearScore`, `action`,
  `experience`) and that is there with a wrong form gives the reply 400
  (`Arguments::has`): it is not the same as no argument. A creature command
  (`spawn_creature`, `remove_creature`, `set_creature_health`, `kill_creature`,
  `reset_creatures`) acts for the character of the command as the reference place: that
  character must be a character of the account of the token, and the state of a character
  has the creatures near it. An admin command that changes a creature (`remove_creature`,
  `set_creature_health`, `kill_creature`) changes only a creature that is 150 m or less from
  that character (`commands::kAdminCreatureRange`, the distance of the list), and the
  character must be in the world: if not, the reply is 404 and nothing changes. The command
  `travel` moves a character of the account of the token (a place id, or three numbers),
  the route `GET /admin/v1/catalog/places` gives the travel places, and the state of a
  character has its position. The command `heal` gives a character its full health (a dead
  character lives again at its place), the command `set_creatures_ignore` (argument
  `ignore`) makes the creatures ignore a character, and the state of a character has the
  member `health` (the health, the dead mark, the ignore value, and if the behavior of the
  creatures is on). The command that lowers the health of a character is a development
  route only (`tools/world hurt`). A
  development route (`/v1/dev/creatures`, `tools/world`) has no such rule: it changes each
  creature of the world. Each creature command of the common layer has the parameter
  `commands::CreatureReach`, and each route set gives its rule. The admin routes and the
  development routes have one layer below them
  (`server/http/character_commands.h`): add a command for a character there, and give it to
  the two route sets. A route has no logic of its own for a character.
- The progression tables loader refuses a file with a table that is not valid (a level table
  whose experience does not start at 0 or does not go up, a rank table that is shorter than
  the largest rank or has an experience of 0 below it, a largest rank with more than 16
  bits, a weapon with no reset cost for each rank). The backend then works with no tables.
- All game traffic stays on the local computer. The backend listens only on loopback. The
  launcher runs the game in a network namespace with loopback only. Do not weaken this.
- Keep SQLite, logs, captures, game data, and analysis files outside the repository:
  `~/.local/share/soulwarden` (runtime data) and `~/Games/soulwarden` (game copy, Proton prefix, Ghidra
  project, Python environment, work files).
- Dear ImGui (version 1.92.6) is in `apps/game-hook/vendor/imgui`, with no change of its
  files. The CMake file compiles each `.cpp` file below `apps/game-hook/vendor`: do not add a
  file of the library that the hook does not use (the demo file). The JSON library of the
  backend is also in `apps/game-hook/vendor/nlohmann`. The OpenSimplex noise code
  (`tools/gamedata/vendor/open-simplex-noise`, public domain, with its license file, no
  change of its files) is a part of the navigation tool only: the client makes the noise of
  the terrain rule with this code, and the tool needs equal numbers. It goes into no product.
- A table that comes from the game files is game data. A tool in `tools/gamedata` makes it
  from the game copy into `~/.local/share/soulwarden/gamedata` (`SOULWARDEN_GAMEDATA_DIR`), the backend
  reads it at its start (`--gamedata-dir`), and the backend must start and work with a
  default when the file is not there. Numbers of a wire contract are code constants. The
  game data files now: `appearance-tables.json`, `slice-ids.json`, `creatures.json` (the
  spawn points of the creatures of the whole world, and for each kind its slice, the slots of its
  components, its vitals ids, its health, its name, and its action table; from stage 3.4b,
  format 2, also the behavior data: the vision, the leash, the speeds, the animation tags,
  and the attacks of each slice, the damage of each kind and of each level, and the terrain
  height at each spawn point; without it the world has no creature; the backend refuses a
  file whose levels, slots, assets, health values, or behavior data do not fit together, as
  a whole; with a file of format 1 the creatures stand at their spawn points), the folder
  `heights` (the terrain heights, one file for each region, from stage 3.4d after the
  cookies of the placements), the folder `navigation` (stage 3.4d: one file for each region
  with the floors, the water, and the sight of each cell of 0.25 m, and `navigation.json`;
  1.2 GB for the world; without it a creature goes on the straight line on the terrain of
  the folder `heights`, and with neither folder no creature leaves its place; the backend
  refuses a folder with a file that is not correct, as a whole, and a tile that does not
  decode alone), `combat-tables.json` (the weapon of each item
  with its base damage, its attribute factors, and the damage rows of its light and of its
  heavy attack, the damage rows and types, the damage values of the attributes, the level
  and gear score values of the damage rule, the defense values of each vitals row, and,
  from stage 3.4b, the armor values of each armor item and the damage values of the level
  differences for the damage of a creature (with a file from before, the armor of a
  character has no effect); without it an attack of a player on a creature has no effect; the backend refuses a file
  with a damage type whose category is not `Physical`, `Elemental`, or `True`, and a file
  with an empty value list of an attribute, as a whole; the tool writes no file when a
  weapon names a damage row or a vitals row names a base row that the tables do not have,
  when a column that it reads is not there, and when a cell is not a finite number: the
  names that the game itself does not have are in the two lists `ROWS_NOT_IN_GAME` and
  `BASES_NOT_IN_GAME` of the tool), `travel-places.json` (the spirit shrines and the inns
  of each territory with their names and positions; without it the list of the travel
  places is empty, and a travel with numbers works), `item-catalog.json` (the
  item list with the index of each item on the wire, the items of each backstory, and the
  list of the backstories of the character creation; without it the backend gives no item,
  and a new character has no clothing), and `progression-tables.json` (the level table, the
  attribute and vitals values, the rank tables of the weapons and the trade skills, and the
  nodes of the weapon trees; without it the backend sends the stored values of a character
  and refuses a command or a request that needs a table).
- A character belongs to the account of the request token (persona id). A route that
  creates, deletes, or logs in a character must compare the owner with that account.
- Write each wire contract in the protocol notes with its evidence level and the client code
  address. Addresses are image-relative and apply to one client build.
- Keep automation and diagnostics behind the `SOULWARDEN_HARNESS=1` process gate. The backend
  refuses the options `--research-session` and `--dev-commands` when its own process does
  not have `SOULWARDEN_HARNESS=1`, before it starts a listener.
- Player identity: the launcher reads only the Steam id and the persona name from
  `config/loginusers.vdf` of the Steam installation, and no Steam credential. The hook gives
  them to the client and to the backend in a local ticket. The ticket is not a credential, and
  the backend does not validate it (stub authentication). The Steam id selects the account
  row; the persona id of the account is the owner key of all player data.
- Privacy: the Steam id, the account name, and the persona name of the user do not go into a
  file of the repository. A log line of the hook or of the backend shows only the last 4
  digits of a Steam id (`masked_steam_id` in the backend). This applies to generated files
  too: the CMake build directories are in `~/Games/soulwarden/work/build` (`SOULWARDEN_BUILD_ROOT`), the
  products have no absolute path (`-ffile-prefix-map`), and the Python tools write no
  bytecode cache.

## Development loop

```sh
tools/ghidra/start-mcp.sh &          # Ghidra MCP server on 127.0.0.1:8765 (needs ~1 minute)
tools/build-soulwarden-backend.sh
tools/test-soulwarden-backend.sh          # tests of the backend with no game process (status 0: all pass)
tools/build-soulwarden-game-hook.sh
SOULWARDEN_GAME_HOOK_OUTPUT=~/Games/soulwarden/work/hook-test/steam_api64.dll tools/build-soulwarden-game-hook.sh   # a hook for a test: not into dist
tools/gamedata/appearance.sh         # one time: appearance tables for the backend
tools/gamedata/slice_ids.sh          # one time: ids of the player slice for the backend
tools/gamedata/items.sh              # one time: item catalog for the backend (index of each item on the wire)
tools/gamedata/progression.sh        # one time: progression tables for the backend (levels, attributes, skills)
tools/gamedata/creatures.sh          # one time: creature data for the backend (spawn points, kinds, slices); needs an Oodle source
tools/gamedata/combat.sh             # one time: combat tables for the backend (weapons, damage rows, defense values of the creatures)
tools/gamedata/places.sh             # one time: travel places for the backend (the spirit shrines and the inns of each territory)
tools/gamedata/heights.sh            # one time: terrain heights for the backend (a creature that walks needs them; 136 MB, 2 minutes, no Oodle source)
tools/gamedata/navigation.sh         # one time: navigation data of the world (1.2 GB, 43 minutes with 8 processes, 2.1 GB of work files; needs an Oodle library, a C and a C++ compiler; it also makes the folder heights again)
tools/scenarios/menu.sh RUN_LABEL    # start, go to the main menu
tools/scenarios/play.sh RUN_LABEL    # the same, then press Play for the first character
tools/scenarios/create.sh RUN_LABEL NAME   # menu, then create a character and enter the world
tools/scenarios/leave.sh             # a game that is in the world: leave it to the main menu
tools/hx 'key 0x77'                  # F8: show or hide the tool window (needs the foreground state)
tools/hx 'toolwindow 1'              # show the tool window with no key ('toolwindow 0' hides it, 'toolwindow' prints its state)
tools/shot NAME                      # screenshot of the game desktop
tools/world list                     # the entities of the world of the run
tools/world spawn NAME --near OTHER 2 2 0   # a character of the database as a remote player
tools/world remove NAME              # take that entity out of the world
tools/world give NAME ITEM 10        # give 10 of an item (ItemID text) to a character
tools/world items NAME               # the bag and the coin of a character
tools/world coin NAME 123.45         # set the coin of a character
tools/world equip NAME 3             # put the item of bag slot 3 into its slot of the paperdoll
tools/world equip NAME 0 25          # put the item of bag slot 0 into slot 25 (weapon 2)
tools/world unequip NAME 1           # put the item of slot 1 of the paperdoll (chest) into the bag
tools/world equipment NAME           # the equipment of a character
tools/world level NAME 20            # set the level of a character
tools/world xp NAME 6000             # give experience (the level goes up at the limit)
tools/world attribute NAME strength 12   # set the value of one attribute
tools/world mastery NAME sword 5     # set the mastery level of a weapon (each level above 1 gives one point)
tools/world tradeskill NAME Mining 50    # set the level of a trade skill
tools/world skillxp NAME sword 100   # give experience to a weapon mastery or to a trade skill
tools/world progress NAME            # the level, the attributes, the masteries, and the trade skills
tools/world creatures NAME 60        # the creatures that are 60 m or less from a character (no name: each creature)
tools/world kinds wolf               # the kinds of the creature data with a text in their id, name, or family
tools/world creature spawn Wolf_Grey_7 NAME 0 4 0   # a creature of a kind 4 m to the north of a character
tools/world creature health 2c 50%   # set the health of the creature whose id ends with 2c
tools/world creature kill 2c         # kill it (the death action, the dead mark)
tools/world creature remove 2c       # take it out of the world
tools/world creature reset           # each spawn point that waits makes its creature again
tools/world creature action 2c Spawn 3   # start an action of the action table of its slice (3 s; no time: no end)
tools/world places windsward         # the travel places with a text in their id, name, or territory
tools/world travel NAME shrine_40170 # move a character to a travel place (in the world: at once; not in the world: its row)
tools/world travel NAME 8866 4250 137.9   # move a character to a position (--screen on|off|auto: the loading screen)
tools/world ignore NAME on           # the creatures ignore a character (off: they attack it again)
tools/world heal NAME                # give a character its full health (a dead character lives again at its place)
tools/world hurt NAME 500            # lower the health of a character (the health 0 is its death)
tools/world path NAME 8861.6 4243.4 139.5   # the path of the navigation between two places (a place: X Y Z or a character name)
tools/world track 2c 20 --tsv FILE   # the place of a creature each 200 ms for 20 s
python3 -B tools/research/nav_draw.py ~/.local/share/soulwarden/gamedata/navigation 8850 4230 8880 4260 OUT.png --path FILE   # a picture of the data
tools/dev-stop.sh                    # stop the development runs (not a game of the user)
tools/dev-stop.sh --check            # status 0: no process of a development run is alive
```

- `tools/test-soulwarden-backend.sh` builds the test program (`soulwarden-backend-tests`, native target
  only) and runs it. The program runs the real game server with made-up clients, a manual
  clock, a transport with no socket, and a database in memory. Its output lists each case
  (`--list` prints the cases, `T1 E3` runs only these cases). The cases: two sessions (T1),
  stored bytes of one session (T2), capture folders (T8), log marks (M1), client context
  instance (C1), session end causes (E1 to E8), research switch (R1), world object (W1 to
  W7), entity table (N1), a character in a second session (T3), refused registration (T4),
  encoder views and the record of a remote player (V1), ids and the check of the client
  messages (T9), interest table (T10), entity with no session (S1), delete route (D1),
  position index (P1), item catalog file (B1), wire bytes of the container state and of the
  currency state (B2), give, remove, and coin (B3), move, swap, stack, and split (B4), the
  drop request and the lock request of a client (B5), the bag with two sessions (B6),
  persistence of the items (B7), a bag in more than one bundle (B8), the item routes (B9),
  the slot rules and the wire bytes of the paperdoll state for the two views (G1), equip,
  unequip, and swap (G2), the equip, unequip, and swap requests of a client with the
  failure reply (G3), the equipment with two sessions (G4), persistence of the equipment
  (G5), the items of an archetype for a new character (G6), the equipment routes (G7), a
  paperdoll in more than one bundle (G8), a create request with a backstory that is not of
  the creation (G9), an unequip into a full bag (G10), short equipment requests (G11), the
  progression tables file (A1), the wire bytes of the progression state and of the attribute
  state for the two views (A2), level and experience (A3), the spend and the reset requests
  of a client (A4), the level and the attributes with two sessions (A5), their persistence
  (A6), their routes (A7), the level in the menu document (A8), the wire bytes of the three
  skill states (K1), mastery and trade skill ranks (K2), the mastery point, bar, and tree
  reset requests of a client with the answer (K3), the skills with two sessions (K4), their
  persistence (K5), a large weapon tree set in one fragment (K6), the skill routes (K7), a rank
  table that is not valid (Q1), a weapon rank with no reset cost row (Q2), a level table whose
  experience does not go up (Q3), white space at the level member of the menu document (Q4),
  the admin interface: who can use it (X1), the state of the characters of an account (X2),
  each command with a correct and with a wrong value (X3), two accounts with their own tokens
  and two sessions (X4), the token in the log and in the capture files (X5), the item search
  (X6), the query of an admin request in the log and in the capture files (X7), an argument
  with a wrong JSON form (X8), the reader of the input report with and without the position
  field (PR1), the check of a position report: sequence, numbers, height limits (PR2), the
  speed check (PR3), the exact position of an entity and of its row, and a refused report
  (PR4), the camera estimate as the fallback (PR5), the positions of two sessions (PR6), the
  id rule of the heartbeat state (PR7), the height of the first report of a session (PR8),
  the total rise and fall of the time window (PR9), the creature data file and a world with
  no data (CR1), the bytes of a creature record, of a health change, and of a death (CR2), a
  creature in the entity table and in the position index (CR3), the spawn point rule (CR4),
  the interest of a session for creatures and many creatures in more than one bundle (CR5),
  health and death as state changes (CR6), the creature commands of the development routes
  and of the admin interface (CR7), three sessions with one creature (CR8), the respawn time
  of a spawn point with its body near a player (CR9), the reference character of an admin
  creature command with two accounts and two sessions (CR10), a creature data file whose
  parts do not fit together (CR11), two fragments of one slot and a long fragment in the
  records of a creature (CR12), the health command with a percent value (CR13), the totals of
  the rises and of the falls of the time window (PR10), the reader of an input report with
  items in its three lists (HT1), the attack inputs of a client: light and heavy, a long
  hold, fast clicks, the input numbers (HT2), the combat tables file and the damage rule
  with the worked example (HT3), one hit with the bytes of the health change, of the
  reaction, and of the damage number (HT4), each refusal of the hit check and a backend with
  no combat tables (HT5), the death of a creature by a hit and attacks on the body (HT6), the
  weapon in the hand and the weapon keys (HT7), three sessions with one wolf: two observers
  and one session far away (HT8), the remote configuration document and its switch (HT9),
  a combat tables file with a category or an attribute list that is not valid (HT10), the
  combat data tool with tables that are not complete (HT11, which runs
  `tools/gamedata/check_combat_tool.py` with `python3`), an input entry that ends after its
  position field (HT12), the exact path of the remote configuration document (HT13), the
  hit limits of a character with more than one session (HT14), position reports that come
  late and together (PR11), the loader of a creature data file with 33,856 spawn points, and
  a file of that size that is not complete (WS1), the cost of the spawn rule in a world of
  33,856 spawn points with 1 player and with 50 players, with a time limit (WS2), the spawn
  action of a slice and the action
  command (WS3), the bytes of a travel for the owner: the teleport number, the two messages,
  the loading screen rule (TV1), the position check after a travel (TV2), a travel with two
  sessions (TV3), the travel commands of the two route sets and the travel places file
  (TV4), the creatures of the old place and of the new place after a travel (TV5), the
  camera estimate after a travel: a camera at the old place and a camera above the new
  place (TV6), more than one travel of a character before one movement state (TV7), a
  travel whose position write failed, and a position that keeps its mark (TV8), the travel
  places tool with a place whose position has a wrong form (TV9, which runs
  `tools/gamedata/check_places_tool.py` with `python3`), a time of the world clock that
  gives its rise and its fall one time (PR12), a creature data file with a fragment number
  outside the range (CR14), a creature with a spawn action and no death action that dies
  while its reaction plays (WS4), the creature file with the behavior data, a file whose
  parts do not fit together, and an old file (FT1), the sense of a creature: the vision
  distance and angle, a target by a hit, one target (FT2), the way to the target: the
  speed, the ground, the position index, the states of an observer (FT3), the attack action
  and its contact at its time, and a target that went away (FT4), the damage rule of a
  creature attack with the worked example, and the bytes for the owner and for an observer
  (FT5), the death of a character, the respawn request, and a dead character that leaves
  the world (FT6), the way back: the leash, a target that is gone, the health at the home
  (FT7), the spawn point rule for a creature that is not at its spawn point (FT8), the load
  of the behavior with 33,856 spawn points and 50 players, with a time limit (FT9), two
  sessions near one creature, a session far away, and a death while a second player fights
  (FT10), the switches: the behavior off, the creatures ignore a character, the heal
  command (FT11), a creature whose spawn point is not at the terrain height, and the spawn
  command on a slope (FT12), a slice with a spawn action and with animation tags in a fight
  (FT13), the reader of the terrain height files and the two functions of the navigation
  (FT14), the reader of the navigation files and each file and tile that is not correct
  (NV1), the ground at a place with more than one floor (NV2), a path around a wall and
  through its opening, and back (NV3), a creature on a floor and on stairs (NV4), a goal with
  no way and no search (NV5), the limit of the search steps of a tick (NV6), the tiles near
  the characters and the thread that reads them (NV7), a wall stops the sight and the attack
  (NV8), shallow water and deep water (NV9), the load of 50 characters with fights and path
  searches, with a time limit, and the memory of the tiles (NV10), two sessions in two
  regions and a session that observes a creature on its path (NV11, in `fight_tests.cpp`),
  which creatures walk and a creature whose place has no data yet (NV12), the shelter of a
  character that came to a place, with two characters (PL1), the health that comes back
  (PL2), light and heavy attacks, their sequence, their pause, and the hits of an attack
  (PL3), the crowd (PL4), the cases of the code review of stage 3.4b, each one with a
  failure before its correction (RF1 to RF9; RF10 runs `tools/gamedata/check_heights_tool.py`
  with `python3`), the cases of the code review of stage 3.4d, each one with a failure
  before its correction (RN1 to RN9; RN10 runs `tools/gamedata/check_navigation_tool.py` with
  `python3`, and the check compiles a small program with `navcore.cpp`), the corrections of
  user test 1: the end of a cutscene and the lock of the input with two sessions (UT1), the
  locomotion action with three sessions (UT2), the creatures of a travel that wait for the
  client (UT3), and the mana state (UT4),
  HTTP stop (H1), a world task that did not start
  (D2), position limits (P2), bundle limits (F1), sessions and stops (T11), a locked item
  and the quantity of a lock request (L1), a delete whose write failed (D3), a spawn offset
  that is not a number (P3), a bag change whose write failed (B10), older database versions
  (B11). `--write-golden` writes the stored file of T2 again: do this only for an intended
  change of the wire bytes. Cases D2 and H1 wait some seconds.

- A development run has its own Proton prefix: `~/Games/soulwarden/proton-dev/1063730`
  (`SOULWARDEN_COMPAT_DIR` selects a different one; `tools/dev-env.sh` has the default). Proton
  makes the prefix at the first start (some seconds). Each process of a development run has
  `SOULWARDEN_DEV_RUN=1` in its environment, and `tools/dev-stop.sh` stops only these processes.
  `tools/dev-run.sh` starts no game when a game that is not a development run uses the data
  directory (the lock file `run.lock`): give the run a different `SOULWARDEN_DATA_DIR`.
- A development run and each development tool (`tools/hx`, `tools/shot`, `tools/world`, the
  scenario scripts, `tools/research/player_position.py`, `check_identity.py`,
  `gde_components.py`) use the development data directory `~/Games/soulwarden/work/dev-data` with
  no variable. `SOULWARDEN_DATA_DIR` selects a different one: then give the same value to each
  tool of that run. `tools/dev-run.sh` removes the captures and the backend log of the data
  directory at each start: copy what you need before the next run.
- `tools/dev-run.sh` starts the game on an isolated agent desktop (`soulwarden-game`), not on the
  user desktop, with capture and the harness on. The game runs in gamescope, which is one
  Wayland window on that desktop and has its own X server (`SOULWARDEN_PRIVATE_DISPLAY=0` turns
  this off, `SOULWARDEN_GAME_SIZE` sets the size). Do not turn it off while the user uses the
  computer: on the X server of the user session, the pointer moves and the focus changes of
  the harness reach the other X11 applications of the user (other games). The script prints a
  warning for the value `0`. It starts no game for a value that is not `0` or `1`, or when
  `gamescope` is not installed.
- Send test input only through `tools/hx`. Do not use a host input tool (`xdotool`,
  `ydotool`, `wtype`, `hyprctl dispatch`, `omarchy-agent-desktop key` or `enter`).
- Identity variables (`docs/REFERENCE.md`, "Player identity"): with `SOULWARDEN_STEAM_ID` not set, a run uses
  the Steam account of the computer. `SOULWARDEN_STEAM_ID=<17 digits>` with `SOULWARDEN_PERSONA_NAME`
  selects a different account (a made-up id for a test: `76561202255233008`).
  `SOULWARDEN_STEAM_ID=local` selects the fallback identity. `SOULWARDEN_SEED_CHARACTER=<name>` gives an
  account with no character one character. `tools/scenarios/play.sh` sets the seed `Soulwarden`
  when the variable is not set; `tools/dev-run.sh` and `tools/scenarios/menu.sh` set no seed.
- This is a development environment: data loss is not a problem. A test uses the fastest
  path to the state that it needs. The development data directory stays between runs:
  `tools/scenarios/play.sh RUN_LABEL` enters the world with a character that is there
  already. Its database has two accounts now: the Steam account of the computer with the
  character `Soulwarden`, and the made-up test identity (`76561202255233008`, `Tester`) with the
  characters `Remote` and `Gearone`. `Soulwarden` has a chest armor, leg armor, an amulet, a
  sword, and a potion stack in a quick slot on it, the level 26 with points on strength,
  dexterity, and constitution (strength 8, dexterity 25: the damage values of protocol notes
  4.15), a spear (`2hSpearT1`) in weapon slot 2 (slot 25), a great axe (`2hGreatAxeT2`), a
  hatchet (`1hHatchetT2`), and a war hammer (`2hWarhammerT2`) in the bag (`tools/world equip
  Soulwarden BAGSLOT 25` puts one of them into weapon slot 2), six weapons (sword, bow, great axe, fire staff, spear,
  hatchet) at the mastery level 20 with 19 nodes each and no ability on a bar (114 nodes:
  rows of a test, written into the database with no check of the tree rules), the war hammer
  at the mastery level 8, and the trade skills mining, cooking, and armoring; `Remote` has the level 35; `Remote` has a chest armor, leg armor,
  and a sword on it; `Gearone` is a character of the creation with the armor and the sword
  of the archetype Soldier. Make a new empty data directory or a new character only when the
  test is about an empty database or about the creation. A database backup is not necessary.
  `sqlite3 ~/Games/soulwarden/work/dev-data/soulwarden.sqlite3 "update characters set position_x=null,
  position_y=null, position_z=null where name='Soulwarden'"` (with no run alive) puts the
  character back at the default spawn position, a street with free space. Places near it
  (stage 2.3a): the street to the west is free for 25 m (a walk of 3.7 s with the camera to
  the west, `'look -900 0'`); a porch with a raised floor is at `8861.567 4243.415 139.542`
  (1.7 m above the street, beside a house wall), and its stairs are at `8863.082 4247.179
  138.752`. At the end of stage 2.3a the row of `Soulwarden` has no position (the default
  spawn).
- A stage tests only what it changes. The regression of the earlier systems runs at the end
  of each milestone (roadmap section 1.3).
- `tools/scenarios/play.sh` stops with status 1 when the account has no character, or when
  the client sends no login queue request in 30 s after the click on Play.
- `tools/scenarios/create.sh RUN_LABEL NAME` clicks the first empty row of the character
  list, skips the intro film, goes through the four steps of the creation, and waits for
  `InGame` and for the end of the loading screen. Status 0: the new character is in the
  world. Status 1: a step failed, and the message names the step (for example `the name NAME
  is not available`). `SOULWARDEN_CREATE_ARCHETYPE=1..7` selects an archetype button,
  `SOULWARDEN_CREATE_APPEARANCE='click 210 166;click 363 166'` gives harness commands for the
  appearance step, and `SOULWARDEN_SCENARIO_SHOTS=1` takes a screenshot of each step. The
  character stays in the database of the data directory. The script starts no game when
  `SOULWARDEN_DATA_DIR` is the data directory of the user (`SOULWARDEN_CREATE_LIVE=1` permits it). The
  name can have 6 digits at most: the client refuses a name with more and sends no request.
- `tools/scenarios/leave.sh` starts no game. It leaves the world with the Game Menu of the
  client (Escape, the gear, "Exit to Main Menu", "Yes"), waits for the line `rep: session
  ended` of the backend log, and then for the next character list request of the client
  (the main menu is back 12 s to 20 s after the leave: a click on Play before that has no
  effect). Status 1: the client did not leave the world in three tries, or the main menu did
  not come back. After it, a click on Play (`tools/hx 'click 1089 572'`) enters the world
  again: the backend log then has a second session (`[s2 ...]`) from client port 27001.
- `tools/gamedata/appearance.sh` writes `appearance-tables.json` to
  `~/.local/share/soulwarden/gamedata`. It reads the pak files of the game copy
  (`SOULWARDEN_GAME_DIR`, default `~/Games/soulwarden/game`) and needs only Python 3 (`SOULWARDEN_PYTHON`,
  default `python3`): no work file of the analysis is necessary. It stops with status 1 and
  one message, and writes no file, when the game copy or a database is not there. All data
  directories use this one file (`SOULWARDEN_GAMEDATA_DIR` selects a different folder). The
  backend log says at the start if the tables are loaded.
- `tools/research/creation_params.py decode FILE` prints the creation data of a capture file
  of the create request (names, list indexes, bytes of the appearance state). It reads the
  list indexes from the appearance tables of the backend.
- `tools/research/check_identity.py [DATA_DIR]` compares the Steam id and the persona id at
  each step of the login path of the last run (captures and database) and prints only the
  last 4 digits of each Steam id. A run can have the sessions of more than one account: each
  session has its own result, compared with the token of its own persona.
- `tools/gamedata/slice_ids.sh` writes `slice-ids.json` to the game data folder: the ids of
  the 186 faceted components of the player slice. With it the game server checks the route
  and the target of each client message (protocol notes 4.2), and the end of a session
  writes the counts to the log (`rep: client messages of the session: ...`). The pak entry
  of the slice has Oodle data: the tool uses `SOULWARDEN_OODLE_DLL` with the helper of
  `tools/research/oodle_loader.c`, or the file that `tools/research/pakx.py` extracted
  (`~/Games/soulwarden/work/assets/extract/slices/player.dynamicslice`). With no file the backend
  starts and makes no check of a target.
- `tools/world` sends a development command to the world of a development run (it finds the
  backend of the data directory and uses `nsenter`): `list`, `characters`, `spawn CHARACTER
  --at X Y Z`, `spawn CHARACTER --near OTHER [DX DY DZ]`, `remove CHARACTER`, `touch
  CHARACTER`, `fragment CHARACTER SLOT TYPE HEX`. `CHARACTER` is a character id or a name of
  the database. `spawn` puts a character into the world as an entity with no session: the
  session of a client near it sends its record, and the client shows a remote player with a
  body and a name tag. `fragment` sends one state fragment with your bytes for an entity: use
  it to test a new state type in a live client before you write its encoder (a slot from 0
  to 4095, a type from 0 to 65535, 16,384 state bytes at most). The backend has these
  commands only in a development run (`--dev-commands`).
- `tools/world` also has the commands for the items and the coin of a character (routes
  `/v1/dev/items`): `items CHARACTER`, `give CHARACTER ITEM [QUANTITY]` (`ITEM` is the
  `ItemID` text of the item catalog, for example `1hSwordT1`, `OreT1`, `PotionHealthT1`),
  `take CHARACTER SLOT [QUANTITY]`, `coin CHARACTER AMOUNT` (for example `123.45`), `move
  CHARACTER FROM TO` (an empty slot gets the item, a stack of the same item takes what it
  can hold, a different item changes places), `split CHARACTER SLOT QUANTITY [TO]`, and `lock
  CHARACTER SLOT on|off`. The character can be in the world or not: a command for a character
  that is not in the world changes its rows, and its next session gets the result. The bag
  window of the client (Tab key, `tools/hx 'key 0x09'`) shows the result at once.
- `tools/world` has the commands for the equipment of a character (routes
  `/v1/dev/items/equip` and `/v1/dev/items/unequip`): `equipment CHARACTER`, `equip
  CHARACTER BAGSLOT [SLOT]` (with no slot: the slot of the item class, and for a weapon the
  first free one of the weapon slots 24 and 25; an item that is in the slot goes into the
  bag slot), and `unequip CHARACTER SLOT [BAGSLOT]`. `tools/world --help` lists the slot
  numbers of the paperdoll (1 chest, 3 legs, 24 weapon 1). `tools/world items` also prints
  the equipment. The fastest way to a character with gear: `give NAME LightChestT1`, `give
  NAME LightLegsT1`, `give NAME 1hSwordT1`, then `equip NAME BAGSLOT` for each one.
- `tools/world` has the commands for the level, the attributes, and the skills of a character
  (routes `/v1/dev/progress`): `progress CHARACTER` (the list), `level CHARACTER LEVEL` (the
  level of the screen, 1 to 60; the experience becomes the start of the level), `xp CHARACTER
  AMOUNT`, `attribute CHARACTER NAME VALUE` (`constitution`, `focus`, `strength`,
  `dexterity`, `intelligence`; the character must have the points), `mastery CHARACTER WEAPON
  LEVEL [EXPERIENCE]` (level 1 to 20; `WEAPON` is `sword`, `bow`, `greataxe`, `firemagic`,
  ..., or the id of the game table, `SwordAbilityTable`), `tradeskill CHARACTER SKILL LEVEL
  [EXPERIENCE]` (`Mining`, `Cooking`, ...; 0 to 200), and `skillxp CHARACTER NAME AMOUNT`.
  The character can be in the world or not. The client shows a change at once: the level
  notice, the character window (K key, `tools/hx 'key 0x4B'`), the HUD.
- `tools/gamedata/progression.sh` writes `progression-tables.json` to the game data folder,
  from the same sources as the item catalog tool (the Oodle helper, or the extracted tables).
  The backend log says at the start if the tables are loaded (70 levels, 36 weapon masteries
  and trade skills, 611 nodes). The largest level of a player is the value of the expansion
  row `None` of the game (60), and the largest trade skill level is 200: the client has no
  expansion with this backend.
- `tools/research/ability_tables.py` reads the ability table list from the memory of the live
  client (`tools/hx`) and compares it with the constant of the backend. Status 0: the lists
  are equal. Run it after a change of the game copy: a wrong sequence puts the abilities of a
  weapon on the bar of a different weapon with no log line.
- `tools/gamedata/items.sh` writes `item-catalog.json` to the game data folder: each item of
  the 43 item tables and the 2 house item tables with its index on the wire (46,631 items;
  the list of the client has 46,632 entries with entry 0). The pak entries of the tables have
  Oodle data: the tool uses `SOULWARDEN_OODLE_DLL` with the helper of `tools/research/oodle_loader.c`,
  or the files that `tools/research/pakx.py` extracted (`SOULWARDEN_DATATABLES_DIR`, default
  `~/Games/soulwarden/work/assets/extract/sharedassets/springboardentitites/datatables`).
  `tools/gamedata/items.sh --find ITEM...` prints the index and the values of an item id.
  With no file the backend starts, the log says so, and `give` is refused. The tool compares
  the extracted tables with the table list of the pak files (names and sizes) and writes no
  file when they are not equal: one table that is not there makes each later index wrong.
  The catalog also has the items of each backstory of the game (member `backstories`, from
  the table `javelindata_backstorydata.datasheet`): the items of a new character, and the
  list of the backstories that the character creation can send (member
  `creationBackstories`). A catalog file from before stage 1.5 has no such list, and a new
  character then gets no item: run the tool again.
- `tools/gamedata/combat.sh` writes `combat-tables.json` to the game data folder (14,073 items
  with a weapon, 587 weapons, 414 damage rows, the defense values of 8,779 vitals rows). It
  reads the data tables and the base attributes file of the game copy from the sources of
  the creature data tool: the extracted files of the work folder are sufficient for it (6 s),
  and `SOULWARDEN_OODLE_LIB` reads the pak files. `tools/gamedata/combat.sh --find TEXT...` prints
  the weapons and the creatures with a text (`1hswordt1`, `wolf_grey_7`). The backend log
  says at the start if the tables are loaded. With no file the backend starts, and each hit
  claim is refused (`no combat tables`).
- `tools/gamedata/creatures.sh` writes `creatures.json` to the game data folder: the spawn
  points of the whole world (32,840 spawn points, 3,848 kinds, 503 slices, 25 regions; a file
  of 8.8 MB; 60 s with `SOULWARDEN_OODLE_LIB`). `--radius METERS` (with `--center X Y`) writes a
  smaller file, `--report FILE` writes one line for each entry that is left out, and
  `--all-slices` also writes the slices that the client does not show (a file for a
  picture tour, not for the game data folder). The tool reads the two slice lists of
  `tools/gamedata/creature_slices.json`, and it stops when that file is not correct or when
  a slice of the list `spawnAction` has no action `Spawn` with a fragment.
- `tools/gamedata/places.sh` writes `travel-places.json` to the game data folder (161 places
  in 16 territories: 133 spirit shrines and 28 inns; 15 s). It needs an Oodle library
  (`SOULWARDEN_OODLE_LIB`, or `SOULWARDEN_OODLE_DLL`): the extracted files of the work folder do
  not have the region files. `--list` prints the places and writes no file. The id of a
  place is its kind and the id of its point of interest (`shrine_40170`, `inn_40300`). The
  tool writes no file when a spirit shrine or an inn has a position with a wrong form (no
  position, not 12 bytes, a number that is not finite): `tools/gamedata/check_places_tool.py`
  is the check of this rule, with made-up region files.
- `tools/world places [TEXT]` prints the travel places, and `tools/world travel CHARACTER
  PLACE` or `tools/world travel CHARACTER X Y Z` moves a character (routes
  `/v1/dev/world/places` and `/v1/dev/world/travel`; `--screen on|off|auto` for the loading
  screen of the client: `auto` shows it for more than 100 m). `tools/world creature action
  CREATURE NAME [SECONDS]` starts an action of the action table of the slice of a creature
  (a test of an action in the live client; with no time the action has no end).
  `tools/world list` prints the line `load:` with the time of the ticks and of the scans.
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
  creatures` prints a mark for a creature that fights (`FIGHT`), that goes home (`GOES
  HOME`), that has no attack (`no attack`), and that does not leave its place (`stays at
  its place`).
- `tools/gamedata/heights.sh` writes the terrain heights to the folder `heights` of the game
  data folder: one file for each of the 49 regions (136 MB, 2 minutes with 8 processes;
  the pak entries of the height maps are stored data: no Oodle source). `--region NAME`
  writes some regions, `--jobs N` sets the number of processes, and `--height X Y` prints
  the terrain height of a position from the files and writes nothing. The creature data
  tool reads the height maps itself: it does not need these files.
- `tools/gamedata/creatures.sh --no-attack FILE` writes one line for each kind that does not
  attack, with the cause (859 kinds of the supported build).
- `tools/research/creature_tour.py` is the picture check of the creature slices: `prepare
  GAMEDATA TOUR` makes a game data folder with one kind for each slice, `run TOUR OUT`
  makes one creature of each slice in front of the character of a live run and takes one
  picture (`--list FILE` for some slices, `--wait SECONDS` for the time before the
  picture), `check OUT` removes a picture that is not a picture of the game, and `sheets
  OUT` puts 25 pictures on one sheet. A person must look at each sheet.
- `tools/research/script_errors.py` prints the errors of the user interface scripts of the
  client (run the game with `SOULWARDEN_TRACE=4A65DA0:512`; the game log has no line for such an
  error). Use it when a screen of the client is not complete after a new state.
- `tools/research/item_index.py` reads the item index list from the memory of the live client
  (`tools/hx`) and compares each entry with the catalog. Status 0: the lists are equal. Run
  it after a change of the game copy or of the catalog tool: a wrong index gives a wrong
  item or an empty slot with no log line.
- `SOULWARDEN_OBSERVE_PLAYERS=1` (backend option `--observe-players`) makes each session observe
  the entities of other characters near its character (100 m to enter, 120 m to leave). It
  is off for a start of the user and on for a development run (`SOULWARDEN_OBSERVE_PLAYERS=0`
  turns it off there).
- `SOULWARDEN_TRACE="RVA[:bytes][s],..."` logs the arguments of a function at entry (`s` adds the
  return addresses on the stack; the first 400 calls of each point). `SOULWARDEN_PROBE="RVA[:bytes],..."`
  logs all registers at any instruction start (the first 3 hits, then one of each 120).
  Output: `SOULWARDEN_DATA_DIR/client/logs/hook.log` (a development run:
  `~/Games/soulwarden/work/dev-data/client/logs/hook.log`).
- `tools/hx 'read ADDRESS LENGTH'` reads client memory (512 bytes at most for one read) and
  `'write ADDRESS HEX'` writes it.
  `tools/hx 'key VK [MS]'`, `'keydown VK'`, `'keyup VK'`, `'click X Y [MS]'`, `'text TEXT'`
  send input to the game window (`MS` is the time that the key or the button stays down).
  `'look DX DY'` moves the pointer by a relative distance: this turns the camera in the
  world (900 units are 90 degrees, with an error of 3 degrees or less; a positive `DX` turns
  to the right; the harness sends 30 units each 20 ms, thus 900 units need 0.6 s). `'wheel
  N'` turns the pointer wheel: a positive number moves the camera nearer to the character.
  `'drag X1 Y1 X2 Y2 [MS]'` presses the left button at the first position, moves the pointer
  to the second position in `MS` milliseconds (default 600), and releases the button: a
  drag of a user interface element, for example an item of the bag. `'rclick X Y'` is a
  click with the right button. `'mousedown left|right'` presses a pointer button and keeps
  it down, and `'mouseup left|right [DX DY]'` releases it (with `DX DY`: the release and a
  relative pointer movement in one input event). `'toolwindow stop'` stops the tool window
  for the rest of the start (a test of the stop after a failure of the renderer).
  `'state'` shows the window and the foreground state. One `tools/hx` call can have many
  commands; they run in sequence.
- A position in a screenshot of `tools/shot` (1280x720) is the window position plus (12, 48).
- `tools/research/player_position.py` prints the position, the ground flag, the movement
  permissions, the action of layer 0, and the camera of the local player (needs
  `SOULWARDEN_TRACE=6769980`). `tools/world list` prints the position that the backend has for
  each entity, with 3 decimals: with the position report on, the two values of a character
  at rest are equal in `x` and `y`, and the height of the backend is 0 m to 0.017 m lower.
- `tools/research/input_packet.py DATA_DIR/captures --summary` counts the input reports of a
  run: how many have a camera position, the position flag (flag bit 15), and the position of
  the character (`posf0`), each flags value, and the items of the three lists. `--changes`
  prints the newest entry of each report that is different from the one before. `--contacts`
  prints each entry with a list item one time (the melee contacts of a run: the target, the
  facet, the point), with its action bits. The tool reads each entry to its last byte: a
  report that does not decode is in the output as `NOT DECODED`.
- Game log of a development run:
  `~/Games/soulwarden/proton-dev/1063730/pfx/drive_c/users/steamuser/AppData/Local/AGS/New World/Game.log`
  (`game_log` of `tools/scenarios/common.sh`). Game log of a start with no development
  variable: the same path below `~/Games/soulwarden/proton/1063730`.
  Backend log: `SOULWARDEN_DATA_DIR/logs/backend.log` (a development run:
  `~/Games/soulwarden/work/dev-data/logs/backend.log`; a start of the user:
  `~/.local/share/soulwarden/logs/backend.log`).
- Each log line of the game server (`rep:`, `carrier:`) ends with the mark of its session:
  ` [s1]`, and ` [s1 4aae16c4]` from the registration on (session id, first 8 characters of
  the character id). The lines `dtls:` name the client port, and the line `rep: secure
  session open, client port N [sK]` connects a port with a session.
- The captures of the game server are in one folder for each session:
  `SOULWARDEN_DATA_DIR/captures/rep/s0001`, `s0002`, ... Each folder has its own file counter and
  its own limit (32,768 files). `tools/research/client_msgs.py`, `input_packet.py`, and
  `check_identity.py` take `SOULWARDEN_DATA_DIR/captures` and read all sessions in sequence
  (`--session N` for one). `tools/research/client_messages.py` takes `captures/rep` or the
  folder of one session.
- The products in `apps/*/dist` are also what the Steam library entry of the user starts. A
  hook DLL that stops the game stops the game of the user too. Build a hook that is not
  tested to a path outside `dist` (`SOULWARDEN_GAME_HOOK_OUTPUT`), start it with `SOULWARDEN_HOOK_DLL`,
  and build into `dist` only after a live run. The backend of the build folder
  (`~/Games/soulwarden/work/build/native-backend/apps/backend/soulwarden-backend`, made by
  `cmake --build ... --target soulwarden_backend` and by `tools/build-soulwarden-backend.sh`)
  starts with `SOULWARDEN_BACKEND_EXE`. `tools/test-soulwarden-backend.sh` builds only the
  test program: after a change, build the backend target before a live run, or the run has
  the old backend (stage 3.4d lost one live run this way; compare the time of the file).
- `tools/dev-run.sh` gives the launcher the variables of the tool window by name:
  `SOULWARDEN_TOOL_WINDOW` (`0`: off), `SOULWARDEN_TOOL_WINDOW_KEY`, and `SOULWARDEN_TOOL_WINDOW_FAULT`
  (`hook`, `device`, or `swapchain`: a test of a start in which the tool window cannot
  start; the hook reads it only with the harness gate). The launcher makes the token of the
  admin interface itself. The same for the position report of the hook: `SOULWARDEN_EXACT_POSITION`
  (`0`: the hook does not change the client code, and the backend uses the camera estimate)
  and `SOULWARDEN_EXACT_POSITION_FAULT=code` (a test of a start with client code that is not the
  code of the supported build: the hook compares with a wrong byte; only with the harness
  gate). The same for the melee contact report: `SOULWARDEN_MELEE_REPORT` (`0`: the backend gets
  the option `--no-melee-report`, the client gets no remote configuration document, and a
  melee attack has no effect).
- More variables of `tools/dev-run.sh`: `SOULWARDEN_BACKEND_EXE` and `SOULWARDEN_HOOK_DLL` start a
  different build of the backend or of the hook. `SOULWARDEN_CREATURE_BEHAVIOR=0` turns the
  behavior of the creatures off for the run (the backend option `--no-creature-behavior`):
  the creatures stand at their spawn points. `SOULWARDEN_RESEARCH_SESSION` is the research
  switch of the game server for live checks (`registration-result=N`, `reject=N`, `kick=N`,
  `kick-delay=SECONDS`; protocol notes 3.6; and `refuse-equipment=1`, which makes the backend
  refuse each equipment request with the message "equip item fail": the client sends only
  requests that its own checks permit, thus this is the only way to see that message in a
  live client). It is off by default, and the launcher gives it to the backend only with
  `SOULWARDEN_HARNESS=1`. Do not use it for a run that is not a check.

## Known traps

- Fights (stage 3.4b):
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
  - With no navigation data (stage 3.4b): a creature whose spawn point is more than 1.0 m
    from the terrain height does not leave its place, and a creature goes on the straight
    line, through a wall, on the terrain. With the navigation data (stage 3.4d) the Ancient
    Guardian Reaver at `8572.83 4241.20 166.60` and the Risen of the hut walk on their
    floors. `stays at its place` of `tools/world creatures` shows only when the world has
    the tile of the creature (a creature far from each character has no answer yet).
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
  - The death screen: the text "YOU DIED!" with a count of 5 s, then the respawn screen
    with the button "Respawn" at `1000 581` (window of 1256x660). The client shows its
    loading screen for 5 s to 8 s after the click. `tools/world heal NAME` makes a dead
    character live again at its place with no loading screen.
  - A character is never dead after a login: the health and the death are not in the
    database. A dead character that leaves the world is at its respawn place (the nearest
    travel place) at its next world entry.
  - The health bar of the HUD is grey at the full health and red below it.
  - A travel place can have a creature that fights near it (a Highlands Goat of level 62
    is 8 m from the Shrine of Pillars): a character that travels there or that respawns
    there takes damage. Turn the ignore value on for a test of a travel.
  - `tools/gamedata/heights.sh` and `tools/gamedata/creatures.sh` write into the common game
    data folder: the next start of the user then has creatures that fight. For a test of a
    different file, give the tool an output path and give the run its folder with
    `SOULWARDEN_GAMEDATA_DIR` (the folder needs the other game data files and the folder
    `heights` too).
  - Case FT9 has a time limit of 1 s for 1,000 ticks with 50 fights (3.6 ms in one run).

- Path search and playable fights (stage 3.4d):
  - The 10 s of shelter: after a world entry, a travel, a respawn, and a revive, no creature
    takes the character for 10 s after the first position report from the place. A test of
    a fight waits for that time, or the character hits the creature first. A travel ends
    each fight of the character: use a walk (`'keydown 0x57'`) to lead a creature to a
    different place.
  - No creature goes into the start settlement: the gates have navigation marks of the
    level designers. A wolf outside a gate stands and looks at a character inside. A fight
    inside the settlement needs a creature of a command inside it.
  - `tools/world path` reads the tiles of the two places in the world thread: the line
    `load:` then has a largest tick of 30 ms to 200 ms. Measure the load in a run with no
    path command.
  - A creature that waits for a different creature does not move: a queue of creatures on
    stairs is the crowd rule, not a fault of the path. Only the creatures in the reach
    attack.
  - The melee contact of the client does not go through a wall: a player cannot hit a
    creature on the other side of the yard wall of the start settlement (7 tries, 0
    contacts).
  - A tile is read 4 ms to 16 ms after it is asked: a creature at the edge of the tiles of a
    character can wait some ticks. The tool window and `tools/world creatures` show the
    creatures of the whole world: a creature far from each character has no data.
  - `tools/gamedata/navigation.sh` writes into the common game data folder by default (1.2
    GB): give it an output folder for a test, and give the run that folder with
    `SOULWARDEN_GAMEDATA_DIR` (with links to the other game data files and a real folder
    `heights`: the tool writes the heights into the folder beside its output). A link to the
    common folder `heights` makes the tool write into the common folder.
  - The test program builds the navigation files of a case in its work folder
    (`TestNavRegion`: a region of the real size, flat ground where a case gives no floor).

- User test 1 (the corrections after stage 3.4d):
  - Still pictures do not show if a creature walks or slides: a pose of a run in some frames
    looked like a walk in stage 3.4b, and the user saw the slide. Record a video of the agent
    desktop (`wf-recorder -o AGENT-0 -f FILE.mp4 -r 30`, with `timeout -s INT SECONDS`; it
    reads the output of the agent desktop and sends no input) and extract frames with
    `ffmpeg` at 10 to 30 frames each second, near the creature.
  - The client keeps the scope fields of a creature: a state with no scope fields after an
    action makes the client start that action again. A new state of a creature must keep
    the rule of `creature_scope` (protocol notes 4.14).
  - A conversation (the key E at a person, "Talk") locks the input of the character until
    the answer 6709. In a test: the camera of the conversation shows the person; E goes to
    the next text, and the conversation ends at the last one. A lore page ("E Read") opens
    the journal: two Escape keys close it (the first one goes back one step).
  - A creature with no way to a character goes home and does not look at it for 8 s: a
    stone 0.6 m higher than the floor around it (the Tomb of the Old Warrior) is enough.
    Keep a test of a walk on one floor.
  - After a travel the creatures of the new place come only after the first position report
    of the client from the place (20 s at most): `tools/world creatures` lists them at once,
    the interest table of the session later.
  - `tools/world message CHARACTER TYPE HEX` sends a message to the client of a character:
    the body starts with the facet target (the entity group id plus the id of the component
    in the root entity of the player slice) and the entity group id. It is the fastest check
    of a message of the server before its code (6709 and 3746 of protocol notes 4.20).
  - The blue bar below the crosshair is the mana meter: it shows when the client has no mana
    state, and it goes away at 100 percent.

- Creatures (stage 3.4a):
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
  - A click with the left pointer button in the world is an attack: each attack moves the
    character 1 m to 2 m forward (from stage 3.2a an attack hurts a creature: the block
    "Hits").
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

- Hits (stage 3.2a):
  - A test of an attack: `tools/world ignore Soulwarden on` (if not, the wolf attacks the
    character, and it moves to the character), `tools/hx 'look -900 0'`, `tools/world
    creature spawn Wolf_Grey_7 Soulwarden -2.2 0 0 --yaw 270`, then `tools/hx 'click 628 330'` (a light attack) or
    `'mousedown left' 'wait 600' 'mouseup left'` (a heavy attack). The backend log has one
    line for each hit (`world: hit: NAME hurts creature ID ... N damage of the type T ...,
    health A to B`), one line for the first refused claim of each cause (`rep: hit claim
    refused (CAUSE)`), and the counts at the end of the session (`rep: hit claims of the
    session`).
  - Each attack moves the character 0.7 m to 2 m to the creature. A creature has no body that
    stops the character: after some attacks the character is 1 m from the creature, and some
    attacks then give no contact (the thrust of a heavy sword attack at 1.0 m, each spear
    attack at 1.7 m or less). Spawn a new creature at the distance of the test.
  - The distance at which an attack gives a contact depends on the weapon: a light sword
    attack 1.8 m (not 2.4 m), a great axe 2.3 m, a light spear attack 2.0 m to 3.4 m, a heavy
    spear attack only near 3.9 m (2 of 10 tries). No contact is not a fault of the backend:
    look at `tools/research/input_packet.py DATA_DIR/captures --contacts`.
  - The damage number shows for about 1 s. Take the screenshots in a loop directly after
    the input (6 calls of `tools/shot` need 1 s). The number is the whole part of the damage
    (75.7 shows as 75).
  - The kind of an attack comes from the time of the attack input: a click (the button down
    for less than 0.27 s) is a light attack, and a hold is a heavy attack. `'click X Y'` of
    the harness is a light attack. For a heavy attack keep the button down for 0.4 s or
    more (`'mousedown left' 'wait 600' 'mouseup left'`).
  - While the button stays down, the client attacks again and again, and it attacks one more
    time after the button went up. Each contact is one hit.
  - The mode of the client comes from a document that the client reads before the main menu
    (and again in the main menu). A backend that starts after the client asked gives no
    document for that start. Read the two values with the method of protocol notes 2.3 when
    an attack has no effect: the game log has the line `Retrieved layer's
    "remoteCfg.publicGameplay.ProductId" remote config ... successfully, size: 50` when the
    client got the document.
  - `tools/gamedata/combat.sh` writes into the common game data folder: the next start of the
    user then has the damage. For a test of a different file, give the tool an output path
    and give the run its folder with `SOULWARDEN_GAMEDATA_DIR` (the folder needs the other game
    data files too).
  - A weapon key (1, 2) selects the weapon slot for the damage of the backend. A world entry
    starts with the first weapon slot again, in the client and in the backend.
  - A dead rabbit shows the text "No equipped Knife" at its name: the skinning interaction of
    the client, which the backend does not have.
  - The capture folder of a session is full after 6 minutes in the world (32,768 files): do
    the attacks of a test early, or leave the world and enter it again.
  - A test host with a creature data set whose kind or spawn point has a level with no row
    gives no creature data (`CreatureData::set` returns false): the loader refuses data that
    do not fit together.

- The position report and the tests of the position (stage 2.3a):
  - The key of the dodge is the left Shift key (`tools/hx 'key 0xA0 80'`): 4 m to the rear
    with no direction key. The right pointer button (`'mousedown right'`) is the block: the
    action sequence of layer 0 is then 1248 (`tools/research/player_position.py`).
  - `tools/scenarios/leave.sh` can move the character. A click of its menu sequence that
    comes while the menu bar is not open is an attack with the equipped weapon (a step
    forward of 1 m to 2 m for each click). For a compare of the saved position with the
    client, use the last line `rep: input ... position X Y Z` of the session in the backend
    log, not a memory read before the script. After a failed try the script left a test
    screen of the client open one time ("Token Name", "Button") with the character in the
    world: stop the run then.
  - The row of a character can be written with `sqlite3` while the backend runs, when the
    character is not in the world: the world reads the row at each world entry. A cycle of
    `leave.sh` and a click on Play (`tools/hx 'click 1089 572'`) needs 50 s.
  - The height of a character at rest depends on how it came there. After a fall of 0.5 m
    (each world entry) it is 0.016 m to 0.039 m lower than after a walk. A spawn 0.1 m above
    the ground does not make the character fall at some places: it stays above the ground
    until it moves. Do not make the spawn lift (`world::kSpawnLift`, 0.5 m) smaller without
    the test of protocol notes 4.6.
  - The input number of the client is at movement facet +0x1622 (`tools/hx 'write ADDRESS
    f0ff'` writes 0xFFF0; the facet address is the first line of
    `tools/research/player_position.py`). The id of the last server state is at facet +0x430
    +8, and the id of the predicted state at facet +0xAE0 +8 (2 bytes each). After a write
    to a number that is not newer, the backend does not use the position of the reports for
    2 s (log line `rep: input N is not newer than the input before it`), then it takes the
    new sequence.
  - A heartbeat state with no id fields (the state before stage 2.3a) makes the client move
    its character to the spawn position of the session when its input number is 0 again
    (36.4 minutes, or a write of the number): a jump of the full distance in one tick. The
    backend log then has `rep: position report refused (too fast)`.
  - One run with `SOULWARDEN_PROBE=2A2480D` (together with the trace points `6769980` and
    `3587E40`) ended 30 s after its start with a crash file of the client, before the main
    menu. The same run with no probe (trace points `6769980`, `486E170`, `3587E40`) was
    correct. The cause is not known: do not use a probe in the code of the input entry
    (`2A236C0` to `2A2493C`) without a check of that start.
  - In a test, do not compare a height of a constant with a height of the backend with `==`:
    the compiler computes a constant with a fused multiply and add, which gives a value one
    float step from the value at run time (`same_height` in `tests/position_tests.cpp`).
  - A fall from a spawn high above the ground also moves the character in the horizontal
    plane (6 m in a fall of 150 m at the default spawn: it lands on the wall beside the
    street).

- World size and travel (stage 3.4c):
  - `tools/world travel NAME PLACE` moves a character that is in the world at once. A travel
    of more than 100 m shows the loading screen of the client, and the first position
    report from the new place comes 3 s to 5 s later: wait for a line `rep: input ...
    position X Y Z` with the new place in the backend log before a screenshot.
  - The character and the camera look north after each travel (yaw 0). A `'look DX 0'`
    after it turns from north: `'look 1300 0'` looks to the south-east.
  - A picture of a creature with its name plate: travel with numbers to 3.5 m south of its
    position (`tools/world creatures NAME 150` prints the positions), with the height of
    the creature plus 0.3 m. The creature is then in front of the character. The place of a
    shrine can have a wall or the shrine between the character and a creature.
  - The height of a travel with numbers must be the height of the ground or more. The
    spawn lift is 0.5 m: a height 0.4 m below the ground worked, and a lower one was not
    tested. The backend has no terrain data.
  - A creature of a slice with a spawn action shows a light for 1 s to 4 s after its record
    (two slices: 5 s to 15 s), then its body. A screenshot directly after a travel can show
    the light.
  - `tools/world` and each tool find the backend by the program name `soulwarden-backend`. A
    copy of the backend with a different file name (`backend-test`) is not found: put a copy
    into its own folder with the name `soulwarden-backend`.
  - `tools/shot NAME` writes `DATA_DIR/shots/NAME.png`. A name with a folder gives no file
    and no error text: copy the file to the evidence folder after the call.
  - A person can show the agent desktop on a monitor. `tools/shot` then gives a picture with
    the size of that monitor (3440x1440, not 1280x720), or a picture of the empty desktop.
    `tools/research/creature_tour.py` checks the size and the content of each picture and
    waits; its command `check` removes a picture that is not correct.
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
  - The client shows the interaction text "Fast Travel" at a shrine. The backend has no
    answer for the fast travel of the game, and the key E at a shrine was not tested.
  - `tools/research/player_position.py` prints nothing in a run of `tools/scenarios/play.sh`
    with no `SOULWARDEN_TRACE=6769980`.
  - The measured values of case WS2 (the times of the scans) change from run to run by 30
    percent. The limit of the case is a ratio (one tenth of a scan of all points), and
    400 ms.
  - Case HT11 runs `tools/gamedata/check_combat_tool.py` with `python3` (`SOULWARDEN_PYTHON`
    selects a different one): with no Python 3 the case fails. The check makes its tables in
    memory: no file of the game. The same for case TV9 and
    `tools/gamedata/check_places_tool.py`.
- The tool window (F8), positions in the window of 1256x660 at its first place (the window is
  at the left side; a user can move it, and it has no saved place): tabs "Items" `50 113`,
  "Character" `125 113`, "Skills" `204 113`, "Creatures" `283 113`, "Travel" `362 113`.
  Travel tab: filter field `203 190`, first row of the list `76 239` (each next row is 21
  pixels lower), the button "Travel" of the selected place directly after the text
  "Selected: NAME, TERRITORY" at y `461` (x `358` for "Shadowmine Shrine, Everfall": take a
  screenshot), the button "Travel" of the position form `363 517`, "Here" `425 517`. Items
  tab: search field `203 169`, first row of the result `138 218`, "+" of the quantity `203 382`, "Give" `246 382`, "Remove" of the
  first bag row `514 462` (each next row is 29 pixels lower). Character tab: "+" of the level
  `469 166`, "Set" of the level `512 166`, experience field `388 195`, "Give" `516 195`,
  the button "Heal" (or "Revive") `516 253`, the mark "Creatures ignore this character"
  `35 285`, dexterity field `388 394` and its "Set" `512 394` (each row below the mark is
  21 pixels lower in a start with the behavior of the creatures off: the tab then has one
  more line of text). A click on a number field selects its
  text: `'text 25'` after the click replaces the number. The layout can change: take a
  screenshot before you use a position.
- The tool window takes each key while it shows: `tools/hx 'key 0x09'` (Tab) then moves the
  focus in the tool window and does not open the bag of the client. Hide the tool window
  before a key for the game. With the tool window open, `'text ...'` goes into the field that
  has the focus.
- The harness makes the game window the foreground window with a click at the middle of the
  window when the state is lost. The first place of the tool window leaves the middle free:
  a tool window that a test moved to the middle gets that click.
- `tools/hx 'toolwindow 1'` gives `not possible` before the first present call of the client
  made the renderer of the tool window, and for the rest of a start in which the tool window
  is off (`tool window: OFF ...` in the hook log).
- A screenshot of `tools/shot` shows the tool window (it is drawn into the picture of the
  game) and the pointer that Dear ImGui draws in the world. It does not show the pointer of
  the system, which the game shows on its screens (the inventory).
- The first Escape after the tool window closed can have no effect on a window of the client
  (the same as the first Escape of a world session): look at a screenshot before the next
  step.
- A datagram of the backend above 1,038 bytes makes the client close its connection 30 ms
  after it, with `Unexpected client disconnect! {"transportError":2}` in the game log and
  `carrier: the client closed the connection, reason 2` in the backend log, and the client
  then tries the world entry again some times. In stage 1.7 this came from two messages in
  one datagram, not from a wrong state byte: compare the sizes of the lines `rep: out N (SIZE
  bytes)` with a run that works before you look for a fault in a state.
- The node list of the point state (slot 67) of a fragment replaces the list of the client.
  Do not send a fragment with a part of the nodes: the client then shows only that part.

- `pkill -f 'NewWorld.exe'` also matches the shell that runs it. Use a bracket pattern
  (`NewWorld[.]exe`) or `tools/dev-stop.sh`.
- An error in a user interface script of the client gives no line in the game log, and the
  screen is then not complete with no other sign. Example of stage 1.3: with no level of the
  player (progression state, slot 70), each item cell of the bag was a wide white cell with
  no tooltip and no action menu, and the inventory screen had no equipment panel. The cause
  was one compare of a value that the client did not have. When a new state shows on the
  screen but a part of the screen is not correct, run with `SOULWARDEN_TRACE=4A65DA0:512` and
  read `tools/research/script_errors.py`: the call stack names the script function. A state
  that is not there yet can be necessary for the screen of a different system.
- The character window (K key, `tools/hx 'key 0x4B'`; Escape closes it), positions in the
  window of 1256x660. Tabs: "Attributes" `253 104`, "Weapon Mastery" `409 104`, "Trade
  Skills" `571 104`. Attributes: the "+" buttons at x `386` (strength `176`, dexterity `245`,
  intelligence `314`, focus `383`, constitution `452`), "Assign" `733 524`, "Respec" `532
  524`, "Yes" of its popup `714 412`. Weapon mastery: the sword row `238 214` opens its tree;
  in the tree, "Assign" `765 559`, "Respec" `765 598`, "Yes" of its popup `714 397`; the
  first node of the first row of the second tree is `826 195`. The positions of the nodes
  depend on the weapon: take a screenshot before you use a position.
- A level, a mastery level, or a trade skill level that changes shows a notice at the top of
  the screen for some seconds (the level, or a list "Unlocked"). It covers a part of an open
  window: wait 6 s before a screenshot of the window.
- The text "N XP to Level M" of the open character window is one state behind after an
  experience gain: the client writes it when the window opens. Close the window and open it
  again before you read the text. The bar is correct at once.
- The button "Respec" of the attributes is on only after an attribute state of the server
  with spent points. A level above 60 shows "Rise of the Angry Earth needed to advance", and
  the riding skill shows 0 for each level: the client has no expansion with this backend.
- The capture folder of a session is full after 32,768 files, which is some minutes in the
  world. A request of the client after that is not in the captures: read the line of the
  request in the backend log (each request of stage 1.5 and 1.6 has one, with its fields).
- A node that a character lost must go out one time with the level 0, and a points fragment
  with no node makes the client give all points of each pool back itself. Change the nodes of
  a character only through the skill system.
- In a test, a loop over a member of a temporary (`for (x : test_progression().nodes)`) reads
  memory that is free: keep the object in a variable.
- The bag window (Tab, `tools/hx 'key 0x09'`; the same key closes it): positions in the
  window of 1256x660 with one item in each section: first cell of the first section `662
  193`, first cell of the section to its right `837 193`, first cell of the second row of
  sections `662 296`. A pointer move to a cell shows its tooltip after 1 s (move in two or
  three steps: `'move 650 185' 'wait 200' 'move 662 193'`). A click on a cell opens the
  action menu (entry "Lock" at `760 336` for the first cell). A drag of a cell to `1103 325`
  (the area "Drop items", which shows only in a drag) drops the full stack: `'drag 837 193
  1103 325 1200'`. A drag to the character at the left is an equip try. The sections and
  their sequence depend on the items: take a screenshot before you use a position.
- The bag of an entity keeps its empty slots until the character leaves the world (`tools/world
  items` then prints, for example, "3 items in 28 slots"). A new item gets the first empty
  slot. The client sorts the items by their class: the slot number does not show.
- The equipment panel of the inventory screen (Tab), positions in the window of 1256x660:
  the armor cells at x `269` (head `152`, chest `195`, hands `237`, legs `279`, feet `320`),
  the weapon cells at x `586` (weapon 1 `152`, weapon 2 `194`), the amulet cell `586 290`,
  the first quick slot `264 424`. A click on a cell of the bag opens its action menu: the
  first entry ("Equip") is 126 pixels to the right of the cell and 36 pixels below it, and
  each next entry is 36 pixels lower (a weapon: Equip, Equip to Slot 1, Equip to Slot 2,
  Lock; armor: Equip, Lock, Drop; `'click 788 229'` is "Equip" for the cell `662 193`). A
  click on an equipped cell opens a menu with "Unequip" as its first entry (`'click 396 313'`
  for the legs cell `269 279`). A drag of weapon cell 1 to weapon cell 2 changes the two
  slots: `'drag 586 152 586 194 1500'`.
- The inventory screen shows the character from the front. The picture depends on the place
  of the character and on the direction of the camera at the time of the Tab key: an object
  near the character (a wall, a cart) can hide it. The spawn position with no camera turn
  gives a clear picture.
- At the equip of a weapon in a session, the client turns its own character to the direction
  of the camera and shows it in the stance of the weapon, with the weapon in the hand. The
  weapon is on the hip after the next world entry, on the inventory screen, and for an
  observer. For a front view of the character in the world, take the screenshot before the
  equip of a weapon, or after a new world entry.
- The client sends an equipment request only for an action that its own checks permit. To
  see the failure reply (type 3626) in a live client, start the run with
  `SOULWARDEN_RESEARCH_SESSION=refuse-equipment=1`: the handler of the client is the trace point
  `687BB60`.
- A paperdoll fragment has only the slots that changed: a slot with no entry keeps its item
  in the client. A fragment of `tools/world fragment NAME 62 3183` whose three lists do not
  have the same length has no effect and gives no log line.
- The bag window does not show an ammunition item (`ArrowT1`): do not use one for a test of
  the bag. The equipment slots for ammunition (12, 13) take no item in the backend.
- A character from before stage 1.4, and a seeded character, have no items of an archetype:
  only the create route gives them, at the creation. Give such a character its gear with
  `tools/world give` and `tools/world equip`.
- One wrong byte in a state fragment makes the read of the bundle fail in the client (game
  log: `Failure during read of ReplicatedStateBundle`), and the other records of that bundle
  are lost. Make the bytes of `tools/world fragment` with a script, not by hand.
- `tools/world touch` for a character that a client plays sends each state again but not the
  metadata state: a second metadata state for the own player makes the client close its
  connection and connect again by itself (protocol notes 4.2).
- Do not change `SOULWARDEN_GAME_SIZE` for a development run when you can avoid it. The client
  saves the size in the development Proton prefix (`user_preload_settings.cfg`, `r_Width` and
  `r_Height`, and `m_windowWidth` and `m_windowHeight` of the settings file of the persona
  in `AppData/Roaming/AGS/New World`). The next run with the default size then has a
  stretched picture and different positions. Put the values back to 1256 and 660 with no run
  alive.
- With a user interface screen open, a `'look DX DY'` moves the pointer by 2 pixels for each
  unit. Use `'move X Y'` for a position on a screen.
- `tools/gamedata/items.sh` writes the catalog to the common game data folder: the next start
  of the user reads it too. The backend that the user starts then gives items only through
  a development run or a later tool window: a start of the user has no development commands.
- The processes of gamescope have the names `gamescope-wl` and `gamescopereaper`.
  `pgrep -x gamescope` finds no process in all conditions. After `tools/dev-stop.sh`, run
  `tools/dev-stop.sh --check`: it must print `no process of a development run`. Do not use
  `pgrep -f soulwarden-backend` or `pgrep -f 'Bin64.NewWorld[.]exe'` for this check: they also
  find a game that the user started, and that game must continue.
- The kernel keeps only the first 15 characters of a process name: the backend has the
  process name `soulwarden-back`. `pgrep -x soulwarden-backend` finds no process (it prints
  a warning, which a script with no error output does not show). A tool finds the backend by
  the program of its command line (`pgrep -f '^([^ ]*/)?soulwarden-backend( |$)'`, as
  `tools/dev-stop.sh` and `tools/world` do).
- Wine makes sockets in the wineserver process. A wineserver from an earlier run is in the
  earlier network namespace. The launcher stops all processes of the prefix before a start.
- Ghidra does not accept a project path with a folder that starts with a dot.
- The decompiler shows some field names as addresses, and some state types register fields
  inline. `tools/st` handles both; check its output against a live trace of the field readers
  when a state is not applied. The decompiler also omits arguments of some constructor
  calls: before 2026-10-13 the tool then gave a wrong group for a field, with no warning
  (27 of the 130 state types). The tool now reads the raw code of the constructor
  (`tools/state_disassembly.py`) and refuses a layout that it cannot prove (the types 41 and
  4896). The audit of all state types is `~/Games/soulwarden/work/research/state-layout-audit.md`.
- A successful build does not prove a feature. Restart the game and check the game log and a
  screenshot.
- The game window has no keyboard focus of the compositor on the agent desktop. Wine takes the
  foreground state away from it at random times, and the keys of the harness then have no
  effect (`tools/hx 'state'` shows `foreground=0`). `SetForegroundWindow` does not give the
  state back. A pointer button on the window does: the harness sends one before a key when
  the state is lost (the left button has live proof, the extra button that the harness tries
  first has not). Read `foreground` before and after an input test.
- The harness has one command file. Two `tools/hx` calls at the same time lose commands.
- `'move X Y'` and `'click X Y'` do not turn the camera. Use `'look DX DY'`.
- The camera does not turn by a constant angle for one large pointer movement: 900 units in
  one input event gave 69 to 120 degrees. Thus the harness sends a `look` in steps of 30
  units. The first `look` after the world entry can be 6 degrees short. Read the yaw with
  `tools/research/player_position.py` when the direction is important.
- The character walks into walls. A key hold of W that moves the character less than you
  expect does not prove a movement fault: look at a screenshot, turn the camera, and do the
  test again.
- A fragment for a slot with the state type of a different component gives no error and has
  no effect. Trace the apply function of the facet (virtual function +0xB0 of the object at
  component +0x80) to see that a new state arrives.
- A state field that the server does not send can have random bytes in the client (rotation
  and look direction of the movement state). A fault that occurs only in some runs can come
  from such a field: read the state object in the client memory.
- The main thread of the client can stay in an endless loop with the loading screen on.
  `gdb -p PID -batch -ex 'info registers rip'` gives the address (subtract 0x140000000).
- The game log, the HTTP captures, the game server captures, and the database of a run have
  the full Steam id and the persona name of the user. Do not copy text from them into the
  repository, a document, or a report. Mask a Steam id to its last 4 digits.
- The agent desktop starts the launcher with the environment of the compositor.
  `tools/dev-run.sh` gives the launcher only the variables that it names: add a new launcher
  variable to its list, or the variable has no effect in a development run.
- The first Steam id that logs in to a database from before the account support takes the
  one persona of that database and its characters. Do the first run on such a database with
  the Steam account of the user, not with an override id and not with `local`.
- Only a login with a correct local ticket takes that persona. A hook DLL from before the
  account support sends a ticket with no Steam id (backend log: `no local ticket`): the login
  then gets the fallback account, the menu shows no character, and the old persona stays
  free. Build the hook again (`tools/build-soulwarden-game-hook.sh`) and start the game again.
- The name of a character is unique for all accounts. A seed name that is in use gives the
  account no character (backend log: `no seed character`), and the login continues.
- A persona that has no settings file in the client gets five first-run screens before the
  main menu (protocol notes 2.1). `tools/scenarios/menu.sh` passes them and presses
  "I Accept" on the code of conduct screen of the client. The space key does not pass them.
- The backend is in the network namespace of the launcher: a request from the host does not
  get to it. To test a route with `curl`, start `apps/backend/dist/soulwarden-backend` alone with a
  copy of a database (`--data-dir`, `--http-port 32299`, `--rep-address 127.0.0.1:32298`,
  `--no-rep-listener`) and send the service host in the header `X-Soulwarden-Host`. `nsenter -t
  BACKEND_PID -U -n --preserve-credentials COMMAND` runs a command in the namespace of a live
  run (`tools/world` does this). A backend that you start directly needs `SOULWARDEN_HARNESS=1` in
  its environment for `--dev-commands` and `--research-session`: without it the backend
  prints a message and stops with status 2.
- Character creation: the intro film of the creation level stops only when Escape is down
  for 3 seconds (`tools/hx 'key 0x1B 3000'`). Do this one time: after the film, Escape goes
  one step back. The buttons of the archetype step come up to 25 seconds after the skip. No
  log line shows these two moments; the first sure sign is the name check request.
- Character creation: an option of the appearance step (body type, skin tone, hair) has a
  list that opens with a click on its row. A click at the position of a list entry does
  nothing when the list is not open. The body that the step shows first is not the same in
  each run. The mark in the name field is not a button: it does not erase the name, and
  `'text ...'` adds to the text that is in the field.
- "Confirm" of the delete popup of the main menu needs a long press: `'click 628 444 3000'`.
- The first click on "Delete Character" of a row menu had no effect one time. A pointer move
  to the entry before the click opened the popup: `'move 382 415' 'wait 700' 'click 382 415'`.
- `tools/steam-shortcut.py add` puts the launcher in the Steam library of the user as a
  non-Steam game. That entry and a development run have different Proton prefixes, and
  `tools/dev-stop.sh` stops only development runs: one does not stop the game of the other.
  They share the game copy, the two products in `apps/*/dist`, and the game data folder
  (`~/.local/share/soulwarden/gamedata`). Thus a build changes the backend and the hook of the
  next start of the user. They do not share the data directory: a development run uses the
  development data directory (`tools/dev-run.sh` stops with a message when a game that is
  not a development run uses its data directory).
- A start with `SOULWARDEN_COMPAT_DIR=~/Games/soulwarden/proton/1063730` uses the prefix of the user:
  the launcher then stops a game that the user started. `tools/dev-run.sh` prints a warning.
- The line `rep: input N camera ... position X Y Z (SOURCE)` of the backend log shows the
  position that the backend has and its source (`client report` or `camera estimate`), one
  time each second while the camera or the character moves. `rep: position X Y Z saved`
  shows a write of the row. With the position report on (the default), the position is the
  exact position of the character.
- Only for the fallback (a run with `SOULWARDEN_EXACT_POSITION=0`, a hook from before stage 2.3a,
  or a different client build): the backend estimates the saved position from the camera
  (protocol notes 4.7). It keeps the known position while the camera axis goes through it: a
  camera turn, the wheel, and a jump write no position. The backend follows `'wheel N'` only
  while the character is at rest (backend log: `rep: camera distance D after a wheel
  input`). With the camera farther than 4 m, the saved position is behind the character by
  the difference after a walk (4 m at the largest distance). With a wall behind the camera,
  a short walk straight forward gives no write (the spawn position of a new character has a
  wall behind the camera), and the saved position can be some meters in front of the
  character at the end of a walk. For a movement test with the fallback, turn the camera to
  a free direction first.
- The create route makes a character only for a request in the form of the client: a name
  that the name rule of the client permits (3 to 15 characters, 6 digits at most, protocol
  notes 2.2.2) and creation data that the backend can read. A tool that sends a create
  request must send the `CharacterCreationParams` text of a capture of the client. The
  reply to a different request is `400` `CreateCharacterFailed`, and the backend log has
  the cause (`gateway: no character ...`).
- The client of an observer shows no body and no name tag for a player whose movement state
  has no action: the entity is in the client at its position (kind "remote", remote player
  slice), and nothing shows. The observer view of the movement state has the idle action of
  layer 0 for this cause (`slayerSequenceId` and `slayerScriptLayers`; protocol notes 4.9).
  A new state for a remote entity that has no effect on the screen is not a proof of a wrong
  record: read the entity in the client memory.
- The client does not move a remote character to the ground: an entity 2 m above the ground
  stays in the air. The saved position of a character is an estimate: a remote player at
  that position can be some centimeters to meters from the ground.
- A client with no input of the user leaves the world itself 20 minutes after the world
  entry (the game log has `CGame::ActorContainerDisconnect`). It sends no carrier disconnect:
  the backend ends the session with the cause `idle` 30 s later. In a long live check, send
  an input in each period (`tools/hx 'look 30 0' 'look -30 0'`).
- Each world entry gives the character a new actor id (`02000000 534F554C ...`, then `04...`,
  `06...`). A capture or a log of one session does not have the target ids of the next
  session: compute them with the rule of protocol notes 4.2.
- The world writes a position that is not saved 5 s after its last write, in its tick: a log
  line `rep: position X Y Z saved` can come with no input of the client.
- A session that got the message "kicked" or a refused registration ends when its client
  closes the connection, or 1 s after the message. Its inputs change nothing in that time.
- `tools/research/player_position.py` and a memory read need the player component of the
  local player. The client calls the trace point 0x6769980 for the player component of each
  player, also of a remote one: only a line with `ret=0x6928E1A` is for the local player.
- The scenario scripts are bash scripts. `source tools/scenarios/common.sh` in a zsh command
  gives wrong paths: call a bash script that has the `source` line.
- The loading screen stays for some seconds after the line `InGame` of the game log (11 s
  in one run). The client sends the position of its camera only when the loading screen is
  gone: wait for the first line `rep: input N camera` of the backend log before a
  screenshot of the world or an input test.
- The Escape key in the world: the first Escape of a world session does nothing. Each later
  Escape opens or closes the bar with the gear (10 key presses in one run). A script cannot
  see if the bar is open: `tools/scenarios/leave.sh` tries again with the other state.
  Positions of the Game Menu: gear `972 23`, "Exit to Main Menu" `474 515`, "Yes" of the
  popup `714 397`.
- When the user leaves the world, the client sends the carrier disconnect and no DTLS close
  notice. The backend ends the session (`rep: session ended, cause: carrier close`) and
  saves the position. The backend sends no close notice at the end of a session: a client
  that gets one does a new DTLS handshake from the same port at once (two sessions with no
  registration in the log of one run). The secure session of a client that went away ends
  60 s after its last datagram (`dtls: no datagram from port N for 60 s`).
- Each session must send a level information version and a client context instance that
  the game process did not get before, or the client drops each state bundle and stays in
  `WaitingForPlayerSpawn` for 120 s (protocol notes 3.5). The backend uses the session id.
  A change of the level information or of the state bundle must keep this.
- A game process that is stopped (`kill -STOP`) sends no datagram: its session ends after
  30 s (`cause: idle`). `kill -STOP` only the process with the name `NewWorld.exe`: a `kill`
  of the game process stops the backend too (the launcher does this).
- The research switch (`SOULWARDEN_RESEARCH_SESSION`) makes the client show an error popup and
  closes its connection. A run with the switch is not a test of the product.
- The hook log (`SOULWARDEN_DATA_DIR/client/logs/hook.log`) was made again at the stop of one
  run, with no trace line. Read the trace lines of a run before `tools/dev-stop.sh`.
- A request member with a wrong type, or a handler that stops with an exception, does not
  stop the backend: the reply has status 400, and the backend log has a line `http: METHOD
  PATH failed`.
- The world at the spawn of a new character is dark in some runs (the backend sends no
  world time). For a view of the character from the front use `'look 1800 0' 'wheel 3'`.
- A character that the backend seeded has no creation data and an empty `archetype`. The
  backend makes creation data with the default appearance for its menu document: the menu
  and the world show the same default body (male, grey hair, no clothing). The body is white
  in the menu only when the backend has no appearance tables.
- The name check and the create ignore the letter case of a name, also for letters with an
  accent (the backend makes the compare, not SQLite). A query with `COLLATE NOCASE` in a
  tool finds only the ASCII case.
- Generated files can have absolute paths of the computer, and a part of such a path can be
  equal to a name of the user (compile commands of CMake, `__FILE__` text of Mbed TLS in the
  backend, bytecode caches of Python). Thus the build directories are outside the repository,
  the top `CMakeLists.txt` sets `-ffile-prefix-map`, and each Python tool that imports a
  module of `tools` sets `sys.dont_write_bytecode`. A work script that imports a module of
  `tools` must run with `python -B`, or a `__pycache__` directory comes back. `.gitignore`
  names `build/`, `dist/`, and `__pycache__/`. After a build, a privacy scan of all files of
  the repository directory must find nothing (`strings -a apps/backend/dist/soulwarden-backend |
  grep -c /home/` gives 0).
