# Spell entities

Read this document before a change of a spell entity (the vortex of Gravity Well, stage 3.3b-S2a), before a new spell
entity (a beacon, a pylon, a trap, a projectile), and before a live test of a spell. The wire contracts are in the
protocol notes (section 4.25). The cast that asks for a spell is in [Abilities](abilities.md); the interest table, the
records, and the stop are in [World and sessions](world-and-sessions.md).

## Start here

| Task | Start here |
| --- | --- |
| The entity of a spell: its id, caster, place, row, times, and end | `apps/backend/src/world/spells.h` (`world::Spells`, `SpellRequest`, `SpellEnd`), `world/entity.h` (`SpellBody`), `World::make_spell`, test case SP2 |
| The hits of a spell: the phases of the cast at the place of the spell | `Spells::tick`, `Abilities::spell_phase`, `Abilities::throw_projectile`, test case SP3 |
| A cancel: the leave and the death of the caster, a command | `Spells::caster_left`, `Spells::caster_died`, `Spells::end`, test cases SP4, SP9 |
| The record of a spell: the metadata, the Spell state, the delayed events | `server/rep/states/spell_state.cpp`, `server/rep/states/state_codecs.cpp` (`spell_codecs`), `RepSession::add_observer_record`, test cases SP2, SP5 |
| The interest of a spell: a late observer, a leave and a return, two sessions | `World::interest_enter`, `RepSession::update_interest`, test cases SP5, SP6 |
| The spell data of the game (the slices, their slots, their delayed events) | `tools/gamedata/spells.sh` (`spells.py`), `gamedata/spell_data.h`, [Game data](../development/game-data.md), test cases SP1, SP8 |
| The cost of the spells | the due list of `Spells`, test case SP7 |
| A live check of a slice record before its code: the probe entity | `tools/world probe`, `World::make_probe`, `server/http/dev_routes.cpp` |

## Rules

- The spell system (`world/spells.h`, `world::Spells`) makes, changes, and removes the spell entities. A spell entity is
  an entity of the world with the kind `spell` (`Entity::spell`, `SpellBody`): its id from the id allocator, its caster
  (the entity, the actor, and the character id), its position, its spell row, and its start and end on the world
  clock. It has no character, no account, and no session (rules MP-1 to MP-4).
- The make. The abilities system asks for a spell at the landing of the projectile of Gravity Well (`Spells::make`,
  from `Abilities::throw_projectile`), with the phases of the cast that the spell runs (the vortex at its start, and the
  burst). The spell row is the row of the plan: `Spell_GreatAxe_GravityWell_Vortex`, or
  `Spell_GreatAxe_GravityWell_Vortex_Upgrade` with the upgrade node. The key of the row on the wire is the CRC-32 of the
  lower-case SpellID (client code 0x683DE00, live check L4: `B5715A91` for the vortex).
- The times come from the slice of the spell (game data, `spell-data.json`): the burst at the activation (event type 2)
  of the child entity with the damage row of the burst (`GreatAxe_GravityWell_Burst`: 3.0 s), the end at the trigger of
  the child entity with a `DestroyGDEComponent` (3.5 s). With no spell data: the times of the ability rule, and the end
  at the last phase. A delay of the data of 0 or less is the start of the spell (the data has -6 s and 0.0001 s in some
  slices); a positive delay is 1 ms at least (finding F9 of the review of stage 3.3b-S1).
- The phases. At the time of each phase the system gives it to the abilities system (`Abilities::spell_phase`), which
  does the hits of the phase at the place of the spell. Each phase runs one time, while the spell is in the world. The
  vortex and the burst of Gravity Well come only from the spell: the old area path of stage 3.3a does not run them a
  second time (test case SP3).
- The cancel. The leave of the caster ends each of its spells (a rule of the brief of the stage): the phases that did not
  come do not run, and each observer gets the stop. The death of the caster ends a spell only when its row has
  `EndOnCasterDeath` (game data: 912 of 1,777 spell rows). The two vortex rows do not have it: a vortex stays after the
  death of its caster, and its burst hits (a decision of the user is open: the brief of the stage asked for a cancel at
  the death).
- The interest. A session observes a spell when its character is not more than the `m_sliceSpawnRadius` of the slice
  from it (game data: 50 m for the vortex), and not more than the interest distance of a character. The session stops
  it at that distance plus the margin of a creature (20 m). A spell with no slice data has no record: no session
  observes it, and its hits stay.
- The record (`server/rep/states/spell_state.cpp`; one encoder for each state): the metadata state (slot 0: the slice
  asset and the sub id), the Spell state (type 2912, at the slot of the Spell component of the slice: 1 for the vortex),
  and one delayed event state (type 3139) for each `DelayedEventComponent` of the slice (slots 4 to 9 for the vortex),
  with the time that is left at the time of the record. An event whose time passed: a deactivation or a trigger is
  completed (the client applies it at once); the activation of the burst is not completed and waits until 1 s after
  the end of the spell (a completed activation plays the burst again: live check L4). A spell has only the observer
  view: the session of the caster observes its own spell as each other session does.
- No Ownership state (slot 2) and no TemporaryAffiliation state (slot 3): the record shows the vortex with no such
  state (live check L4), and their values are not known. Do not make them up.
- The stop. At its end, at a cancel, and at a command, the world removes the entity (`World::remove_spell`), and each
  observer gets the stop in the control block of a bundle. The client does not remove the entity by itself: the destroy
  event of the slice does not remove the entity group (live check L4).
- The slots and the delays come from the data of each slice. The upgrade slice of the vortex has a different sequence of
  its slots (the root at slot 4): do not use constant slots.
- The load. A spell costs work only at its times: the tick takes only the due entries of the phases and the ends (one
  entry for each phase and each end); the interest of a session looks only at the spells near its character (the
  position index). No rule looks at each spell of the world (test case SP7: 5,000 far spells and 10 near ones, 90 ticks
  with no due entry in less than 1 s, the 5,010 ends in less than 2 s).
- No limit of the count of spells: the cooldown of the cast (30 s) limits the vortexes of one character (a decision of
  an agent; the data has no count limit for this spell).
- A new spell entity needs, in this sequence: a live check of its record with a probe (the smallest record, the fields,
  the delayed events, the stop), its rows in the spell data, its request from its system, a test case with two sessions,
  and a live video.

## Commands

```sh
tools/world cast NAME 2 [--yaw DEGREES]   # press place 2 (F) of the bar for a character (also a remote test character)
tools/world spells                         # the spell entities of the world with their age
tools/world spells end NAME                # each spell entity of a character ends (each observer gets the stop)
tools/world probe NAME ASSET [DX DY DZ] [--sub N] [--radius M] [--fragment SLOT:TYPE:HEX]...   # a probe for a live check
tools/world probe fragment ENTITY SLOT TYPE HEX   # one more fragment for a probe
tools/world probe remove ENTITY            # remove a probe: each observer gets the stop
tools/gamedata/spells.sh [OUTPUT.json]     # write spell-data.json (default: the game data folder)
tools/gamedata/spells.sh --find TEXT...    # print the spells with a text and their slice
```

- The routes: `POST /v1/dev/abilities/cast`, `POST /v1/dev/spells/end`, `POST /v1/dev/world/probe`, `POST
  /v1/dev/world/probe/remove`; `GET /v1/dev/world` has the member `spells`. The admin interface has `cast_ability` and
  `end_spells`. The probe and the fragments of a probe are development commands: the backend has them only with
  `--dev-commands` (the harness gate `SOULWARDEN_HARNESS=1`).
- `tools/world cast` gives the press the checks of a press of a client (the bar, the node, the cooldown). For a
  character with no client the server throws the projectile of Gravity Well 0.83 s after the press, 10 m in the
  direction `--yaw` (degrees from the north to the east).
- `tools/gamedata/spells.sh` writes `spell-data.json` (1,565 spells, 934 slices; 1.1 MB, 7 s). 16 spell rows have no
  slice file, and 143 slices have no Spell component: the counts are in the file, and such a spell has no record. With
  no file the backend starts, and a spell has its hits and no record.

## Traps

- The field masks of a state are chained: each mask byte comes before the values of its 7 fields. A record with both
  mask bytes first (the fixture of the research) gives "Failure during read of ReplicatedStateBundle" in the game log.
- With no delayed event state the client starts the delays of the slice at the record: a late observer then sees the
  full vortex and a burst 3 s after its entry. The spawn time (field 8) does not change this.
- A completed delayed event state executes its event at once, also an activation: a late observer after the burst then
  sees a second burst.
- A video of a vortex: the vortex is dark blue on the ground. At dusk or at night, or when the caster stands between the
  camera and the vortex, it is hard to see. Put the observer so that the vortex is in the open, 20 m to 30 m in front of
  the camera (a travel turns the camera to the north).
- A late observer must be more than 70 m away before the cast (50 m and the margin of 20 m): at 64 m it stays in the
  interest.
- A remote test character keeps its cooldown: `tools/world cooldowns NAME reset` before the next cast.
- The creatures of a test stay where they are: remove them (`tools/world creature remove ID`) before a walk in the same
  street, or they block the character.
