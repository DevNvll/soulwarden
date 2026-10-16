# The world, the sessions, and the states

Read this document before a change of the world object, the entity table, a session of the game
server, a component state, or the bundles and datagrams that go to a client. The wire contracts are in
the protocol notes (sections 3.5, 4.2, and 4.9).

## Start here

| Task | Start here |
| --- | --- |
| Sessions of the game server: ids, end causes, capture folders | Protocol notes section 3.5, `apps/backend/src/service/rep_server.h` |
| The world object: clock, task queue, timers, entity table | `apps/backend/src/world/world.h`, `entity.h`, `ids.h`, protocol notes 4.9 |
| A component state for the owner and for an observer | `apps/backend/src/server/rep/states`, protocol notes 4.9 |
| A remote player, development commands of the world | `tools/world`, protocol notes 4.9 |
| Ids of an entity, check of the client messages | Protocol notes 4.2, `tools/gamedata/slice_ids.sh` |
| Add a replicated state to the world | Protocol notes section 4, then `tools/st NAME_REGEX` |
| An entity that is not a character or a creature: a spell, a probe, an object of the level data | [Spell entities](spells.md), [Interactions](interactions.md), `World::make_spell`, `World::make_probe`, `World::make_object`, `codecs_of` in `server/rep/states/state_codecs.h` |

## Rules

- One thread owns the world: the listener thread of the game server, or the thread of the
  world object when the backend has no listener (`--no-rep-listener`). A different thread
  (an HTTP route) gives the world a task (`World::post`, `World::call`) and does not read a
  session. A task of `World::call` that did not start in its time limit is cancelled and
  does not run later (`CallStatus::notStarted`: nothing changed); a task that started is
  awaited. A route must not say "no change" for a task that started. The game server and each session read the time only from the world clock
  (`world/world_clock.h`), not from the clock of the computer. The stop of the backend waits
  for each HTTP connection thread before it destroys the world: `HttpServer::stop` joins
  each thread and has no time limit.
- The entity table of the world has one entity for each character that is in the world
  (`world/entity.h`). The name, the position, the appearance, the vitals, and each later
  state of a character are in its entity, not in `RepSession`. The world gives each entity
  its actor id (`world/ids.h`), keeps its position in the position index, and writes its
  position row. A system changes an entity and marks the component state (`World::touch`).
  The world refuses a position that is not finite or that is outside its limits
  (`valid_position`), for each source.
- The kinds of an entity (`EntityKind`): a player, a creature, a spell (stage 3.3b-S2a,
  [Spell entities](spells.md)), and a probe (a development aid for a live check of a slice
  record). Each kind has its own list of codecs (`codecs_of`: `player_codecs`,
  `creature_codecs`, `spell_codecs`, `probe_codecs`). A spell and a probe have only the
  observer view. A session observes a spell or a probe at its own distance (the
  `m_sliceSpawnRadius` of the slice, 50 m for the vortex; not more than the distance of a
  character) and stops it at that distance plus 20 m. A new spell makes each session look for
  spells near its character in the same tick (`World::last_spell`), not in each tick.
- An object (stage 2.5a, `EntityKind::object`, [Interactions](interactions.md)): an object of the
  level data with a state of the server. Its actor id is the managed actor id of its placement
  (game data), not an id of the allocator, and its entity id is G of that actor (an odd first
  byte; the ids of the allocator have an even one). Only the interaction system makes it
  (`World::make_object`). Its codecs (`object_codecs`): the metadata state and the Interact
  state of each bound (the slots of the data). A session observes it at the distances of a
  character (100 m, 120 m) from its nearest bound (code review of stage 2.5a, finding INT-R3: the
  grid of the interaction system finds it, not the place of its root). The Interactor state of a
  character (slot 26) is in the player codecs for the owner and each observer; it has no fragment
  for a character that had no interaction.
- A server child (stage 2.5b, `EntityKind::object` with `ObjectBody::spawned`, [Interactions](interactions.md)):
  a station of a settlement, its forge, a camp, a fixture. Only the interaction system makes it
  (`World::spawn_object`) and removes it (`World::remove_object`). Its actor id comes from the id
  allocator, it is in the position index, and its body is the slice descriptor of its template
  (game data): the asset, the slot of its PositionInTheWorld state with its rotation, the
  BuildableController and the Ownership slots with their authored values, and the slot of each
  Interact bound. The object codecs give it the metadata state, the PositionInTheWorld state, the
  BuildableController state, the Ownership state, and the Interact states (a slot 0: no fragment);
  the metadata codec gives a player the player slice, a creature its slice, and an object the
  slice of its placement or template (case SC1). The client shows a server child only after its
  record (live check L4 of stage 2.5b); the stop of the record removes it in the client, and a
  new interest gives a new full record with one metadata state.
- A server child with a private list (stage 3.5b.2: a personal loot bag, [Loot bags](loot-bags.md)): `ObjectBody::viewers` has the
  character ids that observe it; the interest of a session skips the object when the list does not have the character of the
  session (MP-3: no record, no state). Its LootDrop state (type 2027, `ObjectBody::lootDropSlot`) is in the object codecs. The player
  codecs have the selected Container (slot 76, type 1755) and the GlobalStorage map (slot 77, type 2938), both owner only.
- A projectile of a creature (stage 3.4f-A, `EntityKind::projectile`, [Projectiles](projectiles.md)): only the projectile system makes
  it (`World::make_projectile`: an actor of the allocator) and removes it (`World::remove_projectile`); it moves with its flight
  (`World::place_projectile`: the position index, no state change). Its codecs (`projectile_codecs`): the metadata state of the
  projectile slice and the Projectile state at the slot of the slice (`encode_projectile_parts`: the launch for a session with no record,
  the collision at its contact). A new projectile starts the interest scan of each session at once (`World::last_projectile`); a session
  observes it in the distance of a character while it flies (100 m, it stops it 20 m farther), and an ended one gets no new observer.
- The velocity of a character (stage 3.4f-A, `World::velocity_of`, a rule of this project): `World::place` keeps a sample of the
  accepted positions of a character and the velocity over 60 ms or more; it is 0 after 300 ms with no change and for more than 15 m/s (a
  travel). The lead of a creature shot reads it. The client reports no velocity that the backend reads.
- A probe can have a given actor id (`ProbeBody::actor`, `tools/world probe ... --actor HEX32`;
  live check L2 of stage 2.5a: the record of a level placement).
- The first record of a character has the states of the world that it has at the entry: the
  world marks the status state and the speed table at each entry (`kEnteredComponents` in
  `world/world.cpp`; code review of stage 3.3b-S1, finding F2, test case RS2). A status that
  runs stays through a leave and comes in the first record of the next session.
- The movement of a character for its observers (stage 3.3b-S4, [Forced movement](forced-movement.md)): the state
  `motion` (`ComponentKey::motion`, slot 16, type 11) is the observer view of the movement state with the place, the
  direction (`Entity::facing`), and the action of layer 0; its encoder gives the owner no fragment (the client of the
  owner moves its character itself). The abilities system marks it in the dash of a cast; a character with no client
  gets its place from the server (`World::move_character`), a character with a client from its accepted reports.
- A heal (stage 3.3b-S6, [Combat and health](combat-and-health.md)): the health system changes the vitals of the character
  and marks its vitals state one time; each observer gets the new health (the health of a friend is public), and only the
  owner view has the number bit, for the state that carries the heal (`Vitals::healShown`, a world revision: a later
  state, a new observer, or a re-entry gets no second number). The notice of the healer (message 4298) goes through
  `SessionSink::healed_player` to the session of the healer only. A heal that changes no health sends nothing.
- A session is an observer: it has transport state, the camera estimate of its client, and
  the interest table (entity to interest number). One encoder for each component state
  (`server/rep/states`) makes the bytes for the owner view or for the observer view. No
  other code writes the bytes of a state. A new state type gets its encoder there, with the
  fields of each view in its file and in the protocol notes. A state that only the owner
  gets has the mark `ownerOnly` in its codec (the coin, the bag): an observer gets no
  fragment of it. A state that can be larger than one carrier message gives a list of
  fragments (`PartsEncoder`). The paperdoll encoder has the two views: the owner gets each
  item, and an observer gets the look of the items that show on the body (the armor slots 0
  to 4 and the weapon slots 24 to 29) and nothing of the other slots. A paperdoll fragment
  has only the slots that changed. The progression encoder has the two views too: the owner
  gets the level and the experience, and an observer gets the level (the name tag of a remote
  player shows it). The attribute state, the three skill states, the cooldown state (stage
  3.3a), the charge state of the heart rune (stage 1.3b, slot 48), the speed table (stage
  3.3b-S2a, slot 43; [Status effects](status-effects.md)), and the Stamina state (stage 3.4f-B,
  slot 42, type 4297; [Stamina and dodge](stamina-and-dodge.md)), and the LootTracker state (slot 33, type 982) and the Gathering state
  (slot 36, type 81) of stage 3.5b.1 ([Chests](chests.md)) are for the owner only.
  The status state of a creature (stage 3.3a) has only the observer view. The status state of
  a player (stage 3.3b-S1, slot 44) has the two views: the owner gets its maps, an observer the
  public statuses ([Status effects](status-effects.md)). The Player state of the owner has the
  wall clock pair (group 1, fields 6 and 7, from one sample of the world clock); an observer gets
  no field of group 1. The entitlement state (slot 19) and the achievement state (slot 66) of stage
  1.1b are for the owner only, and they come before the level in the first record
  ([DLC and entitlements](dlc-and-entitlements.md)). The first record of
  a large paperdoll has more than one part: each list group stays whole in one part (stage
  1.3b, [Items and equipment](items-and-equipment.md)), also a group that is larger than one
  carrier message (code review of stage 1.3b, finding F12: its bundle goes in chunks of the
  carrier).
- A change of more than one system is one change of the store (review of stage 3.5a, finding
  F1): the caller opens a batch (`state::Store::Batch`, one transaction), the write of each
  system is a save point in it (`Store::begin_write`, `end_write`), and the batch commits at
  its end. With no commit the batch writes nothing, and the caller puts the entity values
  back and marks their states again (the reward of a kill, `Rewards::killed`).
- A session sends each bundle below the size of one carrier message (1,024 bytes) when its
  fragments permit it: it cuts a record between two fragments, and it sends two fragments of
  one slot in two bundles. It sends no bundle that is larger than the limit of the client
  (256,000 bytes). The metadata state of an entity goes to its owner one time only: a second
  one makes the client of the owner close its connection.
- The point state (slot 67) is never cut into fragments: one fragment has each pool and each
  node of the character, for each size. The client replaces its node list with the list of
  a fragment (live proof, stage 1.7). A state encoder gives a list of fragments only for a
  state whose fragments the client adds to what it has (the bag, the paperdoll).
- A datagram of the carrier has 1,038 bytes at most (`kMaxDatagram` in
  `server/carrier/carrier.cpp`): the client closes its connection when it gets a larger one.
  A message with more than 1,024 data bytes goes in chunks.
- A session ends through one function (`RepServer::end_session`) with a cause, and the
  session leaves the world there (`World::leave`): the world saves the position of the
  character and removes its entity. The destructor of a session saves nothing.
- The world decides who plays a character (`World::enter`). It refuses a character that the
  store does not have and a character of a different account (registration result -160). A
  character is in one session at most: the newer session wins, and the older one gets the
  message "kicked" and ends. A route that removes a character removes its entity first, in
  one task of the world.

## Commands

```sh
tools/world list                     # the entities of the world of the run
tools/world spawn NAME --near OTHER 2 2 0   # a character of the database as a remote player
tools/world remove NAME              # take that entity out of the world
```

- `tools/gamedata/slice_ids.sh` writes `slice-ids.json` to the game data folder: the ids of
  the 186 faceted components of the player slice. With it the game server checks the route
  and the target of each client message (protocol notes 4.2), and the end of a session
  writes the counts to the log (`rep: client messages of the session: ...`). The pak entry
  of the slice has Oodle data: the tool uses `SOULWARDEN_OODLE_DLL` with the helper of
  `tools/research/oodle_loader.c`, or the file that `tools/research/pakx.py` extracted
  (`~/Games/soulwarden/work/assets/extract/slices/player.dynamicslice`). With no file the backend
  starts and makes no check of a target.
- `tools/world` sends a development command to the world of a development run (it finds the
  backend of the data directory and uses `nsenter`): `list`, `characters`, `spawn CHARACTER
  --at X Y Z`, `spawn CHARACTER --near OTHER [DX DY DZ]`, `remove CHARACTER`, `touch
  CHARACTER`, `fragment CHARACTER SLOT TYPE HEX`. `CHARACTER` is a character id or a name of
  the database. `spawn` puts a character into the world as an entity with no session: the
  session of a client near it sends its record, and the client shows a remote player with a
  body and a name tag. `fragment` sends one state fragment with your bytes for an entity: use
  it to test a new state type in a live client before you write its encoder (a slot from 0
  to 4095, a type from 0 to 65535, 16,384 state bytes at most). The backend has these
  commands only in a development run (`--dev-commands`).
- `SOULWARDEN_OBSERVE_PLAYERS=1` (backend option `--observe-players`) makes each session observe
  the entities of other characters near its character (100 m to enter, 120 m to leave). It
  is off for a start of the user and on for a development run (`SOULWARDEN_OBSERVE_PLAYERS=0`
  turns it off there).

## Traps

- `tools/world message CHARACTER TYPE HEX` sends a message to the client of a character:
  the body starts with the facet target (the entity group id plus the id of the component
  in the root entity of the player slice) and the entity group id. It is the fastest check
  of a message of the server before its code (6709 and 3746 of protocol notes 4.20).
- A datagram of the backend above 1,038 bytes makes the client close its connection 30 ms
  after it, with `Unexpected client disconnect! {"transportError":2}` in the game log and
  `carrier: the client closed the connection, reason 2` in the backend log, and the client
  then tries the world entry again some times. In stage 1.7 this came from two messages in
  one datagram, not from a wrong state byte: compare the sizes of the lines `rep: out N (SIZE
  bytes)` with a run that works before you look for a fault in a state.
- One wrong byte in a state fragment makes the read of the bundle fail in the client (game
  log: `Failure during read of ReplicatedStateBundle`), and the other records of that bundle
  are lost. Make the bytes of `tools/world fragment` with a script, not by hand. From stage 3.5b.1 `entity=` of the route of
  `tools/world fragment` also takes an entity of a level object (made by `tools/world object CHARACTER KEY on`): a probe of a state of a
  chest object (live check L2 of stage 3.5b.1).
- `tools/world touch` for a character that a client plays sends each state again but not the
  metadata state: a second metadata state for the own player makes the client close its
  connection and connect again by itself (protocol notes 4.2).
- A fragment for a slot with the state type of a different component gives no error and has
  no effect. Trace the apply function of the facet (virtual function +0xB0 of the object at
  component +0x80) to see that a new state arrives.
- A state field that the server does not send can have random bytes in the client (rotation
  and look direction of the movement state). A fault that occurs only in some runs can come
  from such a field: read the state object in the client memory.
- The client of an observer shows no body and no name tag for a player whose movement state
  has no action: the entity is in the client at its position (kind "remote", remote player
  slice), and nothing shows. The observer view of the movement state has the idle action of
  layer 0 for this cause (`slayerSequenceId` and `slayerScriptLayers`; protocol notes 4.9).
  A new state for a remote entity that has no effect on the screen is not a proof of a wrong
  record: read the entity in the client memory.
- The client does not move a remote character to the ground: an entity 2 m above the ground
  stays in the air. The saved position of a character is an estimate: a remote player at
  that position can be some centimeters to meters from the ground.
- Each world entry gives the character a new actor id (`02000000 534F554C ...`, then `04...`,
  `06...`). A capture or a log of one session does not have the target ids of the next
  session: compute them with the rule of protocol notes 4.2.
- A session that got the message "kicked" or a refused registration ends when its client
  closes the connection, or 1 s after the message. Its inputs change nothing in that time.
- When the user leaves the world, the client sends the carrier disconnect and no DTLS close
  notice. The backend ends the session (`rep: session ended, cause: carrier close`) and
  saves the position. The backend sends no close notice at the end of a session: a client
  that gets one does a new DTLS handshake from the same port at once (two sessions with no
  registration in the log of one run). The secure session of a client that went away ends
  60 s after its last datagram (`dtls: no datagram from port N for 60 s`).
- Each session must send a level information version and a client context instance that
  the game process did not get before, or the client drops each state bundle and stays in
  `WaitingForPlayerSpawn` for 120 s (protocol notes 3.5). The backend uses the session id.
  A change of the level information or of the state bundle must keep this.
- A leave through the Game Menu can give no carrier disconnect: in stage 3.3b-S2a the client
  went to the main menu, and the session ended 30 s later (`cause: idle`).
  `tools/scenarios/leave.sh` then says that the client did not leave. Until the session ends,
  the character is in the world: a command for it changes the character in the world.
- A game process that is stopped (`kill -STOP`) sends no datagram: its session ends after
  30 s (`cause: idle`). `kill -STOP` only the process with the name `NewWorld.exe`: a `kill`
  of the game process stops the backend too (the launcher does this).
