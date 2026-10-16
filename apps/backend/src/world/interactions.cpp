#include "world/interactions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The option names of the two handlers of stage 2.5a (the data: uidatabase.uidb; live check L1). */
constexpr const char* kLoreOptions[] = {"ReadLore", "AddLoreToJournal"};
/**
 * The options whose use the client shows from its own data, with no effect on the server (a decision of an agent from live
 * check L1: a refusal closes a conversation that the client opened). The animations and the video have the action 64 or 81
 * and no item, status, or reward in their rows.
 */
constexpr const char* kPresentationOptions[] = {"OpenConversation", "ChairInteractionAnimation", "BedInteractionAnimation",
                                                "PlayInteractionAnimation", "PlayPetAnimation", "PlayCinematicVideo"};

std::string hex64(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

float distance_sq(const state::WorldPosition& a, const state::WorldPosition& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

/** The lore page (stage 2.5a): the achievement of the page goes to the character, one row in the database, then its bit. */
class LoreHandler final : public InteractionHandler {
public:
    explicit LoreHandler(World& world) : m_world(world) {}

    [[nodiscard]] const char* name() const override {
        return "lore page";
    }

    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        const gamedata::InteractionData* data = m_world.interactions().data();
        if (data == nullptr || context.bound->lore < 0) {
            return InteractionVerdict::unsupported; // A lore page with no achievement: nothing to keep (the tool marks it).
        }
        const gamedata::InteractionAchievement& achievement = data->achievements()[static_cast<std::size_t>(context.bound->lore)];
        return m_world.ownership().unlock_achievement(character, achievement.id, achievement.index) ? InteractionVerdict::accepted
                                                                                                    : InteractionVerdict::refused;
    }

private:
    World& m_world;
};

/** A presentation of the client (a conversation, an animation, a video): no effect on the server. */
class PresentationHandler final : public InteractionHandler {
public:
    [[nodiscard]] const char* name() const override {
        return "presentation of the client";
    }

    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        (void)character;
        (void)context;
        return InteractionVerdict::accepted;
    }
};

} // namespace

const char* interaction_verdict_text(InteractionVerdict verdict) {
    switch (verdict) {
    case InteractionVerdict::accepted:
        return "accepted";
    case InteractionVerdict::same:
        return "the request of the committed interaction again";
    case InteractionVerdict::notInWorld:
        return "the session has no character";
    case InteractionVerdict::dead:
        return "the character is dead";
    case InteractionVerdict::noData:
        return "the backend has no interaction data";
    case InteractionVerdict::unknownObject:
        return "no object of the data has this reference";
    case InteractionVerdict::unknownOption:
        return "the option, its entity, or the action is not an option of the object";
    case InteractionVerdict::unsupported:
        return "a rule of the data that the backend cannot check";
    case InteractionVerdict::disabled:
        return "the object is disabled";
    case InteractionVerdict::noPosition:
        return "the character has no exact position";
    case InteractionVerdict::tooFar:
        return "the character is not in the range of the object";
    case InteractionVerdict::requirement:
        return "a requirement of the data";
    case InteractionVerdict::full:
        return "the object has its number of users";
    case InteractionVerdict::noHandler:
        return "the backend has no handler for this option";
    case InteractionVerdict::noMainInteraction:
        return "no committed interaction on the object for this part";
    case InteractionVerdict::refused:
        return "the handler refused";
    }
    return "?";
}

const char* interaction_end_text(InteractionEnd cause) {
    switch (cause) {
    case InteractionEnd::clientEnd:
        return "the client ended its request";
    case InteractionEnd::clientCancel:
        return "the client cancelled it (2473)";
    case InteractionEnd::replaced:
        return "a new interaction";
    case InteractionEnd::command:
        return "a command";
    case InteractionEnd::travel:
        return "a travel";
    case InteractionEnd::death:
        return "the death of the character";
    case InteractionEnd::left:
        return "the character left the world";
    }
    return "?";
}

Interactions::Interactions(World& world, const gamedata::InteractionData* data)
    : m_world(world), m_data(data), m_lore(std::make_unique<LoreHandler>(world)), m_presentation(std::make_unique<PresentationHandler>()) {
    for (const char* name : kLoreOptions) {
        register_handler(gamedata::InteractionKind::lorePage, gamedata::interaction_option_id(name), *m_lore);
    }
    // A presentation option is the same for each kind of object (a person, a chair, a pet of an object of the kind "other").
    for (const char* name : kPresentationOptions) {
        for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(gamedata::InteractionKind::other); ++kind) {
            register_handler(static_cast<gamedata::InteractionKind>(kind), gamedata::interaction_option_id(name), *m_presentation);
        }
    }
}

Interactions::~Interactions() = default;

void Interactions::register_handler(gamedata::InteractionKind kind, std::uint32_t optionId, InteractionHandler& handler) {
    m_handlers[{kind, optionId}] = &handler;
}

InteractionHandler* Interactions::handler_of(gamedata::InteractionKind kind, std::uint32_t optionId) const {
    const auto found = m_handlers.find({kind, optionId});
    return found == m_handlers.end() ? nullptr : found->second;
}

InteractionVerdict Interactions::refuse(InteractionVerdict verdict, const Entity& character, const InteractionRequest& request) {
    ++m_counts.verdicts[static_cast<std::size_t>(verdict)];
    if (!m_logged[static_cast<std::size_t>(verdict)]) {
        m_logged[static_cast<std::size_t>(verdict)] = true;
        log::write("world: interaction request of character %s refused (%s): object %s target %s action %u option %08x "
                   "(input %u; the first refusal of this cause)",
                   character.characterId.c_str(), interaction_verdict_text(verdict), hex64(request.object.group).c_str(),
                   hex64(request.object.target).c_str(), static_cast<unsigned>(request.action), request.optionId,
                   static_cast<unsigned>(request.input));
    }
    return verdict;
}

InteractionVerdict Interactions::check(Entity& character, const InteractionRequest& request, InteractionContext& context,
                                       InteractionHandler*& handler) {
    const gamedata::InteractionData* catalog = data();
    if (catalog == nullptr) {
        return InteractionVerdict::noData;
    }
    if (character.life.dead) {
        return InteractionVerdict::dead;
    }
    const std::optional<gamedata::BoundRef> ref = catalog->find(request.object.group, request.object.target);
    if (!ref) {
        return InteractionVerdict::unknownObject;
    }
    const gamedata::InteractionObject& object = catalog->objects()[ref->object];
    if (object.actor != request.object.actor) {
        return InteractionVerdict::unknownObject;
    }
    const gamedata::InteractionBound& bound = object.bounds[ref->bound];
    const gamedata::InteractionOption* option = nullptr;
    for (const gamedata::BoundOption& item : bound.options) {
        const gamedata::InteractionOption& row = catalog->options()[item.option];
        if (row.id == request.optionId && item.entity == request.optionEntity && row.action == request.action) {
            option = &row;
            break;
        }
    }
    if (option == nullptr) {
        return InteractionVerdict::unknownOption;
    }
    context = InteractionContext{ref->object, ref->bound, &object, &bound, option};
    if (!bound.unresolved.empty() || !option->unresolved.empty() || option->statusRules || bound.onlyOnQuest || bound.condition
        || bound.lineOfSight || bound.arcRequired) {
        return InteractionVerdict::unsupported;
    }
    const auto state = m_objects.find(ref->object);
    const BoundState* boundState = state != m_objects.end() ? &state->second.bounds[ref->bound] : nullptr;
    if (boundState != nullptr && !boundState->enabled) {
        return InteractionVerdict::disabled;
    }
    if (character.positionEstimated) {
        return InteractionVerdict::noPosition;
    }
    if (distance_sq(character.position, bound.position) > bound.rangeSq) {
        return InteractionVerdict::tooFar;
    }
    if (option->requiredLevel > character.progression.level || option->requiredItemCount != 0 || !option->requiredItem.empty()
        || option->requiredCurrency != 0 || !bound.progression.empty() || bound.progressionRank != 0) {
        return InteractionVerdict::requirement;
    }
    for (const std::int32_t required : {option->requiredAchievement, bound.requiredAchievement}) {
        if (required >= 0) {
            const std::uint32_t bit = catalog->achievements()[static_cast<std::size_t>(required)].index;
            if (!std::binary_search(character.achievements.begin(), character.achievements.end(), bit)) {
                return InteractionVerdict::requirement;
            }
        }
    }
    if (bound.capacity >= 0 && boundState != nullptr
        && std::find(boundState->users.begin(), boundState->users.end(), character.id) == boundState->users.end()
        && boundState->users.size() >= static_cast<std::size_t>(bound.capacity)) {
        return InteractionVerdict::full;
    }
    if (bound.capacity == 0) {
        return InteractionVerdict::full;
    }
    handler = handler_of(object.kind, option->id);
    if (handler == nullptr) {
        return InteractionVerdict::noHandler;
    }
    return InteractionVerdict::accepted;
}

InteractionVerdict Interactions::request(SessionId session, const InteractionRequest& request) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr || character->kind != EntityKind::player) {
        ++m_counts.verdicts[static_cast<std::size_t>(InteractionVerdict::notInWorld)];
        return InteractionVerdict::notInWorld;
    }
    const std::string mark = log::session_mark(character->owner, character->characterId);
    const log::Context logContext(mark);
    // The request of the committed interaction again: the client restored its record after a one-shot (client 0x49175F0), or
    // its block came back. It is not a new use: no reply, no begin (research Q4).
    if (character->interaction && character->interaction->reference == request.object && character->interaction->action == request.action
        && data() != nullptr && data()->options()[character->interaction->option].id == request.optionId) {
        ++m_counts.verdicts[static_cast<std::size_t>(InteractionVerdict::same)];
        return InteractionVerdict::same;
    }
    InteractionContext context;
    InteractionHandler* handler = nullptr;
    InteractionVerdict verdict = check(*character, request, context, handler);
    if (verdict != InteractionVerdict::accepted) {
        return refuse(verdict, *character, request);
    }
    const bool committed = context.option->committed;
    // A part of a committed interaction: a one-shot whose base action is not its own action (research Q4).
    if (!committed && request.baseAction != 0 && request.baseAction != request.action) {
        const std::optional<ActiveInteraction>& main = character->interaction;
        if (!main || main->object != context.objectIndex || main->action != request.baseAction) {
            return refuse(InteractionVerdict::noMainInteraction, *character, request);
        }
    }
    if (committed && character->interaction) {
        end(*character, InteractionEnd::replaced, false);
    }
    verdict = handler->begin(*character, context);
    if (verdict != InteractionVerdict::accepted) {
        return refuse(verdict, *character, request);
    }
    ++m_counts.verdicts[static_cast<std::size_t>(InteractionVerdict::accepted)];
    ++m_counts.begins;
    const float distance = std::sqrt(distance_sq(character->position, context.bound->position));
    if (committed) {
        ActiveInteraction active;
        active.object = context.objectIndex;
        active.bound = context.boundIndex;
        active.option = static_cast<std::uint32_t>(context.option - data()->options().data());
        active.reference = request.object;
        active.action = request.action;
        active.started = m_world.clock().now();
        active.serial = ++m_serial;
        character->interaction = active;
        m_active.insert(character->id);
        BoundState& boundState = state_of(context.objectIndex).bounds[context.boundIndex];
        boundState.users.push_back(character->id);
        publish(context.objectIndex);
        m_world.touch(character->id, ComponentKey::interactor);
    }
    log::write("world: interaction of character %s: %s at the %s %s (%s, %.2f m, %s): accepted, %s", character->characterId.c_str(),
               context.option->name.c_str(), gamedata::interaction_kind_text(context.object->kind), context.object->key.c_str(),
               context.object->slice.c_str(), static_cast<double>(distance), handler->name(),
               committed ? "committed: it stays until its end" : "one time");
    return InteractionVerdict::accepted;
}

void Interactions::request_ended(SessionId session) {
    Entity* character = m_world.entity_of(session);
    if (character != nullptr && character->interaction) {
        end(*character, InteractionEnd::clientEnd, false);
    }
}

bool Interactions::client_cancel(SessionId session, const InteractionReference& reference) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr || !character->interaction || character->interaction->reference != reference) {
        return false; // A late cancel, or the echo of a cancel of the server: no change (research K7).
    }
    end(*character, InteractionEnd::clientCancel, false);
    return true;
}

bool Interactions::cancel(const std::string& characterId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || !character->interaction) {
        return false;
    }
    end(*character, InteractionEnd::command, true);
    return true;
}

void Interactions::traveled(Entity& character) {
    if (character.interaction) {
        end(character, InteractionEnd::travel, false); // The session sends 3746 and 6709 itself (end_input_lock).
    }
}

void Interactions::died(Entity& character) {
    if (character.interaction) {
        end(character, InteractionEnd::death, true);
    }
}

void Interactions::left(Entity& character) {
    if (character.interaction) {
        end(character, InteractionEnd::left, false);
    }
}

void Interactions::end(Entity& character, InteractionEnd cause, bool notify) {
    if (!character.interaction) {
        return;
    }
    const ActiveInteraction active = *character.interaction;
    character.interaction.reset();
    character.interactionEnded = m_world.touch(character.id, ComponentKey::interactor);
    m_active.erase(character.id);
    ++m_counts.ends;
    const gamedata::InteractionData* catalog = data();
    if (catalog != nullptr && active.object < catalog->objects().size()) {
        const auto state = m_objects.find(active.object);
        if (state != m_objects.end()) {
            std::vector<EntityId>& users = state->second.bounds[active.bound].users;
            users.erase(std::remove(users.begin(), users.end(), character.id), users.end());
            publish(active.object);
        }
        const gamedata::InteractionObject& object = catalog->objects()[active.object];
        InteractionContext context{active.object, active.bound, &object, &object.bounds[active.bound], &catalog->options()[active.option]};
        if (InteractionHandler* handler = handler_of(object.kind, context.option->id)) {
            handler->end(character, context, cause);
        }
        log::write("world: interaction of character %s at the object %s ended: %s", character.characterId.c_str(), object.key.c_str(),
                   interaction_end_text(cause));
    }
    if (notify && character.owner != 0) {
        m_world.interaction_cancelled(character.owner);
    }
}

Interactions::ObjectState& Interactions::state_of(std::uint32_t objectIndex) {
    ObjectState& state = m_objects[objectIndex];
    if (state.bounds.empty()) {
        state.bounds.resize(data()->objects()[objectIndex].bounds.size());
    }
    return state;
}

void Interactions::publish(std::uint32_t objectIndex) {
    const gamedata::InteractionObject& object = data()->objects()[objectIndex];
    ObjectState& state = state_of(objectIndex);
    Entity* entity = state.entity != 0 ? m_world.find(state.entity) : nullptr;
    if (entity == nullptr) {
        ObjectBody body;
        body.index = objectIndex;
        body.asset = object.asset;
        body.subId = object.sub;
        for (const gamedata::InteractionBound& bound : object.bounds) {
            body.bounds.push_back(ObjectBound{bound.slot, true, 0});
        }
        ActorId actor;
        actor.bytes = object.actor;
        entity = &m_world.make_object(body, actor, object.position);
        state.entity = entity->id;
    }
    bool changed = false;
    for (std::size_t index = 0; index < state.bounds.size(); ++index) {
        ObjectBound& target = entity->object.bounds[index];
        const ObjectBound now{target.slot, state.bounds[index].enabled, static_cast<std::uint32_t>(state.bounds[index].users.size())};
        if (now.enabled != target.enabled || now.users != target.users) {
            target = now;
            changed = true;
        }
    }
    if (changed) {
        m_world.touch(entity->id, ComponentKey::interact);
    }
}

ObjectError Interactions::set_enabled(const std::string& key, bool enabled) {
    const gamedata::InteractionData* catalog = data();
    const std::optional<std::uint32_t> index = catalog != nullptr ? catalog->index_of(key) : std::nullopt;
    if (!index) {
        return ObjectError::unknownObject;
    }
    ObjectState& state = state_of(*index);
    for (BoundState& bound : state.bounds) {
        bound.enabled = enabled;
    }
    publish(*index);
    log::write("world: object %s (%s) is %s", catalog->objects()[*index].key.c_str(), catalog->objects()[*index].slice.c_str(),
               enabled ? "enabled" : "disabled");
    return ObjectError::none;
}

InteractionsView Interactions::view(const std::string& characterId, float radius) const {
    InteractionsView view;
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return view;
    }
    view.inWorld = true;
    view.active = character->interaction;
    const gamedata::InteractionData* catalog = data();
    if (catalog == nullptr) {
        return view;
    }
    std::map<std::uint32_t, InteractionObjectView> near;
    catalog->for_each_near(
        character->position, radius,
        [&](const gamedata::BoundRef& ref) {
            const gamedata::InteractionObject& object = catalog->objects()[ref.object];
            const state::WorldPosition& place = object.bounds[ref.bound].position;
            const float distance = std::hypot(place.x - character->position.x, place.y - character->position.y);
            auto [entry, added] = near.try_emplace(ref.object);
            if (added) {
                entry->second.index = ref.object;
                entry->second.object = &object;
                entry->second.distance = distance;
                const auto state = m_objects.find(ref.object);
                for (std::size_t index = 0; index < object.bounds.size(); ++index) {
                    ObjectBound bound{object.bounds[index].slot, true, 0};
                    if (state != m_objects.end()) {
                        bound.enabled = state->second.bounds[index].enabled;
                        bound.users = static_cast<std::uint32_t>(state->second.bounds[index].users.size());
                    }
                    entry->second.bounds.push_back(bound);
                }
                entry->second.entity = state != m_objects.end() ? state->second.entity : 0;
            } else {
                entry->second.distance = std::min(entry->second.distance, distance);
            }
        },
        &view.work);
    for (auto& [index, entry] : near) {
        view.objects.push_back(std::move(entry));
    }
    std::sort(view.objects.begin(), view.objects.end(), [](const auto& a, const auto& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.index < b.index;
    });
    return view;
}

} // namespace soulwarden::backend::world
