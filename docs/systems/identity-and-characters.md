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
| The body of a character in the main menu: the menu document, its clothing (stage U4) | Protocol notes section 2.2.5, `state/creation_data.h` (`make_menu_document`, `kMenuSlotKeys`), `server/http/gateway_routes.cpp` (`published_data`), `world/inventory.h` (`menu_equipment`, `menu_appearance`), test cases ME1 to ME8 |

## Rules

- Player identity: the launcher reads only the Steam id and the persona name from
  `config/loginusers.vdf` of the Steam installation, and no Steam credential. The hook gives
  them to the client and to the backend in a local ticket. The ticket is not a credential, and
  the backend does not validate it (stub authentication). The Steam id selects the account
  row; the persona id of the account is the owner key of all player data.
- The DLC (stage 1.1b): the token route gives each account the entitlements of the DLC, and a world entry gives each
  character the achievement of the expansion, with no purchase and no setting ([DLC and entitlements](dlc-and-entitlements.md)).
- The menu document (stage U4). The main menu makes the body of a character from the member `PublishedData` of the
  character list (`GET /prod/game/getlogininfo`). At each request of the list the backend makes this text for each
  character with one writer (`state::make_menu_document`), from the values of the character: the name, the 12 appearance
  names and the backstory of its creation data (a character with no creation data: the default appearance), the world id,
  the level of its row, and the map `equipment`. The stored creation data does not change. The original server made the
  text again from the character at each publish [client code: 0x67F0600 with
  `PaperdollComponent::WriteEquipmentToPlayerSaveData`, loop 0x684E9B0, entry 0x673EE40]. The other members (the
  position, the coin, the company name, the crest) have the values of a new character, as before [decision of the
  orchestrator: the menu use of them is not known].
- The map `equipment` of the menu document: one entry for each armor item of the paperdoll slots 0 to 4 in the store, in
  the sequence of the slots. The key is the visual slot name `hat`, `shirt`, `gloves`, `pants`, `boots` [client code
  0x673EE40: no entry for a slot above 4; game data: `m_paperdollVisualSlotMapping` of the player slice; the keys of 5 of 5
  create requests]. The keys are code constants (`state::kMenuSlotKeys`): a wire text of the client [decision of the
  orchestrator]. `appearanceId` is the CRC-32 of the lower-case name of `ArmorAppearanceM` (a male body) or
  `ArmorAppearanceF` (a female body) of the item row [client code 0x673EE40: definition +0xDC and +0xE0; behavioral proof:
  25 of 25 entries of 5 create requests of the client]. Only the gender name `Female` selects the F name
  [decision of the orchestrator; the original test is "Male gives M, each other text gives F": the two rules differ only
  for a text that is not a gender]. `itemName` is empty and the four dye bytes are 0 [client code: each writer of the
  client and of the original server; the backend has no dye before stage 5.6].
- An item with no look gets no entry: an item with no class tag `Armor` (a skin display item, a weapon), and an item row
  with an empty name [decision of the orchestrator: the original server wrote the value 0, and the reaction of the menu to
  0 is not known]. Thus the menu shows no weapon, no shield, no jewelry, no tool, and no item of the bag [client code
  0x673EE40]. A character of the creation shows the armor of its archetype that it has, and no helmet: the backstory gives
  the head item with the quantity 0 (before stage U4 the menu showed the helmet of the stored creation data).
- More than one player and the cost (MP-1, MP-8): the route reads the store only (the inventory system writes the rows of a
  change before it changes the entity, thus the rows are the equipment of the entity, also while the character is in the
  world): for each character of the account one level query and one item query (the slots 0 to 4 of the paperdoll, on the
  place index of the table; test case ME7). No world task, no scan of the world. Each account gets only its own characters
  (test case ME4: two sessions).

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
- Character creation, the body type: the row "Body type" (`210 166`) opens a list of two
  icons at its right: `363 166` is the male body, `413 166` the female body (stage U4:
  `SOULWARDEN_CREATE_APPEARANCE='click 210 166;wait 4000;click 413 166;wait 4000'` made a
  female character; the backend log line `world: appearance of character ...` starts with
  1). The creation screens show no body in the pictures of gamescope (a dark screen with the
  panels): check the gender in the log line.
- "Confirm" of the delete popup of the main menu needs a long press: `'click 628 444 3000'`.
- The first click on "Delete Character" of a row menu had no effect one time. A pointer move
  to the entry before the click opened the popup: `'move 382 415' 'wait 700' 'click 382 415'`.
- The create route makes a character only for a request in the form of the client: a name
  that the name rule of the client permits (3 to 15 characters, 6 digits at most, protocol
  notes 2.2.2) and creation data that the backend can read. A tool that sends a create
  request must send the `CharacterCreationParams` text of a capture of the client. The
  reply to a different request is `400` `CreateCharacterFailed`, and the backend log has
  the cause (`gateway: no character ...`).
- Before stage 2.2 the world at the spawn of a new character was dark (the hour 0 of the mission file; the backend sent no
  world time). Now the client gets the world time at its world entry. For a view of the character from the front use
  `'look 1800 0' 'wheel 3'`.
- A character that the backend seeded has no creation data and an empty `archetype`. The
  backend makes its menu document with the default appearance: the menu and the world show
  the same default body (male, grey hair), with the armor that it wears (stage U4). The body
  is white in the menu only when the backend has no appearance tables.
- The menu document reads the paperdoll rows of the store, not the entity. A system that
  changes the look of a worn item must write its row first (the inventory system does). If
  a later stage keeps a look only in the entity (a transmog, a dye: stage 5.6), the route
  must ask the world (`World::call`) for a character in the world.
- The client keeps the map `equipment` as a hash map: the create requests have the order
  `gloves`, `shirt`, `hat`, `boots`, `pants`. The backend writes the order of the slots, and
  the menu accepts it (live check of stage U4). With the values and the order of a create
  request the writer makes the document of the client byte for byte (5 of 5 captures).
- An item catalog from before stage U4 (format 4 or older) has no armor appearance: the menu
  shows each character with no clothing, and the backend log has one line at the start
  (`the item catalog has no armor appearances`). Make the file again with
  `tools/gamedata/items.sh`.
- The body of the menu comes some seconds after the line `gateway.get_login_info` of the
  backend log. A click on a row of the menu selects that character, and the body changes to
  it (the client reads the document of the row again, 0x41F9E20). Wait 5 s before a picture.
- The name check and the create ignore the letter case of a name, also for letters with an
  accent (the backend makes the compare, not SQLite). A query with `COLLATE NOCASE` in a
  tool finds only the ASCII case.
