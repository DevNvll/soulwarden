#include "world/persons.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/**
 * The services of the conversation data and their numbers in the client enum (registrar 0x37B3DA0, the full list: static read of stage
 * 2.5c; the builder 0x37ACF10 maps them to options, its switch table 0x37AE038: Inn 1 becomes option 10, Open_Shop 16 option 24). Invalid
 * (0) gives no option. A wire contract: a code table.
 */
struct ServiceName {
    const char* name;
    std::uint32_t id;
};
constexpr ServiceName kServices[] = {
    {"Inn", 1},         {"Faction_Board", 2},     {"Community_Board", 3},    {"Faction_Selection", 4}, {"Join_Outpost_Rush", 5},
    {"Event_Store", 6}, {"Horse_Store", 7},       {"Wolf_Store", 8},         {"Lion_Store", 9},        {"Expansion_Modal", 10},
    {"Fast_Travel_Guide", 11}, {"PvP_Guide", 12}, {"Join_3v3_Arena", 13},    {"Transmog", 14},         {"Barbershop", 15},
    {"Open_Shop", 16},  {"Open_Mission_Board", 17},
};
constexpr std::uint32_t kServiceFactionBoard = 2;
constexpr std::uint32_t kServiceFactionSelection = 4;

/**
 * The factions of the client (registrar 0x30BDA90; the conversation converter 0x37AF200 reads Syndicate, Marauder, Covenant, and Any by
 * their CRC-32 first). Each other text is None (0): the generic lookup 0x16B64C0 has no "!" form, and a name that it does not know (the
 * data has "Maruaders") leaves the byte 0.
 */
constexpr std::uint8_t kFactionNone = 0;
constexpr std::uint8_t kFactionAny = 4;

std::uint8_t faction_value(std::string_view text);

/** The faction of a character: the backend has no faction (stage 4.5), thus None for each character. */
constexpr std::uint8_t kCharacterFaction = kFactionNone;

bool same_text(std::string_view one, std::string_view two) {
    return one.size() == two.size() && std::equal(one.begin(), one.end(), two.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

std::uint8_t faction_value(std::string_view text) {
    constexpr std::pair<const char*, std::uint8_t> kNames[] = {{"Syndicate", 1}, {"Marauder", 2}, {"Covenant", 3}, {"Any", kFactionAny},
                                                              {"Faction1", 1},  {"Faction2", 2}, {"Faction3", 3}};
    for (const auto& [name, value] : kNames) {
        if (same_text(name, text)) {
            return value;
        }
    }
    return kFactionNone;
}

/**
 * RequiredProgression "PROGRESSION RANK POINTS" (client parser 0x37AF340; the check 0x3CB82D0): the rank of the categorical progression of
 * the character is RANK or more, and with POINTS above 0 its points are POINTS or more. A rule with no id or with no value above 0 is no
 * rule. @return No value for a text that is not of this form.
 */
std::optional<bool> progression_passes(const Entity& character, const std::string& text) {
    std::istringstream input(text);
    std::string id;
    long long rank = 0;
    long long points = 0;
    if (!(input >> id) || !(input >> rank) || rank < 0 || rank > 0xFFFF) {
        return std::nullopt;
    }
    if (!(input >> points)) {
        points = 0;
    }
    if (points < 0) {
        return std::nullopt;
    }
    if (rank == 0 && points == 0) {
        return true;
    }
    // A progression that the character does not have has rank 0 and 0 points (the getters 0x3CB6330, 0x3CB5E60): the faction progressions
    // of the data (Syndicate, ...) too, because the backend has no faction.
    const auto found = character.skills.progressions.find(gamedata::progression_id(id));
    const ProgressionRank have = found != character.skills.progressions.end() ? found->second : ProgressionRank{};
    return have.rank >= rank && (points == 0 || have.points >= static_cast<std::uint64_t>(points));
}

const char* presence_text(gamedata::PersonPresence presence) {
    switch (presence) {
    case gamedata::PersonPresence::always:
        return "always";
    case gamedata::PersonPresence::showWhen:
        return "there while";
    case gamedata::PersonPresence::hideWhen:
        return "gone while";
    }
    return "?";
}

} // namespace

std::optional<std::uint32_t> conversation_service_id(std::string_view name) {
    for (const ServiceName& service : kServices) {
        if (same_text(service.name, name)) {
            return service.id;
        }
    }
    return std::nullopt;
}

const char* topic_result_text(TopicResult result) {
    switch (result) {
    case TopicResult::accepted:
        return "accepted (no reward and no quest change in stage 2.5c)";
    case TopicResult::duplicate:
        return "the character finished this topic in this conversation: no change";
    case TopicResult::noConversation:
        return "the character has no open person conversation";
    case TopicResult::noData:
        return "the backend has no conversation data";
    case TopicResult::notPresent:
        return "the person is not there for the character";
    case TopicResult::unknownTopic:
        return "the data has no topic of this CRC";
    case TopicResult::notOffered:
        return "the conversation state of the character does not offer this topic";
    }
    return "?";
}

Persons::Persons(World& world, const gamedata::ConversationData* data) : m_world(world), m_data(data) {
    // The person handler of OpenConversation: the base of stage 2.5a checks the reference, the option, and the range first.
    m_world.interactions().register_handler(gamedata::InteractionKind::npc, gamedata::interaction_option_id("OpenConversation"), *this);
}

Revision Persons::selection_revision(const Entity& character) {
    Revision revision = 0;
    for (const ComponentKey key :
         {ComponentKey::achievements, ComponentKey::objectives, ComponentKey::progression, ComponentKey::categorical}) {
        if (const auto found = character.changed.find(key); found != character.changed.end()) {
            revision = std::max(revision, found->second);
        }
    }
    return revision;
}

bool Persons::present(const Entity& character, const gamedata::PersonBinding& person) const {
    switch (person.presence) {
    case gamedata::PersonPresence::always:
        return true;
    case gamedata::PersonPresence::showWhen:
        return m_world.quests().conditional(character, person.condition);
    case gamedata::PersonPresence::hideWhen:
        return !m_world.quests().conditional(character, person.condition);
    }
    return false;
}

bool Persons::passes(const Entity& character, const gamedata::ConversationRequirements& requirements, bool topic) const {
    // The predicates of the client: a state 0x37AB440, a topic 0x37AB7A0 (static read of stage 2.5c). A cell that the loader of the backend
    // cannot read does not pass.
    if (requirements.malformed) {
        return false;
    }
    if (requirements.level && character.progression.level < *requirements.level) {
        return false;
    }
    if (!requirements.achievement.empty() && !m_world.quests().conditional(character, requirements.achievement)) {
        return false;
    }
    // RequiredFaction: None (0) is no rule. Any (4) is no rule for a state, and for a topic the character must have a faction. A named
    // faction must be the faction of the character.
    const std::uint8_t faction = faction_value(requirements.faction);
    if (faction != kFactionNone && (faction == kFactionAny ? topic && kCharacterFaction == kFactionNone : faction != kCharacterFaction)) {
        return false;
    }
    if (!topic) {
        // RequiredFactionCooldown (a state only): NotInCooldown is no rule; InCooldownMultipleDays needs more than 24 hours of the cooldown
        // of a faction change, InCooldownSingleDay 1 to 24 hours (0x37AB5F9 to 0x37AB6B5). The backend has no faction change: no cooldown.
        if (same_text(requirements.factionCooldown, "InCooldownMultipleDays") || same_text(requirements.factionCooldown, "InCooldownSingleDay")) {
            return false;
        }
        // RequiredCompletedObjective: the completion achievement of the objective (0x3A48A30).
        if (!requirements.completedObjective.empty() && !m_world.quests().objective_completed(character, requirements.completedObjective)) {
            return false;
        }
        // RequiredActiveObjectiveId with RequiredActiveTaskId (a rule only when both have a value): one objective of the list has the task
        // Active or CompleteActive (IsTaskActiveById 0x3A49520).
        if (!requirements.activeObjective.empty() && !requirements.activeTask.empty()) {
            bool active = false;
            std::size_t start = 0;
            while (!active && start <= requirements.activeObjective.size()) {
                const std::size_t end = std::min(requirements.activeObjective.find(',', start), requirements.activeObjective.size());
                std::string objective = requirements.activeObjective.substr(start, end - start);
                objective.erase(0, objective.find_first_not_of(' '));
                objective.erase(objective.find_last_not_of(' ') + 1);
                if (!objective.empty()) {
                    const std::optional<std::uint8_t> state = m_world.quests().task_state(character, objective, requirements.activeTask);
                    active = state && (*state == quest_state::kActive || *state == quest_state::kCompleteActive);
                }
                start = end + 1;
            }
            if (!active) {
                return false;
            }
        }
    }
    if (!requirements.progression.empty()) {
        const std::optional<bool> passed = progression_passes(character, requirements.progression);
        if (!passed || !*passed) {
            return false;
        }
    }
    return true;
}

PersonSelection Persons::select(const Entity& character, const gamedata::PersonBinding& person) {
    PersonSelection selection;
    const gamedata::ConversationData* catalog = data();
    if (catalog == nullptr) {
        return selection;
    }
    selection.npc = catalog->npc(person.npc);
    if (selection.npc == nullptr) {
        return selection;
    }
    ++m_counts.selections;
    // The states in reverse: the first state whose requirements pass (client 0x37AC710).
    for (auto state = selection.npc->states.rbegin(); state != selection.npc->states.rend(); ++state) {
        ++m_counts.stateVisits;
        if (passes(character, catalog->state(*state).requirements, false)) {
            selection.state = static_cast<std::int32_t>(*state);
            break;
        }
    }
    if (selection.state < 0) {
        return selection;
    }
    const gamedata::ConversationState& state = catalog->state(static_cast<std::uint32_t>(selection.state));
    // The topics of the state whose requirements pass, then the ProvidedTopics of each of them (the client accepts a topic of the root list
    // of the state or of the graph of its ProvidedTopics: 0x375AC50, 0x375ADF0). Each topic one time.
    std::vector<std::uint32_t> next(state.topics.begin(), state.topics.end());
    std::set<std::uint32_t> seen;
    for (std::size_t position = 0; position < next.size(); ++position) {
        const std::uint32_t index = next[position];
        if (!seen.insert(index).second) {
            continue;
        }
        ++m_counts.topicVisits;
        const gamedata::ConversationTopic& topic = catalog->topic(index);
        if (passes(character, topic.requirements, true)) {
            selection.topics.push_back(index);
            next.insert(next.end(), topic.provided.begin(), topic.provided.end());
        }
    }
    // The services of the state: the builder adds the faction board only for the faction of the state and its level, the faction
    // selection also for Any (0x37A7940, 0x37AD46C). Invalid (0) and a name with no number give no service.
    const std::uint8_t stateFaction = faction_value(state.requirements.faction);
    const bool level = !state.requirements.level || character.progression.level >= *state.requirements.level;
    for (const std::string& service : state.services) {
        const std::optional<std::uint32_t> id = conversation_service_id(service);
        if (!id) {
            if (!same_text(service, "Invalid")) {
                selection.unknownServices.push_back(service);
            }
            continue;
        }
        if ((*id == kServiceFactionBoard && (stateFaction != kCharacterFaction || !level))
            || (*id == kServiceFactionSelection && ((stateFaction != kCharacterFaction && stateFaction != kFactionAny) || !level))) {
            continue;
        }
        selection.services.push_back(*id);
    }
    return selection;
}

InteractionVerdict Persons::begin(Entity& character, const InteractionContext& context) {
    const gamedata::PersonBinding* person = context.object->person ? &*context.object->person : nullptr;
    if (person == nullptr) {
        return InteractionVerdict::accepted; // A person of a file of before stage 2.5c: the conversation of stage 2.5a.
    }
    if (!person->unresolved.empty()) {
        return InteractionVerdict::unsupported; // The tool could not bind it: no NPC row to check.
    }
    if (!present(character, *person)) {
        ++m_counts.notPresent;
        log::write("person: %s (%s) is not there for the character %s (%s %s): refused", person->npc.c_str(), context.object->key.c_str(),
                   character.characterId.c_str(), presence_text(person->presence), person->condition.c_str());
        return InteractionVerdict::notPresent;
    }
    return InteractionVerdict::accepted;
}

void Persons::started(Entity& character, const InteractionContext& context) {
    if (!context.object->person || !character.interaction || context.instance != 0) {
        return;
    }
    const gamedata::PersonBinding& person = *context.object->person;
    ++m_counts.uses;
    PersonSelection selection = select(character, person);
    PersonConversation conversation;
    conversation.object = context.objectIndex;
    conversation.bound = context.boundIndex;
    conversation.source = character.interaction->reference;
    conversation.npc = person.npc;
    conversation.npcCrc = person.npcCrc;
    conversation.state = selection.state;
    conversation.topics = selection.topics;
    conversation.services = selection.services;
    conversation.revision = selection_revision(character);
    conversation.open = true;
    conversation.serial = ++m_serial;
    character.conversation = conversation;
    m_talking.insert(character.id);
    const gamedata::ConversationData* catalog = data();
    std::string topics;
    for (const std::uint32_t index : selection.topics) {
        topics += (topics.empty() ? "" : ", ") + catalog->topic(index).id;
    }
    log::write("person: %s (%s) for the character %s: state %s, topics [%s], services %zu%s%s", person.npc.c_str(), context.object->key.c_str(),
               character.characterId.c_str(),
               selection.state >= 0 ? catalog->state(static_cast<std::uint32_t>(selection.state)).id.c_str()
                                    : (catalog == nullptr ? "(no conversation data)" : selection.npc == nullptr ? "(no NPC row)" : "(none passes)"),
               topics.c_str(), selection.services.size(), selection.unknownServices.empty() ? "" : ", services with no number: ",
               selection.unknownServices.empty() ? "" : selection.unknownServices.front().c_str());
    // The services of the state go to the grant API of stage 2.5b: they stay after a conversation-only close (research Q3).
    if (!selection.services.empty()
        && m_world.interactions().grant_service(character, kConversationServiceKind, selection.services, conversation.serial)) {
        ++m_counts.grants;
    }
}

void Persons::end(Entity& character, const InteractionContext& context, InteractionEnd cause) {
    if (!character.conversation || character.conversation->object != context.objectIndex || context.instance != 0) {
        return;
    }
    if (cause == InteractionEnd::replaced) {
        forget(character, "a new interaction");
        return;
    }
    // The close (2473, the end of the block, a cancel): the message 5397 of the close can come after it.
    character.conversation->open = false;
}

const gamedata::PersonBinding* Persons::binding_of(const PersonConversation& conversation) const {
    const gamedata::InteractionData* catalog = m_world.interactions().data();
    if (catalog == nullptr || conversation.object >= catalog->objects().size()) {
        return nullptr;
    }
    const gamedata::InteractionObject& object = catalog->objects()[conversation.object];
    return object.person ? &*object.person : nullptr;
}

void Persons::forget(Entity& character, const char* cause) {
    if (!character.conversation) {
        return;
    }
    log::write("person: the conversation of the character %s with %s ended: %s", character.characterId.c_str(),
               character.conversation->npc.c_str(), cause);
    character.conversation.reset();
    m_talking.erase(character.id);
}

TopicResult Persons::topic(Entity& character, std::uint32_t topicCrc, bool closing) {
    const auto count = [this](TopicResult result) {
        ++m_counts.topics[static_cast<std::size_t>(result)];
        return result;
    };
    const gamedata::ConversationData* catalog = data();
    if (catalog == nullptr) {
        return count(TopicResult::noData);
    }
    if (!character.conversation || (!closing && !character.conversation->open)) {
        return count(TopicResult::noConversation);
    }
    PersonConversation& conversation = *character.conversation;
    const gamedata::PersonBinding* person = binding_of(conversation);
    if (person == nullptr || !present(character, *person)) {
        forget(character, "the person is not there for the character");
        return count(TopicResult::notPresent);
    }
    const std::optional<std::uint32_t> found = catalog->topic_of_crc(topicCrc);
    if (!found) {
        return count(TopicResult::unknownTopic);
    }
    // A change of the state of the character since the selection (a stale revision): the selection again, from the state of now.
    if (const Revision now = selection_revision(character); now != conversation.revision) {
        const PersonSelection selection = select(character, *person);
        conversation.state = selection.state;
        conversation.topics = selection.topics;
        conversation.revision = now;
    }
    if (std::find(conversation.topics.begin(), conversation.topics.end(), *found) == conversation.topics.end()) {
        return count(TopicResult::notOffered);
    }
    if (std::find(conversation.finished.begin(), conversation.finished.end(), topicCrc) != conversation.finished.end()) {
        return count(TopicResult::duplicate);
    }
    conversation.finished.push_back(topicCrc);
    log::write("person: the character %s finished the topic %s of %s (%s)", character.characterId.c_str(), catalog->topic(*found).id.c_str(),
               conversation.npc.c_str(), closing ? "the close, 5397" : "3158");
    return count(TopicResult::accepted);
}

void Persons::achievements_touched(const Entity& character) {
    if (m_talking.count(character.id) != 0) {
        m_recheck.insert(character.id);
    }
}

void Persons::tick() {
    if (m_recheck.empty()) {
        return;
    }
    const std::set<EntityId> marked = std::move(m_recheck);
    m_recheck.clear();
    for (const EntityId id : marked) {
        Entity* character = m_world.find(id);
        if (character == nullptr || !character->conversation) {
            continue;
        }
        ++m_counts.presenceChecks;
        const PersonConversation conversation = *character->conversation;
        const gamedata::PersonBinding* person = binding_of(conversation);
        if (person != nullptr && present(*character, *person)) {
            continue;
        }
        const std::string mark = log::session_mark(character->owner, character->characterId);
        const log::Context logContext(mark);
        ++m_counts.presenceEnds;
        // The person went for this character (its bits): its use and its service grant end; the person of the other characters stays.
        if (character->interaction && character->interaction->object == conversation.object && character->interaction->instance == 0) {
            m_world.interactions().end_use(*character, InteractionEnd::sourceRemoved);
        }
        if (character->serviceGrant && character->serviceGrant->kind == kConversationServiceKind
            && character->serviceGrant->object == conversation.object && character->serviceGrant->instance == 0) {
            (void)m_world.interactions().close_service(character->characterId);
        }
        forget(*character, "the person is not there for the character any more");
    }
}

void Persons::traveled(Entity& character) {
    forget(character, "a travel");
}

void Persons::died(Entity& character) {
    forget(character, "the death of the character");
}

void Persons::left(Entity& character) {
    forget(character, "the character left the world");
}

PersonsView Persons::view(const std::string& characterId, float radius) {
    PersonsView view;
    Entity* character = m_world.find_character(characterId);
    const gamedata::InteractionData* catalog = m_world.interactions().data();
    if (character == nullptr || catalog == nullptr) {
        return view;
    }
    view.inWorld = true;
    view.conversation = character->conversation;
    std::set<std::uint32_t> seen;
    catalog->for_each_near(character->position, radius, [&](const gamedata::BoundRef& ref) {
        const gamedata::InteractionObject& object = catalog->objects()[ref.object];
        if (!object.person || !seen.insert(ref.object).second) {
            return;
        }
        PersonView entry;
        entry.object = &object;
        entry.distance = std::hypot(object.bounds[ref.bound].position.x - character->position.x,
                                    object.bounds[ref.bound].position.y - character->position.y);
        entry.present = object.person->unresolved.empty() && present(*character, *object.person);
        if (entry.present) {
            entry.selection = select(*character, *object.person);
        }
        view.persons.push_back(std::move(entry));
    });
    std::sort(view.persons.begin(), view.persons.end(), [](const PersonView& a, const PersonView& b) { return a.distance < b.distance; });
    return view;
}

} // namespace soulwarden::backend::world
