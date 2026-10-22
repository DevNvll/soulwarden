#include <cmath>
#include <cstdio>
#include <string>

#include "server/http/character_commands.h"

// The commands of the persons (stage 2.5c, world/persons.h, docs/systems/persons.md), in the common layer of the development routes and of
// the admin routes (character_commands.h): the person conversation of a character and the persons near it, each with its NPC row, its
// presence for the character, and the conversation state that the character gets now (the state, the topics, the services).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

std::string hex32(std::uint32_t value) {
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", value);
    return text;
}

json selection_json(const gamedata::ConversationData* data, const world::PersonSelection& selection) {
    json value = json::object();
    value["state"] = selection.state >= 0 && data != nullptr ? json(data->state(static_cast<std::uint32_t>(selection.state)).id) : json(nullptr);
    json topics = json::array();
    for (const std::uint32_t topic : selection.topics) {
        topics.push_back({{"id", data->topic(topic).id}, {"crc", hex32(data->topic(topic).crc)}, {"prompt", data->topic(topic).prompt}});
    }
    value["topics"] = topics;
    value["services"] = selection.services;
    if (!selection.unknownServices.empty()) {
        value["servicesWithNoNumber"] = selection.unknownServices;
    }
    return value;
}

const char* presence_name(gamedata::PersonPresence presence) {
    switch (presence) {
    case gamedata::PersonPresence::always:
        return "always";
    case gamedata::PersonPresence::showWhen:
        return "showWhen";
    case gamedata::PersonPresence::hideWhen:
        return "hideWhen";
    }
    return "?";
}

} // namespace

json persons_view(world::World& target, const std::string& character, float radius) {
    world::Persons& persons = target.persons();
    const world::PersonsView view = persons.view(character, radius);
    const gamedata::ConversationData* data = persons.data();
    json reply = {{"inWorld", view.inWorld}, {"conversationData", data != nullptr}};
    if (view.conversation) {
        const world::PersonConversation& conversation = *view.conversation;
        json topics = json::array();
        for (const std::uint32_t topic : conversation.topics) {
            topics.push_back(data != nullptr ? data->topic(topic).id : std::to_string(topic));
        }
        json finished = json::array();
        for (const std::uint32_t crc : conversation.finished) {
            finished.push_back(hex32(crc));
        }
        reply["conversation"] = {
            {"npc", conversation.npc}, {"npcCrc", hex32(conversation.npcCrc)}, {"open", conversation.open}, {"serial", conversation.serial},
            {"state", conversation.state >= 0 && data != nullptr ? json(data->state(static_cast<std::uint32_t>(conversation.state)).id) : json(nullptr)},
            {"topics", topics}, {"services", conversation.services}, {"finished", finished}, {"revision", conversation.revision}};
    }
    json list = json::array();
    for (const world::PersonView& entry : view.persons) {
        const gamedata::PersonBinding& person = *entry.object->person;
        json item = {{"key", entry.object->key},
                     {"npc", person.npc},
                     {"npcCrc", hex32(person.npcCrc)},
                     {"slice", entry.object->slice.substr(entry.object->slice.rfind('/') + 1)},
                     {"clientChild", person.clientChild},
                     {"presence", presence_name(person.presence)},
                     {"condition", person.condition},
                     {"present", entry.present},
                     {"distance", std::round(static_cast<double>(entry.distance) * 100.0) / 100.0},
                     {"position", {entry.object->position.x, entry.object->position.y, entry.object->position.z}}};
        if (!person.unresolved.empty()) {
            item["unresolved"] = person.unresolved;
        }
        if (entry.present) {
            item["selection"] = selection_json(data, entry.selection);
        }
        list.push_back(item);
    }
    reply["persons"] = list;
    const world::PersonCounts& counts = persons.counts();
    json topicCounts = json::object();
    for (std::size_t index = 0; index < world::kTopicResultCount; ++index) {
        topicCounts[world::topic_result_text(static_cast<world::TopicResult>(index))] = counts.topics[index];
    }
    reply["counts"] = {{"uses", counts.uses},
                       {"notPresent", counts.notPresent},
                       {"selections", counts.selections},
                       {"stateVisits", counts.stateVisits},
                       {"topicVisits", counts.topicVisits},
                       {"grants", counts.grants},
                       {"presenceChecks", counts.presenceChecks},
                       {"presenceEnds", counts.presenceEnds},
                       {"topics", topicCounts},
                       {"conversations", persons.conversation_count()}};
    return reply;
}

} // namespace soulwarden::backend::server::http::commands
