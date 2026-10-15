# Abilities

Read this document before a change of the abilities of a player: the press of a key of the bar, the
cast, the cooldown, the hits of an ability, and the effects of an ability on a creature, and before
a live test of an ability. The wire contracts are in the protocol notes (section 4.22, and 4.7,
4.13, 4.15). The nodes and the bars of a character are in
[Level, attributes, and skills](progression-and-skills.md), and the effects on a creature also in
[Creatures](creatures.md) and [Creature behavior](creature-behavior.md).

## Start here

| Task | Start here |
| --- | --- |
| The press of a key and the cast: the bits of the input report, the checks, the refusals | Protocol notes 4.22, `apps/backend/src/world/abilities.h` (`Abilities::press`, `CastVerdict`), `server/rep/client_input.h` (`InputStep::abilityKey`), `server/rep/rep_session.cpp` (`press_ability`), test cases AB1, AB3 |
| The cooldown: the value of the character, the countdown on the HUD | Protocol notes 4.22, `world/abilities.h`, `server/rep/states/cooldown_state.cpp`, `world/entity.h` (`Entity::cooldowns`), test case AB2 |
| The rule of one ability: its phases, damage rows, spells, effects, and the nodes that change them | `Abilities::plan` and `ability_rules` in `world/abilities.h` and `abilities.cpp`, test case AB12 |
| A melee hit of an ability (Charge, Maelstrom, Wrecking Ball, the swing of Gravity Well) | `Abilities::contact_phase`, `Combat::claim`, `Combat::strike` ([Combat and health](combat-and-health.md)), test case AB4 |
| An area of an ability (Shockwave, Path of Destiny, the vortex and the burst of Gravity Well) | `Abilities::run_area`, `throw_projectile`, `World::for_each_near`, `Navigation::sees`, test cases AB5 to AB7, AB10 |
| The effects on a creature: stun, root, slow, weaken, knockdown, stagger, the icons | `Creatures::apply_effect`, `Creatures::knock`, `world/behavior.cpp`, `server/rep/states/status_state.cpp`, test cases AB8, AB9 |
| The ability data of the game | `tools/gamedata/abilities.sh` (`abilities.py`), `gamedata/ability_data.h`, [Game data](../development/game-data.md) |

## Rules

- The abilities system (`world/abilities.h`, `world::Abilities`) owns the casts and the
  cooldowns. They are values of the character with the character id as their key (rule
  MP-1): a new session and a new entity of the character keep them (`Abilities::entered`
  copies them into `Entity::cooldowns`). Only this system changes them. The cooldowns are not
  in the database: a restart of the backend ends each cooldown (a project rule).
- The cast. The session gives each press bit of a new input step to the world (one cast for
  each input sequence: a client sends an entry in five reports). The world finds the
  character from the session, the weapon in its hand, the ability table of the weapon, and
  the ability on the place of the key. It refuses the press when the character is dead, the
  input is locked (a travel that the client did not confirm), the weapon has no ability
  table, the place is empty, the character does not have the node, the ability has no rule
  or the data has no row for it, the cooldown runs, or a different cast plays. A refused
  press changes nothing: no cast, no cost, no hit. The backend sends no refusal and no
  correction: the client has no message for them (live check L2). The session writes one log
  line for the first refusal of each cause, and `tools/world list` counts all of them.
- The cooldown starts at an accepted press, with the time of the data (`Time`, and the
  minimum `MinTime`) on the world clock. The owner gets the cooldown state (slot 45, type
  2932): one map for each place of the bar. Only the owner gets it. A cooldown is per ability,
  not per place.
- The cost of the six abilities is 0 (live check L8: the client predicts no cost, and the
  data gives none). A different weapon can have a cost: check it live before its rule.
- A melee phase is a window of input steps after the press (and the same time on the world
  clock, plus 0.5 s). A contact of the session of the caster in the window is a hit of that
  phase, with its damage row. A creature takes one hit of each phase. The hit check of a melee
  attack stays (the observer, the immune mark, the weapon, the distance, the point, the rate).
- An area phase runs at its time on the world clock. It takes the creatures of its circle
  from the position index, not more than 3 m higher or lower (or the `Height` of the spell),
  with no wall between the center and the creature (`Navigation::sees`), 32 at most, the
  nearest first. A creature takes one hit of each phase. A creature on its way back takes
  nothing. A caster that left the world or died does nothing.
- Each hit of an ability goes through `Combat::strike`: the damage of the row, the creature
  system with the attacker (the target, the credit, the rewards of a kill), and the damage
  number to the owner.
- The effects on a creature go only through the creature system (`Creatures::apply_effect`
  for a status row, `Creatures::knock` for a knockdown or a stagger). The values come from
  the status row of the data (the time, the stun, the root, the slow, the weaken, the end at
  the next hit). The same effect again gets the later end. A creature on its way back takes
  no effect. The behavior obeys each effect at each step.
- The observers. Each session that observes the creature gets its status state (the icons
  at the name plate) and its movement state (the action of the effect). Only the session of
  the caster gets the cooldown state and the damage number (test case AB9).
- The cost of the rules. A phase looks only at the entities near its center. The end of the
  effects looks only at the creatures with an effect. A cast that has no phase to come goes
  away 5 s after its end (test case AB10 has a time limit).
- The server makes no entity of a spell: the client shows no vortex of Gravity Well. The
  projectile of Gravity Well comes from the shot of the client (the attack "gravitywell"), or
  the server throws it 0.83 s after the press when no shot came.
- A new ability needs, in this sequence: a live check of its keys, its contacts, and its
  shots (protocol notes 4.22: L1, L3, L7), its rule in `Abilities::plan` with the rows and the
  spells of the data, a test case, and a live video. Do not make a message of the server for
  an ability that is not proved.

## Commands

```sh
tools/world cooldowns NAME           # the cooldowns that run, with the seconds to their end
tools/world cooldowns NAME reset     # each cooldown of the character ends
tools/world node NAME greataxe Ability_Greataxe_Charge Upgrade_Greataxe_Maelstorm_Rng   # assign nodes (one point each)
tools/world bar NAME greataxe 0 Ability_Greataxe_Charge   # put an ability on place 0 (Q) of the bar of a weapon
tools/world creature effect 2c WarHammer_Shockwave_Stun 5   # give a creature a status effect of the data for 5 s
tools/world creature effect 2c knockdown                    # a knockdown (2 s) or a stagger (1 s)
tools/world list                     # the line "abilities": the casts, the refusals by cause, the hits, the effects
tools/gamedata/abilities.sh          # write ability-data.json to the game data folder
```

- The routes: `GET /v1/dev/abilities?character=ID`, `POST /v1/dev/abilities/reset`, `POST
  /v1/dev/progress/node`, `POST /v1/dev/progress/bar`, `POST /v1/dev/creatures/effect`. The
  admin interface has `assign_node`, `set_ability_bar`, and `reset_cooldowns`.
- The build of the user (`~/Games/soulwarden/work/builds/medium-bruiser-mikars.md`) on the
  development character: give the mastery levels first (`tools/world mastery NAME greataxe
  20`, the same for `warhammer`), then the 19 nodes of each tree with `tools/world node`, in
  the sequence of the tree (a node of a row needs the points of the rows before it), then the
  three places of each bar with `tools/world bar`. The character window shows the result
  (K key, tab "Weapon Mastery").
- `tools/gamedata/abilities.sh` writes `ability-data.json` (98 cooldowns, 4,516 status effects
  of 4,517 rows, 1,777 spells; 0.4 MB) to the game data folder, from the same sources as the
  creature data tool. `--find TEXT...` prints the rows with a text. With no file the backend
  starts, and it refuses each press (`no data`). `tools/gamedata/creatures.sh` writes the
  reaction fragments of each slice (the member `reactions`) for the knockdown and the
  stagger.

## Traps

- The client plays an ability with no answer of the server, and it does not play a press in
  its own cooldown. Thus the client and the server agree in a normal game. A cooldown that
  only the server has (a command, or a different client) shows a countdown, but the client
  plays a press in it: the server then refuses it, and the animation plays with no damage.
- One press after the first weapon change of a session sent the key bits, and the client did
  not play the ability (live check L1, run r2). The server takes the press as a cast: the
  cooldown starts with no animation. In a test, make one press after the weapon change
  before the test, or reset the cooldowns (`tools/world cooldowns NAME reset`).
- After a travel the body of the character faces north, also when the camera faces a
  different direction. Charge and Wrecking Ball go in the direction of the body: walk forward
  for a short time (`tools/hx 'keydown 0x57' 'wait 250' 'keyup 0x57'`) before such an ability.
- The ability uses the weapon in the hand. A world entry selects the first weapon slot: key
  `1` or `2` (`tools/hx 'key 0x32'`) selects a weapon slot. A weapon with no node on its bar
  refuses each press (`the place of the bar is empty`).
- The stun of Shockwave ends at the next hit (`StopOnHitCount`). To see the stun in a video,
  do not hit the creature in the 2 s.
- The vortex of Gravity Well has no picture in the client: the root and the slow show only
  as the icon at the name plate (the root only) and as a creature that does not move. Show it
  with the track of the creature (`tools/world track ID SECONDS --tsv FILE`).
- With `tools/world ignore NAME on` no creature fights the character, also after a hit: a
  creature with a slow then does not run. Turn the ignore value off for a video of a slow or
  a root (the creature must run to the character), and put a second creature with no effect
  beside it for the comparison.
- `tools/gamedata/abilities.sh` with no argument writes into the common game data folder: the
  next start of the user then has the file. For a test, give it an output path and give the
  run its folder with `SOULWARDEN_GAMEDATA_DIR`.
