# Project Soulwarden

Soulwarden runs the last retail New World: Aeternum client against a server on your own computer. No public service is involved. The game talks to a local backend and to nothing else.

It is early. You can create a character, walk around the start area, put on armor, and hit a wolf with a sword until it dies. The wolf does not fight back yet.

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
- Travel to any spirit shrine or inn of the world, 161 places in 16 territories, with no new login.
- A tool window on F8 that gives items, sets levels and skills, spawns or kills creatures, and moves your character to a travel place.

## What does not work yet

- Creatures stand still. They do not move, attack, drop loot, or come back after death.
- Some creatures are not in the world. The client shows no body for 50 of their 553 models, so 1,541 spawn points stay empty.
- Nothing in the world gives experience. The tool window sets your level.
- No quests, no ranged weapons, no magic, and no abilities.
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

tools/gamedata/appearance.sh         # seven game data tools, one time each
tools/gamedata/slice_ids.sh
tools/gamedata/items.sh
tools/gamedata/progression.sh
tools/gamedata/creatures.sh
tools/gamedata/combat.sh
tools/gamedata/places.sh

linux-compat/run-soulwarden.sh
```

At the main menu, select a character and press Play. `tools/steam-shortcut.py add` puts the launcher in your Steam library.

### Game data

The backend reads its tables from your game copy. Each tool above writes one file to `~/.local/share/soulwarden/gamedata`. The backend starts without any of them, and then the feature of that file is off.

| Tool | Without its file |
| --- | --- |
| `appearance.sh` | Each character has the same default body |
| `slice_ids.sh` | The server does not check the target of a client message |
| `items.sh` | No items, and a new character has no clothing |
| `progression.sh` | No levels and no skill points |
| `creatures.sh` | No creatures |
| `combat.sh` | An attack does nothing |
| `places.sh` | The travel list is empty, and you travel only to a position in numbers |

Some tables of the game have Oodle compression. `creatures.sh` and `places.sh` need an Oodle library, which you give them with `SOULWARDEN_OODLE_LIB=/path/to/liboo2core.so`. `creatures.sh` runs for about one minute. The slice, item, and progression tools take the Windows library in `SOULWARDEN_OODLE_DLL`.

## The tool window

Press F8 in the world. The window has five tabs for items, the character, skills, creatures, and travel. It is how you get anything today, because nothing in the world gives items or experience yet.

The tab "Travel" lists the spirit shrines and the inns of each territory. Type a part of a name in the filter, click a place, and press "Travel". Your character is there after a loading screen of some seconds, and the creatures of that place stand at their spawn points.

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
