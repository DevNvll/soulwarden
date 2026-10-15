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
listener): the state of the characters of the account of the game, an item search, the
kinds of the creatures, the travel places, and commands for the items, the coin, the level,
the attributes, the skills, the nodes and the ability bars (`assign_node`,
`set_ability_bar`), the cooldowns (`reset_cooldowns`), the status effects (`apply_status`,
`remove_status`), and the health of a character, for the
creatures near it, and for a travel.
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
tools/gamedata/combat.sh           # one time: combat tables (without them an attack of your character has no effect; from stage 3.5a also the weapon mastery of each weapon)
tools/gamedata/loot.sh             # one time: loot tables and kill rewards (without them a kill gives nothing); 10 s
tools/gamedata/abilities.sh        # one time: ability data (cooldowns, status effects, spells, costs; without it an ability does nothing)
tools/gamedata/perks.sh            # one time: perk data (perks, buckets, item perk places, consumables, equip load; without it an item has no perk)
tools/gamedata/places.sh           # one time: travel places (without them the travel list is empty); needs an Oodle library
tools/gamedata/heights.sh          # one time: terrain heights (without them and with no navigation data no creature leaves its place); 136 MB, 2 minutes
tools/gamedata/navigation.sh       # one time: navigation data of the world (the paths of the creatures); 1.2 GB, 45 minutes; needs an Oodle library and a C and a C++ compiler
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
world changes its rows in the database. For a fight: `ignore CHARACTER on|off` (the
creatures ignore a character), `heal CHARACTER` (its full health; a dead character lives
again at its place), `hurt CHARACTER AMOUNT` (its health goes down), and `message CHARACTER
TYPE HEX` (one message with bytes of the caller to the client of a character). For the
abilities (stage 3.3a): `node CHARACTER WEAPON NODE...` (assign nodes of a weapon tree, one
mastery point each), `bar CHARACTER WEAPON SLOT ABILITY` (an ability on the place 0, 1, or 2
of the bar of a weapon), `cooldowns CHARACTER [reset]` (the cooldowns that run; reset: each
one ends), and `creature effect CREATURE STATUS [SECONDS]` (a status effect of the ability
data on a creature, or `knockdown` or `stagger`); `list` has the line `abilities`. For the
status effects of a character (stage 3.3b-S1): `status CHARACTER` (each status with its time,
stacks, potency, and source, the speed and armor parts, the counts), `status CHARACTER STATUS
[SECONDS [COUNT]]` (a status row of the ability data; 0 seconds: the duration of the row), and
`status CHARACTER remove STATUS|all`.
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
| Items | Type a text in the search field to find items of the catalog (the text is compared with the id, the type, and the class of an item, for example `sword`, `potionhealth`, `ore`, `consumable`). Click an item, set the quantity, and press "Give": the item is in the bag. "Gear score" (0: the value of the item table) and "Perks" (up to five PerkID texts with "," between them; an empty part is a place with no perk; a gem is a perk) give an item with perks (stage 1.3b): the backend refuses a perk that is not for the item, and it changes a perk as the client does (the perk swap of the game: the Soldier perk of a weapon becomes "Acutely Empowered"). The list "Bag" has each item of the bag with a "Remove" button. |
| Character | Set the level, give experience, set the coin, and set the value of each attribute (the character must have the free points for it). The row "Health" shows the health of your character: "Heal" gives it its full health, and for a dead character the button has the text "Revive" (the character lives again at its place, and the death screen closes). With the mark "Creatures ignore this character", no creature takes your character as its target, and a kill gives it nothing; the mark "Each kill drops loot and coin" makes each kill of your character drop loot and coin (the loot switch, stage 3.5a). The two values stay until the backend stops. |
| Skills | Set the mastery level of each weapon (each level above 1 gives one mastery point for its tree) and the level of each trade skill. |
| Creatures | The list at the bottom has each creature that is 150 m or less from your character, the nearest one first, with its level, its health, and its distance. "Set" sets the health of a creature to the percent value above the list, "Kill" kills it, and "Remove" takes it out of the world. To make a creature, type a text in the search field (the id, the name, or the family of a kind, for example `wolf`, `risen`, `rabbit`), click a kind, set the level and the distance, and press "Spawn": the creature is that many meters to the north of your character (the window covers the left half of the picture: press F8 to see the creature). "Reset" makes each spawn point free again whose creature you killed or removed. |
| Travel | The first line has the position of your character (meters: east, north, up). The list has each spirit shrine and each inn of the world with its territory and its distance from your character. Type a text in the filter (a part of a name or of a territory, `shrine`, or `inn`), click a place, and press "Travel": your character is at that place, with no new login. A place that is more than 100 m away shows the loading screen of the game for some seconds. The form below the list moves your character to a position in numbers ("Here" puts the position of your character into the form): give a height on the ground or above it. Your character and the camera look north after a travel. |

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
| `SOULWARDEN_CREATURE_BEHAVIOR=0` | The behavior of the creatures is off for the start (backend option `--no-creature-behavior`): the creatures stand at their spawn points, do not see your character, and do not attack. The tab "Character" of the tool window has a line that says so. |
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
  of the whole world (32,840 spawn points of 3,848 kinds in the 25 regions of the supported
  build, a file of 8.8 MB; `--radius METERS` with `--center X Y` writes a smaller file with
  the spawn points of a circle), and for each kind its slice, the slots of its components,
  its vitals ids, its health, its name, and its action table (protocol notes 4.14). From
  stage 3.4b the file (9.4 MB) also has what the behavior of a creature needs (protocol
  notes 4.17): the vision, the leash, the speeds, the animation tags, and the attacks of
  each model, the damage of each kind and of each level, and the terrain height at each
  spawn point. From stage 3.4e the file (12.4 MB) also has the patrol route of each spawn
  point that has one (19,814 spawn points) and the walk speed and the wander values of each
  model (protocol notes 4.14). With a file from before, the creatures stand at their spawn
  points: run the tool again. `--no-attack FILE` writes one line for each kind that does not attack (859
  kinds). The tool
  leaves out the spawn points of 50 creature models that the client does not show
  correctly with this backend (1,541 spawn points: the list is in
  `tools/gamedata/creature_slices.json`), and `--report FILE` writes one line for each entry
  that is left out. The pak entries that it reads have Oodle data: set
  `SOULWARDEN_OODLE_LIB` to an Oodle library of your computer (a `liboo2core.so`), or
  `SOULWARDEN_OODLE_DLL` to an Oodle library for Windows (the helper of
  `tools/research/oodle_loader.c`). The tool needs about 1 minute.
  `tools/gamedata/creatures.sh --find TEXT` prints the kinds with a text. Without the file
  the backend starts, and the world has no creature.
- `tools/gamedata/heights.sh` makes the terrain heights
  (`~/.local/share/soulwarden/gamedata/heights`, one file for each of the 49 regions of the
  world, 136 MB): the height of the terrain at each meter, from the height maps of the game
  copy (protocol notes 4.17). The tool needs about 2 minutes and no Oodle library. The
  backend reads only the parts of the files that it needs. `tools/gamedata/heights.sh
  --height X Y` prints the height of a position. Without the files and with no navigation
  data the backend starts, and no creature leaves its place: it attacks only a character
  that is in its reach. From stage 3.4d the navigation tool makes this folder again at its
  end, from the terrain that the client has (the height map with the changes near some
  placements, protocol notes 4.19).
- `tools/gamedata/navigation.sh` makes the navigation data
  (`~/.local/share/soulwarden/gamedata/navigation`, one file for each of the 49 regions and
  `navigation.json`; 1.2 GB): where a creature can stand (the terrain, the floors of
  buildings, bridges, stairs, and rocks, shallow water), and what stops its sight, in cells
  of 0.25 m (protocol notes 4.19). The tool reads the collision shapes, the terrain, the
  water, and the marks of the level designers from the pak files of the game copy. It needs
  an Oodle library (`SOULWARDEN_OODLE_LIB`), Python 3, and a C and a C++ compiler (clang or
  gcc), and about 45 minutes with 8 processes (`--jobs N`; the largest process needs 1.7
  GB). Its work files (2.1 GB) are in `~/Games/soulwarden/work/navigation-build`
  (`SOULWARDEN_NAVIGATION_WORK`). A second run makes only the regions that are not complete
  (`--force` makes each one again, `--region NAME` some of them). `--spawn-points
  creatures.json REPORT_DIR` writes which spawn points of the creature data are on a free
  floor. The backend reads only the tiles of 64 m near the characters, with a thread of its
  own. Without the folder the backend starts, and a creature goes on the straight line on
  the terrain and sees through walls.
- `tools/gamedata/places.sh` makes the travel places
  (`~/.local/share/soulwarden/gamedata/travel-places.json`): the spirit shrines and the inns of
  each territory with their names and positions (161 places in 16 territories in the
  supported build; protocol notes 4.16). It reads the region files and the text files of
  the game copy, with the same Oodle sources as the creature data tool.
  `tools/gamedata/places.sh --list` prints the places and writes no file. Without the file
  the backend starts, the travel list is empty, and a travel takes only a position in
  numbers.
- `tools/gamedata/combat.sh` makes the combat tables
  (`~/.local/share/soulwarden/gamedata/combat-tables.json`): the weapon of each item (base damage,
  attribute factors, the damage rows of its light and of its heavy attack), the damage rows,
  the damage values of the attributes, the level and gear score values, the defense
  values of each creature (protocol notes 4.15), and, from stage 3.4b, the armor values of
  each armor item and the damage values of the level differences (protocol notes 4.18:
  with a file from before, your armor does not lower the damage of a creature), and, from
  stage 3.5a, the weapon mastery of each weapon (with a file from before, a kill gives no
  mastery experience: run the tool again). It reads the data tables and the base
  attributes file of the game copy, from the same sources as the creature data tool.
  `tools/gamedata/combat.sh --find TEXT` prints the weapons and the creatures with a text.
  Without the file the backend starts, and an attack has no effect.
- `tools/gamedata/loot.sh` makes the loot tables
  (`~/.local/share/soulwarden/gamedata/loot-tables.json`, 1.8 MB, 10 s): the experience and the
  coin of a kill of each creature level, the reward factors of each creature type, the level
  differences, the credit data of the game, the loot data of each creature (may it drop
  loot, its chance, its loot table, its tags), the 108 loot tables and the 85 loot buckets
  that a creature can reach, and the gear score ranges of each level (protocol notes 4.21).
  It reads the data tables and the base attributes file of the game copy, from the same
  sources as the creature data tool (the extracted files of the work folder are sufficient
  for it, and `SOULWARDEN_OODLE_LIB` reads the pak files). `tools/gamedata/loot.sh --find TEXT` prints the
  loot data of the creatures and of the tables with a text. Without the file the backend
  starts, and a kill gives nothing. The tool writes no file (and keeps the file that is
  there) when the tables have a reference that does not resolve or a value that the backend
  refuses (protocol notes 4.21).
- `tools/gamedata/abilities.sh` makes the ability data
  (`~/.local/share/soulwarden/gamedata/ability-data.json`, 0.4 MB): the cooldown of each
  ability, the values of each status effect (its time, the stun, the root, the slow, the
  weaken, its icon), and the shape of each spell (protocol notes 4.22). It reads the data
  tables of the game copy, from the same sources as the creature data tool.
  `tools/gamedata/abilities.sh --find TEXT` prints the rows with a text. Without the file the
  backend starts, and an ability does nothing. The creature data tool (stage 3.3a) also
  writes the reaction fragments of each creature model: with a file from before, a
  knockdown shows the small hit reaction. From stage 1.3b the file (format 2, 2 MB) also has
  the cells of each status effect and spell and the cost tables of the players. From stage
  3.3b-S1 the file (format 3, 2.2 MB) also has the status categories with their limits and the
  ability rows that put a status on the caster: with a file of format 2 the limits and the
  caster effects (Safety Measures) do nothing.
- `tools/gamedata/perks.sh` makes the perk data (`~/.local/share/soulwarden/gamedata/perk-data.json`,
  6.5 MB, stage 1.3b): the perk list in the order of the client, the values of each perk, the
  perk buckets and the perk places of each item (the perk roll of a loot item), the
  consumables with their level, the equip load of each armor item, the perk swap table, and the heart runes
  (protocol notes 4.23). It reads the data tables and the base attributes file of the game
  copy, from the same sources as the creature data tool. `tools/gamedata/perks.sh --find TEXT`
  prints the perks, the buckets, and the items with a text. Without the file the backend
  starts, an item has no perk, and a consumable and the heart rune do nothing. From stage
  3.3b-S1 the backend reads only format 2 (with the level of each consumable): with a file of
  format 1 it has no perk data, and the log has the cause. Make the
  combat tables, the creature data, and the progression tables of stage 1.3b with it (the rune
  charge of a hit, the mana regeneration).
- Creatures. With the creature data, the creatures of each region of the world stand at
  their spawn points: rabbits, wolves, Risen, skeletons, sheep, and many others. Each
  one has its body, its idle animation, its name plate with its level, and its health bar.
  The world makes the creature of a spawn point when your character comes near it (150 m),
  and removes it when no character is near (200 m). Some creatures are not in the world yet (the corruption swarmers, the sulfur
  lizards, the chameleons, and others): the client shows no body for them with this
  backend.
- Fights. With the creature data of stage 3.4b and the terrain heights, a creature sees your
  character (the vision distance and angle of its model: 12 m and 75 degrees to each side
  for a wolf), runs to it, and attacks it. A creature that you hit attacks you too. It goes
  back to its place when you are dead or too far, or at the end of its leash (70 m for a
  wolf), and it has its full health there. The rules are rules of this project (protocol
  notes 4.17): the game files have the values of a creature and not its behavior. 859 of
  the 3,848 kinds have no attack that the tool can use: they do not attack you, and a kind
  of them with a patrol route or wander values patrols or wanders as each other creature.
- Paths. With the navigation data of stage 3.4d (protocol notes 4.19) a creature goes around
  walls and rocks, up stairs, on the floors of buildings and ruins, and through shallow
  water, and it does not see you through a wall. A creature with no way to you loses you
  and goes back. No creature comes into a settlement: its gates are closed for creatures,
  as in the game. Not yet: jumps and drops from an edge, swimming and flying creatures,
  doors and gates of a fort (they are not in the world of this backend), groups, ranged
  attacks, magic, and abilities.
- Patrol and wander (stage 3.4e, protocol notes 4.17). Near your character a creature walks
  its patrol route from the game files (the wolves near the start place walk four waypoints
  around their place and wait at each one), or it wanders near its place. A creature that
  sees you leaves its patrol and fights; at the end of its leash (from the place where it saw
  you) it runs back. On that way back it is immune: your hits do nothing, and it does not
  turn back to you. At that place it has its full health again, and it goes on with its
  patrol. A creature that cannot get to you (you stand on a high rock or a ledge) goes to the
  place nearest to you, attacks you from there when you are in its reach, waits 12 s, and
  then goes back. The game shows no "immune" text for a creature on its way back.
- The animation of a creature (user test 1, protocol notes 4.14): a creature that walks or
  runs plays the locomotion of its model (the server sends the action `Move_Combat` at the
  start of each way), an attack replaces it, and at its place the creature plays its idle
  action. Before, the creatures slid in their pose.
- People and lore pages (protocol notes 4.20): you can talk to a person (E) and read a lore
  page. A person says only the first line of its default text, and nothing in the game
  changes. When the talk ends, your character can move at once; a travel and a respawn also
  end each lock of your input.
- The blue bar below the crosshair is the mana meter of the game: it shows only when your
  mana is not full, as in the game (the backend sends the mana state from user test 1).
- Fights that you can play (stage 3.4d): no creature attacks you for 10 s after you came
  into the world, travelled, or respawned, unless you hit it. Out of a fight (6 s with no
  damage) your health comes back, 3 percent each second. A creature makes its strong attack
  (the lunge of a wolf) after three light attacks, with a pause of 1.5 s to 2.5 s after each
  attack, and creatures of one fight do not stand at one place.
- Health and death. An attack of a creature lowers the health of your character: the HUD
  shows the new value, a red mark shows the direction of the attacker, and a red number
  shows the damage. The backend computes the damage from the level and the kind of the
  creature, the attack, the level difference, and your armor (protocol notes 4.18). At the
  health 0 your character dies: the client shows "You died" for 5 s and then its respawn
  screen. The button "Respawn" puts your character at the nearest spirit shrine or inn
  with its full health (with no travel places: at the place of its death). A character is
  never dead after a login. Not yet: health regeneration, food and potions, blocks,
  dodges, critical hits, status effects, and the respawn points of the game (camps,
  houses).
- Travel. The tab "Travel" of the tool window (F8) moves your character to a spirit shrine
  or to an inn of each territory, or to a position in numbers; `tools/world places` and
  `tools/world travel` do the same in a development run. The session continues: there is
  no new login. This is a command of this project, not the fast travel of the game: the
  interaction "Fast Travel" of a shrine has no function yet. The tab "Creatures" of the tool window
  (F8) lists the creatures near the character, and it can make a creature of a kind, set
  its health, kill it, and remove it; `tools/world creatures`, `kinds`, and `creature` do the
  same in a development run. A creature of a spawn point that you killed comes back at its
  respawn time (6 minutes for most spawners, protocol notes 4.21) when you are 150 m or less
  from its point; a creature that you removed does not come back by itself: the button
  "Reset" makes each spawn point that waits free at once.
- Melee attacks. With the combat tables, a melee attack of your character that touches a
  creature lowers its health: a click of the left pointer button is a light attack, and a
  hold is a heavy attack. The damage number shows at the creature, the creature plays its
  reaction to the hit, and it dies at the health 0 (the body falls, and its name plate goes).
  This works with a sword, a great axe, a hatchet, a war hammer, and a spear; the keys 1 and
  2 select the weapon. The backend computes the damage from the weapon, its gear score, the
  attack, the attributes and the level of the character, and the defense values of the
  creature. The client finds each contact itself and reports it, and the backend checks each
  report (the creature, the distance, the weapon, the time of the attack): the rules are
  rules of this project (protocol notes 4.15). Not yet: ranged weapons and magic, critical
  hits, and blocks.
- Abilities (stage 3.3a, protocol notes 4.22, `docs/systems/abilities.md`). With the ability
  data, the keys Q, R, and F use the three abilities of the bar of the weapon in your hand:
  Charge, Maelstrom, and Gravity Well of the great axe, and Wrecking Ball, Shockwave, and Path
  of Destiny of the war hammer. A hit of an ability shows its damage number. Shockwave stuns
  the creatures around you, Path of Destiny hits the creatures on a line in front of you,
  Wrecking Ball knocks a creature down, and Gravity Well throws a vortex that holds the
  creatures in it and slows them, with a burst 3 s later. The button of the ability on the
  HUD shows its cooldown (the time of the game data); a press in the cooldown does nothing.
  The upgrades of the tree change the abilities as in the game (Storm's Reach, No Reprieve,
  Gravity, Unyielding, Crowded Well, Breathing Room, Meteoric Crater, Frailty, Seismic Waves,
  Stimulated Reduction, Aftershock). A stunned or rooted creature does not move, a slowed
  creature runs slower, and the icon of the effect shows at its name plate. A creature on its
  way back takes no effect. An ability has no cost. Not yet: the abilities of the other
  weapons, a picture of the vortex, the pull of Maelstrom and of the vortex, the passive
  nodes and the effects on your own character (fortify, grit, empower, heal), and the perks
  of an item that change an ability.
- Rewards of a kill. With the loot tables of stage 3.5a (`tools/gamedata/loot.sh`; protocol
  notes 4.21), a creature that your hits killed gives your character its experience (the
  experience table of the game with the level of the creature, its type, and the level
  difference), the same amount to the weapon mastery of the weapon in your hand, sometimes
  coin (5 percent of the kills for most creatures), and sometimes items (a wolf gives loot in
  about 1 of 10 kills; then the loot tables of the game decide the item). The HUD shows one
  line for each change ("+27 XP", "+27 War Hammer Mastery", "+4.80 Coin", the name of the
  item), and the items are in the bag (Tab key) with a gear score of the range of the level.
  Your character gets the rewards when all its hits on the creature did 1 percent or more of
  its maximum health, its last hit was 120 s or less before the death, it lives, and it is
  200 m or less from the creature; with more characters, each one gets less experience. A creature that the health command or the
  kill command killed gives nothing. The mark "Each kill drops loot and coin" of the tab
  "Character" of the tool window (F8) makes each kill of your character drop loot and coin.
  Not yet: the perks and the rarity of an item instance (an item shows only the perks of its
  definition), items on the ground, and loot for a group.
- The bag (Tab key) shows the items and the coin of the character. The tool window (F8)
  gives and removes items, and `tools/world give` gives items in a development run. The backend handles the drop request of the client (drag an
  item to the area "Drop items": the item is gone, the world has no items on the ground yet;
  a locked item stays) and its lock request (click an item, then "Lock").
- Level, attributes, and skills. The character window (K key) shows the level, the
  experience, and the attributes. The buttons "+" and "Assign" spend free attribute points,
  and "Respec" makes them free again. The tab "Weapon Mastery" shows the level of each
  weapon; in the tree of a weapon, a click on a node and "Assign" spend a mastery point, and
  an active ability goes to the ability bar of the HUD. The tab "Trade Skills" shows the
  level of each trade skill. A character starts with level 1 and no skill. A kill gives
  experience and weapon mastery experience (stage 3.5a); `tools/world level`, `xp`,
  `attribute`, `mastery`, `tradeskill`, and `skillxp` set the values in a development run,
  and the tool window (F8) sets them in each start of the game. The largest level is 60, and the largest trade skill level
  is 200. Not there yet: the abilities of the weapons other than the great axe and the war
  hammer, the effects of an attribute other than the
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
