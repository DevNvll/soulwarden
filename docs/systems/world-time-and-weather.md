# World time and weather

Read this document before a change of the world time (the day and night cycle), of the time messages of a session, of the
creature points with a time constraint, or before a live test of the sky, the light, or a weather volume. The wire contracts are
in the protocol notes (section 4.43). The research is `~/Games/soulwarden/work/research/world-time-stage.md`.

## Start here

| Task | Start here |
| --- | --- |
| The cycle of the world: its position, its phases, its store, the command | `apps/backend/src/world/world_time.h`, `server/http/world_time_commands.cpp`, `tools/world time`, test cases WT1, WT4, WT9 |
| The time messages of a session: the request 4248, the record 7036, the phase 4247, the clock correction 335 | Protocol notes 4.43, `server/rep/time_messages.h`, `RepSession::handle_time_request`, `send_world_time`, test cases WT2, WT3 |
| The creature points with a time constraint | `gamedata/creature_data.h` (`SpawnPoint::phases`), `world/creatures.cpp` (`Creatures::scan`), `tools/gamedata/creature_data.py` (`read_time_constraint`), test cases WT5 to WT8 |
| The tool window: the tab "World" | `apps/game-hook/src/client/toolwindow/ui/window_layout.cpp` (`draw_world`), [Tool window and admin](tool-window-and-admin.md) |
| A live read of the time of day of the client | `tools/research/time_of_day.py` |

## Rules

- One cycle for the whole world (roadmap, rule MP-1). The world time is one position in the cycle (nanoseconds from 00:00) with
  the world key `newworld_vitaeeterna`. No character and no session owns it. The world thread changes it (rule MP-6): its value is
  the stored position plus the time of the world clock from the start (or from the last command). The world updates it before the
  creature system in each tick (`WorldTime::tick`).
- The cycle (decision D1 of the orchestrator; client code 0x40718B0, 0x407E7D0: the defaults of the Time component; no table of
  the game data has a different value). 1,800 real seconds from 06:00 to 19:00 (26 game seconds in one real second) and 900 real
  seconds from 19:00 to 06:00 (44 game seconds in one real second): one cycle of 45 minutes. The visual dawn and dusk of the client
  are 60 real seconds (`dawnDuration`, `duskDuration`; the engine dawn is 06:00 to 06:22).
- The phases (client numbers, client code 0x6A08270): night 0, dawn 1, day 2, dusk 3. The edges: dawn at 06:00, day 60 real
  seconds later (06:26), dusk 60 real seconds before 19:00 (18:34), night at 19:00 (decision D4: gameplay windows of 60 s; the
  client constructors leave the four edge values at 0). The night edge (19:00 exactly) belongs to dusk. `dawnStart` and
  `nightStart` of the record come from the lengths: the hour formula of the client needs them (0x6A09E60).
- A new world starts at noon (decision D2). The store keeps the position (table `world_time`, schema 11): at a command, each 60 s
  (`WorldTime::kSaveInterval`), and at an orderly stop of the backend (`backend: the world time is saved`). A stop of the backend
  stops the cycle (decision D3): the next start continues from the stored position. A crash loses 60 s at most. A stored value that
  is not a position of the cycle gives a new world at noon. The world time runs while the backend runs, also with no player.
- The run clock of the world (stage 3.6a, `WorldTime::run_ns`): the time that the backend ran with this world, over each start (the
  column `world_time.run_ns`, schema 13: the stored value plus the world clock since the start). It is saved with the position (each
  60 s and at an orderly stop), so a stop stops it too and a crash loses 60 s at most. A respawn of a gathering node is a time of this
  clock: a restart keeps the remaining time, and the downtime does not count (decision of the orchestrator; [Gathering](gathering.md)).
- The command for the world (rule MP-2): `WorldTime::set` writes the row first. A write that the database refuses changes nothing
  (the error `storage`, the reply 500). A time that is not a time of the day changes nothing (the reply 400). The character of the
  command names who gives it (rule MP-9): the time changes for each player.
- The session (rules MP-3 to MP-5). The client asks for the time with 4248 when its local Time facet is ready, one time for each
  world entry, after the mission load of the world level (live check L0). The session accepts only a request of the own entity: a
  session in the world, a body of exactly 16 bytes, the full actor route of its character, the entity field of its character, and
  the target of its Time facet (G + D41EB32C0F154004). A different request changes nothing and gets no reply (one log line for each
  cause). A valid request gets the record 7036 with the ids of the own entity and the world value now; a repeated valid request
  gets it again. After the request the session sends a new record at each command and one cycle after the last record (a rule of
  the precision of the client, not a value of the original server), and the phase 4247 at each phase change of the cycle. A client
  that did not ask gets no time message. The remote player slice has no Time component: an observer gets nothing.
- The clock correction 335 (client image 0x6A3D9B0: the server sender rearms its timer with 3,000 ms). Each session in the world
  sends the server clock each 3 s, the first one 3 s after its registration reply. It needs no request.
- The record (protocol notes 4.43). The anchor is one cycle before the server clock (1 at an earlier time: never 0, never later
  than the clock of the client), and the offset moves the cycle to the position. The client calculates the hour in each frame from
  the record and its copy of the server clock: the float error of this calculation is below 1 ms for two cycles (test case WT1).
  The world sends no hour stream. A travel and a respawn keep the entity, its Time facet, and its record (live check L2): no new
  request. A new world entry (a new session) loads the level again: the record is gone, and the client asks again.
- The creature points with a time constraint (game data, format 4 of `creatures.json`; research section 5). A spawner slice can
  have a TimeOfDayConstraintComponent with the flags m_night, m_dawn, m_day, m_dusk (the phases of the point, as a mask: bit 0 night
  to bit 3 dusk) and m_despawnOnTimeEnd. The spawn rule makes the creature of a point only in a phase of its mask (`outOfPhase`
  counts a look at a point out of its phase). The creature of a point with the despawn flag leaves the world when the phase is not
  one of the point (`removedByPhase`, the log line `leaves the world: the phase ... is not a phase of its point`): it is not a
  kill, it gives no reward, and the point keeps its state (a respawn time of a death, the hold of a command). A point with no
  constraint, and each point of a file before format 4, has each phase. An empty mask makes no creature. A creature of a command has
  no point and no phase rule. A phase change starts the normal scan of the points near the characters in the same tick
  (`Creatures::scan_soon`): no walk of the points of the world (test case WT6).
- The two cooldown flags of a constrained spawner (m_cooldownAfterDespawn, m_cooldownAfterConstraintFail; format 4, value 14 of a
  point). They are false on each point spawner of the supported build and true on one random encounter (the day-only Elk Elemental,
  respawn 3,600 s). The rule (a decision of an agent from the names of the flags: the server code is not in the client): the end of
  the phases of such a point, and a look at it out of its phase, start the respawn time of the point (`Creatures::start_cooldown`),
  as a death does.
- The weather is local data of the level. The level has sky, fog, rain, snow, and wind volumes (TimeOfDayPOIConfig, FogVolume,
  Rain, Snow, WeatherConfiguration): the client selects them by the position of the camera, with no message of the server. The world
  time gives them the hour of their curves. No weather configuration of the level has a seed or a schedule (research K5). A volume
  can override the hour that the client shows (the Dunwood keep interior: hour 0 while the world is at day, live check L7). The
  weather of an event (a siege, a breach, a boss) comes with the slice of the event: the event stages own it.

## Commands

```sh
tools/world time                     # the world time: the time, the phase, the next phase, the cycle, the counts
tools/world time Soulwarden 19:00    # set the time of the day of the whole world (HH:MM or HH:MM:SS)
python3 -B tools/research/time_of_day.py     # the time of day of the live client (the record, the hour, the flags)
```

- The tool window (F8) has the tab "World": the time of the world and its phase, a form with the hour and the minute and the
  button "Set", and four buttons 06:00, 12:00, 19:00, 00:00. The admin command is `set_world_time` (argument `time`), and the state
  of a character has the member `worldTime`.
- `tools/world list` prints the line `world time:` with the counts of the phase rule of the creatures.
- `tools/gamedata/creatures.sh` writes format 4 (the phase mask, the despawn flag, and the cooldown flags of each spawn point;
  `spawnPointsWithPhases` 405 in the supported build). The backend log of the start says how many points have a constraint.

## Traps

- Do not judge the world phase from the brightness of a picture. A local volume changes the look (an interior, the fog of
  Brightwood Isle, a storm). Read `tools/world time` and the hour of the client (`tools/research/time_of_day.py`).
- A command changes the sky in one frame (a jump): the brightness of a picture went from 83 to 21 in 0.1 s. The exposure of the
  renderer then adapts for about 1.5 s. It is not a fade of the clouds.
- The clock of the client of a development run runs faster than the clock of the backend (Wine counts the time with a different
  clock of the computer than `std::chrono::steady_clock`): live check L8 measured 82 ms in 266 s. The clock correction each 3 s
  keeps the difference small. A correction of more than 1 s moves the clock of the client at once (the movement too).
- `gamescopectl screenshot` can need 1 s for its first call: a capture after a command can start with a picture from before it.
- The world time runs while the backend runs: a world entry after some minutes in the menu gets a later time than the time of the
  start.
- The client asks for the time once for each world entry. A record that a development command (`tools/world message ... 7036`)
  sends stays until the next record of the session: a command of the world time replaces it.
- Before stage 2.2 the world was dark after a character creation (the hour 0 of `TimeStart` of the mission) and in the afternoon
  light (16.7) after a menu login. These looks came from the mission file with no record.
- The setting `javelin.enable-time-comp-sync-to-local-client` read 0 in the live client (byte at image RVA 0x9EFF37C), and the bus
  path 0x408C3A0 had no hit: the record comes only from the message 7036.
- A test that moves the clock of the test host by more than 30 s in one step ends each session (the idle limit): pass the time in
  steps of 1 s (`TimeHost::pass` in `tests/world_time_tests.cpp`).

## Sources of the rules

| Rule | Source |
| --- | --- |
| The cycle lengths, dawn 06:00, dusk 19:00, the visual durations 60 s | Client code (0x40718B0, 0x407E7D0); decision D1 of the orchestrator; no game data table has a value |
| The layout of 7036, 4247, 4248, 335; the target G + D41EB32C0F154004 | Client code (readers and writers); live checks L1 and L8 |
| The anchor one cycle back with the offset | Client code (0x6A0ADC0); live check L1 (noon with a nonzero offset, midnight) |
| The clock correction each 3 s | Client code (0x6A3D9B0, the server sender of the image) |
| A new world at noon | Decision D2 of the orchestrator |
| The stop of the cycle while the backend is stopped; the save each 60 s and at an orderly stop | Decision D3 of the orchestrator |
| The gameplay edges 60 s after dawn and 60 s before dusk | Decision D4 of the orchestrator |
| The run clock (the time that the backend ran) for the respawn of a gathering node; the downtime does not count | Decision of the orchestrator for stage 3.6a (as D3) |
| No weather message, the local volumes | Game data (research K5); live check L7 |
| The phase mask and the despawn of a point; no reward at a phase end; the death time stays | Game data (TimeOfDayConstraintComponent, cooldown flags false); decision D6 of the orchestrator |
| The rule of the two cooldown flags (one random encounter) | A decision of an agent from the names of the data flags |
| A new record each cycle | A rule of the precision of the client (research 7.2), a decision of an agent |
