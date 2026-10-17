# The position of a character

Read this document before a change of the position report of the client, the position check, the
camera estimate, or the heartbeat state, and before a live test that moves the character. The wire
contracts are in the protocol notes (sections 4.6 and 4.7).

## Start here

| Task | Start here |
| --- | --- |
| Movement and position of the player | Protocol notes sections 4.6 and 4.7, `apps/backend/src/world/position_report.h`, `server/rep/client_input.h`, `tools/research/input_packet.py` |
| The position report of the client (the change of the client code by the hook) | Protocol notes 4.7, `apps/game-hook/src/client/hooks/movement` |

## Rules

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
- The weight of the bag (stage U3, [Items and equipment](items-and-equipment.md)): the check has only upper limits, and the client
  makes the speed of an encumbered character itself. The check accepts the slow walk of an overencumbered client (live: 1.57 m/s at
  300.2 before the stage; backward 0.62 m/s at 300.4 after it) and its jog (5.4 m/s at 200.2 and at 300.4 forward: the client did not
  make the forward jog slower in the run of stage U3, open). The server does not set the speed.
- The envelope of a dash (stage 3.3b-S4, [Forced movement](forced-movement.md)): in an accepted cast with a movement of
  the caster (Charge), the check takes `MotionAllowance` from the abilities system: the window accepts the distance of
  the envelope more (14.73 m x 1.3), and one step the speed of the envelope (18.07 m/s x 1.3): the fast step only in the
  length of the movement, the distance for the length and one window. One cast gives its distance one time (code review
  of stage 3.3b-S4, finding F1, test case RM1): in the envelope the distance from the place of the character at the
  press is not more than the window speed times the time since the press, the window margin, and the distance of the
  envelope (`MotionAllowance::from`, `since`). Before the correction one Charge moved a client 55 m in 2.5 s. The speed multiplier of a speed table (stage 3.3b-S2a) counts one window after
  its change, and a later lower table does not move that end (code review of stage 3.3b-S2a, finding F1, test case RG1).
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

## Commands

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

## Traps

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
- A fall from a spawn high above the ground also moves the character in the horizontal
  plane (6 m in a fall of 150 m at the default spawn: it lands on the wall beside the
  street).
- `tools/research/player_position.py` prints nothing in a run of `tools/scenarios/play.sh`
  with no `SOULWARDEN_TRACE=6769980`.
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
- The world writes a position that is not saved 5 s after its last write, in its tick: a log
  line `rep: position X Y Z saved` can come with no input of the client.
- `tools/research/player_position.py` and a memory read need the player component of the
  local player. The client calls the trace point 0x6769980 for the player component of each
  player, also of a remote one: only a line with `ret=0x6928E1A` is for the local player.
