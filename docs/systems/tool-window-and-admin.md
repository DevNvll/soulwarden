# The tool window and the admin interface

Read this document before a change of the tool window of the hook (key F8) or of the admin interface
of the backend, and before a live test that uses the tool window. The wire contracts are in the
protocol notes (section 6).

## Start here

| Task | Start here |
| --- | --- |
| The tool window (key F8): renderer hook, input, layout | Protocol notes section 6, `apps/game-hook/src/client/toolwindow`, `tools/hx 'toolwindow'` |
| The admin interface of the backend, a command for a character | Protocol notes 6.4, `apps/backend/src/server/http/admin_routes.cpp`, `character_commands.h` |

## Rules

- The tool window. The hook draws one Dear ImGui window on the picture of the game
  (`apps/game-hook/src/client/toolwindow`; key F8, `SOULWARDEN_TOOL_WINDOW_KEY`). It is on in each
  start, and `SOULWARDEN_TOOL_WINDOW=0` turns it off: the hook then installs no part of it. The
  tool window must never stop the game: each part of its start checks what it needs (the
  bytes of the client code, the import slots, the device, the queue, the swap chain), and a
  failure turns the tool window off for the start with one log line (`tool window: OFF for
  this start of the game: CAUSE`). Three threads: the submission thread of the renderer makes
  each Dear ImGui call and each Direct3D call (the hook is at the present call site
  `0x8D445C`); the window thread only puts input messages in a queue (the hook of the window
  procedure `0x7159680`); one worker thread makes the socket calls. Do not make a Dear ImGui
  call or a socket call on a different thread. While the tool window shows, the game gets
  each release of a pointer button and of a key, and no other input: a raw pointer packet
  with a release and other events (a movement, a different button, the wheel) goes to the
  game, and the game reads only the releases of it (the hook replaces the import slot of the
  client for `GetRawInputData`, `0x7ECA660`). The pointer capture of the hook ends on the
  window thread when the window closes and when the tool window stops. A command of the
  window keeps the character id of the snapshot that the window drew
  (`model::command(character, ...)`): do not read the selected character at send time. The
  tabs: "Items", "Character" (with the health of the character, the button "Heal", which
  has the text "Revive" for a dead character, the mark "Creatures ignore this
  character", and the mark "Each kill drops loot and coin"), "Skills", "Creatures", and "Travel" (the position of the
  character, the list of the travel places with a filter and the distance of each place, a
  button "Travel" for the selected place, and a form with three numbers). The tab
  asks the worker thread for the list of the places (`model::load_places`) until the
  backend gave it, one time each 5 s at most.
- The admin interface of the backend (routes `/admin/v1`, `server/http/admin_routes.cpp`) is
  for the tool window. It needs no harness gate: each start has it when the launcher gave a
  token. A request needs the token of the start in the header `X-Soulwarden-Admin`. The token is
  for the account of the game of the start: the backend finds the account from the token,
  each command names a character id, and a character of a different account gives the reply
  404. The token goes to no log line, to no capture file, to no query, and to no command
  line. The query of a request with a path below `/admin` goes to no log line and to no
  capture file (`capture::recorded_target`): a sender can put the token there by mistake. An
  argument of a command that is not necessary (`quantity`, `gearScore`, `action`,
  `experience`) and that is there with a wrong form gives the reply 400
  (`Arguments::has`): it is not the same as no argument. A creature command
  (`spawn_creature`, `remove_creature`, `set_creature_health`, `kill_creature`,
  `reset_creatures`) acts for the character of the command as the reference place: that
  character must be a character of the account of the token, and the state of a character
  has the creatures near it. An admin command that changes a creature (`remove_creature`,
  `set_creature_health`, `kill_creature`) changes only a creature that is 150 m or less from
  that character (`commands::kAdminCreatureRange`, the distance of the list), and the
  character must be in the world: if not, the reply is 404 and nothing changes. The command
  `travel` moves a character of the account of the token (a place id, or three numbers),
  the route `GET /admin/v1/catalog/places` gives the travel places, and the state of a
  character has its position. The command `heal` gives a character its full health (a dead
  character lives again at its place), the command `set_creatures_ignore` (argument
  `ignore`) makes the creatures ignore a character, the command `set_force_loot` (argument
  `force`) is the loot switch of a character, and the state of a character has the member
  `health` (the health, the dead mark, the ignore value, the loot switch, and if the
  behavior of the creatures is on). The commands `assign_node` (arguments `weapon`, `node`),
  `set_ability_bar` (arguments `weapon`, `slot`, `ability`), and `reset_cooldowns` (no
  argument) are the node, bar, and cooldown commands of stage 3.3a
  ([Abilities](abilities.md)); the tool window has no button for them yet. Stage 1.3b: the
  command `give_item` takes `perks` (up to five PerkID texts with "," between them), and the
  tab "Items" of the tool window has the fields "Gear score" and "Perks" for it (live check
  I2-08); the commands `set_item_perks` (arguments `slot`, `container`, `perks`), `use_item`
  (`slot`, `container`), `set_rune_charge` (`percent`), and `activate_rune` (no argument) are
  the commands of the itemization ([Items and equipment](items-and-equipment.md),
  [Consumables and the heart rune](consumables-and-heart-rune.md)); the tool window has no
  button for these four. The perk roll of an item (`tools/world perkroll`) is a development
  route only. The command that
  lowers the health of a character, the roll of a loot table, the respawn time of a spawn
  point, and an effect on a creature are development routes only (`tools/world hurt`, `roll`,
  `creature respawn`, `creature effect`). A
  development route (`/v1/dev/creatures`, `tools/world`) has no such rule: it changes each
  creature of the world. Each creature command of the common layer has the parameter
  `commands::CreatureReach`, and each route set gives its rule. The admin routes and the
  development routes have one layer below them
  (`server/http/character_commands.h`): add a command for a character there, and give it to
  the two route sets. A route has no logic of its own for a character.

## Commands

```sh
tools/hx 'key 0x77'                  # F8: show or hide the tool window (needs the foreground state)
tools/hx 'toolwindow 1'              # show the tool window with no key ('toolwindow 0' hides it, 'toolwindow' prints its state)
```

## Traps

- The mark "Each kill drops loot and coin" of the tab "Character" is one row below the mark
  "Creatures ignore this character" (`35 314` in the window of 1256x660): each row below it
  is 29 pixels lower than in stage 3.4e. Take a screenshot before you use a position.
- The tool window (F8), positions in the window of 1256x660 at its first place (the window is
  at the left side; a user can move it, and it has no saved place): tabs "Items" `50 113`,
  "Character" `125 113`, "Skills" `204 113`, "Creatures" `283 113`, "Travel" `362 113`.
  Travel tab: filter field `203 190`, first row of the list `76 239` (each next row is 21
  pixels lower), the button "Travel" of the selected place directly after the text
  "Selected: NAME, TERRITORY" at y `461` (x `358` for "Shadowmine Shrine, Everfall": take a
  screenshot), the button "Travel" of the position form `363 517`, "Here" `425 517`. Items
  tab: search field `203 169`, first row of the result `138 218`, "+" of the quantity `203 382`, "Give" `246 382`, "Remove" of the
  first bag row `514 462` (each next row is 29 pixels lower). Character tab: "+" of the level
  `469 166`, "Set" of the level `512 166`, experience field `388 195`, "Give" `516 195`,
  the button "Heal" (or "Revive") `516 253`, the mark "Creatures ignore this character"
  `35 285`, the mark "Each kill drops loot and coin" `35 314`, dexterity field `388 423` and
  its "Set" `512 423` (each row below the two marks is 21 pixels lower in a start with the
  behavior of the creatures off: the tab then has one more line of text; dexterity `388
  444`). A click on a number field selects its
  text: `'text 25'` after the click replaces the number. The layout can change: take a
  screenshot before you use a position.
- The tool window takes each key while it shows: `tools/hx 'key 0x09'` (Tab) then moves the
  focus in the tool window and does not open the bag of the client. Hide the tool window
  before a key for the game. With the tool window open, `'text ...'` goes into the field that
  has the focus.
- The harness makes the game window the foreground window with a click at the middle of the
  window when the state is lost. The first place of the tool window leaves the middle free:
  a tool window that a test moved to the middle gets that click.
- `tools/hx 'toolwindow 1'` gives `not possible` before the first present call of the client
  made the renderer of the tool window, and for the rest of a start in which the tool window
  is off (`tool window: OFF ...` in the hook log).
- A screenshot of `tools/shot` shows the tool window (it is drawn into the picture of the
  game) and the pointer that Dear ImGui draws in the world. It does not show the pointer of
  the system, which the game shows on its screens (the inventory).
- The first Escape after the tool window closed can have no effect on a window of the client
  (the same as the first Escape of a world session): look at a screenshot before the next
  step.
