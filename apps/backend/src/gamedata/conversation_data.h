#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The conversation data of the persons (stage 2.5c, docs/systems/persons.md): the NPC rows, their conversation states in authored order,
// and the topics, with the requirements of each state and topic.
//
// The client builds the conversation of a person from its own tables (research npc-conversations.md Q2): it scans the states of the NPC
// row in reverse and takes the first state whose requirements pass (client 0x37AC710, 0x37AB440), and it shows the topics of that state
// whose requirements pass (0x37AB7A0). The backend makes the same selection for each character (world/persons.h), so that a topic request
// (3158, 5397) and a service of a later stage have a validated source.
//
// The data is game data: the tool tools/gamedata/conversations.sh makes conversation-data.json from the game copy. Without the file the
// backend starts: a person keeps its conversation (the reply 3743 and the close of stage 2.5a), and the backend selects no state, accepts no
// topic, and gives no service.

namespace soulwarden::backend::gamedata {

/** File name of the conversation data in the game data directory of the backend. */
inline constexpr const char* kConversationDataFile = "conversation-data.json";

/** The requirements of a conversation state or of a topic: the source texts of the cells of the data (empty: no requirement). */
struct ConversationRequirements {
    /** RequiredAchievement: an achievement conditional (ids with !, &&, ||, parentheses). */
    std::string achievement;
    /** RequiredLevel: the level of the character must be this or more. No value: none. */
    std::optional<std::uint32_t> level;
    /**
     * RequiredFaction (None, Syndicate, Marauder, Covenant, Any; client converter 0x37AF200: each other text, a name with ! too, is None)
     * and RequiredFactionCooldown (NotInCooldown, InCooldownMultipleDays, InCooldownSingleDay; only a state has it: topic loader 0x379F860).
     */
    std::string faction;
    std::string factionCooldown;
    /** RequiredProgression: "PROGRESSION RANK POINTS" (client parser 0x37AF340), a rank and points of a categorical progression. */
    std::string progression;
    /** RequiredActiveObjectiveId (a list with commas), RequiredActiveTaskId, RequiredCompletedObjective. */
    std::string activeObjective;
    std::string activeTask;
    std::string completedObjective;
    /** A cell that the loader cannot read (a level that is not a whole number): the requirement does not pass. */
    bool malformed{false};
};

struct ConversationTopic {
    std::string id;
    /** The topic on the wire (3158, 5397): the CRC-32 of the lower-case id (client hash 0x12F4730). */
    std::uint32_t crc{0};
    std::string prompt;
    std::string response;
    ConversationRequirements requirements;
    /** The effects of the topic (stage 1.8b: no effect in stage 2.5c). */
    std::string achievementUnlock;
    /** ProvidedTopics: the ids of the next topics (a list with commas), and their indexes (a topic with no row is not in the list). */
    std::string providedTopics;
    std::vector<std::uint32_t> provided;
};

struct ConversationState {
    std::string id;
    /** ConversationType: Linear_Objective or Conversation. */
    std::string type;
    std::string dialogue;
    bool initial{false};
    ConversationRequirements requirements;
    /** The topics of the state (indexes into ConversationData::topics; a topic with no row is not in the list). */
    std::vector<std::uint32_t> topics;
    /** ConversationService1..N: the names of the data (Inn, Open_Shop, ...). */
    std::vector<std::string> services;
    /** LinearObjectiveId1..N (the offers of stage 1.8b). */
    std::vector<std::string> objectives;
};

struct ConversationNpc {
    std::string id;
    std::uint32_t crc{0};
    std::string name;
    std::string title;
    /** The states in authored order (indexes into ConversationData::states; a state with no row is not in the list). */
    std::vector<std::uint32_t> states;
    std::string shop;
    std::string missionBoard;
};

class ConversationData {
public:
    /**
     * Reads the file of the tool (format 1). A file that is not complete is not used, as a whole: a member with a wrong form, two NPC rows
     * or two topics with one CRC-32.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** The same for a document in memory (tests). */
    [[nodiscard]] bool load_text(const std::string& text);
    /**
     * Sets the data with no file (tests). A state or a topic that a row names with no row is not in the list of that row.
     * @return False when two NPC rows or two topics have one CRC-32, or two states have one id.
     */
    [[nodiscard]] bool set(std::vector<ConversationNpc> npcs, std::vector<ConversationState> states, std::vector<ConversationTopic> topics,
                           const std::vector<std::vector<std::string>>& npcStates = {},
                           const std::vector<std::vector<std::string>>& stateTopics = {});

    [[nodiscard]] bool loaded() const {
        return !m_npcs.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    /** @return The NPC row of an NPC id (any case), or null. */
    [[nodiscard]] const ConversationNpc* npc(std::string_view id) const;
    [[nodiscard]] const ConversationState& state(std::uint32_t index) const {
        return m_states[index];
    }
    [[nodiscard]] const ConversationTopic& topic(std::uint32_t index) const {
        return m_topics[index];
    }
    /** @return The index of the topic of a CRC-32 (the topic on the wire), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> topic_of_crc(std::uint32_t crc) const;
    [[nodiscard]] std::size_t npc_count() const {
        return m_npcs.size();
    }
    [[nodiscard]] std::size_t state_count() const {
        return m_states.size();
    }
    [[nodiscard]] std::size_t topic_count() const {
        return m_topics.size();
    }

private:
    bool fail(std::string cause);

    std::vector<ConversationNpc> m_npcs;
    std::vector<ConversationState> m_states;
    std::vector<ConversationTopic> m_topics;
    std::unordered_map<std::string, std::uint32_t> m_npcByLowerId;
    std::unordered_map<std::uint32_t, std::uint32_t> m_topicByCrc;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata
