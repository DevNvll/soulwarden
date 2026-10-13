# Reference

The details behind the [README](../README.md): the architecture, the identity of a player,
each variable, the development tools, and the limits of each feature. The wire contracts are
in the [protocol notes](PROTOCOL_REVERSE_ENGINEERING_NOTES.md).

## Architecture

The tree has two independent products.

| Product | Source | Output |
| --- | --- | --- |
| Game hook | `apps/game-hook` | `apps/game-hook/dist/steam_api64.dll` |
| Backend | `apps/backend` | `apps/backend/dist/soulwarden-backend` |

The game hook replaces `steam_api64.dll`. It gives the client the Steam id and the persona
name that the launcher supplies, sends all HTTP requests of the client to the backend, blocks
all traffic that is not loopback, and makes the client trust the certificate of the backend.
It also draws the tool window (key F8) on the picture of the game, with Dear ImGui and the
Direct3D 12 device of the game, and sends the commands of that window to the admin interface
of the backend. And it makes the client send the exact position of its character in each
input report: a change of 6 bytes of the client code (section "Exact position").

The backend is a standalone C++ service. It has the HTTP services of the login path (channel
document, account token, credentials, login information, name check, character creation and
deletion, login queue) and the game server entry point (DTLS 1.2, datagram carrier, actor
messages, state replication). Accounts, characters with their creation data and archetype,
and the last position of each character in the world are in SQLite. The backend, its
database, its logs, and its captures stay outside the game folder (`~/.local/share/soulwarden`).

Each connection of a client to the game server is one session with a session id. A session
ends when the client closes its connection (the user leaves the world), when the secure
session closes, after 30 s with no datagram from the client, after 30 s with no registration,
and when the backend stops. The backend has a world object with one clock, a task queue,
timers, and the entity table: one thread owns the world. Each character that is in the world
is one entity of the world, with its ids, its position, and its state. The client reports
the position of its character 30 times each second, and the world checks each report before
it keeps the position (a newer input number, numbers in the limits, and a speed limit). The
world saves the position of a character while it moves and when its session ends. A session
is an observer
of the world: it sends each entity of its interest table to its client, in the owner view
for its own character and in the observer view for each other entity.

The game server refuses a registration with a character that the database does not have, or
with a character of a different account (the client shows a connection error). A character
is in one session at most: a newer session of the character takes it, and the client of the
older session gets the message "Your account has connected from another game session". The
delete of a character that is in the world ends its session first.

The backend has an admin interface for the tool window (routes `/admin/v1` on its loopback
listener): the state of the characters of the account of the game, an item search, and
commands for the items, the coin, the level, the attributes, and the skills of a character.
A request needs the token that the launcher makes for each start (protocol notes 6.4).

The build has a library (`soulwarden_backend_core`), the backend program, and a test program that
runs the game server with made-up clients and no game process.

## Player identity

The launcher reads the Steam id and the persona name of the last Steam login from
`config/loginusers.vdf` of the local Steam installation. It reads no other Steam file and no
Steam credential, and it does not start or contact Steam. The hook gives the two values to
the client and writes them in a local ticket (`SOULWARDEN-LOCAL-TICKET;1;<Steam id>;<persona name
in hexadecimal text>`) that the client sends to the backend. The backend finds the account of
the Steam id, or makes one, and gives the client the characters of that account.

**Authentication is a stub.** The ticket is plain local text, not a Steam credential, and the
backend does not validate it: a local process can name any Steam id. This is acceptable only
because the backend listens on loopback and the game has no other network.

| Variable | Value | Result |
| --- | --- | --- |
| `SOULWARDEN_STEAM_ID` | not set | The account of the Steam installation. |
| `SOULWARDEN_STEAM_ID` | a Steam id (17 digits) | That account. `SOULWARDEN_PERSONA_NAME` is its name (default `Soulwarden`). |
| `SOULWARDEN_STEAM_ID` | `local` | The fallback identity (a constant Steam id, name `Soulwarden`). |
| `SOULWARDEN_PERSONA_NAME` | text | Replaces the persona name of the Steam installation. |
| `SOULWARDEN_STEAM_DIR` | path | Steam installation (default `~/.local/share/Steam`). |
| `SOULWARDEN_PROTON_DIR` | path | Proton (default `<SOULWARDEN_STEAM_DIR>/steamapps/common/Proton - Experimental`). |
| `SOULWARDEN_SEED_CHARACTER` | name | Development aid: an account with no character gets a character with this name at its login. The name must not be in use. |
| `SOULWARDEN_GAMEDATA_DIR` | path | Tables that a tool makes from the game copy (default `~/.local/share/soulwarden/gamedata`). See "Characters". |
| `SOULWARDEN_OBSERVE_PLAYERS` | `1` | Each session observes the characters of other sessions that are 100 m or less from its character, and its client shows them as remote players that stand. Off by default (a client then gets only its own character); a development run turns it on. |

With no `loginusers.vdf` in the Steam installation, the fallback identity applies. The
launcher also takes Proton from the Steam installation and stops when it does not find it:
when `SOULWARDEN_STEAM_DIR` is not the directory that has Proton, set `SOULWARDEN_PROTON_DIR`. The first
Steam id that logs in to a database from before the account support takes the character of
that database. The logs of the hook and of the backend show only the last 4 digits of a Steam
id. The log of the client and the protocol captures have the full Steam id and the persona
name: keep them out of the repository.

## Build and run (Linux, Proton)

```sh
tools/build-soulwarden-game-hook.sh     # needs llvm-mingw
tools/build-soulwarden-backend.sh       # needs clang, cmake, ninja
tools/test-soulwarden-backend.sh        # tests of the backend with no game process
tools/gamedata/appearance.sh       # one time: appearance tables from the game copy
tools/gamedata/slice_ids.sh        # one time: ids of the player slice (optional)
tools/gamedata/items.sh            # one time: item catalog (without it the backend gives no item, and a new character has no clothing)
tools/gamedata/progression.sh      # one time: progression tables (without them the backend gives no level and no skill point)
tools/gamedata/creatures.sh        # one time: creature data (without it the world has no creature); needs an Oodle library (see below)
tools/gamedata/combat.sh           # one time: combat tables (without them an attack of your character has no effect)
linux-compat/run-soulwarden.sh
```

`tools/test-soulwarden-backend.sh` builds and runs the test program. Its output lists each case,
and its status is 0 only when each case passes. One case compares the bytes that the game
server sends to one client with a stored file (`apps/backend/tests/golden`).

The build scripts keep the CMake build directories outside the repository, in
`~/Games/soulwarden/work/build` (`SOULWARDEN_BUILD_ROOT`), and the products have no absolute path of
the build computer.

The launcher needs a copy of the game in `~/Games/soulwarden/game` and Proton Experimental from
Steam. It starts `Bin64/NewWorld.exe` directly (no launcher, no anti-cheat service). At the
main menu, select the character and press Play.

`tools/steam-shortcut.py add` puts the launcher in the Steam library as a non-Steam game
(`show` lists the entries, `remove` removes the entry). Do it with Steam closed, or start
Steam again directly after it: Steam reads its shortcut file only at its start. The tool
keeps a copy of the file from before each change in `~/.local/share/soulwarden/backup`.

The launcher keeps its Proton prefix in `~/Games/soulwarden/proton/1063730` (`SOULWARDEN_COMPAT_DIR`)
and stops the processes of that prefix before a start. A development run
(`tools/dev-run.sh`, `tools/scenarios`) has a different prefix
(`~/Games/soulwarden/proton-dev/1063730`), and `tools/dev-stop.sh` stops only development runs.
Thus a development run and a game from the Steam library do not stop each other.

A development run and the development tools (`tools/hx`, `tools/shot`, `tools/world`, the
scenario scripts, the research tools) use the development data directory
`~/Games/soulwarden/work/dev-data`, not the player data of the user (`~/.local/share/soulwarden`).
`tools/dev-env.sh` has this default, and `SOULWARDEN_DATA_DIR` selects a different directory. The
database of the development data directory stays between runs: `tools/scenarios/play.sh RUN`
enters the world with a character that is there already (the first run makes the seed
character `Soulwarden`).

`tools/world` sends development commands to the world of a development run: `list`, `spawn
CHARACTER --near OTHER [DX DY DZ]` (put a character of the database into the world as an
entity with no session; the client shows it as a remote player), `remove`, `touch`, and
`fragment` (one state fragment with bytes of the caller, for a live check of a state type).
It also has the commands for the items and the coin of a character: `items CHARACTER`,
`give CHARACTER ITEM [QUANTITY]`, `take CHARACTER SLOT [QUANTITY]`, `coin CHARACTER AMOUNT`,
`move CHARACTER FROM TO`, `split CHARACTER SLOT QUANTITY [TO]`, and `lock CHARACTER SLOT
on|off`, and for its equipment: `equipment CHARACTER`, `equip CHARACTER BAGSLOT [SLOT]`, and
`unequip CHARACTER SLOT [BAGSLOT]` (`SLOT` is a slot number of the paperdoll: 1 chest, 24
weapon 1; `tools/world --help` has the list). A command for a character that is not in the
world changes its rows in the database.
The backend has these commands only with the option `--dev-commands`, and it accepts that
option and the research switch only when its process has `SOULWARDEN_HARNESS=1`.

## Tool window

In the world, the key **F8** shows a window with the title "Soulwarden tools". F8 again, or the
close button of the window, hides it. This works in each start of the game with the launcher
(`linux-compat/run-soulwarden.sh`, or the entry of the Steam library): no development option is
necessary.

While the window shows, the pointer and the keys go to the window and not to the game. When
it is hidden, the game has its input again. The window is for the character that you play
(an account with more than one character has a selector in the first line).

| Tab | What you can do |
| --- | --- |
| Items | Type a text in the search field to find items of the catalog (the text is compared with the id, the type, and the class of an item, for example `sword`, `potionhealth`, `ore`, `consumable`). Click an item, set the quantity, and press "Give": the item is in the bag. The list "Bag" has each item of the bag with a "Remove" button. |
| Character | Set the level, give experience, set the coin, and set the value of each attribute (the character must have the free points for it). |
| Skills | Set the mastery level of each weapon (each level above 1 gives one mastery point for its tree) and the level of each trade skill. |
| Creatures | The list at the bottom has each creature that is 150 m or less from your character, the nearest one first, with its level, its health, and its distance. "Set" sets the health of a creature to the percent value above the list, "Kill" kills it, and "Remove" takes it out of the world. To make a creature, type a text in the search field (the id, the name, or the family of a kind, for example `wolf`, `risen`, `rabbit`), click a kind, set the level and the distance, and press "Spawn": the creature is that many meters to the north of your character (the window covers the left half of the picture: press F8 to see the creature). "Reset" makes each spawn point free again whose creature you killed or removed. |

The last line of the window shows the result of the last command, or the cause when the
backend refused it. The client shows each change at once: the bag window (Tab), the level
on the HUD, and the character window (K). To equip an item, use the inventory screen of the
game (Tab): click the item and "Equip".

| Variable of the launcher | Effect |
| --- | --- |
| `SOULWARDEN_TOOL_WINDOW=0` | The game starts with no tool window: the hook installs none of its parts. |
| `SOULWARDEN_TOOL_WINDOW_KEY=0x79` | A different key (a virtual key code of Windows; `0x79` is F10). F8 has no function in the default keys of the game. |

In the Steam library, put a variable before the launcher in the launch options of the entry,
for example `SOULWARDEN_TOOL_WINDOW=0 %command%`.

- The window needs the item catalog (`tools/gamedata/items.sh`) for the items and the
  progression tables (`tools/gamedata/progression.sh`) for the level and the skills. Without
  a file, the window says which tool makes it.
- The launcher makes a new random token for each start and gives it to the backend and to
  the game. The backend accepts a command only with that token, only on its loopback
  listener, and only for the characters of the account of the game.
- When a part of the tool window cannot start (for example after a game update that changes
  the client code), the game continues with no tool window, and the hook log
  (`~/.local/share/soulwarden/client/logs/hook.log`) has one line `tool window: OFF for this
  start of the game: CAUSE`.
- Not checked: a picture with HDR, frame generation, and the window on the main menu (the
  window is made for the world).

## Exact position

The client moves its character itself and sends its inputs to the server. Its input report
has a field for the position of the character, which the client fills only inside a housing
plot. The game hook changes 6 bytes of the client code (image address `0x2A242C1`), so that
the client fills the field in each input report. The backend then has the exact position of
the character: the saved position is the place where the character stands, also near a wall,
and the next session starts there. The change has no effect on the camera, the compass, or
the HUD.

| Variable of the launcher | Effect |
| --- | --- |
| `SOULWARDEN_MELEE_REPORT=0` | The backend does not give the client the remote configuration document that turns the melee contact report of the client on. A melee attack of your character then has no effect on a creature. |
| `SOULWARDEN_EXACT_POSITION=0` | The hook does not change the client code. The backend then estimates the position from the camera: the saved position can be some meters from the character (up to 4 m behind it after a camera move with the wheel, some meters in front of it near a wall). |

- The hook compares the client code at the address with the code of the supported client
  build before it changes it. With different code (a game update) it changes nothing, the game
  continues, the hook log has one line `position report: OFF for this start of the game:
  CAUSE`, and the backend uses the camera estimate.
- The backend log (`~/.local/share/soulwarden/logs/backend.log`) says which source a session uses:
  `rep: position source: the position field of the input reports, the exact position` or
  `rep: position source: the camera estimate`.
- The backend refuses a reported position that is too far from the last one for the time (a
  speed limit), that has a number that is not finite, or that is outside the height range.
  The character then keeps its last position in the backend. The client gets no correction
  yet.

## Characters

A Steam account with no character sees four "Create Character" rows in the main menu. A
click on a row starts the character creation of the client: world type, a confirmation, an
intro film (hold Escape to skip it), then the archetype, the appearance, the name, and the
world. The backend stores the character for the Steam account, and the client enters the
world with it. The button `...` of a character row has "Delete Character" (hold the pointer
button on "Confirm").

- The name of a character is unique for all accounts of the database. The letter case makes
  no difference, also for a letter with an accent.
- A new character starts in the town of the seeded character, with the items of its
  archetype that the game data gives it at the start: four armor pieces (chest, hands, legs,
  feet) and one sword, which it wears. The backend needs the item catalog for this.
- The world shows the appearance of the creation only when the backend has the appearance
  tables. `tools/gamedata/appearance.sh` makes them from the game copy
  (`~/.local/share/soulwarden/gamedata/appearance-tables.json`, `SOULWARDEN_GAMEDATA_DIR`). The tool
  reads the pak files of the game copy (`SOULWARDEN_GAME_DIR`, default `~/Games/soulwarden/game`). It
  needs Python 3 with its standard library and no other file. When the game copy or a
  database is not there, it stops with one message and writes no file. The tables are game
  data and stay outside the repository. Without the file each character has the same default
  body in the world, and the backend log has a line about it.
- `tools/gamedata/slice_ids.sh` makes the ids of the faceted components of the player slice
  (`~/.local/share/soulwarden/gamedata/slice-ids.json`). With them the game server checks that the
  target of each component message of a client is a component of an entity of that client
  (protocol notes 4.2). Without the file the game server makes no such check. The pak entry
  of the slice has Oodle data: the tool needs an Oodle library (`SOULWARDEN_OODLE_DLL`) or a file
  of the slice that `tools/research/pakx.py` extracted.
- `tools/gamedata/items.sh` makes the item catalog
  (`~/.local/share/soulwarden/gamedata/item-catalog.json`): each item of the 43 item tables and
  the 2 house item tables of the game, with its index on the wire (protocol notes 4.10), its
  id, the text key of its name, its type, its class, its weight, its stack limit, its
  durability, and its gear score. Without the file the backend starts and gives no item,
  and each bag is empty in the client. The pak entries of the tables have Oodle data: the
  tool needs an Oodle library (`SOULWARDEN_OODLE_DLL`) or the table files that
  `tools/research/pakx.py` extracted (`SOULWARDEN_DATATABLES_DIR`). The tool refuses a folder of
  extracted tables that is not equal to the table list of the pak files (one table that is
  not there makes each later index wrong). The catalog also has the items of each archetype
  (the backstory table of the game) and the list of the backstories of the character
  creation: a new character gets the items of its archetype, and the backend refuses a
  create request with a different backstory. `tools/gamedata/items.sh --find ITEM...` prints
  the catalog line of an item id.
- `tools/gamedata/progression.sh` makes the progression tables
  (`~/.local/share/soulwarden/gamedata/progression-tables.json`): the experience, the attribute
  points, and the health of each level, the health of each value of the constitution, the
  experience of each rank of the weapon masteries and of the trade skills, and the nodes of
  the weapon trees (protocol notes 4.12 and 4.13). It uses the same sources as the item
  catalog tool. Without the file the backend starts, and a character keeps the level and
  the skills that it has: the backend gives no level, no attribute point, and no mastery
  point.
- `tools/gamedata/creatures.sh` makes the creature data
  (`~/.local/share/soulwarden/gamedata/creatures.json`): the usual spawn points of the creatures
  in 800 m around the start place of a new character (1,029 spawn points of 114 kinds in the
  supported build; `--center X Y` and `--radius METERS` select a different area), and for
  each kind its slice, the slots of its components, its vitals ids, its health, its name, and
  its action table (protocol notes 4.14). The pak entries that it reads have Oodle data: set
  `SOULWARDEN_OODLE_LIB` to an Oodle library of your computer (a `liboo2core.so`), or
  `SOULWARDEN_OODLE_DLL` to an Oodle library for Windows (the helper of
  `tools/research/oodle_loader.c`). `tools/gamedata/creatures.sh --find TEXT` prints the
  kinds with a text. Without the file the backend starts, and the world has no creature.
- `tools/gamedata/combat.sh` makes the combat tables
  (`~/.local/share/soulwarden/gamedata/combat-tables.json`): the weapon of each item (base damage,
  attribute factors, the damage rows of its light and of its heavy attack), the damage rows,
  the damage values of the attributes, the level and gear score values, and the defense
  values of each creature (protocol notes 4.15). It reads the data tables and the base
  attributes file of the game copy, from the same sources as the creature data tool.
  `tools/gamedata/combat.sh --find TEXT` prints the weapons and the creatures with a text.
  Without the file the backend starts, and an attack has no effect.
- Creatures. With the creature data, the creatures of the start area stand at their spawn
  points outside the settlement: rabbits, wolves, Risen, skeletons, sheep, and others. Each
  one has its body, its idle animation, its name plate with its level, and its health bar.
  The world makes the creature of a spawn point when your character comes near it (150 m),
  and removes it when no character is near (200 m). A creature does not move and does not
  attack yet. The tab "Creatures" of the tool window
  (F8) lists the creatures near the character, and it can make a creature of a kind, set
  its health, kill it, and remove it; `tools/world creatures`, `kinds`, and `creature` do the
  same in a development run. A creature that you killed or removed does not come back by
  itself (the respawn timer is later work): the button "Reset" makes its spawn point free.
- Melee attacks. With the combat tables, a melee attack of your character that touches a
  creature lowers its health: a click of the left pointer button is a light attack, and a
  hold is a heavy attack. The damage number shows at the creature, the creature plays its
  reaction to the hit, and it dies at the health 0 (the body falls, and its name plate goes).
  This works with a sword, a great axe, a hatchet, a war hammer, and a spear; the keys 1 and
  2 select the weapon. The backend computes the damage from the weapon, its gear score, the
  attack, the attributes and the level of the character, and the defense values of the
  creature. The client finds each contact itself and reports it, and the backend checks each
  report (the creature, the distance, the weapon, the time of the attack): the rules are
  rules of this project (protocol notes 4.15). Not yet: ranged weapons and magic, abilities,
  critical hits, blocks, damage to your character, loot, and experience.
- The bag (Tab key) shows the items and the coin of the character. The tool window (F8)
  gives and removes items, and `tools/world give` gives items in a development run. The backend handles the drop request of the client (drag an
  item to the area "Drop items": the item is gone, the world has no items on the ground yet;
  a locked item stays) and its lock request (click an item, then "Lock").
- Level, attributes, and skills. The character window (K key) shows the level, the
  experience, and the attributes. The buttons "+" and "Assign" spend free attribute points,
  and "Respec" makes them free again. The tab "Weapon Mastery" shows the level of each
  weapon; in the tree of a weapon, a click on a node and "Assign" spend a mastery point, and
  an active ability goes to the ability bar of the HUD. The tab "Trade Skills" shows the
  level of each trade skill. A character starts with level 1 and no skill, and nothing in
  the game gives experience yet: `tools/world level`, `xp`, `attribute`, `mastery`,
  `tradeskill`, and `skillxp` set the values in a development run, and the tool window (F8)
  sets them in each start of the game. The largest level is 60, and the largest trade skill level
  is 200. Not there yet: the use of an ability, the effects of an attribute other than the
  health, and the reset of a weapon tree from mastery level 11 on (it has a cost in a
  currency that the backend does not have).
- Equipment. The inventory screen has the equipment panel. The entry "Equip" of the action
  menu of an item (or a double click) puts it into its slot, the entry "Unequip" of an
  equipped item puts it back into the bag, and a drag between two weapon slots changes
  their items. Armor shows on the body, a weapon hangs on the hip or is in the hand, and the
  HUD shows the active weapon. Not there yet: the draw and the sheathe as an action of the
  server, the level and the attribute rules of an item, dyes and
  skins, and the body of the character in the main menu after a change of equipment (the
  menu shows the clothing of the creation).
- `SOULWARDEN_SEED_CHARACTER` (development aid) still gives an account with no character a
  character with no creation data. The backend gives it the default body in the menu and in
  the world (male, no clothing). Without the appearance tables its body is white in the menu.
- The backend has no movement simulation and no collision data of the world. It checks the
  speed of a reported position, and it cannot find a position in a wall or below the ground.
  A character on a mount will need a larger speed limit. With `SOULWARDEN_EXACT_POSITION=0`, and
  after a game update that changes the client code, the backend estimates the position from
  the camera (section "Exact position"; protocol notes 4.7 and 5).
- A character name has 3 to 15 characters: letters (also with accents), 6 digits at most,
  and the space, `'`, `,`, `-`, `.` (the name rule of the client). The backend refuses a
  different name.
