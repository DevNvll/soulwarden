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
/** The options of a crafting station and of a camp (stage 2.5b; uidatabase.uidb: action 20, committed). */
constexpr const char* kStationOptions[] = {"OpenCraftingOption", "OpenCraftingOptionLevel60"};
/** The timed option of the powder keg fixture (stage 2.5b; uidatabase.uidb: action 33, committed, Delay Time 5 s). */
constexpr const char* kTimedFixtureOptions[] = {"Trigger"};
/**
 * The action PlayInteractionAnimation (Interact Action Type 64 of the rows of the chair, the bed, and the animations): the client
 * plays the action InteractionAnimation, which ends at IfDamaged (game data: playeractions_interact.actionlist).
 */
constexpr std::uint8_t kPlayInteractionAnimation = 64;
/** The action GatherResource (uidatabase.uidb: GatherOption): the client skips the capacity check for it (client code 0x35C3858). */
constexpr std::uint8_t kGatherResource = 14;
/** The depth limit of the template children (the tool writes no deeper chain; the loader refuses a cycle). */
constexpr int kChildDepth = 4;

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

float horizontal(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
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

/**
 * A crafting station or a camp (stage 2.5b): the client opens its crafting screen at the key (live check L4); the use has the
 * StationContext of the object (Interactions::station). An object with no Assembly is refused: the client has no station to open
 * (client 0x2E61E00: "Player is missing valid Assembly Component"). A key with no row of the crafting station database is accepted
 * and marked (a decision of an agent: the crafts are stage 3.7, and the research keeps the authored key).
 */
class StationHandler final : public InteractionHandler {
public:
    [[nodiscard]] const char* name() const override {
        return "station";
    }

    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        (void)character;
        return context.object->assembly ? InteractionVerdict::accepted : InteractionVerdict::refused;
    }
};

/** The timed option of a development fixture (stage 2.5b, the powder keg): the completion counts; no explosion, no reward. */
class TimedFixtureHandler final : public InteractionHandler {
public:
    [[nodiscard]] const char* name() const override {
        return "timed fixture";
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
    case InteractionVerdict::notOnQuest:
        return "the bound is open only during a quest task with its tag, and the character has none";
    case InteractionVerdict::notPresent:
        return "the person is not there for the character (its presence rule)";
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
    case InteractionEnd::sourceRemoved:
        return "the source went away";
    case InteractionEnd::rangeLost:
        return "the character is not in the range of the object any more";
    case InteractionEnd::damage:
        return "damage (the option ends at damage)";
    case InteractionEnd::policyFailed:
        return "the check at the end of the timed use failed";
    }
    return "?";
}

const char* service_error_text(ServiceError error) {
    switch (error) {
    case ServiceError::none:
        return "the grant is valid";
    case ServiceError::notInWorld:
        return "the character is not in the world";
    case ServiceError::noGrant:
        return "the character has no grant of this service";
    case ServiceError::oldEntry:
        return "the grant is of an older world entry";
    case ServiceError::sourceGone:
        return "the source of the grant went away";
    case ServiceError::tooFar:
        return "the character is not in the range of the source";
    case ServiceError::notOffered:
        return "the source does not offer this id";
    case ServiceError::expired:
        return "the grant expired";
    }
    return "?";
}

Interactions::Interactions(World& world, const gamedata::InteractionData* data)
    : m_world(world), m_data(data), m_lore(std::make_unique<LoreHandler>(world)), m_presentation(std::make_unique<PresentationHandler>()),
      m_station(std::make_unique<StationHandler>()), m_fixture(std::make_unique<TimedFixtureHandler>()) {
    for (const char* name : kLoreOptions) {
        register_handler(gamedata::InteractionKind::lorePage, gamedata::interaction_option_id(name), *m_lore);
    }
    // A presentation option is the same for each kind of object (a person, a chair, a pet of an object of the kind "other").
    for (const char* name : kPresentationOptions) {
        for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(gamedata::InteractionKind::other); ++kind) {
            register_handler(static_cast<gamedata::InteractionKind>(kind), gamedata::interaction_option_id(name), *m_presentation);
        }
    }
    for (const char* name : kStationOptions) {
        register_handler(gamedata::InteractionKind::craftingStation, gamedata::interaction_option_id(name), *m_station);
        register_handler(gamedata::InteractionKind::camp, gamedata::interaction_option_id(name), *m_station);
    }
    for (const char* name : kTimedFixtureOptions) {
        register_handler(gamedata::InteractionKind::other, gamedata::interaction_option_id(name), *m_fixture);
    }
}

Interactions::~Interactions() = default;

void Interactions::register_handler(gamedata::InteractionKind kind, std::uint32_t optionId, InteractionHandler& handler) {
    m_handlers[{kind, optionId}] = &handler;
}

void Interactions::register_kind_handler(gamedata::InteractionKind kind, InteractionHandler& handler) {
    m_kindHandlers[kind] = &handler;
}

InteractionHandler* Interactions::handler_of(gamedata::InteractionKind kind, std::uint32_t optionId) const {
    if (const auto found = m_handlers.find({kind, optionId}); found != m_handlers.end()) {
        return found->second;
    }
    const auto byKind = m_kindHandlers.find(kind);
    return byKind == m_kindHandlers.end() ? nullptr : byKind->second;
}

Entity* Interactions::find_entity(EntityId id) const {
    return m_world.find(id);
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

Interactions::Site Interactions::site_of(std::uint32_t object, EntityId instance, std::uint64_t generation) const {
    Site site;
    const gamedata::InteractionData* catalog = data();
    if (catalog == nullptr) {
        return site;
    }
    if (instance == 0) {
        if (object < catalog->objects().size()) {
            site.object = &catalog->objects()[object];
            site.index = object;
        }
        return site;
    }
    const auto found = m_instances.find(instance);
    if (found != m_instances.end()) {
        if (found->second.generation == generation) {
            site.object = &found->second.object;
            site.index = found->second.templateIndex;
            site.instance = instance;
            site.generation = generation;
        }
        return site;
    }
    // An object that the data does not have (stage 3.6a): the same generation, and its body still its source (a creature body).
    const auto external = m_externals.find(instance);
    if (external != m_externals.end() && external->second.value.generation == generation
        && (m_resolver == nullptr || m_resolver->alive(external->second.value))) {
        site.object = &external->second.value.object;
        site.instance = instance;
        site.generation = generation;
        site.external = true;
    }
    return site;
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
    if (character.space != kMainSpace) {
        // Stage DG1: each level object of this system is an object of the main world: a character in an instance has no level object near it
        // (the objects of an instance level are stage DG2). The request changes nothing (rule MP-5). Stage QG9a: a server child of the space
        // of the character (the exit portal of its instance) is there.
        const auto child = m_instances.find(request.object.group);
        const Entity* entity = child != m_instances.end() ? find_entity(child->first) : nullptr;
        if (entity == nullptr || entity->space != character.space) {
            // Stage QG9d: the exit portal of a mode root that the client makes from the level data of the main world level (the portal of
            // QuestSeraphim; live check of stage QG9c: the client sent ExitDungeon to it in the instance) is there in an instance of its
            // mode.
            const std::optional<gamedata::BoundRef> ref = catalog->find(request.object.group, request.object.target);
            const gamedata::InteractionObject* level = ref ? &catalog->objects()[ref->object] : nullptr;
            // Stage DG2 (part 5, docs/systems/instances.md): a level object of the main world level in an instance of that level whose
            // bound has the InteractTag of an active task of the character with the GameModeID of the mode of the instance (the statue of
            // Taurus of 09A_M12_SaveTaurus in QuestMedusa: the client makes it from the level data, and the task of the mode names it).
            const world::Instance* instance = level != nullptr && level->exitMode.empty() && m_world.main_level(character.space)
                                           ? m_world.instances().instance_of_space(character.space)
                                           : nullptr;
            const std::string& tag = level != nullptr ? level->bounds[ref->bound].tag : std::string();
            const bool modeObject = instance != nullptr && !tag.empty()
                                    && m_world.quests().mode_task_active(character, instance->mode, {"t:" + gamedata::lower_text(tag)});
            if (modeObject) {
                ++m_counts.modeLevelObjects;
            } else if (level == nullptr || level->exitMode.empty() || !m_world.instances().in_mode(character, level->exitMode)) {
                return InteractionVerdict::unknownObject;
            }
        }
    }
    // The reference: a bound of a level object (the reference map of the data), or of a server child (its G is its entity id).
    Site site;
    std::uint32_t boundIndex = 0;
    const BoundState* boundState = nullptr;
    ++m_counts.referenceLookups;
    if (const std::optional<gamedata::BoundRef> ref = catalog->find(request.object.group, request.object.target)) {
        site.object = &catalog->objects()[ref->object];
        site.index = ref->object;
        boundIndex = ref->bound;
        const auto state = m_objects.find(ref->object);
        boundState = state != m_objects.end() ? &state->second.bounds[ref->bound] : nullptr;
    } else if (const auto found = m_instances.find(request.object.group);
               found != m_instances.end() && find_entity(found->first) != nullptr && find_entity(found->first)->space == character.space) {
        // Stage QG9a: a server child of the space of the character (a child of an instance is not there for the main world).
        const Instance& instance = found->second;
        for (std::uint32_t index = 0; index < instance.object.bounds.size(); ++index) {
            if (instance.object.bounds[index].target == request.object.target) {
                site = Site{&instance.object, instance.templateIndex, found->first, instance.generation};
                boundIndex = index;
                boundState = &instance.state.bounds[index];
                break;
            }
        }
    } else if (m_resolver != nullptr) {
        // An object that the data does not have (stage 3.6a): a natural gathering node (its offline id), the body of a creature. The
        // resolver gives its object from the data of the stage; its state stays for the same generation.
        if (std::optional<ExternalObject> resolved = m_resolver->resolve(request.object)) {
            const EntityId group = resolved->object.group;
            auto found = m_externals.find(group);
            if (found == m_externals.end() || found->second.value.generation != resolved->generation) {
                External external;
                if (found != m_externals.end()) {
                    external.state.entity = found->second.state.entity;
                    external.cells = found->second.cells;
                }
                external.value = std::move(*resolved);
                external.state.bounds.resize(external.value.object.bounds.size());
                found = m_externals.insert_or_assign(group, std::move(external)).first;
            }
            External& external = found->second;
            for (std::uint32_t index = 0; index < external.value.object.bounds.size(); ++index) {
                if (external.value.object.bounds[index].target == request.object.target) {
                    site = Site{&external.value.object, 0, group, external.value.generation, true};
                    boundIndex = index;
                    boundState = &external.state.bounds[index];
                    break;
                }
            }
        }
    }
    if (site.object == nullptr || site.object->actor != request.object.actor) {
        return InteractionVerdict::unknownObject;
    }
    const gamedata::InteractionObject& object = *site.object;
    const gamedata::InteractionBound& bound = object.bounds[boundIndex];
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
    context = InteractionContext{site.index, boundIndex, &object, &bound, option, site.instance, site.generation, site.external};
    // A handler that owns the sight rule of its bounds checks it itself (stage 3.5b.1: a chest).
    InteractionHandler* const owner = handler_of(object.kind, option->id);
    const bool sightOwned = owner != nullptr && owner->owns_sight();
    if (!bound.unresolved.empty() || !option->unresolved.empty() || option->statusRules || (bound.onlyOnQuest && bound.tag.empty())
        || bound.condition || (bound.lineOfSight && !sightOwned) || bound.arcRequired) {
        return InteractionVerdict::unsupported;
    }
    // Stage 1.8c: InteractOnlyOnQuest (client 0x8261FC0): the bound is open while an active task of the character has its tag.
    if (bound.onlyOnQuest && !m_world.quests().quest_object_open(character, bound.tag)) {
        return InteractionVerdict::notOnQuest;
    }
    if (boundState != nullptr && !boundState->enabled) {
        return InteractionVerdict::disabled;
    }
    if (character.positionEstimated) {
        return InteractionVerdict::noPosition;
    }
    if (distance_sq(character.position, bound.position) > bound.rangeSq) {
        return InteractionVerdict::tooFar;
    }
    // Stage QG9a [data; live L1]: an option row with a required item and the count 0 asks for no item (the Enter options of the solo trial
    // entrances name KeyDynasty or KeyAmrine with the count 0: the window of the client shows no item requirement, and it sends the request).
    if (option->requiredLevel > character.progression.level || option->requiredItemCount != 0 || option->requiredCurrency != 0
        || !bound.progression.empty() || bound.progressionRank != 0) {
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
    // Stage QG5a: the required achievement of a variation that is an expression: the achievement conditional of the quests (the reader of
    // TaskAchievement: ids, !, &&, ||, parentheses; an id that the data does not know is false).
    if (!bound.conditional.empty() && !m_world.quests().conditional(character, bound.conditional)) {
        return InteractionVerdict::requirement;
    }
    // A gather (GatherResource) has no limit of users: the client skips the ordinary capacity check for the action 14 (client code
    // 0x35C2E60 at 0x35C3858; research gathering-stage.md Q1). Stage 3.6a: two characters can begin at one node; the gathering system
    // decides the completion (docs/systems/gathering.md).
    const bool gather = option->action == kGatherResource;
    if (!gather && bound.capacity >= 0 && boundState != nullptr
        && std::find(boundState->users.begin(), boundState->users.end(), character.id) == boundState->users.end()
        && boundState->users.size() >= static_cast<std::size_t>(bound.capacity)) {
        return InteractionVerdict::full;
    }
    if (!gather && bound.capacity == 0) {
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
        if (!main || main->object != context.objectIndex || main->instance != context.instance || main->action != request.baseAction) {
            return refuse(InteractionVerdict::noMainInteraction, *character, request);
        }
    }
    // The handler first: a refusal (a database that did not take the row) leaves the committed interaction of the character as it
    // was, with its users and its Interactor state (code review of stage 2.5a, finding INT-R1).
    verdict = handler->begin(*character, context);
    if (verdict != InteractionVerdict::accepted) {
        return refuse(verdict, *character, request);
    }
    if (committed && character->interaction) {
        end(*character, InteractionEnd::replaced, false);
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
        active.instance = context.instance;
        active.generation = context.generation;
        // A timed use (stage 2.5b): the full Delay Time of the option row on the world clock (live check L6: the client timer). A handler
        // with its own time (the open of a chest; code review of stage 3.5b.1, finding CH-R2) gives it: the same timed use of the base.
        const float handlerTime = handler->use_time(*character, context);
        const float useTime = handlerTime > 0.0F ? handlerTime : context.option->delay;
        if (useTime > 0.0F) {
            const auto delay = std::chrono::duration_cast<WorldClock::time_point::duration>(std::chrono::duration<float>(useTime));
            active.due = active.started + delay;
            ++m_timed;
            m_world.after(std::chrono::duration_cast<std::chrono::milliseconds>(delay) + std::chrono::milliseconds(1),
                          [this, characterId = character->characterId, serial = active.serial](World&) { timed_due(characterId, serial); });
        }
        character->interaction = active;
        m_active.insert(character->id);
        const Site used{context.object, context.objectIndex, context.instance, context.generation, context.external};
        if (ObjectState* state = state_of(used)) {
            state->bounds[context.boundIndex].users.push_back(character->id);
        }
        publish(used);
        m_world.touch(character->id, ComponentKey::interactor);
    }
    // A use of a different source ends the service grant of the character (research Q4: one source grant), also a use of one time
    // (code review of stage 2.5b, finding ST-R4: the lore page of a different object kept the grant of a person). Only an accepted use.
    if (character->serviceGrant
        && (character->serviceGrant->object != context.objectIndex || character->serviceGrant->instance != context.instance
            || character->serviceGrant->generation != context.generation)) {
        end_grant(*character, "a use of a different source");
    }
    log::write("world: interaction of character %s: %s at the %s %s (%s, %.2f m, %s): accepted, %s%s", character->characterId.c_str(),
               context.option->name.c_str(), gamedata::interaction_kind_text(context.object->kind), context.object->key.c_str(),
               context.object->slice.c_str(), static_cast<double>(distance), handler->name(),
               committed ? "committed: it stays until its end" : "one time",
               character->interaction && character->interaction->due
                   ? (", timed: it completes after "
                      + std::to_string(std::chrono::duration<double>(*character->interaction->due - character->interaction->started).count())
                      + " s")
                         .c_str()
                   : "");
    if (committed && context.object->assembly) {
        if (const std::optional<StationContext> station = this->station(*character)) {
            log::write("world: station of the interaction: key %s%s, fee %s, Assembly entity %s target %s, object %s generation %llu, "
                       "parent %s",
                       station->assemblyKey.c_str(), context.object->assembly->resolved ? "" : " (no row of the crafting station data)",
                       station->chargeCraftingFee ? "yes" : "no", hex64(station->assemblyEntity).c_str(),
                       hex64(station->assemblyTarget).c_str(), station->objectKey.c_str(),
                       static_cast<unsigned long long>(station->objectGeneration),
                       station->parentKey.empty() ? "none" : station->parentKey.c_str());
        }
    }
    // Code review of stage 2.5c, finding PR5C-R1: an accepted use of any source ends the person conversation of the character (also a
    // closed one): a late 5397 of the old person does not count. The person handler makes the new conversation after it.
    m_world.persons().source_replaced(*character);
    // The handler after the base registered the use (stage 3.5b.2: the Take All of a loot bag).
    handler->started(*character, context);
    // Stage 1.8c: the start of an interaction with a tag (TaskInteract with OnInteractStart; 0x825E470). A use with no time completes at
    // its start too (the open of a storage or of a station screen: the authoring text names the storage screen as a success). The time is
    // the time of the use (the time of the handler or the Delay Time: code review of stage 1.8c, finding QR8C-R8), and a handler that gives
    // the completion itself (the gathering, finding QR8C-R13) gets no completion from the base.
    if (!context.bound->tag.empty()) {
        QuestEvent interacted;
        interacted.kind = QuestEventKind::interact;
        interacted.keys = {context.bound->tag};
        interacted.started = true;
        m_world.quests().event(*character, interacted);
        const float handlerTime = handler->use_time(*character, context);
        const float useTime = handlerTime > 0.0F ? handlerTime : context.option->delay;
        if (useTime <= 0.0F && !handler->owns_interact_event(context)) {
            interacted.started = false;
            m_world.quests().event(*character, interacted);
        }
    }
    return InteractionVerdict::accepted;
}

void Interactions::request_ended(SessionId session) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr || !character->interaction) {
        return;
    }
    // A timed use that the client ends at its full time or later completes first, through the path of its timer (code review of stage
    // 2.5b, finding ST-R1: the client clears its request after its execution; the timer comes 1 ms after the time, and a report that
    // the world takes before the timer ended the use with no completion). An end before the time is a release: no completion.
    const ActiveInteraction& active = *character->interaction;
    if (active.due && !active.completed && m_world.clock().now() >= *active.due) {
        const std::uint64_t serial = active.serial;
        timed_due(character->characterId, serial);
        if (!character->interaction || character->interaction->serial != serial) {
            return; // The checks at the end ended the use (a source that went away, a disabled object).
        }
    }
    end(*character, InteractionEnd::clientEnd, false);
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
    end_grant(character, "a travel");
}

void Interactions::died(Entity& character) {
    if (character.interaction) {
        end(character, InteractionEnd::death, true);
    }
    end_grant(character, "the death of the character");
}

void Interactions::left(Entity& character) {
    if (character.interaction) {
        end(character, InteractionEnd::left, false);
    }
    end_grant(character, "the character left the world");
}

void Interactions::damaged(Entity& character) {
    if (!character.interaction || data() == nullptr || character.life.dead) {
        return;
    }
    const gamedata::InteractionOption& option = data()->options()[character.interaction->option];
    // The action InteractionAnimation ends at IfDamaged (game data); a timed use ends at damage (a decision of an agent: the
    // client continues it, live check L7); a handler whose use ends at damage (stage 3.5b.1: the open of a chest; the action Gather of
    // the client ends at IfDamagedByPowerLevel, game data).
    const Site site = site_of(character.interaction->object, character.interaction->instance, character.interaction->generation);
    const InteractionHandler* handler = site.object != nullptr ? handler_of(site.object->kind, option.id) : nullptr;
    if (option.action == kPlayInteractionAnimation || option.delay > 0.0F || (handler != nullptr && handler->ends_at_damage())) {
        end(character, InteractionEnd::damage, true);
    }
}

bool Interactions::use_valid(const Entity& character, InteractionEnd& cause) const {
    if (!character.interaction) {
        cause = InteractionEnd::policyFailed;
        return false;
    }
    return still_valid(character, *character.interaction, cause);
}

void Interactions::end_use(Entity& character, InteractionEnd cause) {
    end(character, cause, true);
}

void Interactions::moved(Entity& character) {
    if (character.positionEstimated) {
        return;
    }
    // The range of the source of a service grant (code review of stage 2.5b, finding ST-R3: a walk out of the range and back kept the
    // grant): one lookup of the source of the character, no scan.
    if (character.serviceGrant) {
        const ServiceGrant& grant = *character.serviceGrant;
        const Site site = site_of(grant.object, grant.instance, grant.generation);
        if (site.object == nullptr || grant.bound >= site.object->bounds.size()) {
            end_grant(character, "the source went away");
        } else if (distance_sq(character.position, site.object->bounds[grant.bound].position) > site.object->bounds[grant.bound].rangeSq) {
            end_grant(character, "the character left the range of the source");
        }
    }
    // A closed person conversation keeps the authority of its topics for its 5397 only in the range of the person (code review of stage
    // 1.8b, finding QR8B-R6): one lookup of the conversation of the character.
    m_world.persons().moved(character);
    if (!character.interaction) {
        return;
    }
    const ActiveInteraction& active = *character.interaction;
    const Site site = site_of(active.object, active.instance, active.generation);
    if (site.object == nullptr || active.bound >= site.object->bounds.size()) {
        return;
    }
    const gamedata::InteractionBound& bound = site.object->bounds[active.bound];
    if (distance_sq(character.position, bound.position) > bound.rangeSq) {
        end(character, InteractionEnd::rangeLost, true);
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
    if (active.due && !active.completed && m_timed > 0) {
        --m_timed;
    }
    const gamedata::InteractionData* catalog = data();
    const Site site = site_of(active.object, active.instance, active.generation);
    if (catalog != nullptr && site.object != nullptr) {
        if (ObjectState* state = state_of(site)) {
            std::vector<EntityId>& users = state->bounds[active.bound].users;
            users.erase(std::remove(users.begin(), users.end(), character.id), users.end());
            publish(site);
        }
        InteractionContext context{site.index, active.bound, site.object, &site.object->bounds[active.bound], &catalog->options()[active.option],
                                   site.instance, site.generation, site.external};
        if (InteractionHandler* handler = handler_of(site.object->kind, context.option->id)) {
            handler->end(character, context, cause);
        }
        log::write("world: interaction of character %s at the object %s ended: %s", character.characterId.c_str(), site.object->key.c_str(),
                   interaction_end_text(cause));
    }
    if (notify && character.owner != 0) {
        m_world.interaction_cancelled(character.owner);
    }
}

void Interactions::timed_due(const std::string& characterId, std::uint64_t serial) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || !character->interaction || character->interaction->serial != serial || character->interaction->completed) {
        return; // The use ended before its time (a release, a cancel): no completion.
    }
    if (m_timed > 0) {
        --m_timed;
    }
    const std::string mark = log::session_mark(character->owner, character->characterId);
    const log::Context logContext(mark);
    InteractionEnd cause = InteractionEnd::policyFailed;
    if (!still_valid(*character, *character->interaction, cause)) {
        character->interaction->completed = true; // The end below must not count the timer again.
        end(*character, cause, true);
        return;
    }
    ActiveInteraction& active = *character->interaction;
    active.completed = true;
    const Site site = site_of(active.object, active.instance, active.generation);
    const gamedata::InteractionOption& option = data()->options()[active.option];
    InteractionContext context{site.index, active.bound, site.object, &site.object->bounds[active.bound], &option, site.instance,
                               site.generation, site.external};
    ++m_counts.completions;
    std::uint32_t completions = 0;
    if (site.instance != 0 && !site.external) {
        completions = ++m_instances.at(site.instance).completions;
    }
    // The time of the use (the Delay Time of the option, or the time of the handler: finding CH-R2). The handler can end the use.
    const double seconds = std::chrono::duration<double>(m_world.clock().now() - active.started).count();
    log::write("world: timed interaction of character %s: %s at the object %s completed after %.1f s (completion %u of the object)",
               character->characterId.c_str(), option.name.c_str(), site.object->key.c_str(), seconds, completions);
    InteractionHandler* handler = handler_of(site.object->kind, option.id);
    const bool completed = handler == nullptr || handler->complete(*character, context);
    // Stage 1.8c: the completion of a timed interaction with a tag (TaskInteract with OnInteractStart false): only a use that completed
    // (code review of stage 1.8c, finding QR8C-R12), and only when the handler does not give it itself (finding QR8C-R13).
    if (completed && !context.bound->tag.empty() && (handler == nullptr || !handler->owns_interact_event(context))) {
        if (Entity* own = m_world.find_character(character->characterId)) {
            QuestEvent interacted;
            interacted.kind = QuestEventKind::interact;
            interacted.keys = {context.bound->tag};
            m_world.quests().event(*own, interacted);
        }
    }
}

bool Interactions::still_valid(const Entity& character, const ActiveInteraction& active, InteractionEnd& cause) const {
    const Site site = site_of(active.object, active.instance, active.generation);
    if (site.object == nullptr) {
        cause = InteractionEnd::sourceRemoved;
        return false;
    }
    if (character.life.dead) {
        cause = InteractionEnd::death;
        return false;
    }
    const BoundState* state = nullptr;
    if (site.external) {
        state = &m_externals.at(site.instance).state.bounds[active.bound];
    } else if (site.instance != 0) {
        state = &m_instances.at(site.instance).state.bounds[active.bound];
    } else if (const auto found = m_objects.find(active.object); found != m_objects.end()) {
        state = &found->second.bounds[active.bound];
    }
    if (state != nullptr && !state->enabled) {
        cause = InteractionEnd::policyFailed;
        return false;
    }
    const gamedata::InteractionBound& bound = site.object->bounds[active.bound];
    if (character.positionEstimated || distance_sq(character.position, bound.position) > bound.rangeSq) {
        cause = InteractionEnd::rangeLost;
        return false;
    }
    return true;
}

Interactions::ObjectState& Interactions::state_of(std::uint32_t objectIndex) {
    ObjectState& state = m_objects[objectIndex];
    if (state.bounds.empty()) {
        state.bounds.resize(data()->objects()[objectIndex].bounds.size());
    }
    return state;
}

Interactions::ObjectState* Interactions::state_of(const Site& site) {
    if (site.object == nullptr) {
        return nullptr;
    }
    if (site.external) {
        const auto found = m_externals.find(site.instance);
        return found != m_externals.end() ? &found->second.state : nullptr;
    }
    if (site.instance != 0) {
        const auto found = m_instances.find(site.instance);
        return found != m_instances.end() ? &found->second.state : nullptr;
    }
    return &state_of(site.index);
}

void Interactions::add_cells(std::vector<std::int64_t>& cells, EntityId entity, const gamedata::InteractionObject& object,
                             const state::WorldPosition& place) {
    std::vector<std::int64_t> keys;
    if (object.bounds.empty()) {
        keys.push_back(gamedata::InteractionData::cell_of(place));
    }
    for (const gamedata::InteractionBound& bound : object.bounds) {
        keys.push_back(gamedata::InteractionData::cell_of(bound.position));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (const std::int64_t key : keys) {
        m_entityCells[key].push_back(entity);
        cells.push_back(key);
    }
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
        // Each session observes the object at the distance of a character from its nearest bound (finding INT-R3).
        std::vector<std::int64_t> cells;
        add_cells(cells, entity->id, object, object.position);
        m_levelEntities[entity->id] = objectIndex;
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

void Interactions::publish(Instance& instance) {
    Entity* entity = m_world.find(instance.state.entity);
    if (entity == nullptr) {
        return;
    }
    bool changed = false;
    for (std::size_t index = 0; index < instance.state.bounds.size() && index < entity->object.bounds.size(); ++index) {
        ObjectBound& target = entity->object.bounds[index];
        const ObjectBound now{target.slot, instance.state.bounds[index].enabled,
                              static_cast<std::uint32_t>(instance.state.bounds[index].users.size())};
        if (now.enabled != target.enabled || now.users != target.users) {
            target = now;
            changed = true;
        }
    }
    if (changed) {
        m_world.touch(entity->id, ComponentKey::interact);
    }
}

void Interactions::publish(External& external) {
    Entity* entity = external.state.entity != 0 ? m_world.find(external.state.entity) : nullptr;
    if (entity == nullptr || entity->kind != EntityKind::object) {
        return; // No object entity (a creature body, or a natural node with no state of the server yet).
    }
    bool changed = false;
    for (std::size_t index = 0; index < external.state.bounds.size() && index < entity->object.bounds.size(); ++index) {
        ObjectBound& target = entity->object.bounds[index];
        const ObjectBound now{target.slot, external.state.bounds[index].enabled,
                              static_cast<std::uint32_t>(external.state.bounds[index].users.size())};
        if (now.enabled != target.enabled || now.users != target.users) {
            target = now;
            changed = true;
        }
    }
    if (changed) {
        m_world.touch(entity->id, ComponentKey::interact);
    }
}

void Interactions::publish(const Site& site) {
    if (site.object == nullptr) {
        return;
    }
    if (site.external) {
        if (const auto found = m_externals.find(site.instance); found != m_externals.end()) {
            publish(found->second);
        }
    } else if (site.instance != 0) {
        if (const auto found = m_instances.find(site.instance); found != m_instances.end()) {
            publish(found->second);
        }
    } else {
        publish(site.index);
    }
}

Entity* Interactions::level_entity(std::uint32_t objectIndex) {
    if (data() == nullptr || objectIndex >= data()->objects().size()) {
        return nullptr;
    }
    publish(objectIndex);
    return m_world.find(state_of(objectIndex).entity);
}

Entity* Interactions::external_entity(const ExternalObject& object) {
    auto found = m_externals.find(object.object.group);
    if (found == m_externals.end() || found->second.value.generation != object.generation) {
        External external;
        if (found != m_externals.end()) {
            external.state.entity = found->second.state.entity;
            external.cells = found->second.cells;
        }
        external.value = object;
        external.state.bounds.resize(object.object.bounds.size());
        found = m_externals.insert_or_assign(object.object.group, std::move(external)).first;
    }
    External& external = found->second;
    if (external.value.body != 0) {
        return m_world.find(external.value.body);
    }
    Entity* entity = external.state.entity != 0 ? m_world.find(external.state.entity) : nullptr;
    if (entity == nullptr) {
        // The entity of a natural node (its managed actor: the client applies the states to the node of its level data, live check L2 of
        // stage 3.6a), as the entity of a level object; each session observes it at the distance of a character from its bound.
        ObjectBody body;
        body.asset = external.value.object.asset;
        body.subId = external.value.object.sub;
        for (const gamedata::InteractionBound& bound : external.value.object.bounds) {
            body.bounds.push_back(ObjectBound{bound.slot, true, 0});
        }
        ActorId actor;
        actor.bytes = external.value.object.actor;
        entity = &m_world.make_object(body, actor, external.value.object.position);
        external.state.entity = entity->id;
        external.cells.clear();
        add_cells(external.cells, entity->id, external.value.object, external.value.object.position);
        m_externalEntities.insert(entity->id);
    }
    return entity;
}

const ExternalObject* Interactions::external(EntityId group) const {
    const auto found = m_externals.find(group);
    return found == m_externals.end() ? nullptr : &found->second.value;
}

void Interactions::body_removed(EntityId creature) {
    const auto found = m_externals.find(creature);
    if (found != m_externals.end() && found->second.value.body == creature) {
        m_externals.erase(found);
    }
}

Interactions::SpawnedContainer Interactions::spawn_container(std::uint32_t templateIndex, const state::WorldPosition& place, float yawDegrees,
                                                            const std::string& key, SpaceId space) {
    SpawnedContainer result;
    if (!std::isfinite(yawDegrees)) {
        return result;
    }
    const float half = yawDegrees * 3.14159265358979F / 360.0F;
    const gamedata::Rotation rotation{0.0F, 0.0F, std::sin(half), std::cos(half)};
    result.entity = spawn(templateIndex, place, rotation, key, -1, 0, "", {}, "", -1, 0, space);
    if (result.entity != nullptr) {
        result.generation = m_instances.at(result.entity->id).generation;
    }
    return result;
}

Entity* Interactions::spawn(std::uint32_t templateIndex, const state::WorldPosition& place, const gamedata::Rotation& rotation,
                            const std::string& key, std::int64_t spawnedIndex, EntityId parent, const std::string& parentKey,
                            const std::array<std::uint8_t, 16>& parentActor, const std::string& region, int permissionRule, int depth,
                            SpaceId space) {
    const gamedata::InteractionData* catalog = data();
    if (catalog == nullptr || templateIndex >= catalog->templates().size() || depth > kChildDepth || !valid_position(place)) {
        return nullptr;
    }
    const gamedata::InteractionTemplate& slice = catalog->templates()[templateIndex];
    const int permission = permissionRule >= 0 ? permissionRule : slice.permissionRule;
    ObjectBody body;
    body.index = templateIndex;
    body.asset = slice.asset;
    body.subId = slice.sub;
    body.spawned = true;
    body.positionSlot = slice.positionSlot;
    body.rotation = rotation;
    // The authored initial states of the slice (game data). Live check L4: the station shows with the metadata and the place alone;
    // the Complete state changes nothing that shows. Live checks L5 and L6: a camp and the keg need the permission rule.
    if (slice.buildableState >= 0 && slice.buildableSlot != 0) {
        body.buildableSlot = slice.buildableSlot;
        body.buildableState = static_cast<std::uint8_t>(slice.buildableState);
    }
    if (permission >= 0 && slice.ownershipSlot != 0) {
        body.ownershipSlot = slice.ownershipSlot;
        body.permissionRule = static_cast<std::uint8_t>(permission);
    }
    for (const gamedata::InteractionBound& bound : slice.bounds) {
        body.bounds.push_back(ObjectBound{bound.slot, true, 0});
    }
    Entity& entity = m_world.spawn_object(body, place, space);
    Instance instance;
    instance.templateIndex = templateIndex;
    instance.generation = ++m_generation;
    instance.spawned = spawnedIndex;
    instance.parent = parent;
    instance.parentKey = parentKey;
    instance.parentActor = parentActor;
    instance.state.entity = entity.id;
    instance.state.bounds.resize(slice.bounds.size());
    // The ids of the client (protocol notes 4.2, 4.35; live check L4 of stage 2.5b): K = root XOR G, an instance id is the entity id of
    // the slice XOR K, and a facet target is the instance id plus the component id.
    const std::uint64_t k = slice.root ^ entity.id;
    gamedata::InteractionObject& object = instance.object;
    object.key = key;
    object.kind = slice.kind;
    object.slice = slice.slice;
    object.asset = slice.asset;
    object.sub = slice.sub;
    object.actor = entity.actor.bytes;
    object.group = entity.id;
    object.region = region;
    object.position = place;
    if (slice.assembly) {
        object.assembly = slice.assembly;
        object.assembly->entity = slice.assembly->entity ^ k;
    }
    for (const gamedata::InteractionBound& local : slice.bounds) {
        gamedata::InteractionBound bound = local;
        bound.position = gamedata::transform_point(place, rotation, local.position);
        bound.target = (local.entity ^ k) + local.component;
        for (gamedata::BoundOption& option : bound.options) {
            option.entity ^= k;
        }
        object.bounds.push_back(std::move(bound));
    }
    add_cells(instance.cells, entity.id, object, place);
    const EntityId id = entity.id;
    m_instances.emplace(id, std::move(instance));
    ++m_counts.spawned;
    log::write("world: server child %016llx (%s) of %s at %.2f %.2f %.2f: %zu bound(s), generation %llu", static_cast<unsigned long long>(id),
               slice.slice.c_str(), key.c_str(), static_cast<double>(place.x), static_cast<double>(place.y), static_cast<double>(place.z),
               slice.bounds.size(), static_cast<unsigned long long>(m_instances.at(id).generation));
    // The template children (the forge of a station): the same parent placement, a place in the frame of this child.
    std::size_t childNumber = 0;
    for (const gamedata::TemplateChild& child : slice.children) {
        const state::WorldPosition childPlace = gamedata::transform_point(place, rotation, child.position);
        const gamedata::Rotation childRotation = gamedata::compose_rotation(rotation, child.rotation);
        Entity* made = spawn(child.templateIndex, childPlace, childRotation, key + "/" + std::to_string(++childNumber), -1, id, parentKey,
                             parentActor, region, -1, depth + 1, space);
        if (made != nullptr) {
            m_instances.at(id).children.push_back(made->id);
        }
    }
    return m_world.find(id);
}

bool Interactions::remove_object(EntityId id) {
    auto found = m_instances.find(id);
    if (found == m_instances.end()) {
        return false;
    }
    const std::vector<EntityId> children = found->second.children;
    for (const EntityId child : children) {
        remove_object(child);
    }
    found = m_instances.find(id);
    // Each use of the source ends (the client gets 3746), and each grant of it ends (research Q4: the removal of the source).
    for (const BoundState& bound : found->second.state.bounds) {
        const std::vector<EntityId> users = bound.users;
        for (const EntityId user : users) {
            Entity* character = m_world.find(user);
            if (character != nullptr && character->interaction && character->interaction->instance == id) {
                end(*character, InteractionEnd::sourceRemoved, true);
            }
        }
    }
    end_grants_of(id);
    found = m_instances.find(id);
    for (const std::int64_t cell : found->second.cells) {
        auto& list = m_entityCells[cell];
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
        if (list.empty()) {
            m_entityCells.erase(cell);
        }
    }
    if (found->second.spawned >= 0) {
        m_spawnedLive.erase(static_cast<std::uint32_t>(found->second.spawned));
    }
    if (found->second.parent != 0) {
        if (const auto parent = m_instances.find(found->second.parent); parent != m_instances.end()) {
            auto& list = parent->second.children;
            list.erase(std::remove(list.begin(), list.end(), id), list.end());
        }
    }
    log::write("world: server child %016llx (%s) of %s removed", static_cast<unsigned long long>(id), found->second.object.slice.c_str(),
               found->second.object.key.c_str());
    m_instances.erase(found);
    ++m_counts.removed;
    m_world.remove_object(id);
    return true;
}

void Interactions::tick() {
    scan();
}

void Interactions::scan() {
    const gamedata::InteractionData* catalog = data();
    if (catalog != nullptr && !m_boardsMade) {
        make_boards();
    }
    if (catalog == nullptr || catalog->spawned().empty()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    if (m_scanned && now - m_lastScan < kSpawnInterval) {
        return;
    }
    m_scanned = true;
    m_lastScan = now;
    ++m_counts.scans;
    gamedata::LookupWork work;
    // The children of the placements near each character (the cells of the grid near it: MP-8). Only the stations: the camps of
    // the placements (permanentcamp_master at the places "blankspace_for_player_camp", with no visual) stay in the data for the
    // camps of stage 2.6 (a decision of an agent: live check L5 of stage 2.5b, the client shows only a name and a bind option there).
    std::set<std::uint32_t> wanted;
    m_world.for_each_character([&](const Entity& character) {
        // Stage DG1: the placements are placements of the main world: a character in an instance makes no child.
        if (character.space != kMainSpace) {
            return;
        }
        catalog->for_each_spawned_near(
            character.position, kSpawnEnter,
            [&](std::uint32_t index) {
                if (m_spawnedLive.count(index) == 0
                    && catalog->templates()[catalog->spawned()[index].templateIndex].kind == gamedata::InteractionKind::craftingStation) {
                    wanted.insert(index);
                }
            },
            &work);
    });
    // The live children with no character near: only the live ones (each was made near a character).
    std::vector<EntityId> far;
    for (const auto& [index, id] : m_spawnedLive) {
        const gamedata::SpawnedPlacement& child = catalog->spawned()[index];
        bool near = false;
        m_world.for_each_near(kMainSpace, child.position, kSpawnLeave, [&](Entity& entity) { near = near || entity.kind == EntityKind::player; });
        if (!near) {
            far.push_back(id);
        }
    }
    m_counts.scanWork = work;
    for (const EntityId id : far) {
        remove_object(id);
    }
    for (const std::uint32_t index : wanted) {
        const gamedata::SpawnedPlacement& child = catalog->spawned()[index];
        if (Entity* entity = spawn(child.templateIndex, child.position, child.rotation, child.key, index, 0, child.parent, child.parentActor,
                                   child.region, -1, 0)) {
            m_spawnedLive[index] = entity->id;
        }
    }
}

FixtureResult Interactions::make_fixture(const std::string& characterId, const std::string& name, const state::WorldPosition& offset,
                                         float yawDegrees, int permissionRule) {
    FixtureResult result;
    const gamedata::InteractionData* catalog = data();
    const std::optional<std::uint32_t> fixture = catalog != nullptr ? catalog->fixture(name) : std::nullopt;
    if (!fixture) {
        result.error = FixtureError::unknownFixture;
        return result;
    }
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        result.error = FixtureError::notInWorld;
        return result;
    }
    const state::WorldPosition place{character->position.x + offset.x, character->position.y + offset.y, character->position.z + offset.z};
    if (!valid_position(place) || !std::isfinite(yawDegrees)) {
        result.error = FixtureError::badPosition;
        return result;
    }
    const float half = yawDegrees * 3.14159265358979F / 360.0F;
    const gamedata::Rotation rotation{0.0F, 0.0F, std::sin(half), std::cos(half)};
    const std::string key = "fixture/" + name + "/" + std::to_string(m_generation + 1);
    result.entity = spawn(*fixture, place, rotation, key, -1, 0, "", {}, "", permissionRule, 0);
    if (result.entity == nullptr) {
        result.error = FixtureError::badPosition;
        return result;
    }
    m_instances.at(result.entity->id).fixture = name;
    return result;
}

void Interactions::make_boards() {
    // Stage QG6a (docs/systems/mission-boards.md): the board of a settlement is a child that the client makes at the load of the level (the
    // spawner Town_Project_Board: m_spawnOnInitialize true, m_isClientAuthoratative false; the client id of its place, live: the open 1589
    // names that id). Its TerritoryInterface state is a state of the server: the entity of the level object carries it to each session near
    // it, with the actor of the client child. One time, for the boards of the data (not a scan of the world: MP-8).
    m_boardsMade = true;
    const gamedata::InteractionData* catalog = data();
    for (const std::uint32_t index : catalog->boards()) {
        const gamedata::InteractionObject& object = catalog->objects()[index];
        publish(index);
        Entity* entity = m_world.find(state_of(index).entity);
        if (entity == nullptr) {
            continue;
        }
        entity->object.territorySlot = object.territorySlot;
        entity->object.territory = m_world.territory_of(*entity);
        entity->object.providerIdentity = object.claim ? *object.claim : entity->actor.bytes;
        m_world.touch(entity->id, ComponentKey::territoryInterface);
        m_boards[object.key] = entity->id;
        if (m_spawnObserver != nullptr) {
            m_spawnObserver->child_spawned(object.key, *entity);
        }
    }
    if (!m_boards.empty()) {
        log::write("world: %zu mission board(s) of the interaction data have their entity (the actor of the client child)", m_boards.size());
    }
}

EntityId Interactions::board_entity(const std::string& key) const {
    const auto found = m_boards.find(key);
    return found != m_boards.end() ? found->second : 0;
}

Entity* Interactions::source_entity(const InteractionContext& context) const {
    if (context.instance != 0) {
        return m_world.find(context.instance);
    }
    const auto found = m_objects.find(context.objectIndex);
    return found != m_objects.end() && found->second.entity != 0 ? m_world.find(found->second.entity) : nullptr;
}

EntityId Interactions::grant_source(const ServiceGrant& grant) const {
    if (grant.instance != 0) {
        return grant.instance;
    }
    const auto found = m_objects.find(grant.object);
    return found != m_objects.end() ? found->second.entity : 0;
}

EntityId Interactions::spawned_entity(const std::string& key) const {
    for (const auto& [id, instance] : m_instances) {
        if (instance.spawned >= 0 && instance.object.key == key) {
            return id;
        }
    }
    return 0;
}

const gamedata::InteractionObject* Interactions::object_of_key(const std::string& key) const {
    if (const gamedata::InteractionData* catalog = data()) {
        if (const gamedata::InteractionObject* object = catalog->find_key(key)) {
            return object;
        }
    }
    for (const auto& [id, instance] : m_instances) {
        if (instance.object.key == key) {
            return &instance.object;
        }
    }
    return nullptr;
}

std::uint32_t Interactions::completions_of(EntityId entity) const {
    const auto found = m_instances.find(entity);
    return found != m_instances.end() ? found->second.completions : 0;
}

std::size_t Interactions::object_entities() const {
    return m_levelEntities.size() + m_instances.size() + m_externalEntities.size();
}

float Interactions::distance_to(const Entity& entity, const state::WorldPosition& place) const {
    if (entity.kind != EntityKind::object) {
        return horizontal(entity.position, place);
    }
    const gamedata::InteractionObject* object = nullptr;
    if (const auto instance = m_instances.find(entity.id); instance != m_instances.end()) {
        object = &instance->second.object;
    } else if (const auto level = m_levelEntities.find(entity.id); level != m_levelEntities.end() && data() != nullptr
                                                                    && level->second < data()->objects().size()) {
        object = &data()->objects()[level->second];
    } else if (const auto external = m_externals.find(entity.id); external != m_externals.end()) {
        object = &external->second.value.object;
    }
    if (object == nullptr || object->bounds.empty()) {
        return horizontal(entity.position, place);
    }
    float best = horizontal(object->bounds.front().position, place);
    for (const gamedata::InteractionBound& bound : object->bounds) {
        best = std::min(best, horizontal(bound.position, place));
    }
    return best;
}

ObjectError Interactions::set_enabled(const std::string& key, bool enabled) {
    const gamedata::InteractionData* catalog = data();
    const std::optional<std::uint32_t> index = catalog != nullptr ? catalog->index_of(key) : std::nullopt;
    if (index) {
        ObjectState& state = state_of(*index);
        for (BoundState& bound : state.bounds) {
            bound.enabled = enabled;
        }
        publish(*index);
        log::write("world: object %s (%s) is %s", catalog->objects()[*index].key.c_str(), catalog->objects()[*index].slice.c_str(),
                   enabled ? "enabled" : "disabled");
        return ObjectError::none;
    }
    for (auto& [id, instance] : m_instances) {
        if (instance.object.key == key) {
            for (BoundState& bound : instance.state.bounds) {
                bound.enabled = enabled;
            }
            publish(instance);
            log::write("world: server child %s (%s) is %s", instance.object.key.c_str(), instance.object.slice.c_str(),
                       enabled ? "enabled" : "disabled");
            return ObjectError::none;
        }
    }
    return ObjectError::unknownObject;
}

std::optional<StationContext> Interactions::station_of(const Site& site, std::uint32_t boundIndex, const InteractionReference& reference) const {
    if (site.object == nullptr || !site.object->assembly || boundIndex >= site.object->bounds.size()) {
        return std::nullopt;
    }
    const gamedata::InteractionObject& object = *site.object;
    const gamedata::InteractionBound& bound = object.bounds[boundIndex];
    StationContext station;
    station.objectKey = object.key;
    station.objectGeneration = site.generation;
    station.actor = object.actor;
    station.group = object.group;
    station.interactTarget = reference.target;
    for (const gamedata::BoundOption& option : bound.options) {
        const std::string& name = data()->options()[option.option].name;
        if (name == kStationOptions[0] || name == kStationOptions[1]) {
            station.optionEntity = option.entity;
            break;
        }
    }
    station.assemblyEntity = object.assembly->entity;
    station.assemblyTarget = object.assembly->entity + object.assembly->component;
    station.assemblyKey = object.assembly->key;
    station.chargeCraftingFee = object.assembly->fee;
    station.keyResolved = object.assembly->resolved;
    if (site.instance != 0) {
        const Instance& instance = m_instances.at(site.instance);
        station.parentKey = instance.parentKey;
        station.parentActor = instance.parentActor;
    }
    station.region = object.region;
    station.position = bound.position;
    station.rangeSq = bound.rangeSq;
    station.isCamp = object.kind == gamedata::InteractionKind::camp;
    return station;
}

std::optional<StationContext> Interactions::station(const Entity& character) const {
    if (!character.interaction) {
        return std::nullopt;
    }
    const ActiveInteraction& active = *character.interaction;
    return station_of(site_of(active.object, active.instance, active.generation), active.bound, active.reference);
}

bool Interactions::grant_service(Entity& character, const std::string& kind, std::vector<std::uint32_t> ids, std::uint64_t revision,
                                 std::optional<WorldClock::time_point> deadline) {
    if (!character.interaction) {
        return false;
    }
    const ActiveInteraction& active = *character.interaction;
    ServiceGrant grant;
    grant.kind = kind;
    std::sort(ids.begin(), ids.end());
    grant.ids = std::move(ids);
    grant.worldEntry = character.id;
    grant.object = active.object;
    grant.bound = active.bound;
    grant.instance = active.instance;
    grant.generation = active.generation;
    grant.source = active.reference;
    grant.revision = revision;
    grant.openedAt = m_world.clock().now();
    grant.deadline = deadline;
    character.serviceGrant = std::move(grant);
    m_granted.insert(character.id);
    log::write("world: service grant of character %s: %s with %zu id(s) from the source %s", character.characterId.c_str(), kind.c_str(),
               character.serviceGrant->ids.size(), hex64(active.reference.group).c_str());
    return true;
}

ServiceError Interactions::require_source(const std::string& characterId, const std::string& kind) {
    return require_service(characterId, kind, 0, true);
}

ServiceError Interactions::require_service(const std::string& characterId, const std::string& kind, std::uint32_t id) {
    return require_service(characterId, kind, id, false);
}

ServiceError Interactions::require_service(const std::string& characterId, const std::string& kind, std::uint32_t id, bool anyId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return ServiceError::notInWorld;
    }
    if (kind == kConversationServiceKind) {
        // Code review of stage 2.5c, finding PR5C-R3: the grant of a person is the offer of the conversation of now: the selection again
        // after a change of the state of the character, and the grant of an older conversation does not pass.
        m_world.persons().refresh_offer(*character);
        if (character->serviceGrant && character->serviceGrant->kind == kind
            && (!character->conversation || character->conversation->serial != character->serviceGrant->revision)) {
            end_grant(*character, "the grant of an older conversation");
        }
    }
    if (!character->serviceGrant || character->serviceGrant->kind != kind) {
        return ServiceError::noGrant;
    }
    const ServiceGrant& grant = *character->serviceGrant;
    ServiceError error = ServiceError::none;
    const Site site = site_of(grant.object, grant.instance, grant.generation);
    if (grant.worldEntry != character->id) {
        error = ServiceError::oldEntry;
    } else if (site.object == nullptr || grant.bound >= site.object->bounds.size()) {
        error = ServiceError::sourceGone;
    } else if (character->positionEstimated
               || distance_sq(character->position, site.object->bounds[grant.bound].position) > site.object->bounds[grant.bound].rangeSq) {
        error = ServiceError::tooFar;
    } else if (grant.deadline && m_world.clock().now() > *grant.deadline) {
        error = ServiceError::expired;
    } else if (!anyId && !std::binary_search(grant.ids.begin(), grant.ids.end(), id)) {
        return ServiceError::notOffered; // The grant stays: a different id of the offer can pass.
    }
    if (error != ServiceError::none) {
        end_grant(*character, service_error_text(error));
    }
    return error;
}

void Interactions::update_service(Entity& character, std::vector<std::uint32_t> ids) {
    if (!character.serviceGrant) {
        return;
    }
    if (ids.empty()) {
        end_grant(character, "the source offers no service now");
        return;
    }
    std::sort(ids.begin(), ids.end());
    character.serviceGrant->ids = std::move(ids);
}

bool Interactions::close_service(const std::string& characterId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || !character->serviceGrant) {
        return false;
    }
    end_grant(*character, "a close of the service");
    return true;
}

void Interactions::end_grants_of(EntityId instance) {
    const std::vector<EntityId> granted(m_granted.begin(), m_granted.end());
    for (const EntityId id : granted) {
        Entity* character = m_world.find(id);
        if (character != nullptr && character->serviceGrant && character->serviceGrant->instance == instance) {
            end_grant(*character, "the source went away");
        }
    }
}

void Interactions::end_grant(Entity& character, const char* cause) {
    m_granted.erase(character.id);
    if (!character.serviceGrant) {
        return;
    }
    log::write("world: service grant of character %s (%s) ended: %s", character.characterId.c_str(), character.serviceGrant->kind.c_str(),
               cause);
    character.serviceGrant.reset();
}

InteractionsView Interactions::view(const std::string& characterId, float radius) const {
    InteractionsView view;
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return view;
    }
    view.inWorld = true;
    view.active = character->interaction;
    view.grant = character->serviceGrant;
    const gamedata::InteractionData* catalog = data();
    if (catalog == nullptr) {
        return view;
    }
    if (view.active) {
        view.activeObject = site_of(view.active->object, view.active->instance, view.active->generation).object;
        view.station = station(*character);
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
    // The server children near the character (only the live ones: each was made near a character).
    for (const auto& [id, instance] : m_instances) {
        if (instance.object.bounds.empty()) {
            continue; // A template child with no bound (the forge): no use.
        }
        float distance = horizontal(instance.object.position, character->position);
        for (const gamedata::InteractionBound& bound : instance.object.bounds) {
            distance = std::min(distance, horizontal(bound.position, character->position));
        }
        if (distance > radius) {
            continue;
        }
        InteractionObjectView entry;
        entry.index = instance.templateIndex;
        entry.object = &instance.object;
        entry.distance = distance;
        for (std::size_t index = 0; index < instance.object.bounds.size(); ++index) {
            entry.bounds.push_back(ObjectBound{instance.object.bounds[index].slot, instance.state.bounds[index].enabled,
                                               static_cast<std::uint32_t>(instance.state.bounds[index].users.size())});
        }
        entry.entity = id;
        entry.spawned = true;
        entry.generation = instance.generation;
        entry.parentKey = instance.parentKey;
        entry.fixture = instance.fixture;
        entry.completions = instance.completions;
        view.objects.push_back(std::move(entry));
    }
    std::sort(view.objects.begin(), view.objects.end(), [](const auto& a, const auto& b) {
        if (a.distance != b.distance) {
            return a.distance < b.distance;
        }
        if (a.spawned != b.spawned) {
            return !a.spawned;
        }
        return a.index != b.index ? a.index < b.index : a.object->key < b.object->key;
    });
    return view;
}

} // namespace soulwarden::backend::world
