# Persons

Read this document before a change of a person of the world (a person child that the client makes, a direct person), of the
conversation data, of the selection of a conversation state, of the topics 3158 and 5397, of the offers (3772) and the hand-in (794) at a
person, or of the services of a person. Stage 2.5c is the person base; stage 1.8b adds the offers, the hand-in, and the effects of a topic
(the quest rules, the cutscenes, and the whispers are in [Quests](quests.md)); the operation of a shop is stage 5.1. The use of an object
(the request, the checks, the reply 3743, the cancels) is in [Interactions](interactions.md), the lock of the input of a conversation
(6705, 6709) in [Travel and conversations](travel-and-conversations.md), the conditional of an achievement in [Quests](quests.md). The wire
contracts are in the protocol notes (sections 4.55 and 4.56).

## Start here

| Task | Start here |
| --- | --- |
| The person children of the level data: the client id rule, the binding, the presence | `tools/gamedata/interactions.py` (`child_group`, `person_children`, `person_binding`, `PERSON_PRESENCE`), `check_interactions_tool.py --persons` (case NC1), `apps/backend/src/gamedata/interaction_data.h` (`PersonBinding`) |
| The conversation data: the NPC rows, the states, the topics | `tools/gamedata/conversations.sh`, `conversations.py`, `check_conversations_tool.py`, `gamedata/conversation_data.h`, case NC2 |
| The person system: the presence check, the selection, the topics, the ends | `world/persons.h` (`Persons`), `world/entity.h` (`PersonConversation`), cases NC3, NC4, NC8 |
| The rule of each requirement of a state or a topic | `Persons::passes`, `Quests::conditional`, `Quests::task_state`, `Quests::objective_completed`, case NC8 |
| Stage 1.8f: the validity of a state record (IsInitialState XOR a requirement) | `record_valid` and `Persons::selected_state` in `world/persons.cpp`, case NC9; the static read `~/Games/soulwarden/work/evidence/quests/18f/implement/static/STATE-SELECTION.md` |
| The topic messages 3158 and 5397 | `server/rep/conversation_messages.h`, `RepSession::handle_conversation_topics`, case NC4 |
| The services of a person | `Persons::started`, `Interactions::grant_service` (kind `conversation`), case NC5 |
| Stage 1.8b: the offer (3772) and the hand-in (794) at a person | `Persons::accept`, `Persons::hand_in`, `Persons::quest_source`, `Persons::reselect`; `RepSession::handle_objective_request`; cases QB3, QB5, QB6 |
| Stage 1.8b: the effects of a topic, the state of a selection | `Persons::topic` (5397: `Quests::topic_finished`), `Persons::started` (`Quests::state_selected`); case QB4 |
| The corrections of the code review of stage 2.5c (PR5C-R1, R2, R3, R6) | `Persons::source_replaced`, `Persons::lost_presence`, `Persons::refresh_offer`, `Interactions::update_service`; `child_groups` of `tools/gamedata/interactions.py`; cases QRP1, QRP2, QRP3, QRP6 |
| The commands | `server/http/person_commands.cpp`, `tools/world persons`, the admin state member `persons` |
| The research and the results | `~/Games/soulwarden/work/research/npc-conversations.md`, `quest-engine-3.md` (Q2), `shops-stage.md` (Q5); the static read of stage 2.5c: `~/Games/soulwarden/work/research/scratch/persons-25c-static/ANSWERS.md`; the result: `~/Games/soulwarden/work/evidence/persons/25c/implement/RESULT.md` |
| Stage QG4: the direct NPC templates, the presence by the swap controller, the persons apart | `presence_rule`, `person_binding`, `nested_persons`, `APART_CAUSES` of `tools/gamedata/interactions.py`; `check_interactions_tool.py --persons` (case NC1); `InteractionData::persons_apart`; cases QG4B, QG4D; the result `~/Games/soulwarden/work/evidence/quests/qg4/implement/RESULT.md` |

## Rules

The source of each rule is in brackets: [data] the game data, [client] the client code, [live] a live check of stage 2.5c (of stage 1.8b
when the text says so), [user] a decision of the user, [orchestrator] a decision of the orchestrator, [agent] a decision of the agent of
the stage (with the cause), [review] a finding of the code review of stage 2.5c that the stage 1.8b checked and corrected.

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
  The XOR rule of stage 2.5a gives the targets (K = root XOR G). The client computes the place and the yaw in float (Euler Z of
  0x63344A0, the degrees factor at 0x85725F0; static read): the tool rounds f32(x) * 8, f32(y) * 8, f32(z) * 4 (the product of a float and
  8 is exact), and for a value 0.001 or less from a border of a step or of a bucket it writes each id of the candidates of each axis and of
  the yaw (all the combinations; the id of the float rule first; the other keys end in `~1`, `~2`, ...) [review PR5C-R6: the old rule
  rounded in double and wrote one other id, so a place near two borders could omit the id of the client; agent: the tool has no exact copy
  of the float composition of the client]. The file of the build: 1,311 ids (each of the 1,309 ids of stage 2.5c, the 19 of the trace of
  gate R too), 90 slots with more than one id (4 at most) [live check of stage 1.8b: the conversations of the beach persons with the new
  file; case QRP6]. Two slots with one id: the first keeps it (2 in the build) [agent: the client has one entity for the id].
- The binding of a person child [data; research npc-conversations.md Q1]: the NPC id is the column NPCId of the row of its variant
  (m_sliceVariant of the spawner) in the variation table of the child slice, not the defaults of the generic slice. A variant with no
  row, a row with no NPCId, or a swap slice with no presence rule gives a binding with a cause (`unresolved`: 50 in the build of stage
  2.5c, 0 from stage QG4): the backend refuses its use (3743 result 2) [agent: no NPC row to check]. The server binds a request only by
  the exact reference of the data (G and the target); no alias of an actor and no nearest person [orchestrator: the brief of stage 2.5c].
- Stage QG4, the direct NPC templates [data: the spawners of `settlement_morrowtide_complete` (Grayvale, Mourningdale) and `settlement_eastconsolation_complete` (Restless Shores)
  and of 6 more settlement slices have an empty m_sliceVariant, m_spawnOnInitialize 1, m_isClientAuthoratative 0; their child slices
  (`npc_1102_magistrate`, `npc_1502_magistrate`, ...) have their own NPC component and no variation table]: a person child of an empty
  variant whose slice has an NPC component and no variation table gets the m_npcKey of its slice (`"template": true`; 48 objects). Before
  stage QG4 these slots had the cause "the variant '' has no row". Each such spawner has m_spawnOnTrigger 1 too: the client makes the
  person at the load as the other children with m_spawnOnInitialize 1 [live check I2 of stage QG4: Constable Martin Bayles of
  `1104_Overseer` shows his name, the quest marker, and the prompt; the static read Q4 did not find the exact path of these spawners]. A
  child of an empty variant whose
  slice has a variation table keeps the cause (the defaults of a generic slice are no person).
- Stage QG4, the persons apart [data; agent: a list of causes, no object]: the tool walks the PrefabSpawner children below a child slice
  (4 levels, no slice that spawns itself), the slices of an alias asset, and the placements of the other worlds, and writes each person
  that is not an object into the member `personsApart` with its cause (`APART_CAUSES`): `otherWorld` (a placement of an instance or of an
  introduction world; the world names it: `nw_dungeon_*`, `climaxftue_02`, `nw_trial_season_*`), `levelScript` (a dungeon level script of
  the open world, `slices/dungeon/levelscripts/`), `nested` (a child below a child slice: the client id of such a child is not proved),
  `event` (a spawner with m_spawnOnInitialize false: an event or a trigger spawns it; it needs an owner), `alias` (a choice of slices at
  run time), `idTaken` (the second slot of one client id). The build: 39 otherWorld, 2 levelScript, 1 event, 2 idTaken, 0 nested, 0
  alias. The backend reads the list only for the cause of an NPC with no slot (the support manifest: [Quests](quests.md)).
- The presence of a person for one character [research decision 4, quest-engine-3.md Q2; client 0x3369430; live A2]: the client shows a
  person child by the achievements of its own player. The rule of the slice: `masternpc_swap_defaultshown`
  is gone while the SwapAchievementId of its row is true (`hideWhen`, 172), `masternpc_swap_defaulthidden` is there while it is true
  (`showWhen`), `masternpc_walkaway` is there while AE.3_RequiredAchievementConditional is true (`showWhen`; 859 `showWhen` in all), and a
  slice with no swap controller is always there (227). An empty SwapAchievementId keeps the m_achievementName of the slice. Stage QG4
  [client: the static read `~/Games/soulwarden/work/evidence/quests/qg4/implement/static/ANSWERS.md` Q1; data; live A2 and live check I3
  of stage QG4]: with the achievement swap the client sets the state of the GatherableController to Depleted (8) when the conditional is
  true and to Healthy (4) when it is false (0x3369747 to 0x336977E; an empty achievement keeps Invalid), and it shows the entities below
  the state child whose name is the text of that state (0x33699D2, 0x33699E3). The mode of a person is the state child above its NPC
  entity (`PERSON_SWAP_STATES`): below Healthy it is gone while the condition is true (`hideWhen`: `masternpc_swap_defaultshown`), below
  Depleted it is there while the condition is true (`showWhen`: `masternpc_swap_defaulthidden`, `npc_msq_swap_nessacaptured` of
  `06_NessaHarrower05`, the caged persons of Nighthaven); below no state child it is there in each state; below another state child it is
  a cause. The two named slices keep their rule (the same result); 4 child slots that had no rule get one. The byte
  m_achievementServerState of the server facet has the value of that state child in each slice of the data (4 Healthy, 8 Depleted; the
  count `personSwapStateDiffers` of the tool is 0), but the client does not read it for this rule [client: no reader found]. The
  controller of a person is the controller of the entity of its NPC component, else of its nearest parent entity, else the only
  controller of the slice (a slice of a cage and its person, `io_worldevent_1625_imhotepcage`, has one for each). A direct person whose
  slice has a swap controller has the same rule (21 direct persons and 2 children that were always there: `1225_Giacomo_*`,
  `9930_VK_RomacBarbo*`, `1130_Sensou`, `0317_Warden` of a generic `npc_trapped` slice, ...) [data: the coatgen slices have their variant
  in m_selectedVariant and its achievement in m_achievementName]. The walkaway slice keeps its table rule (`PERSON_PRESENCE`). The person
  system checks the same rule at the key E with the bits of the character (`Quests::conditional`): a person that is not there for the
  character gives the verdict `notPresent` (3743 result 2) [agent: the client of that character does not show it]. Live A2: with the bit
  the person goes for the client, and without it the person comes back (`A2-02-bit-off-return.mp4`).
- A change of the bits of a character with a person conversation: the next tick checks its person again; when the person is not there for
  that character any more, its use ends (3746) and its service grant ends. The persons of the other characters do not change (no shared
  state) [rule MP-2; agent]. A topic request that finds the person not there (before that tick) ends the use and the grant in the same
  way (`Persons::lost_presence`) [review PR5C-R2; case QRP2].
- The conversation data is game data [data]: `tools/gamedata/conversations.sh` makes `conversation-data.json` (format 1): the NPC rows
  (NPCData) with their states in the order of the numbers of their columns (ConversationStateId1 to ConversationStateIdN, not the order of
  their text), the states (ConversationStateData) with their topics, services, and requirements, and the topics (ConversationTopicData)
  with their requirements, AchievementUnlock, and ProvidedTopics. A requirement keeps its source text. 1,478 NPC rows, 3,429 states, 556
  topics in the supported build. The first row of an id wins; a later row, a state with no row, and a topic with no row are counted
  diagnostics. The CRC-32 of the lower-case topic id is the topic on the wire [client hash 0x12F4730; live L3: `e532485a`].
- No conversation data: the backend starts. A person keeps its conversation of stage 2.5a (3743 result 1, the close), with no state, no
  accepted topic, and no service [roadmap rule: a default with no game data file].
- The conversation state of a character [client 0x37AC710, 0x37AB440; static read]: the states of the NPC row in reverse, the first valid
  record whose requirements pass. Stage 1.8f [client 0x379F2F0 to 0x379F377, 0x37AC846 to 0x37AC870; static read
  `~/Games/soulwarden/work/evidence/quests/18f/implement/static/STATE-SELECTION.md`; live Q1 of stage 1.8f]: the loader of the client
  marks a state record valid only as IsInitialState XOR "the row has a requirement", and the selector skips an invalid record before its
  player check. A requirement is a loaded value that is not 0: RequiredLevel, RequiredFaction (a named value, Any too; None, a name with
  `!`, and an unknown name are 0), RequiredFactionCooldown (only the two cooldown values), RequiredCompletedObjective, RequiredAchievement,
  RequiredProgression with an id and a rank or points above 0, RequiredActiveObjectiveId (a list with an id) with RequiredActiveTaskId. A
  requirement that passes for each character counts too (RequiredLevel 1). Thus a state with no requirement that is not the initial state
  (`06_01b_MarcelDupont`: the client offers `06A_MSQ_01_Quartermaster` of `06_01_MarcelDupont`) and an initial state with a requirement
  (`0410a_01_Barkeep`, `1124_PierreAuguste2_Default`) are never selected (`record_valid` in `world/persons.cpp`; case NC9). The data has 50
  invalid records (48 not initial with no requirement, 2 initial with a requirement), none with an offer; in Windsward they change Marcel
  Dupont (3 rows) and Priya Thakur and Utkarsh Singh (their empty state `06_02` hid the topic state `06_01` of `06A_Z_02_SpreadJoy`).
  The backend makes the same selection as the client at the start of the use (`Persons::select`) and keeps it with the
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
  its cause (`TopicResult`). In stage 2.5c an accepted topic gave no reward, set no achievement, and changed no quest task [orchestrator:
  the brief of stage 2.5c]; stage 1.8b gives the effects of a finished topic (the rule above). Live L3: the client sends 3158 with the topic at its selection, then at the close 6705, 2473,
  and 5397 with the finished topics.
- The offer of a quest (3772, stage 1.8b) [client 0x3722700, 0x375A980; live I2 of stage 1.8b]: the person system accepts it only from the
  source of the character: its open person conversation (or the closed one of the close), the person still there for it, the character
  in the range of the bound of the use (not an estimated position), and the objective in the LinearObjectiveId1 to N of the selected state
  of now (a stale selection is made again first, `Persons::reselect`, with the new services) [agent: the client shows only the objectives
  of that state; 3772 names no person]. Then the quest system checks the objective (the RequiredAchievementId of the variant, no active
  instance, no completion: [Quests](quests.md)). Each result has a count (`QuestRequestResult`: accepted, noConversation, notPresent,
  tooFar, unknownObjective, notOffered, wrongPerson, refused).
- The hand-in (794, stage 1.8b) [client 0x37228F0; live P1 and I1 of stage 1.8b]: the same source, and the NpcDestinationId of the
  objective is the NPC of the conversation (else `wrongPerson`) [data]. The quest system finds the active instance of the character and
  completes it with its rewards in one transaction ([Quests](quests.md)).
- The effects of a topic (stage 1.8b) [data; live T2 of stage 1.8b]: 3158 (the selection) records the topic in the list `selected` of the
  conversation and has no effect; 5397 (the finish, at the close) records it in `finished` and gives the world the AchievementUnlock of the
  topic and the event of TaskConversationTopic (the NPC and the topic), one time in one conversation [agent from the live check: an effect
  at 3158 changes nothing in the open conversation, and the person of a walkaway slice must walk away after it]. The selection of a state
  at the start of a use gives the event of TaskConversationState [data].
- The finish of the topics of a 5397 (stage 1.8d, code review of stage 1.8b, findings QR8B-R3 to R6; cases QRB3 to QRB6) [agent through
  the review]: `Persons::finish_topics` takes the whole list of one 5397. A topic counts only when the character selected it with 3158 in
  that conversation (`notSelected` else), the person is there, and the character is in the range of the bound with its accepted position
  (`tooFar`). All topics of the list go to the quest system in one plan (`Quests::topics_finished`): two selected topics of one 5397 both
  finish. A topic is `finished` only after the commit: a refused write of the store (`storeFailed`) leaves it open, and a new 5397 can
  finish it. A character that walks out of the range after the close loses the closed conversation (`Persons::moved`, called by
  `Interactions::moved`): a late 5397 then counts nothing.
- The source of a conversation [review PR5C-R1, PR5C-R3; cases QRP1, QRP3]: a new use replaces the conversation of the character before
  the handler of the new use starts (`Persons::source_replaced`): the old topic context is gone, so a 5397 or a 3772 of the old person
  does not count. Only a close of the client (2473, the end of the block) keeps the closed conversation for its 5397; each other end (a new
  use, the presence, a travel, the death, the leave) forgets it. A new conversation with no service ends the old service grant, and the
  service check of a conversation grant needs the open conversation of the same serial (`Interactions::update_service`).
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
  services, the selected topics (3158), the finished topics (5397), open or closed), and for each person near it the key, the NPC id, the slice, `clientChild`, the presence
  rule, `present` for the character, the distance, and the selection of now (the state, the topics with their CRC, the services); the
  counts (uses, `notPresent`, selections, state and topic visits, the result of each topic, grants, presence checks and ends; stage 1.8b: the offers and the hand-ins with the count of each `QuestRequestResult`).
- `python3 -B tools/gamedata/check_conversations_tool.py` and `check_interactions_tool.py --persons`: the checks of the tools, with made-up
  tables (cases NC1, NC2).

## Traps

- Stage 1.8f: a made-up conversation state for a test needs the shape of the data: a state with no requirement must be the initial state
  (`"initial": true`), or the client rule skips it (case NC9). The fixtures of stages 2.5c and 1.8b had such states (`Pirate_Default`,
  `Inn_Default`, `Review_Talk`, the base states of NC8); they are initial states now, and each rule state of NC8 has RequiredLevel 1, so
  that its record is valid also for a rule that is no requirement (NotInCooldown, a lone objective, 0 and 0).
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
- A person child near a border of the id rule has more than one object (`KEY`, `KEY~1`, ...): the list of `tools/world persons` shows each.
- Stage 1.8b: the client shows an offer and a hand-in from its own state of the person. When the server refuses (a stale bit, a reward
  that it cannot pay), the client gets no answer: it removes the option and waits in the conversation. Compare `tools/world persons` and
  `tools/world quests` with the screen.
- Stage 1.8b: a hand-in moves the quests, and so the selection of the person changes in the same conversation (the next offer shows at
  once). A 5397 of a topic of the old selection is `notOffered` then.
- Stage QG4: a presence condition with a star at the start (`*SwapActive_18007_Cecilia`: 222 person slots of the data, most of Nighthaven) is
  an achievement id with the star: the client keeps the star in the token and its hash (0x3A8AEB0, 0x3A8AF47; static read Q1d). These ids
  are rows of NPAData (428 rows with a star; `*SwapActive_18007_Cecilia` is `u:Quest_18018,l:Quest_18018/Quest_18018_Task_5:c`): the
  presence is the conditional of that row (`Quests::conditional`, the rule of a non-persisted achievement), as in a task.
- Stage QG4: two persons at one place with one slice (`1625_Ghazi_p4` and `1625_Ghazi_p6` at the cave of Brimstone Sands) have one
  client id: only the first slot is in the file, and the second is in `personsApart` (`idTaken`). The offers of the second person (2 rows)
  have no slot. The entity registry of the client keeps the first entity that it registers and refuses the second (0x15E9621,
  "We have an Entity ID collision!"; static read Q2), but which source registers first is not known: the first slot of the file is a
  rule of this project [agent].
- Stage QG4: the persons of `personsApart` are no objects: `tools/world persons` does not list them. Read the member of the file.
- Stage QG4: the presence of a person child is the rule of the client, also for a direct person with a swap controller: a direct person
  that was always there before stage QG4 can be gone for a character now (21 direct persons, for example `1225_Giacomo_*` while
  `SwapActive_1225_GiacomoStart_Off` is set). Compare `tools/world persons` with the screen.
