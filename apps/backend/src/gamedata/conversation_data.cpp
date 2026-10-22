#include "gamedata/conversation_data.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/interaction_data.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/conversations.py). */
constexpr std::uint64_t kFormat = 1;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

/** @return The text of a member, or an empty text (a blank cell is not in the document). False: a member that is not a text. */
bool text_of(const json& entry, const char* key, std::string& value) {
    if (!entry.contains(key)) {
        value.clear();
        return true;
    }
    if (!entry[key].is_string()) {
        return false;
    }
    value = entry[key].get<std::string>();
    return true;
}

bool texts_of(const json& entry, const char* key, std::vector<std::string>& values) {
    values.clear();
    if (!entry.contains(key)) {
        return true;
    }
    if (!entry[key].is_array()) {
        return false;
    }
    for (const json& item : entry[key]) {
        if (!item.is_string()) {
            return false;
        }
        values.push_back(item.get<std::string>());
    }
    return true;
}

/** The requirements of a row (the source texts of its cells). A level that is not a whole number marks the requirement as malformed. */
bool requirements_of(const json& entry, ConversationRequirements& value) {
    if (!entry.contains("requires")) {
        return true;
    }
    const json& cells = entry["requires"];
    if (!cells.is_object()) {
        return false;
    }
    std::string level;
    if (!text_of(cells, "RequiredAchievement", value.achievement) || !text_of(cells, "RequiredLevel", level)
        || !text_of(cells, "RequiredFaction", value.faction) || !text_of(cells, "RequiredFactionCooldown", value.factionCooldown)
        || !text_of(cells, "RequiredProgression", value.progression) || !text_of(cells, "RequiredActiveObjectiveId", value.activeObjective)
        || !text_of(cells, "RequiredActiveTaskId", value.activeTask)
        || !text_of(cells, "RequiredCompletedObjective", value.completedObjective)) {
        return false;
    }
    if (!level.empty()) {
        // The source text of a number cell ("12", "12.0"): a whole number of 0 or more.
        char* end = nullptr;
        const double number = std::strtod(level.c_str(), &end);
        if (end == level.c_str() || *end != '\0' || !(number >= 0.0) || number > 1.0e6 || number != static_cast<double>(static_cast<std::uint32_t>(number))) {
            value.malformed = true;
        } else if (number > 0.0) {
            value.level = static_cast<std::uint32_t>(number);
        }
    }
    return true;
}

} // namespace

bool ConversationData::fail(std::string cause) {
    *this = ConversationData{};
    m_error = std::move(cause);
    return false;
}

bool ConversationData::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("the file is not there");
    }
    std::ostringstream text;
    text << input.rdbuf();
    return load_text(text.str());
}

bool ConversationData::load_text(const std::string& text) {
    const json document = json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file is not a JSON object of the format 1");
    }
    for (const char* list : {"npcs", "states", "topics"}) {
        if (!document.contains(list) || !document[list].is_array()) {
            return fail(std::string("the file has no list ") + list);
        }
    }
    std::vector<ConversationTopic> topics;
    for (const json& entry : document["topics"]) {
        ConversationTopic& topic = topics.emplace_back();
        if (!entry.is_object() || !text_of(entry, "id", topic.id) || topic.id.empty() || !text_of(entry, "prompt", topic.prompt)
            || !text_of(entry, "response", topic.response) || !text_of(entry, "achievementUnlock", topic.achievementUnlock)
            || !text_of(entry, "providedTopics", topic.providedTopics) || !requirements_of(entry, topic.requirements)) {
            return fail("topic " + std::to_string(topics.size() - 1) + " does not have the form of a topic");
        }
    }
    std::vector<ConversationState> states;
    std::vector<std::vector<std::string>> stateTopics;
    for (const json& entry : document["states"]) {
        ConversationState& state = states.emplace_back();
        std::vector<std::string>& names = stateTopics.emplace_back();
        if (!entry.is_object() || !text_of(entry, "id", state.id) || state.id.empty() || !text_of(entry, "type", state.type)
            || !text_of(entry, "dialogue", state.dialogue) || !texts_of(entry, "topics", names) || !texts_of(entry, "services", state.services)
            || !texts_of(entry, "objectives", state.objectives) || !requirements_of(entry, state.requirements)
            || (entry.contains("initial") && !entry["initial"].is_boolean())) {
            return fail("state " + std::to_string(states.size() - 1) + " does not have the form of a conversation state");
        }
        state.initial = entry.contains("initial") && entry["initial"].get<bool>();
    }
    std::vector<ConversationNpc> npcs;
    std::vector<std::vector<std::string>> npcStates;
    for (const json& entry : document["npcs"]) {
        ConversationNpc& npc = npcs.emplace_back();
        std::vector<std::string>& names = npcStates.emplace_back();
        if (!entry.is_object() || !text_of(entry, "id", npc.id) || npc.id.empty() || !text_of(entry, "name", npc.name)
            || !text_of(entry, "title", npc.title) || !texts_of(entry, "states", names) || !text_of(entry, "shop", npc.shop)
            || !text_of(entry, "missionBoard", npc.missionBoard)) {
            return fail("NPC row " + std::to_string(npcs.size() - 1) + " does not have the form of an NPC row");
        }
    }
    if (npcs.empty()) {
        return fail("the file has no NPC row");
    }
    return set(std::move(npcs), std::move(states), std::move(topics), npcStates, stateTopics);
}

bool ConversationData::set(std::vector<ConversationNpc> npcs, std::vector<ConversationState> states, std::vector<ConversationTopic> topics,
                           const std::vector<std::vector<std::string>>& npcStates,
                           const std::vector<std::vector<std::string>>& stateTopics) {
    *this = ConversationData{};
    std::map<std::string, std::uint32_t> stateByLowerId;
    for (std::uint32_t index = 0; index < states.size(); ++index) {
        if (!stateByLowerId.emplace(lower_ascii(states[index].id), index).second) {
            return fail("two conversation states have the id " + states[index].id);
        }
    }
    std::map<std::string, std::uint32_t> topicByLowerId;
    for (std::uint32_t index = 0; index < topics.size(); ++index) {
        ConversationTopic& topic = topics[index];
        topic.crc = interaction_option_id(topic.id);
        if (!topicByLowerId.emplace(lower_ascii(topic.id), index).second || !m_topicByCrc.emplace(topic.crc, index).second) {
            return fail("two topics have the id or the CRC-32 of " + topic.id);
        }
    }
    // The names of the rows to the indexes: a state or a topic with no row is not in the list (a counted diagnostic of the tool).
    for (std::size_t index = 0; index < stateTopics.size() && index < states.size(); ++index) {
        for (const std::string& name : stateTopics[index]) {
            if (const auto found = topicByLowerId.find(lower_ascii(name)); found != topicByLowerId.end()) {
                states[index].topics.push_back(found->second);
            }
        }
    }
    // ProvidedTopics: the next topics of a topic (the client offers them after the topic: 0x3779D87, and accepts them in the state of the
    // topic: 0x375ADF0).
    for (ConversationTopic& topic : topics) {
        std::size_t start = 0;
        while (start <= topic.providedTopics.size()) {
            const std::size_t end = std::min(topic.providedTopics.find(',', start), topic.providedTopics.size());
            std::string name = topic.providedTopics.substr(start, end - start);
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            if (const auto found = topicByLowerId.find(lower_ascii(name)); !name.empty() && found != topicByLowerId.end()) {
                topic.provided.push_back(found->second);
            }
            start = end + 1;
        }
    }
    for (std::uint32_t index = 0; index < npcs.size(); ++index) {
        ConversationNpc& npc = npcs[index];
        npc.crc = interaction_option_id(npc.id);
        if (index < npcStates.size()) {
            for (const std::string& name : npcStates[index]) {
                if (const auto found = stateByLowerId.find(lower_ascii(name)); found != stateByLowerId.end()) {
                    npc.states.push_back(found->second);
                }
            }
        }
        if (!m_npcByLowerId.emplace(lower_ascii(npc.id), index).second) {
            return fail("two NPC rows have the id " + npc.id);
        }
    }
    std::map<std::uint32_t, std::string> crcs;
    for (const ConversationNpc& npc : npcs) {
        if (const auto [other, added] = crcs.emplace(npc.crc, npc.id); !added) {
            return fail("the NPC ids " + other->second + " and " + npc.id + " have one CRC-32");
        }
    }
    for (const ConversationNpc& npc : npcs) {
        for (const std::uint32_t state : npc.states) {
            if (state >= states.size()) {
                return fail("the NPC row " + npc.id + " names a state that the list does not have");
            }
        }
    }
    for (const ConversationState& state : states) {
        for (const std::uint32_t topic : state.topics) {
            if (topic >= topics.size()) {
                return fail("the state " + state.id + " names a topic that the list does not have");
            }
        }
    }
    m_npcs = std::move(npcs);
    m_states = std::move(states);
    m_topics = std::move(topics);
    return true;
}

const ConversationNpc* ConversationData::npc(std::string_view id) const {
    const auto found = m_npcByLowerId.find(lower_ascii(id));
    return found != m_npcByLowerId.end() ? &m_npcs[found->second] : nullptr;
}

std::optional<std::uint32_t> ConversationData::topic_of_crc(std::uint32_t crc) const {
    const auto found = m_topicByCrc.find(crc);
    return found != m_topicByCrc.end() ? std::optional<std::uint32_t>(found->second) : std::nullopt;
}

} // namespace soulwarden::backend::gamedata
