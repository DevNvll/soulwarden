# Development runs and live tests

Read this document before you start the game for a test. It has the rules that keep a development
run away from the game of the user, the scenario scripts, the input harness (`tools/hx`), the
pictures and videos, and the logs and captures of a run. The tests of a system are in the document
of that system.

## Start here

| Task | Start here |
| --- | --- |
| The launcher in the Steam library, the release that it starts | `tools/steam-shortcut.py`, `tools/release.sh` |

## Rules

- A development run and a game of the user are separate. A development run
  (`tools/dev-run.sh`, `tools/scenarios`) has its own Proton prefix
  (`~/Games/soulwarden/proton-dev/1063730`), its own data directory
  (`~/Games/soulwarden/work/dev-data`), and the mark `SOULWARDEN_DEV_RUN=1` in the environment of each
  of its processes. `tools/dev-stop.sh` stops only processes with the mark. A start with no
  development variable (`linux-compat/run-soulwarden.sh`, the Steam library entry) uses the
  prefix `~/Games/soulwarden/proton/1063730` and the data directory `~/.local/share/soulwarden`.
  `tools/dev-env.sh` is the only place of the defaults of the development tools: a new tool
  reads the data directory from it (a Python tool with `tools/dev_env.py`).
- A development run has its own Proton prefix: `~/Games/soulwarden/proton-dev/1063730`
  (`SOULWARDEN_COMPAT_DIR` selects a different one; `tools/dev-env.sh` has the default). Proton
  makes the prefix at the first start (some seconds). Each process of a development run has
  `SOULWARDEN_DEV_RUN=1` in its environment, and `tools/dev-stop.sh` stops only these processes.
  `tools/dev-run.sh` starts no game when a game that is not a development run uses the data
  directory (the lock file `run.lock`): give the run a different `SOULWARDEN_DATA_DIR`.
- A development run and each development tool (`tools/hx`, `tools/shot`, `tools/world`, the
  scenario scripts, `tools/research/player_position.py`, `check_identity.py`,
  `gde_components.py`) use the development data directory `~/Games/soulwarden/work/dev-data` with
  no variable. `SOULWARDEN_DATA_DIR` selects a different one: then give the same value to each
  tool of that run. `tools/dev-run.sh` removes the captures and the backend log of the data
  directory at each start: copy what you need before the next run.
- `tools/dev-run.sh` starts the game on an isolated agent desktop (`soulwarden-game`), not on the
  user desktop, with capture and the harness on. The game runs in gamescope, which is one
  Wayland window on that desktop and has its own X server (`SOULWARDEN_PRIVATE_DISPLAY=0` turns
  this off, `SOULWARDEN_GAME_SIZE` sets the size). Do not turn it off while the user uses the
  computer: on the X server of the user session, the pointer moves and the focus changes of
  the harness reach the other X11 applications of the user (other games). The script prints a
  warning for the value `0`. It starts no game for a value that is not `0` or `1`, or when
  `gamescope` is not installed.
- A development run makes no sound. `tools/dev-run.sh` gives the game the startup setting `-sys_audio_disable 1` (the
  arguments `dev_sound_arguments` of `tools/dev-env.sh`) before the other game arguments: the client then uses its null audio
  system ("1: use NullAudioSystem (disable all audio functionality)"), and the game log has the line `<Audio>: Skipping Audio
  System Initialization - it was disabled by startup settings!` (live check of 2026-10-15; the form `+sys_audio_disable 1` has
  no effect). `SOULWARDEN_DEV_SOUND=1` gives the game its sound. No tool changes a file of a Proton prefix for this, and no tool
  changes the volume of a stream in the sound server: the sound server keeps a volume by the application name, and the game of
  the user has the same name ("New World"). The file mute of the stages 1.1b to 3.3b-S6 (`tools/dev_mute.py`, which set
  `m_masterVolume` in the settings files of the development prefix) is gone: each method that edits a file in a prefix can be
  redirected to a file of the user by a swap of folders (code review of stage 3.3b-S6, finding S6-R1). The settings files of
  the development prefix keep the values that they have. `python3 -B tools/check_dev_sound.py` (status 0) checks the arguments
  and that no tool of the tree edits a settings file (cases RK1, RD1, RV1).
- This is a development environment: data loss is not a problem. A test uses the fastest
  path to the state that it needs. The development data directory stays between runs:
  `tools/scenarios/play.sh RUN_LABEL` enters the world with a character that is there
  already. Its database has two accounts now: the Steam account of the computer with the
  character `Soulwarden`, and the made-up test identity (`76561202255233008`, `Tester`) with the
  characters `Remote` and `Gearone`. `Soulwarden` has a chest armor, leg armor, an amulet, a
  sword, and a potion stack in a quick slot on it, the level 65 (stage 1.1b; `tools/world level Soulwarden 60` gives the
  level of the research of the DLC) with points on strength,
  dexterity, and constitution (strength 8, dexterity 25: the damage values of protocol notes
  4.15), a spear (`2hSpearT1`) in weapon slot 2 (slot 25), a great axe (`2hGreatAxeT2`), a
  hatchet (`1hHatchetT2`), and a war hammer (`2hWarhammerT2`) in the bag (`tools/world equip
  Soulwarden BAGSLOT 25` puts one of them into weapon slot 2), six weapons (sword, bow, great axe, fire staff, spear,
  hatchet) at the mastery level 20 with 19 nodes each and no ability on a bar (114 nodes:
  rows of a test, written into the database with no check of the tree rules), the war hammer
  at the mastery level 8, and the trade skills mining, cooking, and armoring; `Remote` has the level 35; `Remote` has a chest armor, leg armor,
  and a sword on it; from stage 1.1b each account owns the DLC (the rows of `entitlements` and `achievements`), `Soulwarden`
  has a sword of gear score 700 in weapon slot 1, FoodCONT5 in quick slot 17 (key 6), Mining 250, Riding 5, a flail node
  (Arcane Smite), and each character of the two accounts that entered has the amulet of a DLC pack (AmuletIsabellaT1); `Gearone` is a character of the creation with the armor and the sword
  of the archetype Soldier; from stage 3.3b-M2b `Soulwarden` has the 19 war hammer nodes of the build of the user (with
  Breathing Room) and the bar Wrecking Ball, Shockwave, Path of Destiny; from stage 2.5a `Soulwarden` has two lore pages in its
  journal (the rows `Lore_unlock_00_Aeternum_Title_07_02`, the page at 8838.85 4246.60 136.45 near the start, and
  `Lore_unlock_04_Everfall_Title_20_01`; delete the rows of `achievements` with `Lore%` for a test of a first read). Make a new empty data directory or a new character only when the
  test is about an empty database or about the creation. A database backup is not necessary.
  `sqlite3 ~/Games/soulwarden/work/dev-data/soulwarden.sqlite3 "update characters set position_x=null,
  position_y=null, position_z=null where name='Soulwarden'"` (with no run alive) puts the
  character back at the default spawn position, a street with free space. Places near it
  (stage 2.3a): the street to the west is free for 25 m (a walk of 3.7 s with the camera to
  the west, `'look -900 0'`); a porch with a raised floor is at `8861.567 4243.415 139.542`
  (1.7 m above the street, beside a house wall), and its stairs are at `8863.082 4247.179
  138.752`. At the end of stage 2.3a the row of `Soulwarden` has no position (the default
  spawn).

## Commands

```sh
tools/scenarios/menu.sh RUN_LABEL    # start, go to the main menu
tools/scenarios/play.sh RUN_LABEL    # the same, then press Play for the first character
tools/scenarios/create.sh RUN_LABEL NAME   # menu, then create a character and enter the world
tools/scenarios/leave.sh             # a game that is in the world: leave it to the main menu
tools/shot NAME                      # screenshot of the game desktop
tools/dev-stop.sh                    # stop the development runs (not a game of the user)
tools/dev-stop.sh --check            # status 0: no process of a development run is alive
```

- Identity variables (`docs/REFERENCE.md`, "Player identity"): with `SOULWARDEN_STEAM_ID` not set, a run uses
  the Steam account of the computer. `SOULWARDEN_STEAM_ID=<17 digits>` with `SOULWARDEN_PERSONA_NAME`
  selects a different account (a made-up id for a test: `76561202255233008`).
  `SOULWARDEN_STEAM_ID=local` selects the fallback identity. `SOULWARDEN_SEED_CHARACTER=<name>` gives an
  account with no character one character. `tools/scenarios/play.sh` sets the seed `Soulwarden`
  when the variable is not set; `tools/dev-run.sh` and `tools/scenarios/menu.sh` set no seed.
- `tools/scenarios/play.sh` stops with status 1 when the account has no character, or when
  the client sends no login queue request in 30 s after the click on Play.
- `tools/scenarios/leave.sh` starts no game. It leaves the world with the Game Menu of the
  client (Escape, the gear, "Exit to Main Menu", "Yes"), waits for the line `rep: session
  ended` of the backend log, and then for the next character list request of the client
  (the main menu is back 12 s to 20 s after the leave: a click on Play before that has no
  effect). Status 1: the client did not leave the world in three tries, or the main menu did
  not come back. After it, a click on Play (`tools/hx 'click 1089 572'`) enters the world
  again: the backend log then has a second session (`[s2 ...]`) from client port 27001.
- `tools/hx 'read ADDRESS LENGTH'` reads client memory (512 bytes at most for one read) and
  `'write ADDRESS HEX'` writes it.
  `tools/hx 'key VK [MS]'`, `'keydown VK'`, `'keyup VK'`, `'click X Y [MS]'`, `'text TEXT'`
  send input to the game window (`MS` is the time that the key or the button stays down).
  `'look DX DY'` moves the pointer by a relative distance: this turns the camera in the
  world (900 units are 90 degrees, with an error of 3 degrees or less; a positive `DX` turns
  to the right; the harness sends 30 units each 20 ms, thus 900 units need 0.6 s). `'wheel
  N'` turns the pointer wheel: a positive number moves the camera nearer to the character.
  `'drag X1 Y1 X2 Y2 [MS]'` presses the left button at the first position, moves the pointer
  to the second position in `MS` milliseconds (default 600), and releases the button: a
  drag of a user interface element, for example an item of the bag. `'rclick X Y'` is a
  click with the right button. `'mousedown left|right'` presses a pointer button and keeps
  it down, and `'mouseup left|right [DX DY]'` releases it (with `DX DY`: the release and a
  relative pointer movement in one input event). `'toolwindow stop'` stops the tool window
  for the rest of the start (a test of the stop after a failure of the renderer).
  `'state'` shows the window and the foreground state. One `tools/hx` call can have many
  commands; they run in sequence.
- A position in a screenshot of `tools/shot` (1280x720) is the window position plus (12, 48).
- Game log of a development run:
  `~/Games/soulwarden/proton-dev/1063730/pfx/drive_c/users/steamuser/AppData/Local/AGS/New World/Game.log`
  (`game_log` of `tools/scenarios/common.sh`). Game log of a start with no development
  variable: the same path below `~/Games/soulwarden/proton/1063730`.
  Backend log: `SOULWARDEN_DATA_DIR/logs/backend.log` (a development run:
  `~/Games/soulwarden/work/dev-data/logs/backend.log`; a start of the user:
  `~/.local/share/soulwarden/logs/backend.log`).
- Each log line of the game server (`rep:`, `carrier:`) ends with the mark of its session:
  ` [s1]`, and ` [s1 4aae16c4]` from the registration on (session id, first 8 characters of
  the character id). The lines `dtls:` name the client port, and the line `rep: secure
  session open, client port N [sK]` connects a port with a session.
- The captures of the game server are in one folder for each session:
  `SOULWARDEN_DATA_DIR/captures/rep/s0001`, `s0002`, ... Each folder has its own file counter and
  its own limit (32,768 files). `tools/research/client_msgs.py`, `input_packet.py`, and
  `check_identity.py` take `SOULWARDEN_DATA_DIR/captures` and read all sessions in sequence
  (`--session N` for one). `tools/research/client_messages.py` takes `captures/rep` or the
  folder of one session.
- `tools/dev-run.sh` gives the launcher the variables of the tool window by name:
  `SOULWARDEN_TOOL_WINDOW` (`0`: off), `SOULWARDEN_TOOL_WINDOW_KEY`, and `SOULWARDEN_TOOL_WINDOW_FAULT`
  (`hook`, `device`, or `swapchain`: a test of a start in which the tool window cannot
  start; the hook reads it only with the harness gate). The launcher makes the token of the
  admin interface itself. The same for the position report of the hook: `SOULWARDEN_EXACT_POSITION`
  (`0`: the hook does not change the client code, and the backend uses the camera estimate)
  and `SOULWARDEN_EXACT_POSITION_FAULT=code` (a test of a start with client code that is not the
  code of the supported build: the hook compares with a wrong byte; only with the harness
  gate). The same for the melee contact report: `SOULWARDEN_MELEE_REPORT` (`0`: the backend gets
  the option `--no-melee-report`, the client gets no remote configuration document, and a
  melee attack has no effect).
- More variables of `tools/dev-run.sh`: `SOULWARDEN_BACKEND_EXE` and `SOULWARDEN_HOOK_DLL` start a
  different build of the backend or of the hook. `SOULWARDEN_CREATURE_BEHAVIOR=0` turns the
  behavior of the creatures off for the run (the backend option `--no-creature-behavior`):
  the creatures stand at their spawn points. `SOULWARDEN_RESEARCH_SESSION` is the research
  switch of the game server for live checks (`registration-result=N`, `reject=N`, `kick=N`,
  `kick-delay=SECONDS`; protocol notes 3.6; and `refuse-equipment=1`, which makes the backend
  refuse each equipment request with the message "equip item fail": the client sends only
  requests that its own checks permit, thus this is the only way to see that message in a
  live client). It is off by default, and the launcher gives it to the backend only with
  `SOULWARDEN_HARNESS=1`. Do not use it for a run that is not a check.

## Traps

- More than one gamescope can run on this computer (stage 1.8a: an other project had `gamescope-0`, and the development run got
  `gamescope-1`; a later run got `gamescope-0`). `gamescopectl screenshot` with no variable can take the picture of the other display.
  Read the socket of the run from the environment of its backend and give it to each call:
  `GAMESCOPE_WAYLAND_DISPLAY=$(tr '\0' '\n' < /proc/PID_OF_THE_BACKEND/environ | grep '^GAMESCOPE_WAYLAND_DISPLAY=' | cut -d= -f2)`.
  Delete a picture of a different display at once.
- A `tools/world fragment` of a container state (the full form with a sequence) with a sequence above the world revision blocks the real
  states of that slot until the next session: the client takes no full container with a lower sequence (stage 1.8a: a fragment of slot 22
  with the sequence 900,000; the Objectives state of the backend then had no effect). Use a sequence near the world revision, or enter
  the world again before a check of the encoder.

- With `-sys_audio_disable 1` the game process still keeps one output stream of Wine open (`pactl list sink-inputs`: one
  stream "New World" in place of two; live check of 2026-10-15). It is not the audio system of the game. Do not change or
  record it: `pw-record --target` with the id of a playback stream records the default source (the microphone of the user),
  not the stream.
- Still pictures do not show if a creature walks or slides: a pose of a run in some frames
  looked like a walk in stage 3.4b, and the user saw the slide. Record a video of the agent
  desktop (`wf-recorder -o AGENT-0 -f FILE.mp4 -r 30`, with `timeout -s INT SECONDS`; it
  reads the output of the agent desktop and sends no input) and extract frames with
  `ffmpeg` at 10 to 30 frames each second, near the creature.
- A video from `gamescopectl screenshot` in a loop has only the picture of the game. The
  command writes the file some time after it returns, and some calls write no file: in stage
  3.3a 65 of 108 calls in 9 s gave a frame. Wait 1 s or more after the last call, use only
  the frames that are there, and give each frame its real time (a concat list of `ffmpeg`
  with the `duration` of each frame). Check the video with `ffprobe` and a sheet of frames.
- A window of a different program can be on the agent desktop (a person can move a window
  there). `tools/shot` and `wf-recorder` then take that window too: its picture can have
  private data. Take the pictures of the game with `gamescopectl screenshot FILE.png`: it
  has only the picture of the gamescope of the run (1256x660; about 10 pictures each second
  at most in a loop, enough for a video with `ffmpeg` and a concat list of the real times).
- The capture folder of a session is full after 6 minutes in the world (32,768 files): do
  the attacks of a test early, or leave the world and enter it again.
- `tools/world` and each tool find the backend by the program name `soulwarden-backend`. A
  copy of the backend with a different file name (`backend-test`) is not found: put a copy
  into its own folder with the name `soulwarden-backend`.
- `tools/shot NAME` writes `DATA_DIR/shots/NAME.png`. A name with a folder gives no file
  and no error text: copy the file to the evidence folder after the call.
- A person can show the agent desktop on a monitor. `tools/shot` then gives a picture with
  the size of that monitor (3440x1440, not 1280x720), or a picture of the empty desktop.
  `tools/research/creature_tour.py` checks the size and the content of each picture and
  waits; its command `check` removes a picture that is not correct.
- `pkill -f 'NewWorld.exe'` also matches the shell that runs it. Use a bracket pattern
  (`NewWorld[.]exe`) or `tools/dev-stop.sh`.
- The capture folder of a session is full after 32,768 files, which is some minutes in the
  world. A request of the client after that is not in the captures: read the line of the
  request in the backend log (each request of stage 1.5 and 1.6 has one, with its fields).
- Do not change `SOULWARDEN_GAME_SIZE` for a development run when you can avoid it. The client
  saves the size in the development Proton prefix (`user_preload_settings.cfg`, `r_Width` and
  `r_Height`, and `m_windowWidth` and `m_windowHeight` of the settings file of the persona
  in `AppData/Roaming/AGS/New World`). The next run with the default size then has a
  stretched picture and different positions. Put the values back to 1256 and 660 with no run
  alive.
- With a user interface screen open, a `'look DX DY'` moves the pointer by 2 pixels for each
  unit. Use `'move X Y'` for a position on a screen.
- The processes of gamescope have the names `gamescope-wl` and `gamescopereaper`.
  `pgrep -x gamescope` finds no process in all conditions. After `tools/dev-stop.sh`, run
  `tools/dev-stop.sh --check`: it must print `no process of a development run`. Do not use
  `pgrep -f soulwarden-backend` or `pgrep -f 'Bin64.NewWorld[.]exe'` for this check: they also
  find a game that the user started, and that game must continue.
- The kernel keeps only the first 15 characters of a process name: the backend has the
  process name `soulwarden-back`. `pgrep -x soulwarden-backend` finds no process (it prints
  a warning, which a script with no error output does not show). A tool finds the backend by
  the program of its command line (`pgrep -f '^([^ ]*/)?soulwarden-backend( |$)'`, as
  `tools/dev-stop.sh` and `tools/world` do).
- Wine makes sockets in the wineserver process. A wineserver from an earlier run is in the
  earlier network namespace. The launcher stops all processes of the prefix before a start.
- The game window has no keyboard focus of the compositor on the agent desktop. Wine takes the
  foreground state away from it at random times, and the keys of the harness then have no
  effect (`tools/hx 'state'` shows `foreground=0`). `SetForegroundWindow` does not give the
  state back. A pointer button on the window does: the harness sends one before a key when
  the state is lost (the left button has live proof, the extra button that the harness tries
  first has not). Read `foreground` before and after an input test.
- The harness has one command file. Two `tools/hx` calls at the same time lose commands, or
  the second call runs after the end of the first one (stage 3.3b-S2a: the key G of a second
  call during a hold of W of 5 s came after the hold). Put a key that must come during a hold
  into the same call: `tools/hx 'keydown 0x57' 'wait 1000' 'key 0x47' 'wait 4000' 'keyup 0x57'`.
- A travel to a position with a Z below the ground puts the character under the terrain (a
  dark screen, and the character moves away by itself). Give a Z on the ground or above it, or
  a place that a run before used.
- `'move X Y'` and `'click X Y'` do not turn the camera. Use `'look DX DY'`.
- A `gamescopectl screenshot` while a capture loop runs can give no file (stage B1): take the picture from a frame of the loop
  (`ffmpeg -i FRAME -vf crop=W:H:X:Y,scale=...`) before you delete the frames, and choose the frame by its time as a number (the
  name of a frame has the extension: compare the number, not the text).
- `tools/world travel` turns the camera of the character to the north (also a move of 1 m). Turn it again before an aim.
- A long free line for a test of a range: `tools/world path X0 Y0 Z0 X1 Y1 Z1` says "2 corners, ... a straight line that is free"
  for a line with no wall of the navigation data. The streets of a settlement have no free line of 50 m; a field near a spirit
  shrine can have one (stage B1: near `shrine_40297`, to the west). Grass hides a creature at 45 m: the damage number and the log
  show the hit.
- A `look` during a capture loop can give frames from two camera directions, one after the other (stage 3.3b-S2b, the
  trap video). Turn the camera before the capture, take two pictures to see that the view is stable, then capture.
- The camera does not turn by a constant angle for one large pointer movement: 900 units in
  one input event gave 69 to 120 degrees. Thus the harness sends a `look` in steps of 30
  units. The first `look` after the world entry can be 6 degrees short. Read the yaw with
  `tools/research/player_position.py` when the direction is important.
- The character walks into walls. A key hold of W that moves the character less than you
  expect does not prove a movement fault: look at a screenshot, turn the camera, and do the
  test again.
- The agent desktop starts the launcher with the environment of the compositor.
  `tools/dev-run.sh` gives the launcher only the variables that it names: add a new launcher
  variable to its list, or the variable has no effect in a development run.
- `tools/steam-shortcut.py add` puts the launcher of the current release
  (`~/Games/soulwarden/releases/current`, `tools/release.sh`) in the Steam library of the user as a
  non-Steam game. That entry and a development run have different Proton prefixes, and
  `tools/dev-stop.sh` stops only development runs: one does not stop the game of the other.
  They share the game copy. The entry starts the products and the game data of its release; a
  development run starts the two products in `apps/*/dist` and the game data folder
  (`~/.local/share/soulwarden/gamedata`). Thus a build changes only the next release, not the game
  of the user. An entry from before the releases starts the working copy: do
  `tools/steam-shortcut.py add` again (it also replaces an entry of an older project name). They do not share the data directory: a development run uses the
  development data directory (`tools/dev-run.sh` stops with a message when a game that is
  not a development run uses its data directory).
- A start with `SOULWARDEN_COMPAT_DIR=~/Games/soulwarden/proton/1063730` uses the prefix of the user:
  the launcher then stops a game that the user started. `tools/dev-run.sh` prints a warning.
- A client with no input of the user leaves the world itself 20 minutes after the world
  entry (the game log has `CGame::ActorContainerDisconnect`). It sends no carrier disconnect:
  the backend ends the session with the cause `idle` 30 s later. In a long live check, send
  an input in each period (`tools/hx 'look 30 0' 'look -30 0'`).
- The scenario scripts are bash scripts. `source tools/scenarios/common.sh` in a zsh command
  gives wrong paths: call a bash script that has the `source` line.
- The loading screen stays for some seconds after the line `InGame` of the game log (11 s
  in one run). The client sends the position of its camera only when the loading screen is
  gone: wait for the first line `rep: input N camera` of the backend log before a
  screenshot of the world or an input test.
- The Escape key in the world: the first Escape of a world session does nothing. Each later
  Escape opens or closes the bar with the gear (10 key presses in one run). A script cannot
  see if the bar is open: `tools/scenarios/leave.sh` tries again with the other state.
  Positions of the Game Menu: gear `972 23`, "Exit to Main Menu" `474 515`, "Yes" of the
  popup `714 397`.
- The research switch (`SOULWARDEN_RESEARCH_SESSION`) makes the client show an error popup and
  closes its connection. A run with the switch is not a test of the product.
- The hook log (`SOULWARDEN_DATA_DIR/client/logs/hook.log`) was made again at the stop of one
  run, with no trace line. Read the trace lines of a run before `tools/dev-stop.sh`.
- Two calls of `tools/hx` at one time (a key in one call, a memory read in another) give a conflict of the harness. For
  a read of the client state during a key (an action layer in the dash of Charge), put the key and the reads into one
  call (stage 3.3b-S4: `action_sampler.py` of the evidence folder).
- The dash of Charge and the projectile of Gravity Well go in the direction of the camera of the client, not of the
  body: turn the camera to the target before the press (a dash of a test went into a wall after 5.8 m).
- A test of a bow or a staff: read the traps of [Projectiles](../systems/projectiles.md) first (the aim and the
  crosshair, the camera of a staff, the heavy attack of a staff with the harness, the capture folder).
- The world time (stage 2.2) moves on: a night is 15 minutes of real time. Set the time before a picture or a video
  (`tools/world time Soulwarden 12:00`; [World time and weather](../systems/world-time-and-weather.md)), and do not judge the world
  phase from the brightness of a picture (a local volume of the level changes the light).
- A test of a heal (stage 3.3b-S6): Divine Embrace and Sacred Ground need a confirm after the key (`tools/hx 'key 0x51'
  'wait 700' 'key 0xA2'`: Q, then LCTRL). The rest rule heals 3 percent each second after 6 s with no fight, also a
  character with no session: hurt the character (`tools/world hurt`) just before the cast, or the rest heal hides the heal
  of the test. A heal of a friend needs the test fixture (`tools/world spawn OTHER --near NAME`, `tools/world friends
  NAME OTHER`); the number of the healer shows at the feet of the friend, and the white bar under the name of the friend
  is its health.
- `tools/build-soulwarden-backend.sh` has no option: each call builds and installs into `apps/backend/dist`. For a test
  build, run `cmake --build` in a build folder and copy the program to a folder of the run.
- `gamescopectl screenshot FILE` returns before the file is complete: a command that reads the file at once can find no file.
  Wait 1 s before a crop or a montage of the picture.
- A gathering node (stage 3.6a) shows its prompt only with the focus: put the character 1.5 m from the node on the side with no
  other node (a bush next to a tree takes the focus), and press W for about 0.1 s (`tools/hx 'key 0x57 120'`) so that the
  character turns to the camera. A travel does not turn the character. `tools/gamedata/gathering.sh --region REGION --near X Y
  METERS` lists the nodes near a place with their kind and their G.
- A test of a craft (stage 3.7a): read the traps of [Crafting](../systems/crafting.md) first (the positions of the crafting screen, the
  counts that the screen reads only at its open, the 10 s of a refused craft). The Workshop of Oxboro (Engineering5) has its bound at
  `8832.55 4248.92 136.76`: `tools/world travel NAME 8833.2 4248.6 137.2` puts the character at its prompt.
- An encumbered character plays no ability (user test 2: three presses of Wrecking Ball at rest gave no action of layer 0). From stage
  U3 the server refuses such a press too (`encumbered`), and an overencumbered character starts no attack from rest. Take the materials
  out of the bag before a test of an ability (`tools/world take Soulwarden SLOT`); the HUD shows "Encumbered - movement slowed" in the
  frames. Read the weight of the client on the inventory screen (Tab, top right) before a test at the limit: the base weight of the
  development character changes between runs (26.1 and 36.6 in the runs of stage U3).
- The session writes the log line of a refused ability press only for the first press of each cause: a second refused press has no
  line. Count the refusals with `tools/world list` (the line `abilities: ... refused presses: N CAUSE`) and the dodges with the line
  `stamina: dodges: N CAUSE`. An accepted cast has the line `world: ability: NAME casts ABILITY`.
- An alpha wolf of level 65 does not see the character at 16 m: start its fight with a light arrow (`tools/world shoot Soulwarden
  --yaw DEGREES --light` with `tools/world ignore Soulwarden off`). With `ignore on` the hit gives no target, and the wolf stays.
- A test of a hit on a running creature: shoot when the creature has run some meters (the evidence tool `run_shot.py` of stage U3
  polls `tools/world list`, 70 ms for each poll). The run of a wolf is a step of about 1.8 m each 0.2 s on the server; a picture loop
  gives about 5 different pictures each second (two pictures 17 ms apart, then 200 ms).
- `tools/hx` waits 30 s for the result and then prints `harness: no result`, but the harness of the client runs the whole command
  list: a list of 60 s goes on in the game. Read the log after the end of the list, or send parts of less than 30 s.
- A test of the press of an ability in a swing: `python3 -B fragment_sampler.py "mousedown left;wait 117;mouseup left;R1;key 0x51
  60;R60" 0 15` (in the tools of the evidence of user test 2) reads the action of layer 0 between the keys: the start input of the
  charge, of the swing, and of the ability. Compare it with the log line of the cast (`in 33 ms`).
- The development character carries more than its weight limit: the HUD shows "Encumbered" during a gather. It does not stop a
  gather (the action Gather has no weight condition).
- `gamescopectl screenshot NAME.png` with a relative name writes the file into the folder of the shell that started the run (stage
  1.8c: a picture came into the working copy). Give an absolute path in the evidence folder.
- `tools/world travel` turns the camera to the north (stage 1.8c). To have a creature or an object in front of the camera after a
  travel, travel to a point to the south of it. `'look DX DY'`: a positive `DY` turns the camera down.
- A kill while the creatures ignore the character (`tools/world ignore CHARACTER on`) gives no reward and no quest count (stage 1.8c,
  live K1). Turn the mode on again after the kill, before a gather or a picture near other creatures.
- A test of the beach quests (stage 1.8b): a new character (`tools/scenarios/create.sh`) starts in the trigger area of the beach
  cutscene, and the cutscene starts at the first accepted position, some seconds after the loading screen. Start the picture loop
  before the world entry, or the start of the cutscene is not in the video. The first `create.sh` after a start of the game can stop
  at the title screen with `harness: no result`: run it again.
- `tools/world travel` with a Z of a guess can put the character in a rock or under the ground at the beach (stage 1.8b): use the Z of
  the data (a trigger volume, a person in `tools/world persons`) and look at a picture before an input.
- A dialogue of a quest has pages and options (stage 1.8b): E is "Next", the key 1 the first option. A key of a page can select the
  hand-in or the offer of the next page: take a picture before each key.
