# Identity, accounts, and characters

Read this document before a change of the player identity, the accounts, the character creation, the
deletion, or the main menu, and before a live test of these screens. The wire contracts are in the
protocol notes (sections 2.1, 2.2, and 3.4).

## Start here

| Task | Start here |
| --- | --- |
| Player identity, accounts, the Steam id | Protocol notes sections 2.1 and 3.4, `tools/research/check_identity.py` |
| Character creation, deletion, the body in the menu | Protocol notes section 2.2, `tools/scenarios/create.sh`, `tools/research/creation_params.py` |
| Appearance of a character in the world | Protocol notes sections 2.2.5 and 4.5, `tools/gamedata/appearance.sh` |

## Rules

- Player identity: the launcher reads only the Steam id and the persona name from
  `config/loginusers.vdf` of the Steam installation, and no Steam credential. The hook gives
  them to the client and to the backend in a local ticket. The ticket is not a credential, and
  the backend does not validate it (stub authentication). The Steam id selects the account
  row; the persona id of the account is the owner key of all player data.
- The DLC (stage 1.1b): the token route gives each account the entitlements of the DLC, and a world entry gives each
  character the achievement of the expansion, with no purchase and no setting ([DLC and entitlements](dlc-and-entitlements.md)).

## Commands

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

## Traps

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
- The create route makes a character only for a request in the form of the client: a name
  that the name rule of the client permits (3 to 15 characters, 6 digits at most, protocol
  notes 2.2.2) and creation data that the backend can read. A tool that sends a create
  request must send the `CharacterCreationParams` text of a capture of the client. The
  reply to a different request is `400` `CreateCharacterFailed`, and the backend log has
  the cause (`gateway: no character ...`).
- The world at the spawn of a new character is dark in some runs (the backend sends no
  world time). For a view of the character from the front use `'look 1800 0' 'wheel 3'`.
- A character that the backend seeded has no creation data and an empty `archetype`. The
  backend makes creation data with the default appearance for its menu document: the menu
  and the world show the same default body (male, grey hair, no clothing). The body is white
  in the menu only when the backend has no appearance tables.
- The name check and the create ignore the letter case of a name, also for letters with an
  accent (the backend makes the compare, not SQLite). A query with `COLLATE NOCASE` in a
  tool finds only the ASCII case.
