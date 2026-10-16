# Travel and conversations

Read this document before a change of a travel, the travel places, or the lock of the input of a
client (a conversation, a cutscene). The wire contracts are in the protocol notes (sections 4.16 and
4.20).

## Start here

| Task | Start here |
| --- | --- |
| A conversation with a person, a cutscene of the client, a character that the client keeps in place | Protocol notes 4.20, `apps/backend/src/server/rep/lock_messages.h`, `server/rep/rep_session.cpp` (`handle_cutscene_ended`, `end_input_lock`), `tools/world message`, test case UT1 |
| Travel: the server moves a character to a different place, the travel places, the tab "Travel" | Protocol notes 4.16, `apps/backend/src/world/world.h` (`World::travel`), `server/rep/travel_messages.h`, `gamedata/travel_places.h`, `server/http/travel_commands.cpp`, `tools/world travel`, `tools/world places`, `tools/gamedata/places.sh` |

## Rules

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
  2473 (the end of its committed interaction): from stage 2.5a it ends the interaction of the
  character in the interaction system ([Interactions](interactions.md); the request of the
  conversation gets 3743 result 1, and a refusal would close the conversation). A call that
  names a different entity gets nothing. Only the session of the owner gets 6709 and 3746. The
  backend has no conversation content: a new message of the conversation keeps this answer.
  A travel and a respawn also end the interaction of the character in the world. When the
  server ends a conversation with 3746 (a command; stage 2.5b), the client closes it, sends 2473
  and 6705, and the answer 6709 frees the input and the camera (live check L7 of stage 2.5b: a
  walk of 6 m after it); the backend sends no extra 6709 for it.
- The travel places are game data (`travel-places.json`, `gamedata::TravelPlaces`): the
  spirit shrines and the inns of the territories, each with an id, a name, a territory, and
  a position. A command names a place by its id, or it gives the three numbers of a
  position. The commands are in the common layer (`server/http/travel_commands.cpp`): the
  development route (`tools/world travel`), the admin command `travel`, and the tab
  "Travel" of the tool window use it. With no file the list is empty, and a travel with
  numbers works.

## Commands

```sh
tools/world places windsward         # the travel places with a text in their id, name, or territory
tools/world travel NAME shrine_40170 # move a character to a travel place (in the world: at once; not in the world: its row)
tools/world travel NAME 8866 4250 137.9   # move a character to a position (--screen on|off|auto: the loading screen)
```

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

## Traps

- A travel place can have a creature that fights near it (a Highlands Goat of level 62
  is 8 m from the Shrine of Pillars): a character that travels there or that respawns
  there takes damage. Turn the ignore value on for a test of a travel.
- A conversation (the key E at a person, "Talk") locks the input of the character until
  the answer 6709. In a test: the camera of the conversation shows the person; E goes to
  the next text, and the conversation ends at the last one. A lore page ("E Read") opens
  the journal: two Escape keys close it (the first one goes back one step). From stage 2.5a
  the read keeps the page in the journal of the character ([Interactions](interactions.md)).
- After a travel the creatures of the new place come only after the first position report
  of the client from the place (20 s at most): `tools/world creatures` lists them at once,
  the interest table of the session later.
- `tools/world travel NAME PLACE` moves a character that is in the world at once. A travel
  of more than 100 m shows the loading screen of the client, and the first position
  report from the new place comes 3 s to 5 s later: wait for a line `rep: input ...
  position X Y Z` with the new place in the backend log before a screenshot.
- The character and the camera look north after each travel (yaw 0). A `'look DX 0'`
  after it turns from north: `'look 1300 0'` looks to the south-east.
- The height of a travel with numbers must be the height of the ground or more. The
  spawn lift is 0.5 m: a height 0.4 m below the ground worked, and a lower one was not
  tested. The backend has no terrain data.
- The client shows the interaction text "Fast Travel" at a shrine. The backend has no
  answer for the fast travel of the game, and the key E at a shrine was not tested.
