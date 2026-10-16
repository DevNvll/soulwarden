# Project Soulwarden

Soulwarden runs the last retail New World: Aeternum client against a server on your own computer. No public service is involved. The game talks to a local backend and to nothing else.

It is early. You can create a character, walk and travel in the whole world, put on armor, and fight the creatures of the world with melee attacks, with the basic attacks of the bow, the musket, and the magic staffs and gauntlets, with the abilities of the great axe and the war hammer, and with the Ice Pylon, the musket trap, and the Beacon of the life staff: the creatures patrol, see you, attack, shoot arrows at you (the archers), and go back to their place.

It supports one client build, `NewWorld.exe` 1.400.6031.6004151 from Steam app 1063730, on Linux with Proton. The SHA-256 of that file is `8654f01d324636d9f74f1c793b0cc4a417c3c5fa9847d9913c358ca29e0fdc8e`.

This is an independent project with no connection to Amazon Games. The repository has no game files, so you need your own copy of the game.

## What works

- Login with the Steam account of the computer. Each account has its own characters.
- Character creation and deletion, in the screens of the client.
- The world. Your character enters, moves, and comes back at the exact place where it stood.
- The bag, the coin, and equipment. Armor and weapons show on the body.
- Level, attributes, weapon mastery trees, the ability bar, and trade skills.
- The full game with its DLC, for each account and each character, with no purchase and no setting: the expansion Rise of the Angry Earth and the DLC packs of the game. Your character goes up to level 70 (a level 60 character keeps its level and experience and goes on with experience), the trade skills go up to 250 (also Riding), the flail tree opens with its normal points, and items up to gear score 800 can be equipped (with their level). Food of level 65 works at level 65. Each character gets the amulet of a DLC pack (AmuletIsabellaT1) one time. The content of the expansion (the region Elysian Wilds, mounts, expeditions, its quests) is not there yet, and the skins, emotes, and crests of the DLC packs need their systems.
- Creatures at their real spawn points in the whole world, 32,840 points and 3,848 kinds in 25 regions.
- Melee combat with a sword, a rapier, a hatchet, a flail, a spear, a great axe, a war hammer, or a greatsword. A hit shows its damage number, and creatures die. Each swing of a chain has its own damage as in the game: the last swing of a chain, the quick double thrust of the rapier, a heavy attack that you release early, and a full charge (hold the button for 0.67 s) each do their own damage.
- The twelve abilities of the great axe (Charge, Maelstrom, Gravity Well, Reap, Execute, Whirlwind) and of the war hammer (Wrecking Ball, Shockwave, Path of Destiny, Armor Breaker, Mighty Gavel, Clear Out), with the upgrades of their trees. Reap pulls the creatures in its reach to you, Clear Out pushes them away, Whirlwind with Unending Winds spins on while it hits, Execute does more damage to a creature below half health, Armor Breaker ignores a part of the armor, Charge swings with the right button (Unpredictable Strike), and Clear Out with Power Cleaner fortifies you and your friends near you. An ability does its damage, its button on the HUD shows the cooldown, and a creature takes the stun, the root, the slow, the knockdown, or the weaken of the ability, with its icon at the name plate. The vortex of Gravity Well shows on the ground and bursts after 3 s with its damage numbers; another player near it sees the same vortex. Put the nodes and the bar in the Weapon Mastery window (K).
- Pulls and pushes. Maelstrom pulls the creatures around you to you, the vortex of Gravity Well pulls the creatures in it to its center and its burst pushes them out, and Path of Destiny, Shockwave, Charge, and the light and heavy attacks of the melee weapons push a creature back, with the distances of the game data. A wall, a rock, or an edge stops a creature that a hit moves. The dash of Charge goes up to 14.7 m, and another player sees your character run in the dash.
- Bows, muskets, staffs, and gauntlets. The basic attacks of the bow (a hip shot, an aimed shot, a charged shot) and of the fire staff fly on the server with the speed, the gravity, and the range of the game data, and do their damage when they hit a creature, about 0.1 s for 10 m. The musket, the life staff, the ice gauntlet, and the void gauntlet use the same rules with their data (not yet tested in the game). A wall or the ground stops a shot: the creature behind it takes nothing. A shot that misses does nothing. No arrows are necessary (the game does not ask for them).
- The Ice Pylon of the ice gauntlet, the trap of the musket, and the Beacon of the life staff (key Q with the ability on the bar). The Pylon stands 2 m in front of you for 15 s, shoots the creatures near it each 2 s, and a creature that it hits attacks the Pylon (its name plate and health bar show); it costs 15 mana when it comes; while it lives a second press does nothing (the button shows no countdown then), and its cooldown of 10 s starts when it ends. The trap opens where it lands and closes on the first creature that walks into it: damage and a root of 3 s; with no creature it goes away after 30 s. The Beacon sticks to the creature or the player that it hits (it hurts a creature), follows it for 10 s, and gives you its buff and a heal each second while you stand in 3 m of it. Another player near them sees the same Pylon (with no name plate), trap, and Beacon.
- Riposte of the rapier: a creature that hits you in the 1 s after the press takes the counter of the server (its damage, a stun of 2 s, and a push of 1.5 m), one time for each press.
- A haste on your character makes your run and your sprint faster for its time (the Detonate heart rune: 1.2 times for 3 s).
- Creatures fight. A creature sees your character, runs to it with its run animation, attacks, and goes back to its place at the end of its leash. 3,034 of the 3,848 kinds attack.
- Archers shoot. The archers of the game (the Varangian, Lugdunum, and mercenary archers, the skeleton archers, the Dryad archers, and the Damned bowmen) draw the bow, release at the time of the game data, and shoot one arrow that flies on the server and that each player sees. You lose health only when the arrow touches you: step to the side at the release and it misses, and a wall or the ground stops it. An archer shoots from up to 20 m each 3 s, walks back a few meters when you come very near, and uses its melee attack when you are in its reach. The Varangian archers about 450 m to the west of the default spawn show it.
- Creatures find their way around walls and rocks, up stairs, and on the floors of buildings and ruins.
- Creatures walk their patrol routes from the game files, or wander near their place. After a fight a creature runs back to the place where it saw you. On that way it is immune and walks the way that it came, and at that place it has its full health and patrols again.
- Health and death. Your character loses health and dies, and it comes back at the nearest spirit shrine or inn.
- Rewards of a kill. A creature that you kill gives experience and weapon mastery experience, sometimes coin, and sometimes loot from the loot tables of the game, with the notices of the HUD. A loot item gets its perks with the perk roll of the game. A creature of a spawn point comes back after its respawn time, 6 minutes for most of them.
- Item perks and gems. An item has up to five perks; the tooltip shows them, the gem, the gear score, and the rarity. The perks of your gear give their values: health, armor, absorption, damage, the gem conversion, the regeneration, and the perks of a hit (Sundering Shockwave, Insatiable Gravity Well, Refreshing Move, the void perks). The equip load changes your damage.
- Potions, food, and dust. The quick slot keys (3 to 6) use the item of the slot: it heals, gives its effect, and starts its cooldown, with the countdown on the slot. The other potions of the same kind get 80 percent of the cooldown. An item above the level of your character does nothing. The HUD shows the icon of the effect with its countdown, and the character sheet shows the attributes of a food and the armor of a dust.
- The heart rune. Hits fill the charge of the heart rune (key G). At a full charge, G starts Detonate: its icon and its effect show on your character, and 3 seconds later it hurts each creature within 5 m.
- Critical hits. Each hit can be critical with the critical chance of your weapon and of your nodes, perks, and effects; a critical hit shows the orange damage number and does the critical damage of the weapon. A hit from behind a creature (135 degrees or more from its front, for a wolf) and an arrow or a shot in the head of a creature are always critical.
- The passive nodes of the great axe and the war hammer, from the rows of the game data: the effects on your own character after a hit or when creatures are near you (Mauler's Fury, Greed, Hammer Time, Safety Measures, Center of Attention), the effects on the creature (the exhaust of Exhaustive Attacks, the slow of Aftershock), the shorter cooldowns, the heals of a part of the damage, the armor penetration, and the critical nodes. In a development run, `tools/world passives NAME` lists each node with what the backend does with it. Another player sees the icons of your public effects at your name plate.
- The heals of the life staff. Divine Embrace (its key on the bar, then LCTRL for yourself or a click for a friend) heals after its cast of 1 second; Sacred Ground (its key, then LCTRL or a click) puts a holy area at your feet that heals you each second for 12 seconds; the Beacon heals each second. Your heals scale with the base damage of your staff, its gear score, and your Focus. You see a green number for each heal, and a green number at the feet of a friend that you heal. Effects that lower healing (Disease) and the heal nodes (Divine Blessing: more healing on a target below half health; Anointed) count. Potions, food, and the life steal of a perk heal through the same rules.
- Lore pages. Press E (Read) at a lore page: the journal opens on the page, and the page stays in the journal of your character, also after a restart and on the next day. T (Add To Journal) adds the page with no read. The prompt then shows "Already Read". The server checks each use of an object (the object, its option, your distance, and its limits) with the data of the game: 43,225 objects of the world (1,157 lore pages, 146 persons, gathering nodes, shrines, doors) are in the interaction data.
- Talks with the persons of the settlements, and the sit and pet animations of the objects that have them: the server accepts them, and a chair takes one character at a time.
- The crafting stations of the settlements. Each settlement has its stations at their places (for example the forge and the woodshop of Oxboro in Everfall): go into the station, and press E (Craft) at its prompt ("FORGE TIER 5"). The crafting screen of the game opens with your recipes. A craft does not work yet.
- Travel to any spirit shrine or inn of the world, 161 places in 16 territories, with no new login.
- A tool window on F8 that gives items, sets levels and skills, spawns or kills creatures, heals your character, moves it to a travel place, and makes each kill drop loot.

## What does not work yet

- Creatures do not jump, drop from an edge, climb, swim, or fly. A creature that has no way to you waits at the place nearest to you for 12 s and then goes back.
- Creatures do not fight in groups, call others, or flee. Only 23 of the 24,895 waypoint tasks of the patrols have an animation, so a creature stands in its idle pose at the other waypoints.
- 814 kinds of creature have no attack yet: they do not attack you, but they patrol or wander when their data has a route. Of the ranged attacks only the basic shot of the archers works: no creature has a charged, a fast, or a triple shot, magic, or an ability, and you cannot dodge or block an arrow.
- Only the twelve abilities of the great axe and the war hammer, the counter of Riposte, and Divine Embrace, Sacred Ground, and the Beacon of the life staff work. Sacred Ground with a click comes at your feet, not at the point of the aim; Divine Embrace with a click heals the nearest friend in sight, not the friend under the aim. There are no groups: a friend for a heal exists only in a test of a development run. The other abilities of the rapier and the abilities of the other weapons do nothing. The blunderbuss does nothing (its spread is not done). Other players do not see the arrow or the fire ball of your shot, only its hit. A heavy attack of a staff in a development run of the harness is a light attack (the input of the harness). The fall of the damage with the distance is not done. Your character does not play the counter animation of Riposte (it plays its block pose and then stands). An elite creature is pushed as far as a normal creature. A hit does not push your character. The vortex stays and bursts when its caster dies. The nodes of an ability that does not work yet, and the nodes that need grit, a block, stamina, or an action of the client that the server does not have, do nothing (`tools/world passives NAME` names each one). The shield of Maelstrom with Absorb takes the arrow of an archer by its rule, but this is not yet checked in the game. In the tests of the combo stage the spear did not hit a creature (the client sent no contact; open). The stances of the greatsword and the weapon oils are not done.
- Wrecking Ball with no Breathing Room seldom hits: its hammer hits only a creature at the place of the landing. With Breathing Room (the build of the user) it hits each creature in 1.5 m of the landing.
- Perks that need grit, stamina, a dodge, a block, a consumable, or another player do nothing. Only the Detonate heart rune works. No crafting, repair, salvage, or gem socket.
- Nothing drops on the ground. A full bag loses the items that have no slot.
- You cannot block or dodge. The tool window heals your character.
- Some creatures are not in the world. The client shows no body for 50 of their 553 models, so 1,541 spawn points stay empty.
- Only kills give experience. No quest and no gathering gives experience yet.
- No quests, no ranged weapons, and no magic. People in the settlements say only their first line: talk to them and press E or Escape to end the talk.
- Storage sheds, chests, gathering nodes, shops, inns, and teleport doors do not work yet: the server refuses their use (a window of the client can open and stays empty). A crafting station opens its screen, but a craft does nothing. No camps. A lore page gives no experience yet.
- Some persons of the settlements (the artificer at the forge of Oxboro) are not in the interaction data: a talk with them closes at once.
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

tools/gamedata/appearance.sh         # sixteen game data tools, one time each
tools/gamedata/slice_ids.sh
tools/gamedata/items.sh
tools/gamedata/progression.sh
tools/gamedata/creatures.sh
tools/gamedata/combat.sh
tools/gamedata/loot.sh
tools/gamedata/abilities.sh
tools/gamedata/perks.sh
tools/gamedata/spells.sh
tools/gamedata/projectiles.sh
tools/gamedata/weapon-actions.sh
tools/gamedata/places.sh
tools/gamedata/interactions.sh
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
| `progression.sh` | No levels and no skill points. With a file from before stage 1.1b, no DLC: level 60 and trade skill 200 at most, and the flail tree is locked: run the tool again |
| `creatures.sh` | No creatures. With a file from before the fight stage, the creatures stand still; with a file from before the ability stage, a knockdown shows the small hit reaction; with a file from before the projectile stage, a shot hits each creature with one size: run the tool again |
| `combat.sh` | An attack does nothing. With a file from before the fight stage, your armor does not lower the damage of a creature; with a file from before the loot stage, a kill gives no weapon mastery experience; with a file from before the stage of the pulls and pushes, a hit moves no creature: run the tool again |
| `loot.sh` | A kill gives no experience, no coin, and no loot |
| `abilities.sh` | An ability does nothing. With a file from before the itemization stage, a perk of a hit, a consumable, and the heart rune have no status data, and the mana does not come back: run the tool again |
| `perks.sh` | An item has no perk, a perk has no effect, and a potion, a food, and the heart rune do nothing |
| `spells.sh` | The vortex of Gravity Well has no picture (its damage stays), and the Ice Pylon, the musket trap, and the Beacon do nothing; with a file from before stage 1.1b they do nothing too (the backend refuses that file): run the tool again |
| `projectiles.sh` | A shot of a bow, a musket, a staff, or a gauntlet does nothing; with a file from before stage 3.3b-S2b the musket trap and the Beacon do nothing, and the Ice Pylon does not shoot: run the tool again |
| `weapon-actions.sh` | Each melee hit has the damage of the light or of the heavy attack of its weapon: no ender, no full charge, and the second quick thrust of the rapier does not hit |
| `places.sh` | The travel list is empty, and you travel only to a position in numbers. You come back at the place of your death |
| `interactions.sh` | The server refuses each use of an object: a talk with a person closes at once, a lore page that you read does not stay in the journal, and the settlements have no crafting stations. With a file from before stage 2.5b the settlements have no crafting stations: run the tool again |
| `heights.sh` | With no navigation data, creatures do not leave their place. They attack only when you are in their reach |
| `navigation.sh` | Creatures go on a straight line on the terrain, through walls and rocks, and they see you through a wall. A creature on a floor of a building does not leave its place |

Some tables of the game have Oodle compression. `creatures.sh`, `places.sh`, `interactions.sh` (about 4 minutes), `spells.sh`, `projectiles.sh`, and `weapon-actions.sh` need an Oodle library, which you give them with `SOULWARDEN_OODLE_LIB=/path/to/liboo2core.so`. `creatures.sh` runs for about one minute, and `heights.sh` for about two minutes (it needs no Oodle library). `navigation.sh` needs the Oodle library and a C and a C++ compiler, and it runs for about 45 minutes with 8 processes. It makes the navigation data of the whole world from the collision shapes of the game copy, and then the folder of `heights.sh` again. When it stops, run it again: it makes only the regions that are not complete. The slice, item, and progression tools take the Windows library in `SOULWARDEN_OODLE_DLL`.

## The tool window

Press F8 in the world. The window has five tabs for items, the character, skills, creatures, and travel. It gives items, levels, and skills at once; in the world only kills give them yet.

The tab "Items" gives an item with a gear score and with perks: type the PerkID texts with "," between them (for example `PerkID_Stat_TwoHandSoldier,PerkID_Weapon_CDRBasic,PerkID_Gem_VoidDMG4`; a gem is a perk).

The tab "Character" shows your health. "Heal" gives your character its full health, and it makes a dead character live again at its place. With the mark "Creatures ignore this character", no creature attacks you, and your kills give nothing. With the mark "Each kill drops loot and coin", each kill of your character drops loot and coin (without it, a wolf drops loot in about 1 of 10 kills). `SOULWARDEN_CREATURE_BEHAVIOR=0` starts the game with creatures that stand still. After you come into the world, travel, or respawn, no creature attacks you for 10 seconds unless you hit it. Out of a fight, your health comes back in about 30 seconds.

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
- [Agent instructions](AGENTS.md) have the project rules and the common workflows, written for coding agents.
  The rules, commands, and traps of each system are in [docs/systems](docs/systems) and
  [docs/development](docs/development).

## Dependencies and license

The source of each library is in a `vendor` folder, with its own license.

- [Microsoft Detours](https://github.com/microsoft/detours) hooks client functions.
- [Dear ImGui](https://github.com/ocornut/imgui) 1.92.6 draws the tool window.
- [JSON for Modern C++](https://github.com/nlohmann/json) reads and writes the JSON of the HTTP services.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) does the DTLS 1.2 of the game server.
- [SQLite](https://www.sqlite.org) is the player database.

The code of this project has the MIT license. See [LICENSE](LICENSE).
