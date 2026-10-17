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

The client asks for the use of an object of the world (the key E or T at a lore page or a
person) in its input report. The interaction system of the world checks the request against
the interaction data of the game (`interaction-data.json`: the object, its option, the 3D
range, its limits) and the session replies (accepted or refused). A read lore page is a row of
the database: the journal of the character keeps it. The state of an object (on or off, its
users) goes to each session near it as the record of the level object of the client
([Interactions](systems/interactions.md), protocol notes 4.35). Stage 2.5b: the world makes the
crafting stations of each settlement near a character as server entities (the client shows a
station only after its record), keeps the facts of a use of a station for the crafting stage
(the Assembly key, the fee flag, the parent settlement), counts a timed use on the world clock,
and ends a use when its source goes away, when the character is out of its range, or at damage
for an animation or a timed use (protocol notes 4.36).

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
`remove_status`), the cast of an ability and the spell entities (`cast_ability`,
`end_spells`), a shot of the weapon in the hand (`shoot`), and the health of a character, for the
creatures near it, and for a travel, and the interaction commands (`set_object_enabled` for an object 30 m or less from the
character, `cancel_interaction`; stage 2.5a), `clear_chest_claim` (stage 3.5b.1), and `set_world_time` (stage 2.2: the time of the
day of the whole world). The state of a character has its DLC (member
`ownership`, stage 1.1b), its interactions (member `interactions`: its committed interaction and the objects near it, stage 2.5a), and
its chests (member `chests`: its open, its loot claims, and the chests near it, stage 3.5b.1), and its loot bags (member `lootBags`:
its open bag and its bags, stage 3.5b.2), and the world time (member `worldTime`, stage 2.2). The backend
also answers the entitlement service of the client (the sync and the list of the entitlements of the account, protocol
notes 2 and 4.29).
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
| `SOULWARDEN_GAMEDATA_DIR` | path | Tables that a tool makes from the game copy (default `~/.local/share/soulwarden/gamedata`; the launcher of a release uses the copy in its release). See "Characters". |
| `SOULWARDEN_RELEASE_ROOT` | path | The folder of the releases of `tools/release.sh` (default `~/Games/soulwarden/releases`). |
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
tools/gamedata/combat.sh           # one time: combat tables (without them an attack of your character has no effect; from stage 3.5a also the weapon mastery of each weapon; from stage 3.3b-S4 the pull and the push of each damage row)
tools/gamedata/loot.sh             # one time: loot tables and kill rewards (without them a kill gives nothing); 10 s
tools/gamedata/abilities.sh        # one time: ability data (cooldowns, status effects, spells, costs; without it an ability does nothing)
tools/gamedata/spells.sh           # one time: spell entity data (slices, slots, delayed events; without it the vortex has no picture)
tools/gamedata/projectiles.sh      # one time: projectile data (profiles, slices, ammunition, weapons, shots; without it a shot does nothing); needs an Oodle library
tools/gamedata/weapon-actions.sh   # one time: the attack graphs of the melee weapons (without them a melee hit has the light or the heavy row of its weapon); needs an Oodle library
tools/gamedata/perks.sh            # one time: perk data (perks, buckets, item perk places, consumables, equip load; without it an item has no perk)
tools/gamedata/places.sh           # one time: travel places (without them the travel list is empty); needs an Oodle library
tools/gamedata/interactions.sh     # one time: the interaction objects of the world (without them each use of an object is refused); 24 MB, 4 minutes; needs an Oodle library
tools/gamedata/chests.sh           # one time: the chests of the world (without them no chest opens); 3.6 MB; needs an Oodle library
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

`tools/release.sh` makes a release: a fixed copy of the launcher of the commit, the products of
`apps/*/dist`, and the game data folder in `~/Games/soulwarden/releases/VERSION`. The link
`~/Games/soulwarden/releases/current` names the current release. `tools/release.sh status` lists
the releases, `rollback` goes back to the release before the current one, and `activate VERSION`
selects a release. While the game runs, the command does not change the current release. The
details are in [Build and test](development/build-and-test.md), section "Releases".

`tools/steam-shortcut.py add` puts the launcher of the current release in the Steam library as
a non-Steam game, and the launcher of the working copy when there is no release (`--launcher
release` or `--launcher working-copy` selects one; `show` lists the entries, `remove` removes the
entry). Do it with Steam closed, or start
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
world changes its rows in the database. For the objects of the world (stage 2.5a):
`interactions CHARACTER [METERS]`, `object CHARACTER KEY on|off`, and `interaction cancel
CHARACTER`; stage 2.5b: `fixture CHARACTER NAME [DX DY DZ] [--yaw D] [--permission N]` (a
fixture near a character: `powderKeg`, the timed option Trigger; `campT5`; `camp`) and
`fixture remove CHARACTER ENTITY`; stage 3.5b.1: `chests CHARACTER [METERS]` (the open, the loot claims, and the chests near a
character), `chest clear CHARACTER KEY` (remove a loot claim: the chest opens again), and `chest reset CHARACTER KEY SECONDS` (a
development aid: a looted claim resets after that time, not at the time of the data); stage 3.5b.2: `bags CHARACTER` (the open bag
and each bag with a part of the character, with its items and its end). For a fight: `ignore CHARACTER on|off` (the
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
`status CHARACTER remove STATUS|all`. For the spell entities (stage 3.3b-S2a): `cast CHARACTER
PLACE [--yaw DEGREES]` (the press of a key of the bar for a character, also for a remote test
character with no client: the server throws the projectile of Gravity Well in the direction
of the yaw; from stage 3.3b-S4 the dash of Charge of such a character: 13.77 m in 1 s; from
stage 3.3b-S2b the toss of the trap and the projectile of the Beacon of such a character),
`spells` (the spell entities of the world with their age; from stage 3.3b-S2b also the role,
the health, the attachment, and the friends in an aura), `spells end CHARACTER` (each spell
entity of a character ends), `spells hurt ENTITY AMOUNT` (stage 3.3b-S2b: a Pylon or a trap
takes damage; the health 0 starts its death chain), and `probe CHARACTER ASSET [DX DY DZ]
[--sub N] [--radius M] [--fragment SLOT:TYPE:HEX]...`, `probe fragment ENTITY SLOT TYPE HEX`,
and `probe remove ENTITY` (an entity of a slice with the fragments of the caller, for a live
check of a record before its code). For the forced movement (stage 3.3b-S4): `list` has the
line `force` (the creatures that a hit moves now, the movements, the steps, the stops at a
wall or a drop) and the counters of Riposte in the line `abilities`, and `creatures` marks a
creature that a hit moves now with `MOVED BY A HIT (ROW)`. For the projectiles (stage
3.3b-S3): `shoot CHARACTER [--yaw DEGREES] [--pitch DEGREES] [--heavy]` (a shot of the server
with the weapon in the hand of a character, also with no client), `projectiles` (the
projectiles in the air, the shots by verdict, the ends by cause), and the line `projectiles`
of `list`.
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
| World | The time of the world (stage 2.2), its phase (night, dawn, day, dusk), and the time to the next phase. Set the hour and the minute and press "Set", or press 06:00, 12:00, 19:00, or 00:00: the time of the day changes for the whole world (each player), not only for your character. The sky changes at once. |

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

## World time

The world has one day and night cycle (stage 2.2, [World time and weather](systems/world-time-and-weather.md)): 30 minutes of
real time from 06:00 to 19:00 and 15 minutes from 19:00 to 06:00. The game client shows the sky, the light, and the shadows of
the time of the server; the backend sends the time when your character enters the world and when the time of the world changes.

- A new world starts at noon. The backend keeps the time in its database (each minute and at its stop): the next start goes on
  from that time. The time does not move while the backend is stopped, and it moves while the backend runs, also with no player.
- The phases: night (19:00 to 06:00), dawn (06:00 to about 06:26), day, dusk (about 18:34 to 19:00). Some creatures of the world
  come only in some phases (405 spawn points, for example night creatures), and they go away at the end of their phases.
- The rain, the snow, the fog, and the light of an interior are local data of the level: they show at their places with the time
  of the world. Some interiors show their own hour (the keep of Dunwood is dark at noon).
- Set the time with the tab "World" of the tool window. In a development run: `tools/world time` and `tools/world time
  CHARACTER HH:MM`.

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
  the weapon trees (protocol notes 4.12 and 4.13), and from stage 1.1b (format 2) the rows of the expansion table, the
  entitlements of the DLC with their bits, and the largest rank of each skill with the expansion (protocol notes 4.29). It
  uses the same sources as the item catalog tool. Without the file the backend starts, and a character keeps the level and
  the skills that it has: the backend gives no level, no attribute point, no mastery point, and no DLC.
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
  points: run the tool again. From stage 3.4f-A the file (12.7 MB) also has the ranged attacks of
  each model (225 attacks of 95 models; the backend uses the basic shot FireBow of 20 archer
  models), the 69 projectile slices, and the capsule of a player (protocol notes 4.37): with a file
  from before, no creature shoots. From stage 3.4f-C the file also has the area attacks of each model
  (11 attacks of 9 models with 6 spells: the list `areaSpells` of `tools/gamedata/creature_slices.json`; with a file from before
  no creature casts an area); from stage 3.4f-D 24 attacks of 22 models with 10 spells, the status actions of 3 models (the self heal
  of the Inquisitor Cleric, the Howl of the Evil Knight hound: the list `statusActions`), and the low-health part of 119 models (with a
  file from before no creature heals or buffs itself). From stage 2.2 (format 4, 13.1 MB) each spawn point also has the time
  constraint of its spawner: 405 points make their creature only in some phases of the day, and their creature goes away at the end
  of them (with a file from before, each creature comes at each time). `--no-attack FILE` writes one line for each kind that does not attack (747
  kinds from stage 3.4f-D, 789 in stage 3.4f-C: a kind with the basic shot of an archer, a cast, or an area attack attacks). The tool
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
- `tools/gamedata/interactions.sh` makes the interaction objects
  (`~/.local/share/soulwarden/gamedata/interaction-data.json`, stage 2.5a; format 2 from stage
  2.5b): each placement of the level data whose slice has an Interact component (43,225 objects
  in the supported build: 1,157 lore pages, 146 persons, 40,768 gathering nodes, 136 shrines),
  with its ids on the wire, its place, its options with the rows of the user interface database,
  and the achievement of each lore page; the server children of the placements (425: the 176
  stations of the settlements and 249 camps) with the templates of their slices (the slots, the
  Assembly key and fee flag, the forge child); and the development fixtures (protocol notes 4.35,
  4.36, [Interactions](systems/interactions.md)). `--region NAME` and `--radius METERS` write a
  smaller file, `--list KIND` prints the objects of a kind (`--list spawned`: the server
  children). Without the file the backend starts, makes no station, and refuses each use of an
  object: a talk with a person then closes at once, and a read lore page is not kept. From stage
  3.4f-A the tool stops (and keeps the file that is there) for a placement of a named object with
  no complete position, for a station or a forge child whose slice asset the catalog does not
  have, and for one placement id in two spellings; the loader of the backend refuses a template
  with two components at one slot (code review of stage 2.5b).
- `tools/gamedata/combat.sh` makes the combat tables
  (`~/.local/share/soulwarden/gamedata/combat-tables.json`): the weapon of each item (base damage,
  attribute factors, the damage rows of its light and of its heavy attack), the damage rows,
  the damage values of the attributes, the level and gear score values, the defense
  values of each creature (protocol notes 4.15), and, from stage 3.4b, the armor values of
  each armor item and the damage values of the level differences (protocol notes 4.18:
  with a file from before, your armor does not lower the damage of a creature), and, from
  stage 3.5a, the weapon mastery of each weapon (with a file from before, a kill gives no
  mastery experience: run the tool again), and, from stage 3.3b-S4, the impact of each damage
  row with an impact distance (the member `impacts`, 234 rows: the pull and the push of a hit,
  protocol notes 4.26; with a file from before, a hit moves no creature: run the tool again).
  It reads the data tables and the base
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
  caster effects (Safety Measures) do nothing. From stage 3.3b-S2a the file also has the
  limits of the stat multipliers (`statMultipliers`, StatMultiplierData): with an older file
  the speed of a haste has the limit of the client, 1.5 times (from stage 3.3b-S4; before it
  the speed had no upper limit). From stage 3.3b-S4 the tool stops when an ability table of
  the players does not have a rule column of the backend, or a status table does not have the
  absorption and damage columns of each damage type (it keeps the file that is there). From stage
  3.3b-S5 the file (format 4, 2.5 MB) has each row of the ability tables of the players (the
  passive rules of the nodes and of the perks) and the spell selections of the action lists:
  with a file of format 3 most nodes do nothing, and Shockwave, Path of Destiny, Gravity Well,
  and the Beacon keep the spells of stage 3.3a. The combat tool writes the critical values of
  each weapon and the head and critical cells of each damage row, and the creature tool
  writes the angles of a hit from behind and the head volume of each creature model (from
  its skeleton): with older files no hit is critical.
- `tools/gamedata/weapon-actions.sh` makes the weapon actions
  (`~/.local/share/soulwarden/gamedata/weapon-actions.json`, 0.4 MB, stage 3.3b-M1, format 2 from stage 3.3b-M2): the attack
  graph of each melee weapon of the game (the light chain, the charges, the swings and the thrusts with their damage rows and
  their times, the dodge windows, the length of each animation; protocol notes 4.32), and the abilities of each weapon (the
  animation of each ability, the animations that its nodes select, its spells, and its branches; protocol notes 4.33). It reads the weapon
  tables, the animation databases, the action lists, and the animation files of the game copy (it needs an
  Oodle library). `--find TEXT` prints the graph of the weapons with a text. Without the file the backend
  starts, and each melee hit uses the light or the heavy row of its weapon.
- `tools/gamedata/stamina.sh` makes the stamina data (`~/.local/share/soulwarden/gamedata/stamina-data.json`, 2 KB, stage
  3.4f-B): the Stamina component of the player slice (the maximum 100, the regeneration 33.33 in a second, the winded time 2 s),
  the delays of the base vitals (1 s), the dodge of each load class (its animation and its cost: 45, 45, 50), the times of the dodge
  (the payment at 0.1 s, immune to 0.5 s, the next dodge from 0.267 s), and the exhausted state (protocol notes 4.38). It reads the
  data tables, the action lists, the animation database of the player, and the player slice of the game copy (it needs an Oodle
  library). Without the file the backend starts: the stamina stays full and the server has no rule for a dodge.
- `tools/gamedata/projectiles.sh` makes the projectile data
  (`~/.local/share/soulwarden/gamedata/projectile-data.json`, 72 KB, stage 3.3b-S3): the ranged
  attack profiles of the game (the range, the radius, the near range, the acceleration delay),
  the flight values of the projectile slices (the speed, the acceleration, the life time), the
  ammunition, the profile of each weapon, and the shots of the basic attacks (protocol notes
  4.27). It reads the data tables, the ranged attack data, the slices, and the animation
  databases of the game copy (it needs an Oodle library). `--find TEXT` prints the attacks,
  the shots, and the weapons with a text. Without the file the backend starts, and a shot of a
  bow, a musket, a staff, or a gauntlet does nothing. The creature data tool (stage 3.3b-S3)
  also writes the capsule of the character of each creature slice: with an older creature file
  a shot hits each creature with one size. From stage 3.3b-S2b the file also has the projectile
  spells of the abilities (the shot of the Ice Pylon, the toss of the musket trap, the Beacon):
  with an older file the trap and the Beacon do nothing, and the Pylon does not shoot. The tool
  stops (and keeps the file that is there) when a source that a shot needs is not there.
- `tools/gamedata/spells.sh` makes the spell entity data
  (`~/.local/share/soulwarden/gamedata/spell-data.json`, 1.6 MB, stage 3.3b-S2a): for each spell
  row with a prefab slice, the slice asset, the slots of its replicated components, its
  interest radius, and its delayed events (protocol notes 4.25). It reads the data tables and
  the slices of the game copy, from the same sources as the creature data tool (it needs an
  Oodle source). `tools/gamedata/spells.sh --find TEXT` prints the spells with a text and their
  slice. Without the file the backend starts, and the vortex of Gravity Well has its damage
  but no picture. From stage 3.4f-C the file has format 4: the shape, the height, the offset, and the times of each detection
  volume, and the delays of each area status (the areas of the creatures need it; with a file from before no creature casts an
  area). From stage 3.4f-D the time of a volume with an activation event of its own is the time of that event (the backend refuses a
  file of the tool of stage 3.4f-C: write it again). From stage 3.3b-S4 the tool stops for a delay that is not a 32-bit number,
  and the backend refuses a file with a delayed event slot that has no event. From stage
  3.3b-S2b the file has the format 2 (the trap slice with its position slot, the health, the
  shots, the detection volume, the offense, and the timelines of a slice, and the rows of the
  base vitals tables; protocol notes 4.28): with a file of format 1 the Ice Pylon, the musket
  trap, and the Beacon do nothing.
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
  notes 4.17): the game files have the values of a creature and not its behavior. 814 of
  the 3,848 kinds have no attack that the tool can use (stage 3.4f-A): they do not attack you, and a kind
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
  never dead after a login. Not yet: blocks and the respawn points of the game (camps,
  houses).
- Stamina and dodge (stage 3.4f-B, [Stamina and dodge](systems/stamina-and-dodge.md), protocol notes 4.38). Left Shift is the
  dodge. The server keeps the stamina of your character with the values of the game data: a dodge costs 45 (Dodge_Roll_Fast in
  light armor, Dodge_Roll in medium armor; 50 for Dodge_Roll_Slow in heavy armor; no dodge when overburdened) at 0.1 s of the roll,
  and the stamina comes back 33.33 in each second, 1 s after the payment and 1 s after the end of the roll (0.5 s). At 0 your
  character is winded for 2 s and cannot dodge until its stamina is full again. For 0.5 s from the start of the roll a creature hit
  does nothing (no damage, no effect): an arrow or a ball flies through your character and can hit a different character or a wall.
  The client shows its own stamina bar (the server copy is the record: the client does not take it). A status with Stamina gives
  stamina (Stamina Recovery 20, Mauler's Stamina 40), and StaminaRate makes the regeneration faster. In a development run `tools/world
  stamina` lists the stamina of each character and its dodge, and `tools/world passives --table stamina` lists the rules of the data
  with a stamina or dodge part. From stage 3.4f-C the first record of your character has the Stamina state: the HUD shows the
  maximum 100 (before: 95, a default of the client). Not yet: the block and grit.
- Archers (stage 3.4f-A, [Creature behavior](systems/creature-behavior.md), [Projectiles](systems/projectiles.md)).
  An archer of the game data (20 models: the Varangian, Lugdunum, and mercenary archers, the
  skeleton archers, the Dryad archers, the Damned bowmen, and their variants) shoots at your
  character in 20 m with a free line: you see the draw of the bow, the release at the time of
  the game data (1.33 s; 0.65 s for a Dryad archer), and one arrow that flies with its trail.
  Your health goes down only when the arrow touches your character; an arrow that misses, or that
  a wall or the ground stops, does nothing. The archer aims at the place where you will be (the
  lead of the game data), so a step to the side at the release makes it miss. It shoots again 3 s
  after the start of its last shot, walks to 18 m when you are farther or behind a wall, walks back
  to 14 m one time in 6 s when you are nearer than 8 m, and uses its melee attack when you are in
  its reach. Other players see the same arrows. The Varangian archers near the start place (about
  450 m to the west of the default spawn) and the skeleton archers (300 m to the west) show it.
  Not yet: the other shots of the data (a charged, a fast, or a triple shot) and a block of an
  arrow. In a development run `tools/world creature shoot CREATURE CHARACTER` starts a shot, and
  `tools/world projectiles` lists the flights. From stage 3.4f-B an archer that walks to you walks
  on to 18 m (the code of stage 3.4f-A stopped at 20 m), and a dodge lets an arrow fly through you.
- Mages (stage 3.4f-B, [Creature behavior](systems/creature-behavior.md)). A creature with the cast
  `Cast_One_Projectile` and no bow shot (10 models of 88 kinds: the Skeleton Mages, the Ancient
  Guardian mages, the Dynasty summoners, the Dryad shamans, the Damned priests) casts at your
  character in 16 m: the windup, the release at the time of the game data (0.5 s for a Skeleton
  Mage), and one ball (the slow ball of the Skeleton Mage: 20 m/s, 56 damage of the type Arcane at
  level 17). It casts again 2.5 s after the start of its last cast, walks to 14 m when you are
  farther, and walks back to 11 m when you are nearer than 6 m. A Celestial Mage
  (`Skeleton_Mage:2`) of `tools/world creature spawn` shows it.
- Areas on the ground (stage 3.4f-C, [Spell entities](systems/spells.md), [Creature behavior](systems/creature-behavior.md),
  protocol notes 4.39). Six spells of the game data: the poison mushroom of the Dryad shamans (`Dryad_Bomb`: a tell, the cloud
  at 1 s, 26 s; `AI_Poison_55` each 1 s while you are in the cloud, a tick each 2 s of 2 percent of your maximum health plus 4
  percent of the damage of the shaman), the purple area of the Ancient Guardian mages (`Mage_Ranged_AOE`: `AI_Snare_6sec` and
  `AI_Weaken`), and the strikes: the lightning of the Undead shamans (2.5 m, the bolt at 0.9 s), the fire pillar of the
  Inquisitor clerics, the ground area of the named Damned priests, and the geyser of the Soul Harvesters. A strike hits each
  character in its area one time; a dodge avoids it. An exit ends the statuses of an area. The death of the caster ends the
  lightning and the pillar (EndOnCasterDeath of the game data); the mushrooms and the purple area stay to their end. A creature
  casts its area when you are in the CastDistance of the spell (30 m for the mushroom, 12 m for the lightning), each 10 s (an
  Undead shaman each 5 s: it walks to 10 m), and shoots or hits between. In a development run `tools/world creature area
  CREATURE CHARACTER` (route `POST /v1/dev/creatures/area`) starts the next area attack of a creature at once. From stage 3.4f-D four
  more spells at the caster (protocol notes 4.40): the plague nova of the Plagued ghosts (4 m), the corrupted blast of the signifers,
  the anchor slam of the undead officers, and the shockwave of the war hammer knights in the direction of the knight; the creature casts
  one when you are in the Radius of its spell. Not yet: the self-destructs and the shield of the officers.
- Heals and buffs of the creatures (stage 3.4f-D, [Creatures](systems/creatures.md), [Creature behavior](systems/creature-behavior.md),
  protocol notes 4.40). The Inquisitor Clerics heal themselves (`InqCleric_Rekindle_Self`: `AI_InqCleric_Rekindle`, 4.5 percent of the
  maximum health each second for 8 s) when their health is 15 percent or less (the low-health part of their slice in the game data);
  the Evil Knight hounds howl when they take you as their target (`Howl`: `AI_Evil_Knight_Wolf_Haste`, 10 s, 10 percent more damage, 20
  percent more speed). Each one at most each 20 s (a rule of this project). The icons show at the name plate of the creature; the heal
  and the buff end at their time, at the death of the creature, and at a dispel of the data (a row with RemoveStatusEffectCategories,
  for example the Null Chamber of the void gauntlet). In a development run `tools/world creature status-action CREATURE [ACTION]`
  (route `POST /v1/dev/creatures/status-action`) starts the action at once, and `tools/world creature effect CREATURE STATUS` gives a
  status row. Not yet: the heal of a different creature, the other buffs of the data.
- Interactions (stage 2.5a). The client shows the prompt of an object and executes its option
  at the key; the backend checks the request with the interaction data of the game and
  replies. A lore page: E (Read) opens the journal on the page, T (Add To Journal) adds it, and
  the page stays in the journal of the character (the achievement of the page, a row of the
  database; after a restart the prompt shows "Already Read"). A talk with a person and the
  animations of a chair, a bed, or a pet are accepted (a chair takes one character). The
  crafting stations of the settlements (stage 2.5b) are at their places: E (Craft) at a station
  opens the crafting screen of the game (a craft does not work yet: stage 3.7). Each other
  option (storage, a gathering node, a shop, an inn, a teleport door, a camp) is refused: its system is
  not there yet (the chests: stage 3.5b.1, below). In a development run, `tools/world interactions NAME` lists the objects near a
  character with their state (also the stations and the facts of a station use),
  `tools/world object NAME KEY on|off` hides or shows the prompt of an object,
  `tools/world interaction cancel NAME` ends the interaction of a character, and
  `tools/world fixture NAME powderKeg 0 2 0 --permission 1` puts a powder keg near a character:
  E (Ignite Fuse), held for 5 seconds, completes one time (a count; no explosion)
  ([Interactions](systems/interactions.md)).
- Chests (stage 3.5b.1). E at an ordinary chest of the world (a supply cache, a provisions crate, an alchemy chest, an elite supply
  chest: 5,149 chests of the data) starts its open: the ring of the prompt runs the open time of the data (2.5 s; 5 s for an elite
  chest), and at its end the character gets the loot of the root of the chest (the content level and the loot tags of its territories,
  the container floor of the gear score), with the loot notice. The chest is then looted for that character only (no prompt) until its
  reset: 60 minutes, an elite chest 82,800 s (23 hours), on the wall clock (a restart of the backend keeps it). A walk out of the
  range, a hit, a release of E, a travel, or the death before the end gives nothing. When the bag has no place for the whole loot, no
  item comes, the roll stays for the next open, and the chest stays closed. A chest with a key, a lock, a quest, or an event (370 chests
  of the data) has no prompt (stage 3.5b.3). In a development run `tools/world chests NAME [METERS]` lists the claims and the chests
  near a character, `tools/world chest clear NAME KEY` removes a claim, and `tools/world chest reset NAME KEY SECONDS` makes a looted
  claim reset sooner ([Chests](systems/chests.md)). Since stage 3.5b.2 the open is a timed use of the interaction base, a roll with an
  item that the item catalog does not have stays in its claim, and the chests of a point of interest have its loot tags.
- Loot bags (stage 3.5b.2). The items of a kill go into a personal loot bag at the place of the creature (the experience, the mastery,
  and the coin at the kill). Only the characters with the credit of the kill see the bag, and each one has its own part. E takes all
  that fits; T (or Tab near the bag) opens the panel: LSHIFT and a click take one stack, LCTRL and a click a part of a stack, Take All
  takes all that fits; a drag of an item of the bag into the open panel gives it back to the bag. A drag of an item (of the bag or of
  the paperdoll) to "Drop items" puts it into a Discarded bag at the feet, private to the character. A bag ends 180 s after it came,
  or 1.25 s after its last item (the times of the game data); the rows keep the bags and their item instances over a restart. With
  an `interaction-data.json` from before stage 3.5b.2 the items of a kill go into the bag of the character and a drop removes the
  items, as before. In a development run `tools/world bags NAME` lists the bags of a character ([Loot bags](systems/loot-bags.md)).
  Since stage 2.2 an item with Nonremovable or NotDroppable in the item table (a quest item, for example) does not leave the
  character: a drop or a give into a bag changes nothing (with an `item-catalog.json` from before stage 2.2 no item has this
  protection). An item of a kill that the item catalog does not have stays in the bag until a start with a catalog that has it. A bag
  takes 500 stacks, and a restart does not renew the end of an empty bag.
- Mana (stage 3.5b.2). The maximum mana is the base of the progression tables times 1 plus the MaxMana parts of the threshold
  abilities of the attributes (Focus 100: 110), of the perks, and of the effects; the regeneration has the ManaRate parts. The mana bar
  of the HUD goes away at full mana ([Combat and health](systems/combat-and-health.md)).
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
  This works with a sword, a rapier, a hatchet, a flail, a spear, a great axe, a war hammer,
  and a greatsword; the keys 1 and 2 select the weapon. With the weapon actions (stage 3.3b-M1,
  protocol notes 4.32) each swing of a chain has its own damage row as in the game: the last
  swing of a chain (the ender of the sword, the hatchet, the great axe, the war hammer, the
  spear), the three swings of the greatsword (the third one 1.3), the two quick thrusts of the
  second swing of the rapier (each one 0.66, and both hit), a heavy attack that you release
  before the full charge (the great axe 1.2, the war hammer 1.3), and a full charge (hold the
  button for 0.67 s: the great axe 1.6, the war hammer 1.7). A swing hits each creature one
  time; a weapon change, an ability, or a dodge ends the chain. The backend computes the damage from the weapon, its gear score, the
  attack, the attributes and the level of the character, and the defense values of the
  creature. The client finds each contact itself and reports it, and the backend checks each
  report (the creature, the distance, the weapon, the time of the attack): the rules are
  rules of this project (protocol notes 4.15). Not yet: blocks.
- Critical hits (stage 3.3b-S5, protocol notes 4.30, `docs/systems/combat-and-health.md`). Each
  hit of your character on a creature (a melee attack, an ability, a projectile) can be
  critical: one roll of the server for each hit with the critical chance of the weapon (the
  great axe 7 percent, the war hammer 6, the bow 8) plus the critical chance of your nodes,
  perks, and effects. A critical hit does the critical multiplier of the weapon (the great axe
  1.35, the war hammer 1.2, the bow 1.3) plus the critical damage of your nodes and effects,
  and the damage number shows in orange and larger. A hit from behind a creature (for a wolf,
  135 degrees or more from its front) is critical with no roll, and a projectile that touches
  the head of a creature (the head volume of the game data) is a critical headshot. In a
  development run, `tools/world passives NAME` shows the chance and the multiplier of the
  weapon in the hand.
- Passive nodes (stage 3.3b-S5, `docs/systems/passive-rules.md`). The nodes of the weapon
  trees and the ability rows of your perks act with the rules of their rows in the game data:
  an effect on your character after a hit, after a hit that you take, or while creatures are
  near you; an effect on the creature; a shorter cooldown; a heal of a part of the damage; a
  damage part, an armor penetration, or a critical part. A node of an ability that does not
  work yet, and a node that needs grit, a block, or an action of the client that the server
  does not have, does nothing: `tools/world passives NAME` (and `--table greataxe`) lists each
  node with what the backend does with it.
- Heals (stage 3.3b-S6, protocol notes 4.31, `docs/systems/combat-and-health.md`). Each heal of a
  character (an ability, a potion, a food, the gear, a life steal) goes through one rule: the
  real gain to the maximum health, a green number for the owner, and for a heal of a friend a
  green number at the feet of the friend for the healer. The heal of an ability is the base
  damage of the staff at its gear score times the Focus scaling of the staff and the level part
  (the same curve and level part as the damage; stage 3.3b-M1), times the value of the effect (Divine Embrace 1.2, Sacred Ground and the Beacon
  0.16 each second), times the outgoing part of the healer (effects, Divine Blessing, the equip
  load) and the received part of the target (Disease halves it; Anointed adds to it). Divine
  Embrace and Sacred Ground need a confirm after the key: LCTRL heals you, a click heals the
  nearest friend in sight that is not at its full health (Sacred Ground comes at your feet, and it
  heals only a friend that it sees: not through a wall). Divine Embrace on yourself at your full
  health heals nothing and costs nothing. A change of your bar or a travel ends the aim. There are no groups: in a
  development run, `tools/world friends NAME OTHER` makes two characters friends for a test,
  and `tools/world heals NAME [TARGET]` shows the heal base, the outgoing and the received
  parts, and the counts.
- Ranged and magic basic attacks (stage 3.3b-S3, protocol notes 4.27,
  `docs/systems/projectiles.md`). With the projectile data, a shot of the bow (a hip shot, an
  aimed shot with the right pointer button, a charged shot with a hold of 0.5 s or more) or a
  light attack of the fire staff flies on the server with the speed, the gravity, and the range
  of the game data (the arrow: 100 m/s, the fall after 5 m, 100 m; the fire ball: 100 m/s, 50
  m). The first creature that the projectile touches (the capsule of its body in the game
  data) takes the damage of the row of the shot (the light arrow 1.0, the charged arrow 1.5,
  times the damage modifier 1.2 of the ammunition), and the damage number shows at it; a wall
  or the ground stops the projectile, and a creature behind it takes nothing. A creature 2 m
  or less in front of you takes the hit at once (the client reports it). One shot hits one
  time. No arrows are necessary. The musket, the life staff, the ice gauntlet, and the void
  gauntlet use the same rules with their data (no live test yet). Not yet: the blunderbuss
  (its spread), the projectile for other players (they see only the hit), headshots, the fall
  of the damage with the distance, the heavy attack of a staff in a development run of the
  harness (the server rule works: `tools/world shoot NAME --heavy`), and the abilities of these
  weapons.
- Abilities (stage 3.3a, protocol notes 4.22, `docs/systems/abilities.md`). With the ability
  data, the keys Q, R, and F use the three abilities of the bar of the weapon in your hand:
  Charge, Maelstrom, and Gravity Well of the great axe, and Wrecking Ball, Shockwave, and Path
  of Destiny of the war hammer. A hit of an ability shows its damage number. Shockwave stuns
  the creatures around you, Path of Destiny hits the creatures on a line in front of you,
  Wrecking Ball knocks a creature down, and Gravity Well throws a vortex that holds the
  creatures in it and slows them, with a burst 3 s later. The button of the ability on the
  HUD shows its cooldown (the time of the game data); a press in the cooldown does nothing.
  From stage 3.3b-M2b each creature is a target of the homing of an ability and of the camera
  lock (protocol notes 4.34): Wrecking Ball, Mighty Gavel, and the other abilities with a
  homing turn you to a creature near the direction of the ability, as in the game. Wrecking
  Ball hits each creature in 1.5 m of the landing with Breathing Room; with no Breathing Room
  its hammer hits only a creature at the place of the landing, and it seldom hits.
  The upgrades of the tree change the abilities as in the game (Storm's Reach, No Reprieve,
  Gravity, Unyielding, Crowded Well, Breathing Room, Meteoric Crater, Frailty, Seismic Waves,
  Stimulated Reduction, Aftershock). A stunned or rooted creature does not move, a slowed
  creature runs slower, and the icon of the effect shows at its name plate. A creature on its
  way back takes no effect. An ability has no cost. From stage 3.3b-S2a the vortex of Gravity
  Well is an entity of the world (protocol notes 4.25, `docs/systems/spells.md`): you and each
  other player near it see the vortex on the ground and its burst 3 s later, also a player that
  comes near it later; it goes away at its end (3.5 s) and when its caster leaves the world.
  A haste or a slow on your character changes the speed of your run and your sprint (the
  Detonate heart rune: 1.2 times for 3 s), and the server accepts the faster positions only
  after your client has the speed. From stage 3.3b-S4 (protocol notes 4.26,
  `docs/systems/forced-movement.md`) a hit moves a creature by the impact of its damage row:
  Maelstrom pulls the creatures 1.5 m to you (2 m with Storm's Reach), the start of the vortex
  pulls the creatures in its 4 m to its center at 7 m/s, the burst pushes them 0.5 m, Path of
  Destiny pushes 2 m away from you, Shockwave and Charge push 0.25 m, and the light and heavy
  attacks of the melee weapons push too (the light attack of the great axe 1 m). The server moves the creature in steps of 50 ms, and each player near
  it sees the slide; a wall, a drop, or a rock stops it. A creature on its way back does not
  move. The dash of Charge (14.7 m with the swing) is accepted by the position check of the
  server, and another player sees your character run in the dash. Riposte of the rapier (key
  of its place on the rapier bar): a creature that hits you in the 1 s window of the press
  does no damage, and 0.43 s later the counter of the server hits it one time (the damage of
  the row, the stun of 2 s, a push of 1.5 m), when it is still in the reach of its hit. Not
  yet: the counter animation of your character, the other abilities of the rapier, the
  abilities of the other weapons, grit, and the perks of an item that change an ability (the
  passive nodes: stage 3.3b-S5).
- The other abilities of the great axe and the war hammer (stage 3.3b-M2, protocol notes 4.33,
  `docs/systems/abilities.md`). Reap, Execute, Whirlwind, Armor Breaker, Mighty Gavel, and Clear
  Out work as the client plays them: each hit is a strike of the animation of the ability in the
  game data, with its damage row and its time. Reap hits, then pulls the creatures in front of
  you to you (farther with The Collector; Fatal Attraction adds its spin attack; Gravity roots
  the pulled creatures). Execute does 1.5 times the damage to a creature below half health.
  Whirlwind spins four times; with Unending Winds it spins on, to seven spins, while each spin
  hits. Armor Breaker ignores 35 percent of the armor. Mighty Gavel shows its ground. Clear Out
  pushes the creatures 3.8 m away; Power Cleaner then gives you and each friend in 6 m a fortify
  for 4 s. Charge with Unpredictable Strike swings when you press the right button in its run.
  An ability can start in a swing of a basic attack in the windows of the game data; a press
  that the swing does not take does nothing. The nodes of the two trees act now, but the nodes
  that need grit or a block: Unyielding (the armor of the friends in the vortex),
  Absorb (a shield at you in Maelstrom: from stage 3.4f-A it takes the arrow of an archer, checked in the game in stage 3.4f-B), Blood Lust (a creature in front
  of your camera in 14 m), Guarded Sprint (less damage while you run with the war hammer and do not dodge), and from stage 3.4f-B
  Mauler's Stamina (40 stamina at a hit below half health).
  `tools/gamedata/weapon-actions.sh`, `combat.sh`, `spells.sh`, and `abilities.sh` must make
  their files again for this stage.
- Spell abilities (stage 3.3b-S2b, protocol notes 4.28, `docs/systems/spells.md`). With the
  spell data and the projectile data of this stage, the key Q uses Ice Pylon of the ice
  gauntlet, Traps of the musket, and Beacon of the life staff when they are on the bar. The
  Pylon comes 0.6 s after the press, 2 m in front of you; it costs 15 mana when it comes (a
  press with less mana does nothing); a press while it lives does nothing (the button shows no
  countdown then: the client starts its own hold at a message that the backend does not send),
  and its cooldown of 10 s starts at its end. It has 20 percent of your maximum health (its name plate and health bar
  show), it shoots the nearest creature in 20 m that it sees each 2 s (1 s after it comes
  first), and a creature that it hits attacks it; it goes away 15 s after it comes, when its
  health is 0, and when you leave the world. The trap flies from your hand, opens where it
  lands, and the first creature in it starts it: each creature in 1.1 m takes the damage of
  the trap and a root of 3 s, and the trap goes away; with no creature it goes away after 30
  s. The Beacon (16 mana) flies to the first creature or player on its line: a creature takes
  its damage (1.46 of the weapon damage). The Beacon stays on that creature or player (or on
  the ground) for 10 s and follows it; while you stand in 3 m of it you have its buff (the icon,
  the light, "Healing Pulse"), which ends when you go out and at its end. It ends when its
  target or you leave the world. Each other player near them sees the same Pylon (with no name
  plate), trap, and Beacon. The name plate of your Pylon has a red bar (the affiliation of a
  summon is not known). Not yet: the heal amount of the Beacon (stage 3.3b-S6), the buff for another player
  (the groups), the upgrades of the Pylon, the interaction option of the trap, and the picture
  of the shot of the Pylon.
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
  and the tool window (F8) sets them in each start of the game. The largest level is 70, and the largest trade skill level
  is 250 (also Riding): each player owns the expansion (stage 1.1b, next item). Not there yet: the abilities of the weapons other than the great axe and the war
  hammer, the effects of an attribute other than the
  health, and the reset of a weapon tree from mastery level 11 on (it has a cost in a
  currency that the backend does not have).
- The DLC (stage 1.1b). Each account and each character owns the expansion Rise of the Angry Earth and the DLC packs of
  the game data, with no purchase and no setting: the backend gives the account its 19 entitlements at each login (and at
  the entitlement sync of the client), and each character the achievement of the expansion at its world entry. The client
  then has the limits of the expansion: the level 70 (a level 60 character keeps its level and its experience, and the
  character window shows the experience to level 61), the trade skills 250, the flail tree with its normal points, the
  equip gear score 800 (an item keeps its level requirement of the client: 700 needs level 65). Each character gets one
  AmuletIsabellaT1 (the item of a DLC pack) at its first entry with a free bag slot. `tools/world ownership NAME` shows the
  expansion, the entitlements, and the achievements of a character; the admin state of the tool window has them (member
  `ownership`). The progression tables of format 2 (`tools/gamedata/progression.sh`) are necessary: with a file from before,
  the players have the limits of the game with no expansion. Not there yet: the content of the expansion (the region Elysian
  Wilds and its story, the mounts, the expeditions, its quests, its recipes) and the use of the skins, the emotes, the
  crests, the housing item, and the title of the DLC packs. The client shows the modal "Congratulations on purchasing the
  new expansion" only for a change during play, not at a world entry.
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
