# Interactions

Read this document before a change of the use of an object of the world (the key E or T at a lore page, a person, a chair, a
storage shed), of the interaction data, or of the states of an object and of the Interactor of a character, and before a later
stage registers a handler (chests 3.5b, gathering 3.6, crafting 3.7, shops 5.1, quests 1.8). The wire contracts are in the
protocol notes (section 4.35).

## Start here

| Task | Start here |
| --- | --- |
| The request of a client, the checks, the reply 3743, the cancels 2473 and 3746 | Protocol notes 4.35, `apps/backend/src/world/interactions.h`, `server/rep/client_input.h` (`InputInteraction`), `server/rep/rep_session.cpp` (`observe_interaction`, `handle_interaction_cancel`), `server/rep/lock_messages.h`, test cases IT2, IT4, IT6 |
| The objects of the level data: the file, the tool, the ids of the client | `tools/gamedata/interactions.sh`, `tools/gamedata/interactions.py`, `gamedata/interaction_data.h`, `tools/gamedata/check_interactions_tool.py`, [Game data](../development/game-data.md), test cases IT1, IT10 |
| The state of an object (Enabled, HasInteractors) and the Interactor state of a character | `server/rep/states/interaction_states.cpp`, `World::make_object`, `EntityKind::object`, test cases IT3, IT7 |
| The lore page: its achievement and the journal | `world/ownership.h` (`Ownership::unlock_achievement`), [DLC and entitlements](dlc-and-entitlements.md) (the achievement state), test case IT5 |
| A handler of a later stage | `world/interactions.h` (`InteractionHandler`, `Interactions::register_handler`) |
| The commands: the list, the enable of an object, the cancel | `server/http/interaction_commands.cpp`, `tools/world interactions`, `tools/world object`, `tools/world interaction cancel`, test case IT9 |
| The research | `~/Games/soulwarden/work/research/interaction-stage.md`, `interaction.md`; the result of the stage: `~/Games/soulwarden/work/evidence/interaction/implement/RESULT.md` |

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
    and no person of the data has such a rule);
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
- A committed interaction ends at the end of the block (the client), at the message 2473 with its reference, at a new committed
  request, at a command (the client gets 3746), at the death of the character (3746), at a travel or a respawn (the session sends
  3746 and 6709 as before: [Travel and conversations](travel-and-conversations.md)), and when the character leaves the world. A
  2473 with a different reference or with no interaction changes nothing (the answer of the client to 3746). Stage 2.5b adds the
  range loss and the removal of a source.
- The temporary state of a use (the committed interaction, the users of a bound, a disabled bound) is a value of the world, not of
  the database (the research: no resume contract): a restart has none. The read lore pages are rows of the database.
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
- The cost depends on the requests and on the objects near the players, not on the size of the world (rule MP-8; case IT8): a
  request finds its bound in the reference map of the data (a hash), the list near a character looks only at the cells of 64 m
  near it, and the world tick has no interaction work.
- No data (no file): the backend starts with an empty catalog, refuses each request (3743 result 2), and a read lore page is not
  kept. The lock of a conversation (6705, 6709) works as before.
- A later stage registers a handler for a kind and its options (`Interactions::register_handler`); it does not add an action switch
  to the session. Its domain (the loot, the gathering, the crafting) owns its state and its transactions.

## Commands

```sh
tools/world interactions NAME [METERS]       # the committed interaction of a character and the objects near it, with their state
tools/world object NAME KEY on|off           # enable or disable an object (KEY: its placement UUID); the prompt goes away
tools/world interaction cancel NAME          # end the committed interaction of a character (its client gets 3746)
tools/gamedata/interactions.sh               # interaction-data.json: the objects of the level data of the whole world
```

- The routes: `GET /v1/dev/interactions?character=ID[&radius=N]`, `POST /v1/dev/interactions/object?character=ID&object=KEY&enabled=0|1`,
  `POST /v1/dev/interactions/cancel?character=ID`. The admin commands `set_object_enabled` (an object 30 m or less from the
  character) and `cancel_interaction`; the admin state has the member `interactions` (the objects 30 m or less from the
  character). The tool window has no tab for them yet.
- `tools/world probe CHARACTER ASSET ... --actor HEX32` (a development aid of live check L2): a probe with a given actor id, for
  example the managed actor id of a placement, with the fragments of the command.
- `tools/gamedata/interactions.sh` writes `interaction-data.json` to the game data folder (43,225 objects with 43,485 bounds in 49
  regions of the supported build: 1,157 lore pages, 146 persons, 40,768 gathering nodes, 136 shrines; 24 MB; about 4 minutes).
  `--region NAME` and `--radius METERS [--center X Y]` write a smaller file, `--list KIND` prints the objects of a kind.

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
- The file of the tool is 24 MB: the backend reads it in 0.5 s at its start.
