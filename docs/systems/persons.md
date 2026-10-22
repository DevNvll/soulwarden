# Persons

Read this document before a change of a person of the world (a person child that the client makes, a direct person), of the
conversation data, of the selection of a conversation state, of the topics 3158 and 5397, or of the services of a person. Stage 2.5c is
the person base. The offers, the hand-in, the effects of a topic, the cutscenes, and the whispers are stage 1.8b; the operation of a shop
is stage 5.1. The use of an object (the request, the checks, the reply 3743, the cancels) is in [Interactions](interactions.md), the lock
of the input of a conversation (6705, 6709) in [Travel and conversations](travel-and-conversations.md), the conditional of an achievement
in [Quests](quests.md). The wire contracts are in the protocol notes (section 4.55).

## Start here

| Task | Start here |
| --- | --- |
| The person children of the level data: the client id rule, the binding, the presence | `tools/gamedata/interactions.py` (`child_group`, `person_children`, `person_binding`, `PERSON_PRESENCE`), `check_interactions_tool.py --persons` (case NC1), `apps/backend/src/gamedata/interaction_data.h` (`PersonBinding`) |
| The conversation data: the NPC rows, the states, the topics | `tools/gamedata/conversations.sh`, `conversations.py`, `check_conversations_tool.py`, `gamedata/conversation_data.h`, case NC2 |
| The person system: the presence check, the selection, the topics, the ends | `world/persons.h` (`Persons`), `world/entity.h` (`PersonConversation`), cases NC3, NC4, NC8 |
| The rule of each requirement of a state or a topic | `Persons::passes`, `Quests::conditional`, `Quests::task_state`, `Quests::objective_completed`, case NC8 |
| The topic messages 3158 and 5397 | `server/rep/conversation_messages.h`, `RepSession::handle_conversation_topics`, case NC4 |
| The services of a person | `Persons::started`, `Interactions::grant_service` (kind `conversation`), case NC5 |
| The commands | `server/http/person_commands.cpp`, `tools/world persons`, the admin state member `persons` |
| The research and the results | `~/Games/soulwarden/work/research/npc-conversations.md`, `quest-engine-3.md` (Q2), `shops-stage.md` (Q5); the static read of stage 2.5c: `~/Games/soulwarden/work/research/scratch/persons-25c-static/ANSWERS.md`; the result: `~/Games/soulwarden/work/evidence/persons/25c/implement/RESULT.md` |

## Rules

The source of each rule is in brackets: [data] the game data, [client] the client code, [live] a live check of stage 2.5c, [user] a
decision of the user, [orchestrator] a decision of the orchestrator, [agent] a decision of the agent of the stage (with the cause).

- Two kinds of persons [data; live L1]:
  - a direct person: a level placement whose own slice has an NPC component (`4bdb17d5-...`, m_npcKey). It is a level object of stage
    2.5a. 145 in the supported build;
  - a person child: the child of a PrefabSpawner of a level placement with m_isClientAuthoratative false and m_spawnOnInitialize true
    whose slice has an NPC component (the mission slices `masternpc_swap_defaultshown`, `masternpc_swap_defaulthidden`,
    `masternpc_walkaway`, and `npc_variantmaster`). The client makes it itself at the load of the level (its streamer, client 0x17898F0),
    with a client id of tag 7 [client 0x6160B60, called from 0x69EE550; live L1: 19 of 19 ids of the trace]. The server makes no body for
    it. A use gets the reply 3743 and the record of the object state (the metadata state and 2930 with HasInteractors) at this id, as a
    level object of stage 2.5a [agent; case NC3; live L2: the conversation of a person child stays open with the reply 3743 result 1].
    1,221 person children in the supported build. A child with m_spawnOnInitialize false is not made by the client: not in the
    file (2) [client; agent: its server source is not known].
- The client id of a person child [client 0x6160B60, 0x69EE550; live L1]: G = ((((round(z * 4) & 0xFFF) | ((bucket | (asset << 4)) <<
  12)) << 18 | (round(y * 8) & 0x3FFFF)) << 18 | (round(x * 8) & 0x3FFFF)) << 3 | 7, with x, y, z the place of the spawner in the world
  (the placement matrix times the m_worldTM of the spawner entity), bucket the yaw of that transform in degrees from 0 to 360, cut to a
  whole number, divided by 23 (4 bits), asset the first 8 bytes of the asset UUID of the child slice (little-endian; only the low 9 bits
  stay), and round the roundf of the C library (a half goes away from 0; client import 0x7ECAC28). The actor id is G and 8 zero bytes.
  The XOR rule of stage 2.5a gives the targets (K = root XOR G). The client computes the yaw in float (Euler Z of 0x63344A0, the degrees
  factor at 0x85725F0; static read): a slot whose value is 0.001 or less from a border of a step or of a bucket gets both ids in the file
  (89 slots; the second key ends in `~1`) [agent: the tool has no exact copy of the float path]. Two slots with one id: the first keeps
  it (2 in the build) [agent: the client has one entity for the id].
- The binding of a person child [data; research npc-conversations.md Q1]: the NPC id is the column NPCId of the row of its variant
  (m_sliceVariant of the spawner) in the variation table of the child slice, not the defaults of the generic slice. A variant with no
  row, a row with no NPCId, or a swap slice with no presence rule gives a binding with a cause (`unresolved`, 50 in the build): the backend
  refuses its use (3743 result 2) [agent: no NPC row to check]. The server binds a request only by the exact reference of the data (G and
  the target); no alias of an actor and no nearest person [orchestrator: the brief of stage 2.5c].
- The presence of a person for one character [research decision 4, quest-engine-3.md Q2; client 0x3369430; live A2]: the client shows a
  person child by the achievements of its own player. The rule of the slice (`PERSON_PRESENCE` of the tool): `masternpc_swap_defaultshown`
  is gone while the SwapAchievementId of its row is true (`hideWhen`, 172), `masternpc_swap_defaulthidden` is there while it is true
  (`showWhen`), `masternpc_walkaway` is there while AE.3_RequiredAchievementConditional is true (`showWhen`; 859 `showWhen` in all), and a
  slice with no swap controller is always there (227). An empty SwapAchievementId keeps the m_achievementName of the slice. The person
  system checks the same rule at the key E with the bits of the character (`Quests::conditional`): a person that is not there for the
  character gives the verdict `notPresent` (3743 result 2) [agent: the client of that character does not show it]. Live A2: with the bit
  the person goes for the client, and without it the person comes back (`A2-02-bit-off-return.mp4`).
- A change of the bits of a character with a person conversation: the next tick checks its person again; when the person is not there for
  that character any more, its use ends (3746) and its service grant ends. The persons of the other characters do not change (no shared
  state) [rule MP-2; agent].
- The conversation data is game data [data]: `tools/gamedata/conversations.sh` makes `conversation-data.json` (format 1): the NPC rows
  (NPCData) with their states in the order of the numbers of their columns (ConversationStateId1 to ConversationStateIdN, not the order of
  their text), the states (ConversationStateData) with their topics, services, and requirements, and the topics (ConversationTopicData)
  with their requirements, AchievementUnlock, and ProvidedTopics. A requirement keeps its source text. 1,478 NPC rows, 3,429 states, 556
  topics in the supported build. The first row of an id wins; a later row, a state with no row, and a topic with no row are counted
  diagnostics. The CRC-32 of the lower-case topic id is the topic on the wire [client hash 0x12F4730; live L3: `e532485a`].
- No conversation data: the backend starts. A person keeps its conversation of stage 2.5a (3743 result 1, the close), with no state, no
  accepted topic, and no service [roadmap rule: a default with no game data file].
- The conversation state of a character [client 0x37AC710, 0x37AB440; static read]: the states of the NPC row in reverse, the first whose
  requirements pass. The backend makes the same selection as the client at the start of the use (`Persons::select`) and keeps it with the
  revision of the bits, the quests, the level, and the categorical progressions of the character (`Entity::conversation`). A topic request
  after a change of these values makes the selection again (a stale revision) [agent: the client selects again from its state of now].
  Live L2 and L3: the client shows the default state of a person, and the state of a task topic while the task is active.
- The rule of each requirement [client; the static read of stage 2.5c]:
  - RequiredLevel: the level of the character is the value or more [0x37AB467, 0x37AB7BF];
  - RequiredAchievement: the conditional of the achievements (the rule of TaskAchievement, [Quests](quests.md));
  - RequiredFaction [0x37AF200, 0x37AB5B4, 0x37AB898]: the value of the text is Syndicate 1, Marauder 2, Covenant 3, Any 4 (also the
    names Faction1 to Faction3), and 0 for each other text (None, a name with `!`, a name that the client does not know: the data has
    "Maruaders"). For a state, 0 and 4 are no rule; for a topic, 0 is no rule and 4 needs a faction. A named faction must be the faction of
    the character. The backend has no faction of a character (roadmap 4.5): a named faction does not pass, and Any does not pass for a
    topic;
  - RequiredFactionCooldown (a state only; the topic loader 0x379F860 does not read it): NotInCooldown is no rule; InCooldownMultipleDays
    needs more than 24 hours of the cooldown of a faction change, InCooldownSingleDay 1 to 24 hours [0x37AB5F9 to 0x37AB6B5]. The backend
    has no faction change: the two cooldown values do not pass;
  - RequiredActiveObjectiveId and RequiredActiveTaskId (a state only): a rule only when both have a value; one objective of the list
    (commas) has the task Active (1) or CompleteActive (3) [IsTaskActiveById 0x3A49520];
  - RequiredCompletedObjective (a state only): the character has the completion achievement of the objective (AchievementId of its row);
    an objective with no achievement does not pass [IsObjectiveComplete 0x3A48A30]. The client has a first path with a global flag whose
    meaning is not known (an open point);
  - RequiredProgression: "PROGRESSION RANK POINTS"; the rank of the categorical progression of the character is RANK or more, and with
    POINTS above 0 its points are POINTS or more; a rule with no id or with 0 and 0 is no rule; a progression that the character does
    not have has rank 0 and 0 points [0x37AF340, 0x3CB82D0]. The data has it in 31 states (the faction progressions and
    LifeMagicAbilityTable);
  - a cell that the loader of the backend cannot read (a level that is not a whole number) does not pass [agent].
- The topics of a selection [client 0x37AB7A0, 0x37ACF10, 0x375AC50, 0x375ADF0; static read]: the topics of the state whose requirements
  pass, then the passing ProvidedTopics of each of them (the client shows these after their topic and accepts them in the state of the
  topic), each topic one time. The client has no rule that hides a topic after its first use, and AchievementUnlock is no visibility rule
  [static read]: a new conversation offers a topic again.
- A topic request (3158 during the conversation, 5397 at its close) [client; live L3]: the session gives the world only a message to the
  Conversation facet of its own player (rule MP-5). The world accepts a topic when the character has an open person conversation (3158)
  or the conversation of the close (5397), the person is still there for it, the data has the topic, and the selection of now offers it;
  each topic counts one time in one conversation (a second finish: `duplicate`). Each other request changes nothing and has a count with
  its cause (`TopicResult`). In stage 2.5c an accepted topic gives no reward, sets no achievement, and changes no quest task (stage 1.8b)
  [orchestrator: the brief of stage 2.5c]. Live L3: the client sends 3158 with the topic at its selection, then at the close 6705, 2473,
  and 5397 with the finished topics.
- The services of a person [client registrar 0x37B3DA0, builder 0x37ACF10 with its switch table 0x37AE038; static read; live L4]: the
  services of the selected state, as the numbers of the client enum (Inn 1, Faction_Board 2, Community_Board 3, Faction_Selection 4,
  Join_Outpost_Rush 5, Event_Store 6, Horse_Store 7, Wolf_Store 8, Lion_Store 9, Expansion_Modal 10, Fast_Travel_Guide 11, PvP_Guide 12,
  Join_3v3_Arena 13, Transmog 14, Barbershop 15, Open_Shop 16, Open_Mission_Board 17), go to the grant API of stage 2.5b
  (`Interactions::grant_service`, the kind `conversation`, the serial of the conversation as its revision). Faction_Board only for the
  faction of the state and with its level, Faction_Selection also for Any [0x37A7940, 0x37AD46C]. Invalid gives no service; a name with
  no number is a log line. The grant stays after the close (the purchase of stage 5.1 comes after the 2473) and ends with the rules of
  stage 2.5b (a new source, the range, the death, a travel, the leave) and at the loss of the presence. Stage 2.5c has no operation of a
  service. Live L4: the Inn option of an innkeeper shows; its selection sends 2473 and a new request of the action 70 OpenInnScreen
  (option `6cb6b4f9`), which the backend refuses (no handler: a later stage).
- The end of a person conversation: the close (2473, the end of the block, a cancel) marks it closed (its 5397 can come after it); a new
  use, the loss of the presence, a travel, the death, and the leave end it. It is a value of the entity, not of the database: a restart
  and a new world entry have none [the rule of a use of stage 2.5a].
- Two characters at one person [rule MP-10; case NC3]: each has its own selection, its own topics, and its own grant; the close of one
  ends only its own use (the other gets no 6709 and no 3746).
- The cost [rule MP-8; case NC7]: a use finds its person in the reference map of the interaction data (a hash); a selection looks at the
  states of one NPC row and the topic graph of one state; a topic request looks at one character; a change of bits looks only at the
  characters with a person conversation. No tick looks at the persons of the world. NC7: 2,000 and 200,000 authored person slots, 20,000
  requests: the same 20,000 lookups and 32,000 state visits, 0.06 s and 0.05 s.

## Commands

```sh
tools/gamedata/conversations.sh [OUTPUT.json]      # conversation-data.json (a test: a path outside the common game data folder)
tools/gamedata/conversations.py --npc ID           # print the states and the topics of one NPC row, and write no file
tools/gamedata/interactions.sh                     # interaction-data.json format 4: the persons too
tools/world persons NAME [METERS]                  # the person conversation of a character and the persons near it, with the state for it
```

- The route `GET /v1/dev/persons?character=ID[&radius=N]` (the development routes; `tools/world persons`) and the member `persons` of the
  admin state (the persons 30 m or less from the character): the conversation of the character (the NPC, the state, the topics, the
  services, the finished topics, open or closed), and for each person near it the key, the NPC id, the slice, `clientChild`, the presence
  rule, `present` for the character, the distance, and the selection of now (the state, the topics with their CRC, the services); the
  counts (uses, `notPresent`, selections, state and topic visits, the result of each topic, grants, presence checks and ends).
- `python3 -B tools/gamedata/check_conversations_tool.py` and `check_interactions_tool.py --persons`: the checks of the tools, with made-up
  tables (cases NC1, NC2).

## Traps

- A file of the interaction data of format 3 or before has no person child: a conversation with such a person is refused (3743 result 2)
  and closes at once (the trap of stage 2.5b, the artificer at the forge of Oxboro). Make the file again with the tool of stage 2.5c.
- The client opens the conversation at the key E with its own state from its own tables, before the reply of the server. A character with
  a different value on the server (a bit) sees the state of its client, and the server refuses the topics that its own selection does not
  offer: compare `tools/world persons` with the screen.
- The 5397 of the close comes after the 2473 (live L3): the world keeps the closed conversation until a new use, so that the finished
  topics of the close can count.
- Two persons can stand at one place with one slice and one yaw bucket: their ids are equal, and only the first slot is in the file.
- A backend that is older than the route answers 404 to `tools/world persons`: build the backend of the run again.
- The Inn option of an innkeeper closes the conversation with no inn screen: the backend has no handler of OpenInnScreen (action 70) yet.
- A person child near a border of the id rule has two objects (`KEY` and `KEY~1`): the list of `tools/world persons` shows both.
