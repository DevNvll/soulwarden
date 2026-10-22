# Interactions

Read this document before a change of the use of an object of the world (the key E or T at a lore page, a person, a chair, a
storage shed, a station of a settlement, a camp, a timed fixture), of the interaction data, or of the states of an object and of
the Interactor of a character, and before a later stage registers a handler (chests 3.5b, gathering 3.6, crafting 3.7, shops 5.1,
transmog 5.6, quests 1.8). The wire contracts are in the protocol notes (sections 4.35 and 4.36).

## Start here

| Task | Start here |
| --- | --- |
| The request of a client, the checks, the reply 3743, the cancels 2473 and 3746 | Protocol notes 4.35, `apps/backend/src/world/interactions.h`, `server/rep/client_input.h` (`InputInteraction`), `server/rep/rep_session.cpp` (`observe_interaction`, `handle_interaction_cancel`), `server/rep/lock_messages.h`, test cases IT2, IT4, IT6 |
| The objects of the level data: the file, the tool, the ids of the client | `tools/gamedata/interactions.sh`, `tools/gamedata/interactions.py`, `gamedata/interaction_data.h`, `tools/gamedata/check_interactions_tool.py`, [Game data](../development/game-data.md), test cases IT1, IT10 |
| The state of an object (Enabled, HasInteractors) and the Interactor state of a character | `server/rep/states/interaction_states.cpp`, `World::make_object`, `EntityKind::object`, test cases IT3, IT7 |
| The lore page: its achievement and the journal | `world/ownership.h` (`Ownership::unlock_achievement`), [DLC and entitlements](dlc-and-entitlements.md) (the achievement state), test case IT5 |
| A handler of a later stage | `world/interactions.h` (`InteractionHandler`, `Interactions::register_handler`; stage 3.5b.1: `owns_sight`, `ends_at_damage`, `Interactions::use_valid`, `end_use`); the chests: [Chests](chests.md) |
| The commands: the list, the enable of an object, the cancel | `server/http/interaction_commands.cpp`, `tools/world interactions`, `tools/world object`, `tools/world interaction cancel`, test case IT9 |
| The server children (stage 2.5b): the stations of a settlement, the template of a slice, the spawn rule, the record of a child | `gamedata/interaction_data.h` (`InteractionTemplate`, `SpawnedPlacement`), `world/interactions.h` (`Interactions::spawn`, `scan`), `World::spawn_object`, `server/rep/states/object_states.cpp`, protocol notes 4.36, test cases SC1, SC2 |
| A crafting station or a camp: the facts of a use for stage 3.7 | `world/interactions.h` (`StationContext`, `Interactions::station`), test case SC2; the crafts: [Crafting](crafting.md) |
| A timed use (Trigger of the powder keg), the ends of a use (the removal, the range, damage) | `world/interactions.h` (`timed_due`, `still_valid`, `damaged`, `moved`), test cases SC3, SC4 |
| The persons (stage 2.5c): the person children that the client makes, the presence for one character, the conversation state, the topics | [Persons](persons.md), `world/persons.h`, cases NC1 to NC8 |
| The service handoff for the shops (5.1) and the transmog (5.6) | `world/interactions.h` (`grant_service`, `require_service`, `close_service`), `world/entity.h` (`ServiceGrant`), test case SC5 |
| The fixtures of a development run (the keg, the camps) | `tools/world fixture`, `server/http/interaction_commands.cpp` (`make_fixture`, `remove_object`), test case SC9 |
| The research | `~/Games/soulwarden/work/research/interaction-stage.md`, `interaction.md`; the results of the stages: `~/Games/soulwarden/work/evidence/interaction/implement/RESULT.md` (2.5a), `~/Games/soulwarden/work/evidence/stations/implement/RESULT.md` (2.5b) |

## Rules

- The client makes each object of the level data itself (a lore page, a person of a settlement, a storage shed, a chair): it
  shows the prompt, and at the key it executes the option at once (the journal opens, a conversation starts) and sends its
  request in its input report (live check L1 of stage 2.5a). The backend validates the request and replies 3743 (result 1 or 2).
  A refusal clears the records of the client: a conversation then closes; a journal stays open (a local window).
- The request is the interaction block of mask bit 8 of an input entry (`InputInteraction`). An older entry with no bit 8 has the
  block of the newer entry before it; the newest entry with no bit 8 has no request. The session gives the world each change of
  the block of a new input step (the rule of the input numbers of the attacks: an old report changes nothing). The same block in
  each report is one request. A block that goes away ends the committed interaction (the close of the journal: the client sends
  no 2473 there, live check L1).
- The checks of a request (`Interactions::check`; each refusal changes nothing; the source of each rule):
  - the character comes from the session (rule MP-5); a dead character is refused;
  - the reference (G and the facet target) is a bound of the interaction data, and the actor id is the actor of its object
    (game data and client code: the UUID rule and the XOR rule, protocol notes 4.35); the option id, the option entity, and the
    action are an option of that bound (game data; the values of the block are claims);
  - a bound with an unresolved name of the data, a status rule, a quest rule, a condition, a line of sight, or an arc is refused
    (a decision of an agent from the research: an object is not open because the backend cannot check a rule of it; no lore page
    and no person of the data has such a rule); a handler that owns the sight (`InteractionHandler::owns_sight`; stage 3.5b.1: the
    chests, whose bounds all have a line of sight that the client checks) takes a bound with a line of sight; a condition of the data
    with no value (one of the three empty condition classes of `interactions.py`) is not a requirement (stage 3.5b.1: each chest and each
    gathering node had one);
  - the bound is enabled (the server state; live check L2);
  - the character has an exact position (an accepted position report: the research), and the 3D squared distance to the bound
    is not more than `m_interactRangeSq` (game data; client code 0x35C2E60, research K4: equal passes). No margin;
  - the level of the option (game data); the achievement of the option and of the bound (game data); an item, a coin, or a
    progression requirement has no check yet, and it is refused (a decision of an agent: none of the handled options has one);
  - the capacity `m_numberOfInteractorsAllowed` of the bound (game data; -1: no limit; a user of the bound passes);
  - a handler of the kind and the option; no handler: refused (the research: the domain is not in the backend).
- A one-shot request whose base action is not its own action is a part of the committed interaction of the character: it needs
  that interaction on the same object with that action. The request of the committed interaction again (the client restores it
  after the part) is no new use and gets no reply. A committed request replaces the committed interaction of the character.
- The handlers of stage 2.5a:
  - the lore page (`ReadLore` 47, committed; `AddLoreToJournal` 50, one-shot; game data): the achievement of the page goes to
    the character (the lore row of the data: `javelindata_loreitems` AchievementId, `javelindata_achievements` AchievementIndex).
    The system writes the row of `achievements` first, then it sets the bit of the entity and marks the achievement state. A
    database that refuses the row gives the refusal 3743 result 2 and no change. A read again writes no row. Live checks L3: the
    journal of the client shows each page of a bit of the achievement state, also a page that the client did not read in that
    run; after a restart of the backend and a new world entry the prompt shows "Already Read".
  - the presentation of the client: `OpenConversation`, `ChairInteractionAnimation`, `BedInteractionAnimation`,
    `PlayInteractionAnimation`, `PlayPetAnimation`, `PlayCinematicVideo` (a decision of an agent from live check L1: the client
    plays these from its own data, the rows have no item, status, or reward, and a refusal closes a conversation that the client
    opened). The checks of the base still apply: a chair with one place takes one character.
- A committed request replaces the committed interaction of the character only after its handler accepted it: a refusal (a
  database that refuses the lore row) leaves the use before it as it was, with its users and its Interactor state (code review of
  stage 2.5a, finding INT-R1, case IR1).
- After a matched 2473 the session forgets the block of that attempt: a later input step with the same block is a new request
  (finding INT-R2, case IR2: the absent interval of the client can fall between two reports). The input numbers of the processed
  steps stay: an old report changes nothing. Live check of stage 2.5b: after the 2473 of a close the next request is the next key.
- A committed interaction ends at the end of the block (the client), at the message 2473 with its reference, at a new committed
  request, at a command (the client gets 3746), at the death of the character (3746), at a travel or a respawn (the session sends
  3746 and 6709 as before: [Travel and conversations](travel-and-conversations.md)), and when the character leaves the world. A
  2473 with a different reference or with no interaction changes nothing (the answer of the client to 3746). Stage 2.5b:
  - the removal of the source (a server child goes away): 3746 (live: the keg goes away during the hold, the use stops);
  - the loss of the range: an accepted position of the character (an exact report) whose 3D squared distance to the bound is more
    than `m_interactRangeSq` (game data; no margin): 3746. The world calls it only for a character with a use (O(1));
  - damage, for a use whose option ends at damage: 3746. The option with the action PlayInteractionAnimation (64: a chair, a bed,
    an animation): the client action InteractionAnimation ends at IfDamaged (game data: playeractions_interact.actionlist). A timed
    option (Delay Time above 0): a decision of an agent (live check L7 of stage 2.5b: the client goes on with its timed use at a hit,
    and the action DelayedInteraction has no damage rule). A station, a lore page, and a conversation do not end at damage (no rule
    of the data);
  - the check at the end of a timed use (below).
- 3746 closes a conversation and a crafting screen (live check L7 of stage 2.5b): the client sends 2473, and for a conversation 6705
  (the session answers 6709: the input and the camera are free). It does not close the journal.
- The temporary state of a use (the committed interaction, the users of a bound, a disabled bound, a service grant, the server
  children) is a value of the world, not of the database (the research: no resume contract): a restart has none. The read lore
  pages are rows of the database.
- The state of an object (`EntityKind::object`): the system makes the entity of an object when its state first differs from the
  level data (a user, a disabled bound) and keeps it while the backend runs. Its actor id is the managed actor id of its placement
  (game data), its entity id is G of that actor (an odd first byte; an id of the allocator has an even one). Each session observes
  it at the interest distance of a character (100 m, 120 m to leave; a decision of an agent). Its record has the metadata state
  (the slice of the placement and the managed actor id) and the state 2930 of each bound (Enabled, HasInteractors): the client
  applies it to the object that it made from the level data, with no second body (live check L2). The client keeps the last state
  after the stop of the record: a session that observes the entity again sends the state of now.
- The Interactor state of a character (slot 26, type 3752; the owner and each observer): the G of the object of its committed
  interaction, and the empty value 0x00000000FFFFFFFF at its end. A session that got a begin before an end gets the empty value
  first, then the new G (client 0x49199D0 ends the old object only on the empty value: research K8).
- The server children (stage 2.5b, live check L4). A PrefabSpawner of a slice with m_isClientAuthoratative false has a child that
  the server makes; the client shows it only after the record of the server. The interaction data has the station children of the
  placements (`spawned`: the stations of each settlement, `Public_Blacksmith` and the others, and the camps of the permanent camps)
  with their templates (`templates`: the slots of the slice, the initial states, the Assembly, the bounds in the frame of the
  slice, the template children). The rules:
  - the entity of a child (`World::spawn_object`): an actor of the id allocator; the template gives the ids of the client: K = root
    XOR G, an instance id is the entity id of the slice XOR K, a facet target is the instance id plus the component id (protocol
    notes 4.2; live check L4: the block of the client has these ids). The places of the bounds come from the place and the rotation
    of the child (the placement matrix times the m_worldTM of the spawner entity, from the tool);
  - its record: the metadata state (the asset of its slice) and the PositionInTheWorld state (the place and the rotation, at the slot
    of the slice): with these two the client shows a station, its prompt, and its crafting screen (live check L4: the smallest
    record). The backend also sends the BuildableController state with m_initialState of the slice (game data: 5, Complete, for a
    station; it changes nothing that shows), the Ownership state with m_initialPermissionRule of the slice (game data: 1 for a camp;
    without it a camp shows no option: live check L5), and the Interact state of each bound;
  - a template child (`children`: a spawner of the slice with m_isClientAuthoratative false and m_spawnOnInitialize true) comes and
    goes with its parent: the forge of a station is such a child (live check L4: the slice of the station has no mesh; the forge
    `craftingstation_t5_blacksmith_complete` shows at the record of the child). The children of a station: the forge and a spawn
    effect;
  - the spawn rule (`Interactions::scan`, each 500 ms of the world clock): the world makes the station children of the placements
    that are 150 m or less from a character, and removes a child when no character is 200 m or less from it (the distances of the
    spawn points of the creatures; a decision of an agent). A new child has a new entity and a new generation. The camp children of
    the placements (249 permanent camps, most at the places `blankspace_for_player_camp` with no visual) stay in the data for stage
    2.6: the client shows there only "CAMP" and a bind option (live check L5; a decision of an agent);
  - the station children have m_spawnOnInitialize false in the data: the server of the game made them from the state of the town,
    which the game files do not have. The backend makes the authored child of each spawner (a decision of an agent from the
    research: "a fixed valid child").
- Each session observes an object at the distance of a character (100 m, 120 m to leave) from its nearest interest point: a bound,
  or the place of a child with no bound (code review of stage 2.5a, finding INT-R3: a bound of a level object can be more than 100 m
  from the root of its placement; case IR3). The grid of the entities of this system (`for_each_entity_near`) finds them.
- A crafting station or a camp (stage 2.5b; `OpenCraftingOption`, `OpenCraftingOptionLevel60`): the station handler accepts a use
  of an object with an Assembly (an object with no Assembly is refused: the client has no station to open), and
  `Interactions::station` gives the facts of the use (`StationContext`) from the data of the object, never from the block: the
  Assembly key (m_craftingStationEntry), the fee flag (m_chargeCraftingFee), the Assembly entity and its facet target, the parent
  placement and its managed actor, the bound, and the generation. Live check L4: the client has the same key, fee flag, and
  Assembly reference (client 0x2E61E00, 0x2E6A440). A key with no row of the crafting station database (Carpentry2) is accepted and
  marked (`keyResolved` false; a decision of an agent: the client opens its crafting screen at such a key too, live check of stage
  2.5b). Stage 3.7a ([Crafting](crafting.md)): the crafting system registers the handler of the station options of both kinds; it keeps the
  rule of the base for the use (an Assembly) and ends the craft batch of the character at the end of the use. A craft at a key with no row
  is refused. Decision D1 of the orchestrator: the station children of the master slices whose key has no row (Smelting2, Carpentry2,
  Masonry2, Tanning2, Weaving2) use the `_t2` variant of their family (key ending in 3) from the interaction tool (`station_overrides`,
  the members `authoredTemplate` and `override` of a server child); the backend writes one log line with the overrides.
- A timed use (an option with a Delay Time: Trigger of the powder keg, 5 s; live check L6): the client counts the Delay Time from
  its request and executes the object at zero (0x49106F0, 0x49146F0, 0x4910750). The server counts the full Delay Time of the option
  row on the world clock from the accepted request (a decision of an agent from the research: the client timer has no factor 0.6,
  and the rule of the server of the game is not known), checks the use again at the end (the source, Enabled, the life, the exact
  position and the range), and completes it one time (`InteractionHandler::complete`). A failed check ends the use (3746). A
  release (the block goes away), a 2473, a cancel, a travel, the death, and damage before the end: no completion. Damage ends a use
  of a timed option and a use of a handler with `ends_at_damage` (stage 3.5b.1: the open of a chest). A handler with its own time
  gives it with `InteractionHandler::use_time` (stage 3.5b.2: the open time of a chest; code review of stage 3.5b.1, finding CH-R2): the
  use is then a timed use of the base with that time, and the rules of this paragraph apply (the timer, the checks again, the
  completion of a late release). A handler ends a use with `end_use` (3746). The same block in
  each report is the same use: the time does not start again. After the completion the client clears its request (live: 5.0 s after
  the press). The handler of the fixture counts the completions (no explosion, no reward). A report with no block that the world takes
  at the full time or later, before the timer (1 ms after the time), completes the use first through the path of the timer and then
  ends it (code review of stage 2.5b, finding ST-R1, case RY1); an end before the time is a release.
- A cancel before its press (code review of stage 2.5b, finding ST-R2, case RY2): the press (an input report) and its release (2473) go
  on different carrier channels, and a press can come after its 2473. A 2473 that matches no use marks its reference with the sequence
  number of its datagram (`carrier::Connection::delivered_datagram`: one sequence for all channels of a client); a request of that
  reference from an earlier datagram starts no use (`RepSession::m_earlyCancels`). A request of a later datagram ends the mark, and the
  mark ends 3 s after the cancel (a decision of an agent: the numbers have 16 bits).
- The marks of the early cancels (code review of stage 3.4f-A; stage 3.4f-B): each reference has its own mark, 8 at most (finding
  CR-R3, test case RR3: a cancel of a second source does not erase the mark of the first one). A mark also has the input steps of the
  cancelled attempt: the estimate of the input number of the client at the cancel (`AttackTrack::estimate`: the last input and one
  step for each 1/30 s since it) and 5 steps more (`kEarlyCancelSteps`: an entry is in the five reports of its client). A request of a
  newer datagram whose input step is not later than that is the old entry that the client repeats: no use (finding CR-R4, test case
  RR4). A cancel of a reference that the session took in the last 3 s and that has no use is late (the echo of a use that ended,
  `m_seenRequests`): it gives no mark (test case IT6). One cancel for each attempt (code review of stage 3.4f-B, finding CS-R3, test
  case RZ3): the cancel that ends a use, and the late cancel, take the seen request of the attempt out of `m_seenRequests`. A cancel of
  the next attempt of the same reference is thus early when its press did not come yet: it gives its mark, and the delayed press
  starts no use. A use that the world ends (3746: a command, a death, a travel, a respawn) keeps its seen request only until the release
  of the client (an input step with no block or with a different block; code review of stage 3.4f-C, finding AR-R8, case RAR8): an echo
  2473 before the release is late (case IT6), and a cancel after the release belongs to a new attempt (its mark stops the delayed press).
  An echo that comes after the release gives a mark: a new press in the next 5 input steps then starts no use (a trap of the rule). The
  echo of a use that the world ended also ends the attempt of its block when the session still has that block (code review of stage
  3.4f-D, finding SU-R5, case RSU5): the next input step with the same block (no step with no block between, the order of INT-R2) is a
  new request; a block of a different reference stays. A matched cancel and a late cancel (also the echo of a server end) give a mark
  with only the datagram of the cancel (code review of stage 3.5b.1, finding CH-R1, case RCH1): a delayed input report of the held
  block that the client made before the cancel starts no new use; a press in a later datagram is a new request (case RSU5). Source:
  decisions of an agent with the rules of the carrier (protocol notes 4.7).
- The hook `InteractionHandler::started` (stage 3.5b.2): the base calls it after it registered an accepted use, so that the handler can
  end the use (`end_use`) in the same request. The loot bags take the items of Take All there ([Loot bags](loot-bags.md)).
- The service handoff (stage 2.5b; the base of the shops 5.1 and the transmog 5.6; research Q4): a handler of a later stage gives the
  character of a committed use a service of its source (`grant_service`: the kind, the ids of the offer, the revision, a deadline
  only when the data has one). The grant is a value of the entity (`Entity::serviceGrant`), and only this system changes it. It stays
  after the end of the use (the 2473 of a conversation), so that a later operation with no source in its message (a purchase 3741)
  can check it: `require_service` passes for the same world entry (the entity), the same source and generation, the source near
  (the 3D range of its bound), the service, and an id of the offer. An accepted use of a different source (also a use of one time:
  code review of stage 2.5b, finding ST-R4, case RY4), the removal of the source, the range (each accepted position of the character
  tests the range of its source: a walk out of it ends the grant, and a walk back does not give it back; finding ST-R3, case RY3), the
  death, a travel, the leave, an expired deadline, and `close_service` end it. No fixed time limit: the option data has none
  (research). Two characters at one source have two grants. From stage 1.8b (code review of stage 2.5c, findings PR5C-R1 and PR5C-R3;
  cases QRP1, QRP3): a new use of a character first replaces its old person conversation (`Persons::source_replaced`, before the handler
  of the new use starts); a new person conversation with no service ends the old grant; a selection that the person system makes again
  sets the new ids and revision of the grant (`update_service`); and `require_service` for the kind `conversation` also needs the
  conversation of the grant (the same serial as the revision).
- The cost depends on the requests and on the objects near the players, not on the size of the world (rule MP-8; cases IT8, SC7): a
  request finds its bound in the reference map of the data (a hash) or in the map of the server children (by G), the list near a
  character looks only at the cells of 64 m near it, the spawn rule looks at the cells of the grid of the children near each
  character and at the live children, a timed use has a timer of the world, and the world tick has no other interaction work.
- No data (no file): the backend starts with an empty catalog, makes no server child, refuses each request (3743 result 2), and a
  read lore page is not kept. The lock of a conversation (6705, 6709) works as before. A file of format 1 (stage 2.5a) loads with no
  server child.
- The loader refuses a file as a whole when a part is not complete or not consistent: also two achievements with one bit, an
  achievement with no id, a bit above the limit of the achievement state (finding INT-R4, case IR4), a cycle of template children, a
  child with no template, a rotation that is not a unit, a template with no PositionInTheWorld slot, a fixture with no bound, and two
  server children with one key (case SC8), and a template with two components at one slot (its PositionInTheWorld, BuildableController,
  Ownership, Vitals, and Interact slots; code review of stage 2.5b, finding ST-R5, case RY5).
- A later stage registers a handler for a kind and its options (`Interactions::register_handler`); it does not add an action switch
  to the session. Its domain (the loot, the gathering, the crafting) owns its state and its transactions. Stage 3.5b.1 registers the
  chest handler for GatherOption of the kind `gathering` ([Chests](chests.md)). Since stage 3.6a the gathering system registers one
  handler for GatherOption of that kind that wraps the chest handler (`Interactions::handler`): a chest of the chest data goes to the
  chests, a node of the gathering data to the gathering, and an object that neither system knows to the chests when they have no data
  (the refusal "no chest data" of stage 3.5b.1), else to the gathering ([Gathering](gathering.md)).
- Objects that the data does not have (stage 3.6a): a system registers a resolver (`ObjectResolver`, `register_resolver`). A request
  whose reference is not in the reference map and not a server child goes to the resolver: it gives the object of a natural gathering
  node (from its offline id) or of a creature body (from the entity of the creature), with the bound, the option, and the range of its
  slice. The base keeps the object and its bound states (`ExternalObject`, by its G) for the same generation, and checks it as a
  placement; `alive` tells if its source is still there (a body: the same dead creature). `external_entity` makes the object entity of
  a natural node one time (its managed actor at its place, in the grid of the base); `level_entity` makes the entity of a placement of
  the data; the node state of a gathering node goes on these entities. The removal of a creature removes the object of its body
  (`body_removed`): the entries do not grow with the kills.
- The capacity of a bound (MaxInteractors) does not apply to the action 14 GatherResource [client code 0x35C2E60: the check at
  0x35C3858 skips the action]: two characters can use one gathering node (stage 3.6a). Each other action keeps the capacity.
- When the source of a use goes (a body that left the world), the base ends the use with no call of the handler (the object is gone):
  a handler with its own state of the use must check its works itself (the gathering ends such a work at its next tick).

- Stage 1.8c (the quests, [Quests](quests.md)): a bound with InteractOnlyOnQuest and a tag is open only while an Active quest task of the
  character has the tag (the verdict `notOnQuest` else; before stage 1.8c such a bound was refused as unsupported). A bound with
  InteractOnlyOnQuest and no tag stays unsupported. After the start of a use of a bound with a tag the quest system gets the interaction
  (TaskInteract with OnInteractStart 1); after its completion, or at its start for a use with no time, it gets the completed interaction
  (TaskInteract with OnInteractStart 0). The code review of stage 1.8c (corrected in stage 2.5c): the time of a use is the time of its
  handler (`use_time`) or the Delay Time of its option (finding QR8C-R8); `InteractionHandler::complete` gives true only for a completion
  that the handler accepted, and only such a completion counts (finding QR8C-R12: a gather that a full bag refused); a handler that gives
  the completed interaction itself after its commit (`owns_interact_event`: the gathering) gets none from the base (finding QR8C-R13).
- Stage 2.5c (the persons, [Persons](persons.md)): the person handler of `OpenConversation` checks the presence of a person for the
  character (its bits); a person that is not there for the character gives the verdict `notPresent` (3743 result 2). An object with a
  person binding that the tool could not make (`unresolved`) is refused as unsupported. A person of a file of format 3 or before keeps the
  conversation of stage 2.5a. The count `referenceLookups` counts the lookups of the reference map (case NC7).

## Commands

```sh
tools/world interactions NAME [METERS]       # the committed interaction of a character and the objects near it, with their state
tools/world object NAME KEY on|off           # enable or disable an object (KEY: its placement UUID); the prompt goes away
tools/world interaction cancel NAME          # end the committed interaction of a character (its client gets 3746)
tools/world fixture NAME powderKeg 0 2 0 --permission 1   # a fixture near a character (stage 2.5b): the keg, campT5, camp
tools/world fixture remove NAME ENTITY       # remove a server child (its uses end: 3746)
tools/gamedata/interactions.sh               # interaction-data.json: the objects of the level data of the whole world
tools/gamedata/interactions.py --list spawned   # the server children of the placements (the stations), with their keys
```

- The routes: `GET /v1/dev/interactions?character=ID[&radius=N]`, `POST /v1/dev/interactions/object?character=ID&object=KEY&enabled=0|1`,
  `POST /v1/dev/interactions/cancel?character=ID`; stage 2.5b: `POST /v1/dev/interactions/fixture?character=ID&name=NAME[&dx&dy&dz][&yaw][&permission]`
  and `POST /v1/dev/interactions/remove?character=ID&entity=HEX16` (the development routes only). The list has the server children
  (their entity, generation, parent, fixture, completions), the station facts of the use (`station`), the service grant (`grant`),
  and the counts of the timed uses and of the spawn rule. `tools/world object NAME KEY on|off` also takes the key of a live server
  child. The admin commands `set_object_enabled` (an object 30 m or less from the
  character) and `cancel_interaction`; the admin state has the member `interactions` (the objects 30 m or less from the
  character). The tool window has no tab for them yet.
- `tools/world probe CHARACTER ASSET ... --actor HEX32` (a development aid of live check L2): a probe with a given actor id, for
  example the managed actor id of a placement, with the fragments of the command.
- `tools/gamedata/interactions.sh` writes `interaction-data.json` to the game data folder (format 2: 43,225 objects with 43,485 bounds
  in 49 regions of the supported build: 1,157 lore pages, 146 persons, 40,768 gathering nodes, 136 shrines; 425 server children of 93
  templates: 176 stations and 249 camps; the fixtures powderKeg, campT5, camp; 25 MB; about 2 to 4 minutes). `--region NAME` and
  `--radius METERS [--center X Y]` write a smaller file, `--list KIND` prints the objects of a kind (`--list spawned`: the server
  children). The tool stops for a layer with no list Capitals, a named slice with an asset id of a wrong form, one placement id with
  two different placements, and two referenced achievements of one bit (findings INT-R4 to INT-R6, case IR5).
  Format 4 (stage 2.5c): the persons too (44,534 objects, 1,455 of the kind `npc`: 1,221 person children that the client makes, 89 of
  them with two ids, 145 direct persons, 50 with no binding; about 2 minutes). `check_interactions_tool.py --persons` is the check of the
  person rules ([Persons](persons.md)).

## Traps

- After `Enabled` true the prompt comes back only when the character leaves the trigger of the object and comes back: the client
  makes no new focus while the character stays in it (live check L2).
- After `Enabled` false and a stop of the record (the character goes far, the backend stops), the client keeps the object
  disabled until a new record or a new load of the level object.
- The close of the journal sends no 2473: the end is the end of the block. The end of a conversation sends 6705 and then 2473.
- 3746 does not close the journal (a local window): it ends the record of the client, and the client sends 2473.
- A client with no exact position (the hook change off: `SOULWARDEN_EXACT_POSITION=0`) gets each request refused
  (`noPosition`): a conversation then closes at once.
- A bench of the data (`poi_interactfurniture_chair_nomesh`) shows an "E" mark, but the key E sent no request in one live try (stage
  2.5a): the chair rule has only the test case IT7.
- The prompt "Add To Journal" of a page becomes "Already Read" only at a new focus of the page after the bit (a travel, a step out
  of the trigger).
- The file of the tool is 25 MB: the backend reads it in 0.5 s at its start.
- The slice of a station has no mesh: the forge is its template child (a second server entity). A record of the station alone shows a
  prompt in the air and no station (live check L4).
- The prompt of a station shows only in the trigger area of its bound (a cylinder of radius 2 m and height 4 m at the InteractBound
  entity; the bounds of the forge are in the cabin). A person near a station (the artificer at the forge) takes the focus outside it.
- A person that a client spawner makes (the artificer at the forge of Oxboro) is in the interaction data from format 4 (stage 2.5c,
  [Persons](persons.md)): with a file of format 3 or before its conversation is refused (3743 result 2) and closes at once. The commander
  near the lore pages is a level object and works.
- A camp (permanentcamp_master, CampT5) shows no option with no Ownership state, "Camping not unlocked yet." for a character with no
  rank of the categorical progression Camping (client 0x42AB230), and with a rank only "Set Camp Recall Point". Its option
  OpenCraftingOption stays hidden (live check L5: not found; the bound has m_hidePermissionDeniedElement). CampT5 has the authored
  initial state 3: the client shows a blue ghost of the tent.
- The keg fixture has m_initialPermissionRule 4 in the data: "No public access." Make it with `--permission 1` for a use. Its prompt
  shows only in its trigger box (2 x 2 x 3 m at the keg).
- With the reply 3743 result 1, the client clears its timed request after its execution (5.0 s after the press); with no reply it
  kept the request until the release of the key and then sent 2473 (live checks L6 and of the final run).
