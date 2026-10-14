# Project Soulwarden

Soulwarden runs the last retail New World: Aeternum client against a server on your own computer. No public service is involved. The game talks to a local backend and to nothing else.

It is early. You can create a character, walk and travel in the whole world, put on armor, and fight the creatures of the world: they patrol, see you, attack, and go back to their place.

It supports one client build, `NewWorld.exe` 1.400.6031.6004151 from Steam app 1063730, on Linux with Proton. The SHA-256 of that file is `8654f01d324636d9f74f1c793b0cc4a417c3c5fa9847d9913c358ca29e0fdc8e`.

This is an independent project with no connection to Amazon Games. The repository has no game files, so you need your own copy of the game.

## What works

- Login with the Steam account of the computer. Each account has its own characters.
- Character creation and deletion, in the screens of the client.
- The world. Your character enters, moves, and comes back at the exact place where it stood.
- The bag, the coin, and equipment. Armor and weapons show on the body.
- Level, attributes, weapon mastery trees, the ability bar, and trade skills.
- Creatures at their real spawn points in the whole world, 32,840 points and 3,848 kinds in 25 regions.
- Melee combat with a sword, a great axe, a hatchet, a war hammer, or a spear. A hit shows its damage number, and creatures die.
- Creatures fight. A creature sees your character, runs to it with its run animation, attacks, and goes back to its place at the end of its leash. 2,989 of the 3,848 kinds attack.
- Creatures find their way around walls and rocks, up stairs, and on the floors of buildings and ruins.
- Creatures walk their patrol routes from the game files, or wander near their place. After a fight a creature runs back to the place where it saw you. On that way it is immune and walks the way that it came, and at that place it has its full health and patrols again.
- Health and death. Your character loses health and dies, and it comes back at the nearest spirit shrine or inn.
- Travel to any spirit shrine or inn of the world, 161 places in 16 territories, with no new login.
- A tool window on F8 that gives items, sets levels and skills, spawns or kills creatures, heals your character, and moves it to a travel place.

## What does not work yet

- Creatures do not jump, drop from an edge, climb, swim, or fly. A creature that has no way to you waits at the place nearest to you for 12 s and then goes back.
- Creatures do not fight in groups, call others, or flee. Only 23 of the 24,895 waypoint tasks of the patrols have an animation, so a creature stands in its idle pose at the other waypoints.
- 859 kinds of creature have no attack yet and stand still. No creature has a ranged attack, magic, or an ability.
- Creatures do not drop loot, give no experience, and do not come back after death.
- You cannot block or dodge. The tool window heals your character.
- Some creatures are not in the world. The client shows no body for 50 of their 553 models, so 1,541 spawn points stay empty.
- Nothing in the world gives experience. The tool window sets your level.
- No quests, no ranged weapons, no magic, and no abilities. People in the settlements say only their first line: talk to them and press E or Escape to end the talk.
- No world clock, so some sessions are dark.
- One player. In a development run a second character can stand in the world, and that is all.

The [roadmap](docs/ROADMAP.md) has the order of the work. Combat comes first.

## How it works

Two programs come out of this tree.

The hook replaces `steam_api64.dll`, and the game loads it at its start. It gives the client its Steam identity, sends every HTTP request of the client to the local backend, blocks all traffic that is not loopback, and makes the client trust the certificate of the backend. It also draws the tool window, and it changes 6 bytes of client code so that the client reports the exact position of your character.

The backend is a C++ service that listens on loopback only. It answers the HTTP calls of the login path and runs the game server, which is DTLS 1.2, a datagram carrier, and state replication. One thread owns the world. Accounts, characters, items, and progress are in a SQLite file in `~/.local/share/soulwarden`.

The launcher starts the game in a network namespace that has loopback and no other interface. From there the game cannot reach the internet, with or without the hook.

## Run it

You need Linux, Steam with Proton Experimental, your copy of the game in `~/Games/soulwarden/game`, clang, cmake, ninja, llvm-mingw, and Python 3.

```sh
tools/build-soulwarden-game-hook.sh
tools/build-soulwarden-backend.sh
tools/test-soulwarden-backend.sh     # the backend tests, with no game process

tools/gamedata/appearance.sh         # nine game data tools, one time each
tools/gamedata/slice_ids.sh
tools/gamedata/items.sh
tools/gamedata/progression.sh
tools/gamedata/creatures.sh
tools/gamedata/combat.sh
tools/gamedata/places.sh
tools/gamedata/heights.sh
tools/gamedata/navigation.sh

linux-compat/run-soulwarden.sh
```

At the main menu, select a character and press Play. `tools/steam-shortcut.py add` puts the launcher in your Steam library.

### Game data

The backend reads its tables from your game copy. Each tool above writes one file to `~/.local/share/soulwarden/gamedata` (`heights.sh` writes one folder with 136 MB, and `navigation.sh` one folder with 1.2 GB). The backend starts without any of them, and then the feature of that file is off.

| Tool | Without its file |
| --- | --- |
| `appearance.sh` | Each character has the same default body |
| `slice_ids.sh` | The server does not check the target of a client message |
| `items.sh` | No items, and a new character has no clothing |
| `progression.sh` | No levels and no skill points |
| `creatures.sh` | No creatures. With a file from before the fight stage, the creatures stand still: run the tool again |
| `combat.sh` | An attack does nothing. With a file from before the fight stage, your armor does not lower the damage of a creature: run the tool again |
| `places.sh` | The travel list is empty, and you travel only to a position in numbers. You come back at the place of your death |
| `heights.sh` | With no navigation data, creatures do not leave their place. They attack only when you are in their reach |
| `navigation.sh` | Creatures go on a straight line on the terrain, through walls and rocks, and they see you through a wall. A creature on a floor of a building does not leave its place |

Some tables of the game have Oodle compression. `creatures.sh` and `places.sh` need an Oodle library, which you give them with `SOULWARDEN_OODLE_LIB=/path/to/liboo2core.so`. `creatures.sh` runs for about one minute, and `heights.sh` for about two minutes (it needs no Oodle library). `navigation.sh` needs the Oodle library and a C and a C++ compiler, and it runs for about 45 minutes with 8 processes. It makes the navigation data of the whole world from the collision shapes of the game copy, and then the folder of `heights.sh` again. When it stops, run it again: it makes only the regions that are not complete. The slice, item, and progression tools take the Windows library in `SOULWARDEN_OODLE_DLL`.

## The tool window

Press F8 in the world. The window has five tabs for items, the character, skills, creatures, and travel. It is how you get anything today, because nothing in the world gives items or experience yet.

The tab "Character" shows your health. "Heal" gives your character its full health, and it makes a dead character live again at its place. With the mark "Creatures ignore this character", no creature attacks you. `SOULWARDEN_CREATURE_BEHAVIOR=0` starts the game with creatures that stand still. After you come into the world, travel, or respawn, no creature attacks you for 10 seconds unless you hit it. Out of a fight, your health comes back in about 30 seconds.

The tab "Travel" lists the spirit shrines and the inns of each territory. Type a part of a name in the filter, click a place, and press "Travel". Your character is there after a loading screen of some seconds, with the creatures of that place around it.

While the window is open, the keyboard and the pointer go to it and not to the game. `SOULWARDEN_TOOL_WINDOW=0` starts the game without it.

## Identity and safety

The launcher reads your Steam id and your persona name from `config/loginusers.vdf` of the local Steam installation. It reads no Steam credential, and it does not start or contact Steam.

Authentication is a stub. The backend accepts the Steam id that the local ticket names, so any process on your computer can log in as any account. That is acceptable on loopback and would be a hole anywhere else. Do not put the backend on a network, and do not connect this build to a public game service.

The game log, the captures, and the database have your full Steam id. Keep them out of bug reports and out of the repository.

## More

- [Reference](docs/REFERENCE.md) has each variable, the development tools, and the details of each feature.
- [Protocol notes](docs/PROTOCOL_REVERSE_ENGINEERING_NOTES.md) have each wire contract with its evidence and its client code address.
- [Roadmap](docs/ROADMAP.md) has the systems and the order of the work.
- [Agent instructions](AGENTS.md) have the development loop, written for coding agents.

## Dependencies and license

The source of each library is in a `vendor` folder, with its own license.

- [Microsoft Detours](https://github.com/microsoft/detours) hooks client functions.
- [Dear ImGui](https://github.com/ocornut/imgui) 1.92.6 draws the tool window.
- [JSON for Modern C++](https://github.com/nlohmann/json) reads and writes the JSON of the HTTP services.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) does the DTLS 1.2 of the game server.
- [SQLite](https://www.sqlite.org) is the player database.

The code of this project has the MIT license. See [LICENSE](LICENSE).
