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
  tabs: "Items", "Character" (with the health of the character, the row "Azoth" of stage 3.7c (the balance of Azoth_Currency and
  its cap, a field, and "Set": the command `set_currency`; [Crafting](crafting.md)), the button "Heal", which
  has the text "Revive" for a dead character, the mark "Creatures ignore this
  character", the mark "Each kill drops loot and coin", the attributes, and at its end the group "Build presets" of stage B0:
  [Build presets](build-presets.md)), "Skills", "Creatures" (from stage BT2 the column "Tree": the behavior tree of each creature, the
  member `tree` of its row in the state; its tooltip has the binding that selected it, `treeBinding`; a tree of a binding that the slice did
  not fit shows red with "(refused)" and its cause in the tooltip, `treeRefused`; [Creature behavior](creature-behavior.md), section
  "Behavior trees"; a backend of an earlier stage gives no tree: "-"), "Travel" (the position of the
  character, the list of the travel places with a filter and the distance of each place, a
  button "Travel" for the selected place, and a form with three numbers), and "World" (stage 2.2: the time of the world and
  its phase, a form with the hour and the minute and the button "Set", and the buttons 06:00, 12:00, 19:00, 00:00; the time
  changes for each player, [World time and weather](world-time-and-weather.md)). The tab
  asks the worker thread for the list of the places (`model::load_places`) until the
  backend gave it, one time each 5 s at most.
- The result of a command has a number (`Snapshot::resultNumber`): the worker adds 1 for each reply of a command, also when its
  text is the same as the text before. A part of the window that does a thing one time for each result (the group "Build presets"
  fills the fields of the form again after a successful apply) compares this number, not the text (code review of stage B0, finding
  B0-R6: a second apply of one preset gave the same text, and the field "New" of an attribute kept the old value; live pictures of
  stage B1).
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
  button for these four. Stage 3.3b-S1: the commands `apply_status` (arguments `status`,
  `seconds`, `potency`, `count`) and `remove_status` (`status`, or `all`) are the commands of
  the status effects ([Status effects](status-effects.md)); the tool window has no button for
  them. Stage 3.3b-S2a: the commands `cast_ability` (arguments `place`, 0 to 2, and `yaw`,
  degrees from the north to the east) and `end_spells` (no argument) are the commands of the
  spell entities ([Spell entities](spells.md)); the tool window has no button for them. Stage 3.3b-S3: the command
  `shoot` (arguments `yaw`, degrees from the north to the east, `pitch`, degrees up, and `heavy`, 0 or 1) is the shot of
  the weapon in the hand ([Projectiles](projectiles.md)); the tool window has no button for it. Stage 1.1b: the state of a
  character has the member `ownership` (the owned expansion with its limits, the entitlements of the account, the
  achievements, the rewards; [DLC and entitlements](dlc-and-entitlements.md)); there is no command that gives or takes an
  entitlement, and the tool window does not show the member yet. Stage 2.5a: the commands
  `set_object_enabled` (arguments `object`, the key of an object of the interaction data, and
  `enabled`, 1 or 0; only an object 30 m or less from the character, the rule `nearCharacter`
  of `commands::CreatureReach`) and `cancel_interaction` (no argument), and the member
  `interactions` of the state (the committed interaction and the objects 30 m or less from the
  character, with the stations of a settlement, the facts of a station use (`station`), and the service
  grant (`grant`) from stage 2.5b; [Interactions](interactions.md)); the tool window has no button for them.
  Stage 2.2: the command `set_world_time` (argument `time`, "HH:MM" or "HH:MM:SS") sets the time of the day of the whole world (the character of the command names who gives it; the reply has `"scope":"world"`), and the state of a character
  has the member `worldTime` (the time, the phase, the next phase, the cycle); the tab "World" of the tool window uses them. The
  perk roll of an item (`tools/world perkroll`) is a development
  route only. Stage 3.7a: the commands `craft_preview` (arguments `recipe`, the RecipeID text, and `quantity`, 1 to 10000: the plan of a
  recipe with the station of the use of the character, no change) and `stop_craft` (no argument: the craft batch stops), and the member
  `crafting` of the state of a character (the station of its use, its batch, the counts; [Crafting](crafting.md)); the tool window has no
  button for them yet. Stage 3.7b: the commands `set_craft_seed` and `set_craft_cooldown` are diagnostics: they need
  `SOULWARDEN_HARNESS=1` of the backend process, else the reply is 403 (code review of stage 3.7b, finding CR7B-R2; test case CR7B2), and the
  tool window has no button for them. Stage 3.7c: the command `set_currency` (`amount`, 0 to the cap; `currency`, default Azoth_Currency),
  the arguments `perks`, `upgrade`, and `materials` of `craft_preview`, and the members `azoth` and `azothCap` of `items` in the state of a
  character. Stage 1.8a ([Quests](quests.md)): the command `apply_backstory` (argument `template`: a backstory template of the quest data;
  decision D2 option C: its achievements and its start objectives, not its level, items, or faction), the route
  `GET /admin/v1/catalog/backstories` (the templates with their start objectives and the mark `inData`), and the member `quests` of the
  state of a character (its quests with their tasks and pins, its one-time grants); the tool window has no control for them yet. The
  grant of an objective and the switch of an achievement are development routes only (`tools/world quest grant`, `tools/world
  achievement`). Stage 1.8e: the route `GET /admin/v1/catalog/quest-support` (the support manifest of the quests: a reader of about 6 MB);
  the notice of a cinematic is a development route only (`tools/world quest cinematic`). The command that
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

- The group "Build presets" is at the end of the tab "Character", below the bottom of the window: move the pointer onto the
  window and turn the wheel (`tools/hx 'move 300 450' 'wheel -10'`). Then "Apply" is at `51 583` and, after the next wheel,
  "Confirm" at `59 583` (stage B1, the window of 1256x660 at its first place). A value in a field: a click on the field, then
  `'keydown 0x11' 'key 0x41' 'keyup 0x11' 'text 40'`, then a click on its "Set". Take a screenshot before you use a position.
- The mark "Each kill drops loot and coin" of the tab "Character" is one row below the mark
  "Creatures ignore this character" (`35 343` in the window of 1256x660 from stage 3.7c): each row below it
  is 29 pixels lower than in stage 3.4e, and from stage 3.7c 29 more (the row Azoth). Take a screenshot before you use a position.
- The tool window (F8), positions in the window of 1256x660 at its first place (the window is
  at the left side; a user can move it, and it has no saved place): tabs "Items" `50 113`,
  "Character" `125 113`, "Skills" `204 113`, "Creatures" `283 113`, "Travel" `362 113`, "World" `425 113` (stage 2.2: the hour
  field `38 268`, the minute field `175 268`, "Set" `326 268`, the buttons 06:00 `50 297`, 12:00 `112 297`, 19:00 `174 297`,
  00:00 `236 297`).
  Travel tab: filter field `203 190`, first row of the list `76 239` (each next row is 21
  pixels lower), the button "Travel" of the selected place directly after the text
  "Selected: NAME, TERRITORY" at y `461` (x `358` for "Shadowmine Shrine, Everfall": take a
  screenshot), the button "Travel" of the position form `363 517`, "Here" `425 517`. Items
  tab: search field `203 169`, first row of the result `138 218`, "+" of the quantity `203 382`, "Give" `246 382`, "Remove" of the
  first bag row `514 462` (each next row is 29 pixels lower). Character tab from stage 3.7c (live pictures of stage 3.7c): the row
  Azoth: its field `380 253`, "-" `431 253`, "+" `459 253` (a step of 100), "Set" `501 253`; "Heal" `506 282`, the mark "Creatures
  ignore this character" `35 314`, the mark "Each kill drops loot and coin" `35 343`, the dexterity field `380 473` and its "Set"
  `501 473` (a start with the behavior of the creatures off). Character tab before stage 3.7c: "+" of the level
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
