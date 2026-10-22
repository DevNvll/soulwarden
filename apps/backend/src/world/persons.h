#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gamedata/conversation_data.h"
#include "gamedata/interaction_data.h"
#include "world/entity.h"
#include "world/interactions.h"
#include "world/quests.h"

// The persons (stage 2.5c, docs/systems/persons.md; research npc-conversations.md, quest-engine-3.md Q2 and Q6): the use of a person of
// the world (the key E, OpenConversation), the selection of its conversation state for one character, the topics of that state (messages
// 3158 and 5397), and the services of the state for the grant API of stage 2.5b.
//
// Where a person comes from (live check L1 of stage 2.5c): a person child of a PrefabSpawner of a level placement (m_isClientAuthoratative
// false, m_spawnOnInitialize true) is made by the client at the load of the level, with an id from its place, its yaw, and its slice asset
// (client 0x6160B60). The interaction data has each such slot with that id (tools/gamedata/interactions.py): a request names its authored
// slot, and the base of stage 2.5a checks it (the reference, the option, the range). A direct person is a level object (stage 2.5a). The
// server makes no body for a person.
//
// The presence for one character (research decision 4, quest-engine-3.md Q2; live check A2): the client shows a person of a swap or a
// walkaway slice by the achievements of its own player. The person system checks the same rule at E with the bits of the character: a
// character for whom the person is not there cannot use it. A change of the bits of a character with a person conversation ends the use
// and the service grant when the person goes for that character (the person of the other characters stays: no shared state changes).
//
// The conversation state (client 0x37AC710, 0x37AB440, 0x37AB7A0; the rules of each requirement: the static read of stage 2.5c and
// docs/systems/persons.md): the states of the NPC row in reverse, the first whose requirements pass; then the topics of that state whose
// requirements pass, and the passing ProvidedTopics of each of them. The backend has no faction and no faction change of a character: a
// named faction and a faction cooldown do not pass. The selection is a value of the character (Entity::conversation) with the revision of
// the state of the character; a topic request after a change of that state makes the selection again (a stale revision).
//
// The topics (3158 during the conversation, 5397 at its close): a topic of the selection of the character at its source counts one time
// (a second selection or finish changes nothing). Stage 1.8b: the finish (5397) gives the effects of the topic (its AchievementUnlock and
// the TaskConversationTopic tasks of the character: Quests::topic_finished).
//
// The quests of a person (stage 1.8b): the offer (3772) of an objective of the selected state of the character (LinearObjectiveId1..N),
// and the hand-in (794) of a ready quest whose NpcDestinationId is the person. Both need the open conversation of the character with a
// person that is there for it and near. 3772 and 794 name no person (research npc-conversations.md): the conversation of the character is
// the source.
//
// The source of a conversation (code review of stage 2.5c, findings PR5C-R1 to PR5C-R3): only a close of the client (2473, the end of its
// block) keeps the closed conversation for the 5397 of the close; each other end (the range, the removal of the source, a new use of any
// source, a travel, the death, the leave) forgets it. One function ends a conversation whose person is not there any more (its use, its
// grant, its value). Each new conversation and each new selection sets the service grant to the services of the state of now (an empty
// set ends the grant).
//
// The services (research Q3, shops-stage.md Q5): the services of the selected state (the numbers of the client enum) go to the grant API of
// stage 2.5b (Interactions::grant_service, kind "conversation"), which keeps them after a conversation-only close. No operation of a service
// is in stage 2.5c (a shop purchase is stage 5.1).
//
// The cost (MP-8): a use finds its person in the reference map of the interaction data (stage 2.5a); a selection looks at the states of
// one NPC row and the quests of one character; a topic request looks at one character; a change of the bits looks only at the characters
// with a person conversation. No tick scans the persons of the world. World thread only.

namespace soulwarden::backend::world {

class World;

/** The kind of the service grant of a person (Interactions::grant_service). */
inline constexpr const char* kConversationServiceKind = "conversation";

/** @return The number of a service of the data in the client enum (registrar 0x37B3DA0; research Q3), or no value. */
[[nodiscard]] std::optional<std::uint32_t> conversation_service_id(std::string_view name);

/** The selection of a person for one character: the state and the offer. */
struct PersonSelection {
    const gamedata::ConversationNpc* npc{nullptr};
    std::int32_t state{-1};
    std::vector<std::uint32_t> topics;
    std::vector<std::uint32_t> services;
    /** The services of the state with no number in the client enum of this code (not in the grant). */
    std::vector<std::string> unknownServices;
};

/** The result of a topic request (3158, 5397). */
enum class TopicResult : std::uint8_t {
    accepted,
    /** The character finished this topic in this conversation: no change. */
    duplicate,
    /** The character has no person conversation (or, for 3158, no open one). */
    noConversation,
    /** The backend has no conversation data. */
    noData,
    /** The person of the conversation is not there for the character now. */
    notPresent,
    /** The data has no topic of this CRC. */
    unknownTopic,
    /** The selection of the character at its source does not offer the topic. */
    notOffered,
    /** 5397 (code review of stage 1.8b, finding QR8B-R3): the character did not select the topic (3158) in this conversation. */
    notSelected,
    /** 5397 (finding QR8B-R6): the accepted position of the character is out of the range of the bound of the person. */
    tooFar,
    /** 5397 (finding QR8B-R4): the database did not take the effects: the topic is not finished, and a new 5397 can finish it. */
    storeFailed,
};
inline constexpr std::size_t kTopicResultCount = 10;

/** Stage 1.8b: the result of an offer (3772) or a hand-in (794) of the client. */
enum class QuestRequestResult : std::uint8_t {
    accepted,
    /** The character has no open person conversation. */
    noConversation,
    /** The person of the conversation is not there for the character now (its use and its grant end). */
    notPresent,
    /** The character is farther from the person than the range of its bound. */
    tooFar,
    /** The quest data has no objective of this CRC. */
    unknownObjective,
    /** 3772: the selected state of the character does not offer the objective. */
    notOffered,
    /** 794: the person of the conversation is not the NpcDestinationId of the objective. */
    wrongPerson,
    /** The quest system refused it (QuestRequest::error: a second accept, a quest that is not ready, a reward that cannot be paid, ...). */
    refused,
};
inline constexpr std::size_t kQuestRequestResultCount = 8;

[[nodiscard]] const char* quest_request_text(QuestRequestResult result);

struct QuestRequest {
    QuestRequestResult result{QuestRequestResult::accepted};
    QuestError error{QuestError::none};
    std::string detail;
    [[nodiscard]] bool ok() const {
        return result == QuestRequestResult::accepted;
    }
};

[[nodiscard]] const char* topic_result_text(TopicResult result);

/** The counts of the person system: for the log, the commands, and the tests (NC7). */
struct PersonCounts {
    std::uint64_t uses{0};
    std::uint64_t notPresent{0};
    std::uint64_t selections{0};
    /** The states and the topics that the selections visited (the work of a selection). */
    std::uint64_t stateVisits{0};
    std::uint64_t topicVisits{0};
    std::array<std::uint64_t, kTopicResultCount> topics{};
    std::uint64_t grants{0};
    /** The characters that a change of their bits made the system look at, and the ends of a conversation for a lost presence. */
    std::uint64_t presenceChecks{0};
    std::uint64_t presenceEnds{0};
    /** Stage 1.8b: the results of the offers (3772) and of the hand-ins (794). */
    std::array<std::uint64_t, kQuestRequestResultCount> offers{};
    std::array<std::uint64_t, kQuestRequestResultCount> handIns{};
};

/** One person near a character, for a reader (a command). */
struct PersonView {
    const gamedata::InteractionObject* object{nullptr};
    float distance{0.0F};
    bool present{false};
    PersonSelection selection;
};

/** The persons near a character, for a reader. */
struct PersonsView {
    bool inWorld{false};
    std::optional<PersonConversation> conversation;
    std::vector<PersonView> persons;
};

class Persons final : public InteractionHandler {
public:
    /**
     * @param data The conversation data (game data), or null: a person keeps its conversation (3743 result 1, as in stage 2.5a), with no
     *        state selection, no topic, and no service.
     */
    Persons(World& world, const gamedata::ConversationData* data);

    [[nodiscard]] const gamedata::ConversationData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    // InteractionHandler: OpenConversation of a person (the kind npc).
    [[nodiscard]] const char* name() const override {
        return "person";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override;
    void started(Entity& character, const InteractionContext& context) override;
    void end(Entity& character, const InteractionContext& context, InteractionEnd cause) override;

    /** @return True when the person of a binding is there for the character now (its presence rule with the bits of the character). */
    [[nodiscard]] bool present(const Entity& character, const gamedata::PersonBinding& person) const;
    /** @return The selection of a person for a character now (its state and its offer). No data: no state. */
    [[nodiscard]] PersonSelection select(const Entity& character, const gamedata::PersonBinding& person);

    /**
     * A topic of the client of a character: 3158 (`closing` false: the conversation must be open) or one topic of 5397 (`closing` true:
     * the close of the conversation). A valid topic counts one time; each other result changes nothing.
     */
    TopicResult topic(Entity& character, std::uint32_t topicCrc, bool closing);
    /**
     * The topics of one 5397 (the close of the conversation; code review of stage 1.8b, findings QR8B-R3 to R6): the whole list is checked
     * before any effect (the source, its presence, the range of its bound with the accepted position, each topic selected with 3158 in this
     * conversation and not finished), then the effects of all valid topics are one plan of the quest system, and the topics are finished
     * only after its commit. @return The result of each topic, in the order of the list.
     */
    std::vector<TopicResult> finish_topics(Entity& character, const std::vector<std::uint32_t>& topics);
    /** Finding QR8B-R6: an accepted position of the character (Interactions::moved): a closed conversation out of its range ends. */
    void moved(Entity& character);
    /** Stage 1.8b: the client accepts an offer (3772, the CRC-32 of the ObjectiveID). */
    QuestRequest accept(Entity& character, std::uint32_t objectiveCrc);
    /** Stage 1.8b: the client hands in a quest (794, the CRC-32 of the ObjectiveID). */
    QuestRequest hand_in(Entity& character, std::uint32_t objectiveCrc);
    /**
     * Code review of stage 2.5c, finding PR5C-R1: the interaction system accepted a new use of any source for the character. Its conversation
     * (also a closed one) is not a source any more.
     */
    void source_replaced(Entity& character);
    /**
     * Code review of stage 2.5c, finding PR5C-R3: the selection of the conversation of the character again when the state of the character
     * changed (a stale revision), with its services into the grant (Interactions::update_service). The interaction system calls it before
     * it checks a grant of the kind "conversation".
     */
    void refresh_offer(Entity& character);

    /** The world calls it when the achievement state of a character changes: a character with a person conversation is checked at the tick. */
    void achievements_touched(const Entity& character);
    /** The world tick: the presence of the persons of the marked characters (O(marked characters)). */
    void tick();
    /** The world calls these: a travel, the death, or the leave ends the person conversation of a character. */
    void traveled(Entity& character);
    void died(Entity& character);
    void left(Entity& character);

    /** @return The conversation of a character and the persons `radius` or less from it (horizontal). */
    [[nodiscard]] PersonsView view(const std::string& characterId, float radius);

    [[nodiscard]] const PersonCounts& counts() const {
        return m_counts;
    }
    /** @return The number of characters with a person conversation (MP-8: a change of bits visits only them). */
    [[nodiscard]] std::size_t conversation_count() const {
        return m_talking.size();
    }

private:
    /** @return The revision of the state that a selection reads (the achievements, the quests, the level of the character). */
    [[nodiscard]] static Revision selection_revision(const Entity& character);
    /** @param topic True: the predicate of a topic (no faction cooldown, no objective rule; Any needs a faction). */
    [[nodiscard]] bool passes(const Entity& character, const gamedata::ConversationRequirements& requirements, bool topic) const;
    /** @return The person binding of the object of a conversation, or null (the data changed: no such object). */
    [[nodiscard]] const gamedata::PersonBinding* binding_of(const PersonConversation& conversation) const;
    /** Ends the conversation of a character (its value); `cause` for the log. */
    void forget(Entity& character, const char* cause);
    /**
     * Code review of stage 2.5c, finding PR5C-R2: the one end of a conversation whose person is not there for the character any more: its
     * use (3746), its service grant, and its value. The presence tick and a topic request use it.
     */
    void lost_presence(Entity& character, const char* cause);
    /** @return The open conversation of a character for a quest request, with its person there and near, or the cause of a refusal. */
    [[nodiscard]] QuestRequestResult quest_source(Entity& character);
    /** The selection again for a stale revision (the state, the topics, the services, and the grant). */
    void reselect(Entity& character, const gamedata::PersonBinding& person);

    World& m_world;
    const gamedata::ConversationData* m_data;
    PersonCounts m_counts;
    /** The characters with a person conversation (MP-8). */
    std::set<EntityId> m_talking;
    /** The characters whose bits changed since the last tick, with a person conversation or a grant of a person. */
    std::set<EntityId> m_recheck;
    std::uint64_t m_serial{0};
};

} // namespace soulwarden::backend::world
