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
// (a second finish changes nothing). In stage 2.5c a topic gives no reward and changes no quest task (stage 1.8b does that).
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
};
inline constexpr std::size_t kTopicResultCount = 7;

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
