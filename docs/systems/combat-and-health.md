# Combat and health

Read this document before a change of a melee attack of a player, the damage rules, the health and
the death of a character, or the mana, and before a live test of a hit. The wire contracts are in the
protocol notes (sections 2.3, 4.5, 4.7, 4.15, and 4.18).

## Start here

| Task | Start here |
| --- | --- |
| A melee attack of a player on a creature: the contact report of the client, the hit check, the damage rule, the damage number | Protocol notes 2.3, 4.7, and 4.15, `apps/backend/src/world/combat.h`, `gamedata/combat_tables.h`, `server/rep/client_input.h`, `server/rep/combat_messages.h`, `server/http/aws_routes.cpp`, `tools/gamedata/combat.sh`, `tools/research/input_packet.py --contacts` |
| A hit of an ability: the contact of an ability phase, the hit of an area, the weaken of a creature | Protocol notes 4.22, [Abilities](abilities.md), `world/combat.h` (`Combat::claim`, `Combat::strike`), `world/abilities.h` (`Abilities::contact_phase`), test cases AB4, AB5 |
| The mana of a character and the mana meter of the HUD (the blue bar below the crosshair) | Protocol notes 4.5, `apps/backend/src/server/rep/states/mana_state.cpp`, test case UT4 |
| The health and the death of a character: the damage of a creature, the damage mark, the death screen, the respawn | Protocol notes 4.18, `apps/backend/src/world/health.h`, `world/combat.h` (`Combat::creature_damage`), `server/rep/health_messages.h`, `server/rep/states/vitals_state.cpp`, `server/http/health_commands.cpp`, `tools/world heal` |

## Rules

- The health and the death of a character (protocol notes 4.18). Only the health system
  (`world/health.h`) changes the health and the life state of a character
  (`Entity::vitals.health`, `Entity::life`); the progress system sets the maximum. The
  damage of a creature attack is computed in one function (`Combat::creature_damage`, a
  project rule: change it there and in the notes together): the damage of the level and of
  the kind of the creature, the row of the contact, the level difference, and the armor of
  the character. The health 0 is the death: the vitals state gets the dead bit (bit 0 of
  `vitalsData`) for the owner and for each observer, and the session of the owner sends the
  death notice (type 3950) with the time to the respawn screen (5 s). Only the session of
  the character that took damage gets the damage mark (type 3601,
  `SessionSink::damage_taken`). A dead character takes no damage, does no damage, and is
  the target of no creature. The respawn request of the client (type 1938) makes the
  character live again with its full health at the nearest travel place, or at the place
  of its death with no travel places: the change of the place is a travel
  (`World::travel`). The world finds the character from the session. The health and the
  death are not in the database: each world entry starts with the full health, and a dead
  character that leaves the world gets its respawn place in its row (`World::leave`).
- The first vitals state of a session for a character that lives has no `vitalsData` and no
  `healthChangeFlags` (the stored bytes of case T2). Each later vitals state of a character
  has the two fields.
- The mana of a character is in `Entity::vitals` and goes to its owner in two states with the
  same values: group 1 of the vitals state, and the mana state (slot 47, type 1652, owner
  only, `states/mana_state.cpp`). The mana meter of the HUD (the blue bar below the
  crosshair) reads only the mana state: with no mana state it shows always. A change of the
  mana marks the two states (`ComponentKey::vitals`, `ComponentKey::mana`). Protocol notes
  4.5. Stage 1.3b: each change of the mana goes through one path (`Abilities::change_mana`:
  a cost, a potion), and the regeneration of the data runs after a spend
  ([Abilities](abilities.md)).
- The heal of a potion, a food, or a perk goes through one function (`Health::heal_amount`,
  stage 1.3b): a character that lives gets the health to its maximum, and the vitals state
  changes. The rest rule (3 percent of the maximum each second after 6 s with no fight) and
  the regeneration of the gear (an earring, a food: a part of the maximum each second, also in
  a fight) run in `Health::tick`.
- A melee attack of a player (protocol notes 4.15). The client finds each melee contact
  itself and reports it in the third list of its input entry (the contact report). The
  report is on in each start: the backend answers one remote configuration request of the
  client with the document that turns the client setting
  `javelin.enable-client-authoritative-melee` on (`server/http/aws_routes.cpp`; the exact
  request of protocol notes 2.3; each other request to that host keeps the reply 404: the
  route compares the full path text and the host with no port, `Router::add_exact`).
  `SOULWARDEN_MELEE_REPORT=0` (backend option `--no-melee-report`) turns it off: an attack then
  has no effect. The document has this one setting: do not add a setting without a live
  check.
- A contact is a claim of a client, not a fact. The session gives each claim to the combat
  system of the world (`world/combat.h`, `Combat::claim`), which finds the attacker from the
  session and checks the claim: the character lives, the target is a creature of the world
  that lives and that the session observes, the reference names a component of its slice, an attack input of
  the client fits the contact, the character has a melee weapon in its hand, the same attack
  did not hurt the creature, the exact position of the character is 5 m or less from the
  creature, and the client made 12 claims or less in 1 s. A contact in the window of a
  phase of an ability of the character (stage 3.3a, `Abilities::contact_phase`) is a hit of
  that phase: the damage row of the phase, the weapon of the cast, and one hit of a creature
  for each phase in place of each attack; the other checks stay. A refused claim changes nothing
  and gets no reply; the log counts the refusals with their cause. The values of one client
  for the check (`world::AttackTrack`: its attack inputs and the counts) are in its session,
  as the values of the position check. The two limits of a hit (the rate of the claims, and
  one hit of a creature for each attack) are values of the character, not of a session
  (`world::HitLimits`, `Combat::limits_of`, with the character id as the key): a new session
  of a character gets the limits that the character has, thus a client that connects again
  and again gets no more hits. The session takes each input step one time: a client sends
  an entry in 5 reports. An input entry that ends before the end of its body has no
  position of the character (`ClientInput::hasCharacter`).
- The damage of an attack is computed in one function (`Combat::damage`), from the combat
  tables (game data) and the entity of the attacker: the weapon of the item, its gear score,
  the damage row of the light or of the heavy attack, the attributes and the level of the
  attacker, and the mitigation, the weakness, and the absorption of the creature. The rule is
  a project rule (protocol notes 4.15): change it there and in the notes together. The health
  goes down through the creature system, each observer gets the vitals state, and only the
  session of the attacker gets the damage number (`SessionSink::damage_dealt`, message type
  2071 for the damage receiver facet of its own character). The client computes no damage.
  Each hit of a player on a creature goes through one function (`Combat::strike`, stage
  3.3a): `Combat::damage` with the row, `Creatures::hurt` with the attacker (the target, the
  credit, the rewards of a kill), the damage number, and the log line. A melee attack, the
  contact of an ability, and the hit of an area of an ability use it; a new hit uses it too.
  The damage of a creature to a character (`Combat::creature_damage`) is multiplied by the
  weaken of the creature (`Creature::damage_factor`: 1 plus the `DMGStandard` of each of its
  effects, not below 0).
- The gear in the two damage rules (stage 1.3b, protocol notes 4.15, 4.18, and 4.23; the
  rules in [Gear and perks](gear-and-perks.md)): the part of the gear of the attacker, the
  conversion of a gem, the DMG and ABS parts, the armor parts, a rend of the creature, and
  BaseDamageReduction. A hit of a fixed amount (a tick, a chain) goes through
  `Combat::strike_amount`. The damage number has a second entry for a converted part. The
  hit of an ability uses the conversion of the gem of the weapon at the press of the cast
  (`CastConversion`, the last argument of `Combat::strike`), not the weapon in the slot at
  the time of the hit (code review of stage 3.3b-S1, finding F3, test case RS3).
- The death of a character ends its spell entities only when the spell row has
  `EndOnCasterDeath` (`Spells::caster_died`; [Spell entities](spells.md)). The revival
  (`Health::heal` of a dead character) and the respawn refresh the gear: the statuses of the
  gear come back (finding F5, test case RS5).
- The weapon in the hand of a character is the item of the weapon slot that its client
  selected with a weapon key (`Entity::combat.selectedWeapon`), or the first weapon slot
  with an item. The sheathe bits and the active bits of the paperdoll state do not follow
  the keys yet (roadmap 3.1).

## Commands

```sh
tools/world heal NAME                # give a character its full health (a dead character lives again at its place)
tools/world hurt NAME 500            # lower the health of a character (the health 0 is its death)
```

- `tools/gamedata/combat.sh` writes `combat-tables.json` to the game data folder (14,073 items
  with a weapon, 587 weapons, 414 damage rows, the defense values of 8,779 vitals rows). It
  reads the data tables and the base attributes file of the game copy from the sources of
  the creature data tool: the extracted files of the work folder are sufficient for it (6 s),
  and `SOULWARDEN_OODLE_LIB` reads the pak files. `tools/gamedata/combat.sh --find TEXT...` prints
  the weapons and the creatures with a text (`1hswordt1`, `wolf_grey_7`). The backend log
  says at the start if the tables are loaded. With no file the backend starts, and each hit
  claim is refused (`no combat tables`). The damage rows of the abilities (the rows
  `GreatAxe_*` and `Warhammer_Damage_*`) are in the same file (`CombatTables::row_index`).

## Traps

- The death screen: the text "YOU DIED!" with a count of 5 s, then the respawn screen
  with the button "Respawn" at `1000 581` (window of 1256x660). The client shows its
  loading screen for 5 s to 8 s after the click. `tools/world heal NAME` makes a dead
  character live again at its place with no loading screen.
- A character is never dead after a login: the health and the death are not in the
  database. A dead character that leaves the world is at its respawn place (the nearest
  travel place) at its next world entry.
- The health bar of the HUD is grey at the full health and red below it.
- The melee contact of the client does not go through a wall: a player cannot hit a
  creature on the other side of the yard wall of the start settlement (7 tries, 0
  contacts).
- The blue bar below the crosshair is the mana meter: it shows when the client has no mana
  state, and it goes away at 100 percent.
- A sword attack gives a contact at 1.8 m or less, a spear attack only at 2.0 m or more:
  select the sword (`tools/hx 'key 0x31'`) for a test of a hit at 1.5 m.
- A click with the left pointer button in the world is an attack: each attack moves the
  character 1 m to 2 m forward (from stage 3.2a an attack hurts a creature: the block
  "Hits").
- A test of an attack: `tools/world ignore Soulwarden on` (if not, the wolf attacks the
  character, and it moves to the character), `tools/hx 'look -900 0'`, `tools/world
  creature spawn Wolf_Grey_7 Soulwarden -2.2 0 0 --yaw 270`, then `tools/hx 'click 628 330'` (a light attack) or
  `'mousedown left' 'wait 600' 'mouseup left'` (a heavy attack). The backend log has one
  line for each hit (`world: hit: NAME hurts creature ID ... N damage of the type T ...,
  health A to B`), one line for the first refused claim of each cause (`rep: hit claim
  refused (CAUSE)`), and the counts at the end of the session (`rep: hit claims of the
  session`).
- Each attack moves the character 0.7 m to 2 m to the creature. A creature has no body that
  stops the character: after some attacks the character is 1 m from the creature, and some
  attacks then give no contact (the thrust of a heavy sword attack at 1.0 m, each spear
  attack at 1.7 m or less). Spawn a new creature at the distance of the test.
- The distance at which an attack gives a contact depends on the weapon: a light sword
  attack 1.8 m (not 2.4 m), a great axe 2.3 m, a light spear attack 2.0 m to 3.4 m, a heavy
  spear attack only near 3.9 m (2 of 10 tries). No contact is not a fault of the backend:
  look at `tools/research/input_packet.py DATA_DIR/captures --contacts`.
- The damage number shows for about 1 s. Take the screenshots in a loop directly after
  the input (6 calls of `tools/shot` need 1 s). The number is the whole part of the damage
  (75.7 shows as 75).
- The kind of an attack comes from the time of the attack input: a click (the button down
  for less than 0.27 s) is a light attack, and a hold is a heavy attack. `'click X Y'` of
  the harness is a light attack. For a heavy attack keep the button down for 0.4 s or
  more (`'mousedown left' 'wait 600' 'mouseup left'`).
- While the button stays down, the client attacks again and again, and it attacks one more
  time after the button went up. Each contact is one hit.
- The mode of the client comes from a document that the client reads before the main menu
  (and again in the main menu). A backend that starts after the client asked gives no
  document for that start. Read the two values with the method of protocol notes 2.3 when
  an attack has no effect: the game log has the line `Retrieved layer's
  "remoteCfg.publicGameplay.ProductId" remote config ... successfully, size: 50` when the
  client got the document.
- `tools/gamedata/combat.sh` writes into the common game data folder: the next start of the
  user then has the damage. For a test of a different file, give the tool an output path
  and give the run its folder with `SOULWARDEN_GAMEDATA_DIR` (the folder needs the other game
  data files too).
- A weapon key (1, 2) selects the weapon slot for the damage of the backend. A world entry
  starts with the first weapon slot again, in the client and in the backend.
- A dead rabbit shows the text "No equipped Knife" at its name: the skinning interaction of
  the client, which the backend does not have.
