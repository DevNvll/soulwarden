#include "world/gathering.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/chests.h"
#include "world/data_rules.h"
#include "world/gear.h"
#include "world/interactions.h"
#include "world/rewards.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The option of a gather (uidatabase.uidb: action 14 GatherResource, committed; live check L2 of stage 3.6a). */
constexpr const char* kGatherOption = "GatherOption";
constexpr std::uint64_t kNoTarget = 0x00000000FFFFFFFFULL;
/** The source of the identity of a node (the column `source` of the store). */
constexpr const char* kCapitalSource = "capital";
constexpr const char* kDistributionSource = "distribution:";
/** The smallest speed of a gather (a guard: the parts of the data can be negative). */
constexpr float kSmallestSpeed = 0.01F;

std::string hex64(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

std::string item_list(const std::vector<GrantItem>& items) {
    std::string text;
    for (const GrantItem& item : items) {
        text += (text.empty() ? "" : ", ") + std::to_string(item.quantity) + " " + item.itemId;
    }
    return text.empty() ? "no item" : text;
}

/** @return The creation time of a creature (microseconds of the world clock): a new creature of one id is a new body. */
std::uint64_t body_generation(const Entity& creature) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(creature.creature.madeAt.time_since_epoch()).count());
}

/** The actor id of a natural node: G little-endian and 8 zero bytes (live check L2 of stage 3.6a). */
std::array<std::uint8_t, 16> actor_of_group(std::uint64_t group) {
    std::array<std::uint8_t, 16> actor{};
    for (std::size_t index = 0; index < 8; ++index) {
        actor[index] = static_cast<std::uint8_t>(group >> (8 * index));
    }
    return actor;
}

/** The handler of GatherOption of an object of the kind "gathering": the chests (world/chests.h) and the gathering nodes (this system). */
class GatherHandler final : public InteractionHandler {
public:
    GatherHandler(Gathering& gathering, Chests& chests, InteractionHandler* chest) : m_gathering(gathering), m_chests(chests), m_chest(chest) {}

    [[nodiscard]] const char* name() const override {
        return "gathering";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        return chest(context) ? m_chest->begin(character, context) : m_gathering.begin(character, context);
    }
    void started(Entity& character, const InteractionContext& context) override {
        if (chest(context)) {
            m_chest->started(character, context);
        } else {
            m_gathering.started(character, context);
        }
    }
    [[nodiscard]] float use_time(const Entity& character, const InteractionContext& context) const override {
        return chest(context) ? m_chest->use_time(character, context) : m_gathering.work_seconds(character);
    }
    bool complete(Entity& character, const InteractionContext& context) override {
        return chest(context) ? m_chest->complete(character, context) : m_gathering.complete(character, context);
    }
    /** A gather gives the quests its completed interaction itself, after its commit (code review of stage 1.8c, findings QR8C-R8, R13). */
    [[nodiscard]] bool owns_interact_event(const InteractionContext& context) const override {
        return !chest(context);
    }
    void end(Entity& character, const InteractionContext& context, InteractionEnd cause) override {
        if (chest(context)) {
            m_chest->end(character, context, cause);
        } else {
            m_gathering.ended(character, context, cause);
        }
    }
    /** The chests own the sight of their bounds (docs/systems/chests.md); the gathering bounds have no sight rule in the data. */
    [[nodiscard]] bool owns_sight() const override {
        return true;
    }
    /** The action Gather of the client ends at a hit (game data: playeractions_gather.actionlist, IfDamagedByPowerLevel >= 0). */
    [[nodiscard]] bool ends_at_damage() const override {
        return true;
    }

private:
    /**
     * @return True for an object of the chest system: a chest of the chest data; or an object that neither system knows while the chest
     *         system has no data (it refuses with "no chest data", the rule of stage 3.5b.1). Each other object goes to the gathering.
     */
    [[nodiscard]] bool chest(const InteractionContext& context) const {
        if (m_chest == nullptr) {
            return false;
        }
        if (m_chests.owns(context)) {
            return true;
        }
        return !m_gathering.owns(context) && m_chests.data() == nullptr;
    }

    Gathering& m_gathering;
    Chests& m_chests;
    InteractionHandler* m_chest;
};

} // namespace

const char* node_kind_text(NodeKind kind) {
    switch (kind) {
    case NodeKind::capital:
        return "capital";
    case NodeKind::natural:
        return "natural";
    case NodeKind::body:
        return "body";
    case NodeKind::encounter:
        return "encounter";
    }
    return "?";
}

const char* gather_refusal_text(GatherRefusal refusal) {
    switch (refusal) {
    case GatherRefusal::noData:
        return "the backend has no gathering data";
    case GatherRefusal::notNode:
        return "the object is not a node of the gathering data";
    case GatherRefusal::unsupported:
        return "a rule of the gatherable row that the backend cannot check";
    case GatherRefusal::depleted:
        return "the node is depleted";
    case GatherRefusal::alive:
        return "the creature lives";
    case GatherRefusal::noTool:
        return "no tool of the gathering type in its paperdoll slot";
    case GatherRefusal::brokenTool:
        return "the tool is broken";
    case GatherRefusal::rank:
        return "the trade skill rank is below the rank of the node";
    case GatherRefusal::noLoot:
        return "the loot tables have no table of the root of the row";
    case GatherRefusal::noSkill:
        return "the progression tables have no trade skill of the event of the row";
    case GatherRefusal::notOnQuest:
        return "the object is a quest object, and no active quest task of the character has its tag";
    case GatherRefusal::gatheredQuestObject:
        return "the character gathered the quest object: it is back after the respawn of its row";
    case GatherRefusal::missingItem:
        return "the bag does not have the item of the restriction ItemRequired";
    }
    return "?";
}

const char* node_error_text(NodeError error) {
    switch (error) {
    case NodeError::none:
        return "done";
    case NodeError::noData:
        return "the backend has no gathering data";
    case NodeError::unknownNode:
        return "no node with this key";
    case NodeError::notDepleted:
        return "the node is not depleted";
    case NodeError::badTime:
        return "the time is not in the range of the override (1 s to a week)";
    case NodeError::storeFailed:
        return "the store did not write the change";
    }
    return "?";
}

Gathering::Gathering(World& world, const gamedata::GatheringData* data) : m_world(world), m_data(data) {
    const std::uint32_t option = gamedata::interaction_option_id(kGatherOption);
    // One handler for GatherOption of the kind "gathering" (the base has one handler for each option): it gives a chest of the chest data
    // to the chest system (the handler that the chests registered) and each other object to this system.
    InteractionHandler* chest = world.interactions().handler(gamedata::InteractionKind::gathering, option);
    m_handler = std::make_unique<GatherHandler>(*this, world.chests(), chest);
    world.interactions().register_handler(gamedata::InteractionKind::gathering, option, *m_handler);
    world.interactions().register_resolver(*this);
}

Gathering::~Gathering() = default;

std::int64_t Gathering::wall_ms(WorldClock::time_point time) const {
    return static_cast<std::int64_t>(m_world.clock().wall_time_ns(time) / 1000000ULL);
}

std::optional<std::uint32_t> Gathering::gather_option() const {
    const gamedata::InteractionData* catalog = m_world.interactions().data();
    if (catalog == nullptr || data() == nullptr) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < catalog->options().size(); ++index) {
        if (catalog->options()[index].id == m_data->option_id()) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<ExternalObject> Gathering::natural_object(const gamedata::NaturalNode& node) const {
    const std::optional<std::uint32_t> option = gather_option();
    if (!option) {
        return std::nullopt;
    }
    const gamedata::GatheringTemplate& slice = m_data->templates()[node.templateIndex];
    const gamedata::GatheringRegion& region = m_data->regions()[node.region];
    // The ids of the client (protocol notes 4.2): K = root XOR G; an instance id is the entity id of the slice XOR K, and a facet
    // target is the instance id plus the component id (live check L2 of stage 3.6a: the target of the request of record 18627).
    const std::uint64_t k = slice.root ^ node.group;
    ExternalObject external;
    gamedata::InteractionObject& object = external.object;
    object.key = region.name + "#" + std::to_string(node.record);
    object.kind = gamedata::InteractionKind::gathering;
    object.slice = slice.slice;
    object.asset = slice.asset;
    object.sub = slice.sub;
    object.actor = actor_of_group(node.group);
    object.group = node.group;
    object.region = region.name;
    object.position = node.position;
    gamedata::InteractionBound bound;
    bound.slot = slice.bound.slot;
    bound.component = slice.bound.component;
    bound.target = (slice.bound.entity ^ k) + slice.bound.component;
    bound.position = state::WorldPosition{node.position.x + slice.bound.translation[0], node.position.y + slice.bound.translation[1],
                                          node.position.z + slice.bound.translation[2]};
    bound.rangeSq = slice.bound.rangeSq;
    bound.capacity = slice.bound.capacity;
    bound.lineOfSight = slice.bound.lineOfSight;
    bound.arcRequired = slice.bound.arcRequired;
    bound.condition = slice.bound.requirement;
    bound.options.push_back(gamedata::BoundOption{*option, slice.bound.option ^ k});
    object.bounds.push_back(std::move(bound));
    return external;
}

std::optional<ExternalObject> Gathering::body_object(const Entity& creature) const {
    const std::optional<std::uint32_t> option = gather_option();
    if (!option || creature.kind != EntityKind::creature || creature.creature.body == nullptr) {
        return std::nullopt;
    }
    const std::optional<gamedata::GatheringLink> link = m_data->creature(creature.creature.body->asset, creature.creature.body->subId);
    if (!link) {
        return std::nullopt;
    }
    const gamedata::GatheringTemplate& slice = m_data->templates()[link->templateIndex];
    const std::uint64_t k = slice.root ^ creature.id;
    ExternalObject external;
    external.body = creature.id;
    // A new creature is a new entity (a new id): the time of its creation separates two bodies of one id in a test host.
    external.generation = body_generation(creature);
    gamedata::InteractionObject& object = external.object;
    object.key = "body/" + hex64(creature.id);
    object.kind = gamedata::InteractionKind::gathering;
    object.slice = slice.slice;
    object.asset = slice.asset;
    object.sub = slice.sub;
    object.actor = creature.actor.bytes;
    object.group = creature.id;
    object.position = creature.position;
    // The bound in the frame of the body: its translation turned by the direction of the creature (game data: the GatherBound entity).
    const float yaw = creature.creature.yaw * 3.14159265358979F / 180.0F;
    const float x = slice.bound.translation[0];
    const float y = slice.bound.translation[1];
    gamedata::InteractionBound bound;
    bound.slot = slice.bound.slot;
    bound.component = slice.bound.component;
    bound.target = (slice.bound.entity ^ k) + slice.bound.component;
    bound.position = state::WorldPosition{creature.position.x + x * std::cos(yaw) + y * std::sin(yaw),
                                          creature.position.y - x * std::sin(yaw) + y * std::cos(yaw),
                                          creature.position.z + slice.bound.translation[2]};
    bound.rangeSq = slice.bound.rangeSq;
    bound.capacity = slice.bound.capacity;
    bound.lineOfSight = slice.bound.lineOfSight;
    bound.arcRequired = slice.bound.arcRequired;
    bound.condition = slice.bound.requirement;
    bound.options.push_back(gamedata::BoundOption{*option, slice.bound.option ^ k});
    object.bounds.push_back(std::move(bound));
    return external;
}

std::optional<ExternalObject> Gathering::resolve(const InteractionReference& reference) {
    if (data() == nullptr) {
        return std::nullopt;
    }
    if (const std::optional<gamedata::NaturalNode> node = m_data->natural(reference.group)) {
        ++m_counts.resolved;
        return natural_object(*node);
    }
    // A G of a region whose file cannot be read, or is not the file of the data (code review of stage 3.6a, finding GA-R2): no node of
    // the region can be gathered (the base refuses the request). The log names the region one time.
    if (const std::optional<std::uint32_t> region = m_data->region_of(reference.group);
        region && !m_data->region_valid(*region) && m_badRegions.insert(*region).second) {
        log::write("world: gathering: the file of the region %s is not there, not complete, or not the file of the gathering data: no node "
                   "of the region can be gathered, and its stored depletions stay (write the files again with tools/gamedata/gathering.sh)",
                   m_data->regions()[*region].name.c_str());
    }
    const Entity* creature = m_world.find(reference.group);
    if (creature != nullptr && creature->kind == EntityKind::creature && creature->actor.bytes == reference.actor) {
        if (std::optional<ExternalObject> body = body_object(*creature)) {
            ++m_counts.resolved;
            return body;
        }
    }
    return std::nullopt;
}

bool Gathering::alive(const ExternalObject& object) const {
    if (object.body == 0) {
        return true; // A natural node is always the node of its record.
    }
    const Entity* creature = m_world.find(object.body);
    return creature != nullptr && creature->kind == EntityKind::creature && creature->creature.dead
           && body_generation(*creature) == object.generation;
}

bool Gathering::owns(const InteractionContext& context) const {
    return target_of(context).has_value();
}

std::optional<Gathering::Target> Gathering::target_of(const InteractionContext& context) const {
    if (data() == nullptr || context.object == nullptr) {
        return std::nullopt;
    }
    Target target;
    if (!context.external) {
        if (context.instance != 0) {
            // Stage QG5c2: the start object of an encounter stage (a server child of the encounter system). Each other server child is no
            // node.
            const std::string* slice = m_world.encounters().start_object(context.instance);
            const std::optional<gamedata::GatheringLink> link = slice != nullptr ? m_data->encounter_object(*slice) : std::nullopt;
            if (!link) {
                return std::nullopt;
            }
            target.key = "encounter/" + hex64(context.instance);
            target.kind = NodeKind::encounter;
            target.row = link->row;
            target.templateIndex = link->templateIndex;
            target.objectIndex = context.objectIndex;
            target.group = context.instance;
            target.source = "encounter";
            const Entity* object = m_world.find(context.instance);
            target.position = object != nullptr ? object->position : state::WorldPosition{};
            target.interactTag = link->interactTag;
            target.onlyOnQuest = link->onlyOnQuest;
            return target;
        }
        const std::optional<gamedata::GatheringLink> link = m_data->capital(context.object->key);
        if (!link) {
            return std::nullopt;
        }
        target.key = context.object->key;
        target.kind = NodeKind::capital;
        target.row = link->row;
        target.templateIndex = link->templateIndex;
        target.objectIndex = context.objectIndex;
        target.group = context.object->group;
        target.source = kCapitalSource;
        target.position = context.object->position;
        target.interactTag = link->interactTag;
        target.onlyOnQuest = link->onlyOnQuest;
        return target;
    }
    if (const std::optional<gamedata::NaturalNode> node = m_data->natural(context.instance)) {
        const gamedata::GatheringRegion& region = m_data->regions()[node->region];
        target.key = region.name + "#" + std::to_string(node->record);
        target.kind = NodeKind::natural;
        target.row = node->row;
        target.templateIndex = node->templateIndex;
        target.group = node->group;
        target.source = std::string(kDistributionSource) + region.version;
        target.position = node->position;
        return target;
    }
    const Entity* creature = m_world.find(context.instance);
    if (creature != nullptr && creature->kind == EntityKind::creature && creature->creature.body != nullptr) {
        if (const std::optional<gamedata::GatheringLink> link = m_data->creature(creature->creature.body->asset, creature->creature.body->subId)) {
            target.key = "body/" + hex64(creature->id);
            target.kind = NodeKind::body;
            target.row = link->row;
            target.templateIndex = link->templateIndex;
            target.group = creature->id;
            target.source = "body";
            target.position = creature->position;
            return target;
        }
    }
    return std::nullopt;
}

InteractionVerdict Gathering::refuse(GatherRefusal refusal, const Entity& character, const std::string& key, const std::string& detail) {
    ++m_counts.refusals[static_cast<std::size_t>(refusal)];
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: gathering: the gather of %s by character %s refused: %s%s%s", key.c_str(), character.characterId.c_str(),
               gather_refusal_text(refusal), detail.empty() ? "" : ": ", detail.c_str());
    return InteractionVerdict::refused;
}

float Gathering::effect_sum(const Entity& character, const std::string& column) const {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (abilities == nullptr || !abilities->loaded()) {
        return 0.0F;
    }
    float sum = 0.0F;
    const WorldClock::time_point now = m_world.clock().now();
    const auto status_value = [&](std::string_view id) {
        const gamedata::StatusRow* row = id.empty() ? nullptr : abilities->status(id);
        return row != nullptr ? row->cells.number(column) : 0.0F;
    };
    // The statuses on the character (a food, a potion, the statuses of the gear: status rows of the data with EFF, ROL, MULT, INS cells).
    for (const ActiveStatus& status : character.statuses) {
        if (!status.permanent && now >= status.ends) {
            continue;
        }
        sum += status_value(status.status) * status.potency * static_cast<float>(status.stacks);
    }
    // The attribute thresholds (game data: Str_Bonus_25_2 has OnEquipStatusEffect ATB_Str_Bonus_25_2_Effect, EFFMining 0.1).
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    if (tables != nullptr && tables->loaded()) {
        static constexpr std::array<std::pair<unsigned, const char*>, attribute::kCount> kNames{
            {{attribute::kStrength, "strength"}, {attribute::kDexterity, "dexterity"}, {attribute::kIntelligence, "intelligence"},
             {attribute::kFocus, "focus"}, {attribute::kConstitution, "constitution"}}};
        for (const auto& [type, name] : kNames) {
            const auto found = tables->data().attributeAbilities.find(name);
            if (found == tables->data().attributeAbilities.end()) {
                continue;
            }
            const std::uint32_t value = character.attributes.value(type);
            for (const gamedata::AttributeThreshold& threshold : found->second) {
                if (threshold.value > value) {
                    break;
                }
                for (const std::string& id : threshold.abilities) {
                    if (const gamedata::AbilityRow* row = abilities->ability_row(id)) {
                        sum += row->cells.number(column) + status_value(row->cells.text("OnEquipStatusEffect"));
                    }
                }
            }
        }
    }
    return sum;
}

GatherParts Gathering::parts_of(const Entity& character, const gamedata::GatherableRow& row, std::string* toolError) const {
    GatherParts parts;
    if (data() == nullptr) {
        return parts;
    }
    const std::uint32_t slot = gamedata::tool_slot(row.type);
    if (slot != 0) {
        const ItemInstance* item = character.equipment.item(slot);
        const gamedata::GatheringTool* tool = item != nullptr ? m_data->tool(item->itemId) : nullptr;
        if (tool != nullptr && std::find(tool->types.begin(), tool->types.end(), row.type) != tool->types.end()) {
            // Live check L7: the "Gather Speed" of the tooltip is MinGatherEFF to MaxGatherEFF, linear over the gear score range of the
            // item (T5 axe: 625 % at 500, 738 % at 550, 850 % at 600).
            float part = 0.0F;
            const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
            const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item->itemId) : nullptr;
            if (definition != nullptr && definition->maxGearScore > definition->minGearScore) {
                part = std::clamp((static_cast<float>(item->gearScore) - static_cast<float>(definition->minGearScore))
                                      / static_cast<float>(definition->maxGearScore - definition->minGearScore),
                                  0.0F, 1.0F);
            }
            parts.toolSpeed = tool->minEfficiency + part * (tool->maxEfficiency - tool->minEfficiency);
            parts.multiplier += tool->multiplier;
            parts.toolItem = item->itemId;
            parts.toolGearScore = item->gearScore;
            // The perks of the tool (its places and the fixed perks of its definition): their affix and ability rows and the statuses
            // that they name (a perk of luck: ROL; of speed: EFF; of yield: MULT).
            const gamedata::PerkData* perks = m_world.options().perkData;
            const gamedata::AbilityData* abilities = m_world.options().abilityData;
            if (perks != nullptr && !row.tradeskill.empty()) {
                const auto status_value = [&](std::string_view id, const std::string& column) {
                    const gamedata::StatusRow* status = (abilities != nullptr && abilities->loaded() && !id.empty()) ? abilities->status(id) : nullptr;
                    return status != nullptr ? status->cells.number(column) : 0.0F;
                };
                for (const GearPerk& perk : m_world.gear().item_perks(*item, slot)) {
                    if (perk.perk == nullptr) {
                        continue;
                    }
                    const auto add = [&](const gamedata::CellValues* cells, const char* statusColumn) {
                        if (cells == nullptr) {
                            return;
                        }
                        for (const auto& [prefix, target] : {std::pair<const char*, float*>{"EFF", &parts.effects},
                                                             {"ROL", &parts.rollEffects}, {"MULT", &parts.multiplier},
                                                             {"INS", &parts.instant}}) {
                            const std::string column = prefix + row.tradeskill;
                            *target += (cells->number(column) + status_value(cells->text(statusColumn), column)) * perk.potency;
                        }
                    };
                    for (const std::string& id : perk.perk->affixes) {
                        add(perks->affix(id), "StatusEffect");
                    }
                    for (const std::string& id : perk.perk->abilities) {
                        add(perks->ability(id), "OnEquipStatusEffect");
                    }
                }
            }
        } else if (toolError != nullptr) {
            *toolError = item == nullptr ? "the paperdoll slot " + std::to_string(slot) + " has no item"
                                         : "the item " + item->itemId + " in slot " + std::to_string(slot) + " is not a tool of the type "
                                               + gamedata::gathering_type_text(row.type);
        }
    }
    if (!row.tradeskill.empty()) {
        const std::uint32_t rank = character.skills.rank(gamedata::progression_id(row.tradeskill));
        const gamedata::GatheringRank values = m_data->rank(row.tradeskill, rank);
        parts.rankEfficiency = values.efficiency;
        parts.rollBonus = values.rollBonus;
        parts.multiplier += values.multiplier;
        parts.effects += effect_sum(character, "EFF" + row.tradeskill);
        parts.rollEffects += effect_sum(character, "ROL" + row.tradeskill);
        parts.multiplier += effect_sum(character, "MULT" + row.tradeskill);
        parts.instant += effect_sum(character, "INS" + row.tradeskill);
    }
    // Stage QG1: the GainGatherRate card of the territory at the position of the character (docs/systems/progression-and-skills.md).
    parts.territory = m_world.skills().territory_bonus(character.skills, m_world.territory_of(character), gamedata::TerritoryBonus::gainGatherRate);
    // The composition (a decision of an agent, docs/systems/gathering.md: the client has no rule that composes the parts; live check L7):
    // the tool speed times one plus the rank part, the EFF parts, and (stage QG1) the territory card (the trade skill window and the
    // effects show them as percents).
    parts.speed = std::max(kSmallestSpeed, parts.toolSpeed * (1.0F + parts.rankEfficiency + parts.effects + parts.territory));
    return parts;
}

std::optional<GatherRefusal> Gathering::check_gates(const Entity& character, const gamedata::GatherableRow& row,
                                                            std::string& detail) const {
    const std::uint32_t slot = gamedata::tool_slot(row.type);
    if (slot != 0) {
        // The tool of the gathering type in its paperdoll slot (live check L6: a tool in the bag does not count; client 0x48FF240,
        // FindFirstGatheringTool of the paperdoll), and its weapon row has the type (GatheringTypes, game data).
        const ItemInstance* item = character.equipment.item(slot);
        const gamedata::GatheringTool* tool = item != nullptr ? m_data->tool(item->itemId) : nullptr;
        if (tool == nullptr || std::find(tool->types.begin(), tool->types.end(), row.type) == tool->types.end()) {
            detail = item == nullptr ? "slot " + std::to_string(slot) + " is empty" : item->itemId + " is not a tool of the node";
            return GatherRefusal::noTool;
        }
        // A broken tool: maximum durability above 0 and durability 0 (client 0x5C65650); the action of the client also ends at it.
        if (item->maxDurability > 0 && item->durability == 0) {
            detail = item->itemId;
            return GatherRefusal::brokenTool;
        }
    }
    if (!row.tradeskill.empty() && row.level > 0) {
        const std::uint32_t rank = character.skills.rank(gamedata::progression_id(row.tradeskill));
        if (rank < row.level) {
            detail = row.tradeskill + " " + std::to_string(rank) + " of " + std::to_string(row.level);
            return GatherRefusal::rank;
        }
    }
    // Stage QG5b (static read B1 and B2 of stage QG5b): the restriction ItemRequired (enum 4; only it reaches the item branch of the gather
    // validation 0x35C2C8E) needs ItemRestrictionQuantity of ItemRestrictionId in the slots of the bag (the Container check 0x48D6200: the sum
    // of the stacks, at least the quantity; no paperdoll, no storage). A blank or 0 quantity needs nothing (0x48D626D). NoToolNecessary and
    // ToolRequired with an item do not check it (0x35C2A18).
    if (row.restriction == gamedata::kRestrictionItemRequired && !row.item.empty() && row.itemCount > 0) {
        const std::uint64_t have = Inventory::bag_count(character, row.item);
        if (have < row.itemCount) {
            detail = row.item + " " + std::to_string(have) + " of " + std::to_string(row.itemCount) + " in the bag";
            return GatherRefusal::missingItem;
        }
    }
    return std::nullopt;
}

bool Gathering::is_depleted(const std::string& key) const {
    const auto found = m_nodes.find(key);
    return found != m_nodes.end() && found->second.depleted;
}

const GatherNode* Gathering::node(const std::string& key) const {
    const auto found = m_nodes.find(key);
    return found == m_nodes.end() ? nullptr : &found->second;
}

std::size_t Gathering::depleted_count() const {
    std::size_t count = 0;
    for (const auto& [key, node] : m_nodes) {
        count += node.depleted ? 1 : 0;
    }
    return count;
}

InteractionVerdict Gathering::begin(Entity& character, const InteractionContext& context) {
    const std::string key = context.object != nullptr ? context.object->key : std::string("?");
    if (data() == nullptr) {
        return refuse(GatherRefusal::noData, character, key, "make the files with tools/gamedata/gathering.sh");
    }
    const std::optional<Target> target = target_of(context);
    if (!target) {
        return refuse(GatherRefusal::notNode, character, key, context.object != nullptr ? context.object->slice : "");
    }
    const gamedata::GatherableRow& row = m_data->rows()[target->row];
    if (!row.unsupported.empty()) {
        return refuse(GatherRefusal::unsupported, character, key, row.id + ": " + row.unsupported.front());
    }
    if (target->kind == NodeKind::body) {
        const Entity* creature = m_world.find(target->group);
        if (creature == nullptr || !creature->creature.dead) {
            return refuse(GatherRefusal::alive, character, key, row.id);
        }
        // The node state of the body is the rule (code review of stage 3.6a, finding GA-R1): a skinned body stays Depleted while the
        // creature system has it, also when its entry is not there.
        if (creature->creature.skinState == gathering_rules::kDepleted) {
            return refuse(GatherRefusal::depleted, character, key, row.id);
        }
    }
    // Stage 1.8c: a quest object is open only while an active task of the character has its tag (InteractOnlyOnQuest; client 0x8261FC0,
    // live check G1). Each character gathers its own object: no shared depletion (InstancedLootType 4).
    if (target->onlyOnQuest && !m_world.quests().quest_object_open(character, target->interactTag)) {
        return refuse(GatherRefusal::notOnQuest, character, key, row.id + ": " + target->interactTag);
    }
    // Stage QG5a: a quest object that the character gathered is not there for it until its respawn (its own LootTracker entry).
    if (row.quest && character.questGathers.count(target->key) != 0) {
        return refuse(GatherRefusal::gatheredQuestObject, character, key, row.id);
    }
    if (!row.quest && is_depleted(target->key)) {
        return refuse(GatherRefusal::depleted, character, key, row.id);
    }
    std::string detail;
    if (const std::optional<GatherRefusal> refusal = check_gates(character, row, detail)) {
        return refuse(*refusal, character, key, row.id + ": " + detail);
    }
    const gamedata::LootTables* tables = m_world.rewards().tables();
    if (!no_roll(row, target->kind) && (tables == nullptr || !tables->table_index(row.loot))) {
        return refuse(GatherRefusal::noLoot, character, key, row.id + ": " + row.loot);
    }
    if (!row.event.empty()) {
        // Stage 1.8c: the event of a quest object is a game event of the quests (MSQ_Treasure_B_interact has no row of the event tables):
        // no trade skill experience.
        const gamedata::GatheringEvent* event = m_data->event(row.event);
        const gamedata::ProgressionTables* progression = m_world.options().progressionTables;
        if ((event == nullptr && !row.quest) || (event != nullptr && event->amount > 0 && (progression == nullptr || !progression->loaded()
                                                       || progression->progression(event->progression) == nullptr))) {
            return refuse(GatherRefusal::noSkill, character, key, row.id + ": " + row.event);
        }
    }
    // The work amount: the rest of an earlier work of the character on the same node (a row with ResetGatherTimeOnEndInteraction false;
    // game data), else BaseGatherTime.
    const std::uint64_t generation = m_nodes.count(target->key) != 0 ? m_nodes.at(target->key).generation : 0;
    float amount = row.time;
    if (!row.resetOnEnd) {
        for (const Rest& rest : m_rests[character.characterId]) {
            if (rest.node == target->key && rest.generation == generation && rest.amount > 0.0F && rest.amount < row.time) {
                amount = rest.amount;
            }
        }
    }
    const WorldClock::time_point now = m_world.clock().now();
    GatherWork work;
    work.node = target->key;
    work.nodeGeneration = generation;
    work.kind = target->kind;
    work.row = target->row;
    work.amount = amount;
    work.parts = parts_of(character, row);
    work.seconds = amount / work.parts.speed;
    work.started = now;
    work.due = now + std::chrono::duration_cast<WorldClock::time_point::duration>(std::chrono::duration<float>(work.seconds));
    work.group = target->group;
    // The work begins at started(): the base calls it after the end of the use that this use replaces, and that end keeps the rest of the
    // old work (code review of stage 3.6a, finding GA-R3). A refusal of the base before started() is not possible after begin.
    m_pending[character.characterId] = work;
    ++m_counts.begins;
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    log::write("world: gathering: character %s gathers the %s node %s (%s, G %s): amount %.2f, speed %.3f (tool %.3f%s%s, rank %+.3f, "
               "effects %+.3f, territory card %+.4f), %.2f s",
               character.characterId.c_str(), node_kind_text(target->kind), target->key.c_str(), row.id.c_str(), hex64(target->group).c_str(),
               static_cast<double>(amount), static_cast<double>(work.parts.speed), static_cast<double>(work.parts.toolSpeed),
               work.parts.toolItem.empty() ? "" : " ", work.parts.toolItem.c_str(), static_cast<double>(work.parts.rankEfficiency),
               static_cast<double>(work.parts.effects), static_cast<double>(work.parts.territory), static_cast<double>(work.seconds));
    return InteractionVerdict::accepted;
}

void Gathering::started(Entity& character, const InteractionContext& context) {
    const auto found = m_pending.find(character.characterId);
    if (found == m_pending.end()) {
        return;
    }
    const GatherWork work = found->second;
    m_pending.erase(found);
    const std::optional<Target> target = target_of(context);
    if (!target || target->key != work.node) {
        return; // Not the use of the prepared work.
    }
    m_work[character.characterId] = work;
    // The Gathering state of the owner (live check L3: Success, the amount, the speed in field 2, the G of the node: the ring runs
    // amount / speed seconds).
    set_gather(character, gathering_rules::kStatusSuccess, work.amount, work.parts.speed, true, work.group);
}

float Gathering::work_seconds(const Entity& character) const {
    // The base asks for the time between begin and started: the prepared work.
    if (const auto pending = m_pending.find(character.characterId); pending != m_pending.end()) {
        return pending->second.seconds;
    }
    const auto found = m_work.find(character.characterId);
    return found != m_work.end() ? found->second.seconds : 0.0F;
}

void Gathering::set_gather(Entity& character, std::uint8_t status, float remaining, float speed, bool active, std::uint64_t target) {
    GatherProgress& gather = character.gathering;
    gather.status = status;
    gather.remaining = remaining;
    gather.speed = speed;
    gather.gatherers = active ? 1 : 0;
    gather.active = active;
    gather.target = target;
    // The client applies the fields only at a change of their reference counters (apply 0x4905130).
    ++gather.reference;
    ++gather.syncReference;
    m_world.touch(character.id, ComponentKey::gathering);
}

void Gathering::fail_work(Entity& character) {
    m_work.erase(character.characterId);
    set_gather(character, gathering_rules::kStatusFailure, 0.0F, gathering_rules::kNoToolSpeed, false, kNoTarget);
}

void Gathering::ended(Entity& character, const InteractionContext& context, InteractionEnd cause) {
    const auto found = m_work.find(character.characterId);
    if (found == m_work.end()) {
        return; // No work (a completed work: the client ends its use after Done).
    }
    const std::optional<Target> target = target_of(context);
    if (!target || target->key != found->second.node) {
        return; // The end of a different use.
    }
    const GatherWork work = found->second;
    const gamedata::GatherableRow& row = m_data->rows()[work.row];
    // The rest of the work (a row with ResetGatherTimeOnEndInteraction false: game data): the next use of the same node continues it.
    float rest = 0.0F;
    if (!row.resetOnEnd) {
        const float done = std::chrono::duration<float>(m_world.clock().now() - work.started).count() * work.parts.speed;
        rest = std::max(0.0F, work.amount - done);
        std::vector<Rest>& rests = m_rests[character.characterId];
        rests.erase(std::remove_if(rests.begin(), rests.end(), [&](const Rest& entry) { return entry.node == work.node; }), rests.end());
        if (rest > 0.0F) {
            rests.push_back(Rest{work.node, work.nodeGeneration, rest});
        }
    }
    ++m_counts.cancels;
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    log::write("world: gathering: the gather of %s by character %s ended before its time: %s (the rest of the work: %.2f)", work.node.c_str(),
               character.characterId.c_str(), interaction_end_text(cause), static_cast<double>(rest));
    fail_work(character);
}

bool Gathering::no_roll(const gamedata::GatherableRow& row, NodeKind kind) {
    // A quest row (its result is the quest drops), and (stage QG5c5) an encounter start object whose row has the loot table Empty or none
    // (BoatEncounter_Scallywag: Handcrafting, no tool, no event; the encounter counts the gather) [agent: the rule of the quest rows].
    return row.quest || (kind == NodeKind::encounter && (row.loot.empty() || row.loot == "Empty"));
}

bool Gathering::complete(Entity& character, const InteractionContext& context) {
    const std::string characterId = character.characterId;
    const auto found = m_work.find(characterId);
    const std::optional<Target> target = target_of(context);
    if (found == m_work.end() || !target || target->key != found->second.node) {
        return false; // No work on this node (a different use).
    }
    const GatherWork work = found->second;
    const gamedata::GatherableRow& row = m_data->rows()[work.row];
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    // Two characters at one node: the first valid completion gets the materials and depletes the node; the work of the other ends (a
    // decision of an agent through the orchestrator: docs/systems/gathering.md).
    // (The base checked the use again before this call: the source, a creature body that went away, the life, the range.)
    const auto node = m_nodes.find(work.node);
    const Entity* body = work.kind == NodeKind::body ? m_world.find(work.group) : nullptr;
    // Code review of stage QG5c5, finding QG5C5R-R5: an encounter start object with an ordinary row gives its reward one time (the first
    // completion after its commit claims it).
    const bool claimed = work.kind == NodeKind::encounter && !row.quest && m_claimedObjects.count(work.group) != 0;
    if ((node != m_nodes.end() && (node->second.depleted || node->second.generation != work.nodeGeneration))
        || (body != nullptr && body->kind == EntityKind::creature && body->creature.skinState == gathering_rules::kDepleted) || claimed) {
        ++m_counts.contested;
        log::write("world: gathering: the node %s was depleted before the gather of character %s ended: no reward", work.node.c_str(),
                   characterId.c_str());
        fail_work(character);
        m_world.interactions().end_use(character, InteractionEnd::policyFailed);
        return false;
    }
    std::string detail;
    if (const std::optional<GatherRefusal> refusal = check_gates(character, row, detail)) {
        ++m_counts.cancels;
        log::write("world: gathering: the gather of %s by character %s ends with no reward: %s (%s)", work.node.c_str(), characterId.c_str(),
                   gather_refusal_text(*refusal), detail.c_str());
        fail_work(character);
        m_world.interactions().end_use(character, InteractionEnd::policyFailed);
        return false;
    }
    // The roll: a kept roll of an earlier completion that found no place (no new roll), else the root of the row with the roller of
    // the rewards (client 0x368B070) and the luck of the rank and the ROL parts (a bonus of each table of the path).
    // A character has one kept roll for each node (code review of stage 3.6a, finding GA-R7).
    std::vector<GrantItem> items;
    std::vector<Rest>& rests = m_rests[characterId];
    std::vector<KeptRoll>& kept = m_kept[characterId];
    const auto roll = std::find_if(kept.begin(), kept.end(), [&](const KeptRoll& entry) { return entry.node == work.node; });
    if (no_roll(row, target->kind)) {
        // Stage 1.8c: a quest object has no loot of its own (the loot table Empty): its result is the quest drops below. Stage QG5c5: the
        // same for an encounter start object with no loot table (the spyglass of the boat reinforcements).
    } else if (roll != kept.end() && roll->generation == work.nodeGeneration) {
        items = roll->items;
    } else {
        LootContext loot;
        loot.characterLevel = character.progression.level;
        loot.creatureLevel = character.progression.level;
        const float luck = std::max(0.0F, work.parts.rollBonus + work.parts.rollEffects);
        loot.rollBonus = static_cast<std::uint32_t>(std::lround(std::min(luck, 1.0e6F)));
        const std::uint32_t root = *m_world.rewards().tables()->table_index(row.loot);
        items = m_world.rewards().grant_items(m_world.rewards().roll_root(root, loot), character.progression.level);
        ++m_counts.rolls;
    }
    const gamedata::GatheringEvent* event = row.event.empty() ? nullptr : m_data->event(row.event);
    // Stage 1.8c: the quest drops of the character (TaskGatherItems, TaskGatherAndReturnItems: ItemDropVC names the gatherable row). They
    // are a part of the result (one transaction; a full bag refuses the whole gather) and not a part of a kept roll (a later gather rolls
    // them again).
    const std::vector<QuestDrop> drops = m_world.quests().plan_drops(character, true, {row.id});
    std::vector<GrantItem> given = items;
    for (const QuestDrop& drop : drops) {
        given.push_back(GrantItem{drop.itemId, drop.quantity, std::nullopt, {}});
    }
    // One transaction of the store (MP-7): the items, the trade skill experience, and the row of the depleted node.
    const Bag bagBefore = character.bag;
    const Wallet walletBefore = character.wallet;
    const SkillSet skillsBefore = character.skills;
    const WorldClock::time_point now = m_world.clock().now();
    const std::chrono::milliseconds wait = respawn_time(row);
    GatherNode next;
    if (node != m_nodes.end()) {
        next = node->second;
    } else {
        next.key = target->key;
        next.kind = target->kind;
        next.row = target->row;
        next.templateIndex = target->templateIndex;
        next.objectIndex = target->objectIndex;
        next.group = target->group;
        next.source = target->source;
        next.position = target->position;
        next.generation = 0;
    }
    if (body != nullptr && body->kind == EntityKind::creature) {
        next.bodyMade = body_generation(*body);
    }
    next.depleted = true;
    ++next.generation;
    next.respawn = now + wait;
    next.respawnRunNs = m_world.world_time().run_ns() + static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count());
    next.depletedAt = wall_ms(now);
    state::Store::Batch batch(m_world.store());
    bool written = batch.open();
    ItemResult grant;
    if (written && !given.empty()) {
        grant = m_world.inventory().grant(characterId, given, 0, nullptr, true);
        written = grant.error != ItemError::storeFailed;
    }
    // A full bag, or an item of the roll that the item catalog does not have: the gather is refused before the commit (decision of the
    // orchestrator: no material, no experience, no depletion; the node stays). The roll stays with the character for its next
    // completion on this node (no item is lost, nothing is rolled again; the rule of the chests, finding CH-R3).
    const bool full = grant.error == ItemError::bagFull || grant.error == ItemError::unknownItem || grant.error == ItemError::noItemType;
    if (written && !full && event != nullptr && event->amount > 0) {
        const SkillResult skill = m_world.skills().add_experience(characterId, event->progression, event->amount);
        written = skill.ok();
    }
    written = written && !full && m_world.quests().write_drops(character, drops);
    // Stage QG5c5: a start object of an encounter (also one with an ordinary row: the Azoth Staff rows of the Darkness pylons and cores)
    // has no node row: the next instance makes a new object.
    if (written && !full && target->kind != NodeKind::body && target->kind != NodeKind::encounter && !row.quest) {
        state::GatheringNodeRow stored;
        stored.key = next.key;
        stored.source = next.source;
        stored.row = row.id;
        stored.generation = next.generation;
        stored.respawnRunNs = next.respawnRunNs;
        stored.depletedAt = next.depletedAt;
        written = m_world.store().write_gathering_node(stored);
    }
    written = written && !full && batch.commit();
    if (!written) {
        character.bag = bagBefore;
        character.wallet = walletBefore;
        character.skills = skillsBefore;
        m_world.touch(character.id, ComponentKey::container);
        m_world.touch(character.id, ComponentKey::currency);
        m_world.touch(character.id, ComponentKey::categorical);
        if (full) {
            ++m_counts.fullBags;
            // The kept roll of this node replaces only an earlier roll of this node; a roll of a node that changed since (depleted or
            // respawned) goes (finding GA-R7).
            kept.erase(std::remove_if(kept.begin(), kept.end(),
                                      [&](const KeptRoll& entry) { return entry.node == work.node || !roll_current(entry); }),
                       kept.end());
            kept.push_back(KeptRoll{work.node, work.nodeGeneration, items});
            log::write("world: gathering: %s character %s for the roll of %s (%s): no item, no experience, the node stays",
                       grant.error == ItemError::bagFull ? "the bag has no place for" : "the item catalog has no item of the roll of",
                       characterId.c_str(), work.node.c_str(), item_list(given).c_str());
        } else {
            ++m_counts.storeFailures;
            log::write("world: gathering: the store did not write the gather of %s by character %s: no item, no experience, no depletion",
                       work.node.c_str(), characterId.c_str());
        }
        if (kept.empty()) {
            m_kept.erase(characterId);
        }
        fail_work(character);
        m_world.interactions().end_use(character, InteractionEnd::policyFailed);
        return false;
    }
    // Only the kept roll of this node goes (finding GA-R7: the rolls of the other nodes stay).
    kept.erase(std::remove_if(kept.begin(), kept.end(), [&](const KeptRoll& entry) { return entry.node == work.node; }), kept.end());
    if (kept.empty()) {
        m_kept.erase(characterId);
    }
    rests.erase(std::remove_if(rests.begin(), rests.end(), [&](const Rest& rest) { return rest.node == work.node; }), rests.end());
    m_work.erase(characterId);
    m_world.quests().drops_done(character, drops);
    if (target->kind == NodeKind::encounter) {
        // Stage QG5c2: the start object of an encounter stage: its quest events (the game event of its row, its tag), then the encounter
        // system removes it in its next tick (its stage counts it: SliceDestroyedObjective). No LootTracker entry: the object goes. Stage
        // QG5c5: also a start object with an ordinary row (the pylons and the cores of the Darkness: the items of its loot table and the
        // trade skill experience of the row, as a node; then the encounter counts it).
        if (!row.quest) {
            m_claimedObjects.insert(target->group); // Finding QG5C5R-R5: the reward of the object is given (the rows are written).
        }
        ++m_counts.completions;
        m_counts.itemsGiven += given.size();
        set_gather(character, gathering_rules::kStatusDone, 0.0F, work.parts.speed, false, kNoTarget);
        log::write("world: gathering: character %s used the start object %s of an encounter (%s): %s", characterId.c_str(),
                   target->key.c_str(), row.id.c_str(), item_list(given).c_str());
        quest_events(character, row, items, target->interactTag, context.bound->tag);
        m_world.encounters().object_gathered(target->group, character);
        return true;
    }
    if (row.quest) {
        // Stage 1.8c: a quest object stays for the other characters (InstancedLootType 4): no depletion, no node state. Stage QG5a: it goes
        // for this character (the per-character state of the quest objects of stage 1.8e: its LootTracker entry Looted) until a uniform
        // draw of the respawn bounds of its row.
        ++m_counts.completions;
        m_counts.itemsGiven += given.size();
        set_gather(character, gathering_rules::kStatusDone, 0.0F, work.parts.speed, false, kNoTarget);
        QuestGatherState gathered;
        if (context.object != nullptr) {
            gathered.actor = context.object->actor;
        }
        gathered.position = target->position;
        gathered.generation = ++m_questGeneration;
        character.questGathers[target->key] = gathered;
        publish_quest_objects(character);
        const std::string objectKey = target->key;
        const std::uint64_t generation = gathered.generation;
        m_world.after(wait, [characterId, objectKey, generation](World& world) { world.gathering().quest_object_due(characterId, objectKey, generation); });
        log::write("world: gathering: character %s gathered the quest object %s (%s): %s; it is back for the character in %lld s, and it stays "
                   "for the other characters",
                   characterId.c_str(), target->key.c_str(), row.id.c_str(), item_list(given).c_str(),
                   static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(wait).count()));
        quest_events(character, row, items, target->interactTag, context.bound->tag);
        return true;
    }
    GatherNode& stored = m_nodes[next.key];
    stored = next;
    if (stored.kind == NodeKind::body) {
        // A skinned body stays Depleted until the creature system removes it (its respawn time is not the respawn of a body: research
        // Q1). Its entry stays while the body is there: the due list looks at the body again after kBodyCheck (code review of stage
        // 3.6a, finding GA-R1: a time is no proof of the removal).
        stored.respawn = now + gathering_rules::kBodyCheck;
    }
    m_due.emplace(stored.respawn, Due{stored.key, stored.generation});
    publish(stored);
    ++m_counts.completions;
    ++m_counts.depletions;
    m_counts.itemsGiven += given.size();
    m_counts.experience += event != nullptr ? event->amount : 0;
    set_gather(character, gathering_rules::kStatusDone, 0.0F, work.parts.speed, false, kNoTarget);
    const std::string after = stored.kind == NodeKind::body
                                  ? std::string("the body stays depleted until the creature system removes it")
                                  : "the node respawns in " + std::to_string(std::lround(std::chrono::duration<double>(wait).count())) + " s";
    const std::string experience = event != nullptr && event->amount > 0
                                       ? std::to_string(event->amount) + " " + event->progression + " experience"
                                       : std::string("no experience");
    log::write("world: gathering: character %s gathered the %s node %s (%s): %s, %s; %s (generation %llu)", characterId.c_str(),
               node_kind_text(stored.kind), stored.key.c_str(), row.id.c_str(), item_list(given).c_str(), experience.c_str(), after.c_str(),
               static_cast<unsigned long long>(stored.generation));
    quest_events(character, row, items, target->interactTag, context.bound->tag);
    return true;
}

void Gathering::quest_events(Entity& character, const gamedata::GatherableRow& row, const std::vector<GrantItem>& items,
                             const std::string& interactTag, const std::string& boundTag) {
    // Stage 1.8c (finding G12 and G14 of research quest-engine-3.md): the gather of the quests after the commit. TaskRequireGatherItems counts
    // the items of the roll (not the quest drops: 0x825FED0); the game event of the row (GameEventID: MSQ_Treasure_A_interact) applies
    // (TaskGameEvent); the tag of a quest object is an interaction that completed (TaskInteract).
    if (!items.empty()) {
        QuestEvent gathered;
        gathered.kind = QuestEventKind::gather;
        for (const GrantItem& item : items) {
            gathered.keys.push_back(item.itemId);
            gathered.amounts.push_back(item.quantity);
        }
        m_world.quests().event(character, gathered);
    }
    if (!row.event.empty()) {
        QuestEvent applied;
        applied.kind = QuestEventKind::gameEvent;
        applied.keys = {row.event};
        m_world.quests().event(character, applied);
    }
    // The completed interaction of the gather: one event with the tag of the quest object and the tag of the bound, each tag one time (code
    // review of stage 1.8c, finding QR8C-R13: the base gives no second completion of a gather; finding QR8C-R8: only after the success).
    QuestEvent interacted;
    interacted.kind = QuestEventKind::interact;
    for (const std::string& tag : {interactTag, boundTag}) {
        if (!tag.empty() && std::none_of(interacted.keys.begin(), interacted.keys.end(),
                                         [&](const std::string& other) { return data_rules::same_text(other, tag); })) {
            interacted.keys.push_back(tag);
        }
    }
    if (!interacted.keys.empty()) {
        m_world.quests().event(character, interacted);
    }
}

void Gathering::quest_object_due(const std::string& characterId, const std::string& key, std::uint64_t generation) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return; // The character left: its gathered objects went with its entity.
    }
    const auto found = character->questGathers.find(key);
    if (found == character->questGathers.end() || found->second.generation != generation) {
        return;
    }
    character->questGathers.erase(found);
    ++m_counts.questObjectRespawns;
    publish_quest_objects(*character);
}

void Gathering::publish_quest_objects(Entity& character) {
    std::vector<LootTrackerEntry> entries;
    for (const auto& [key, gathered] : character.questGathers) {
        LootTrackerEntry entry;
        entry.actor = gathered.actor;
        entry.state = quest_object_rules::kLooted;
        entry.kind = quest_object_rules::kEntryKind;
        entry.position = gathered.position;
        entries.push_back(entry);
    }
    if (entries != character.questGatherTracker) {
        character.questGatherTracker = std::move(entries);
        m_world.touch(character.id, ComponentKey::lootTracker);
    }
}

std::chrono::milliseconds Gathering::respawn_time(const gamedata::GatherableRow& row) {
    // One uniform draw between the bounds of the row at the depletion (decision of the orchestrator), with the generator of the world.
    float seconds = row.respawnMin;
    if (row.respawnMax > row.respawnMin) {
        seconds += m_world.random_unit() * (row.respawnMax - row.respawnMin);
    }
    return std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(seconds, 1.0F) * 1000.0F)));
}

void Gathering::encounter_object_made(Entity& object, const std::string& slice) {
    const std::optional<gamedata::GatheringLink> link = data() != nullptr ? m_data->encounter_object(slice) : std::nullopt;
    if (!link || object.kind != EntityKind::object || link->templateIndex >= m_data->templates().size()) {
        return;
    }
    object.object.nodeSlot = m_data->templates()[link->templateIndex].controllerSlot;
    object.object.nodeState = gathering_rules::kHealthy;
    m_world.touch(object.id, ComponentKey::gatherable);
}

void Gathering::publish(GatherNode& node) {
    if (data() == nullptr) {
        return;
    }
    const gamedata::GatheringTemplate& slice = m_data->templates()[node.templateIndex];
    const std::uint8_t state = node.depleted ? gathering_rules::kDepleted : gathering_rules::kHealthy;
    Entity* entity = nullptr;
    switch (node.kind) {
    case NodeKind::capital:
        entity = m_world.interactions().level_entity(node.objectIndex);
        break;
    case NodeKind::natural: {
        const std::optional<gamedata::NaturalNode> natural = m_data->natural(node.group);
        const std::optional<ExternalObject> object = natural ? natural_object(*natural) : std::nullopt;
        entity = object ? m_world.interactions().external_entity(*object) : nullptr;
        break;
    }
    case NodeKind::body:
        entity = m_world.find(node.group);
        if (entity != nullptr && entity->kind == EntityKind::creature) {
            entity->creature.skinSlot = slice.controllerSlot;
            entity->creature.skinState = state;
            m_world.touch(entity->id, ComponentKey::gatherable);
        }
        return;
    case NodeKind::encounter:
        return; // Stage QG5c2: a start object has no node entry (encounter_object_made gives its state).
    }
    if (entity == nullptr || entity->kind != EntityKind::object) {
        return;
    }
    entity->object.nodeSlot = slice.controllerSlot;
    entity->object.nodeState = state;
    m_world.touch(entity->id, ComponentKey::gatherable);
}

void Gathering::creature_died(Entity& creature) {
    if (data() == nullptr || creature.kind != EntityKind::creature || creature.creature.body == nullptr) {
        return;
    }
    const std::optional<gamedata::GatheringLink> link = m_data->creature(creature.creature.body->asset, creature.creature.body->subId);
    if (!link) {
        return;
    }
    // The body can be skinned (the GatherableController of its slice: Healthy). A new body is a new node (the respawn of the creature
    // system makes a new entity; the skinning has no respawn of its own: research Q1, "Skinning is a second depletion lifecycle").
    creature.creature.skinSlot = m_data->templates()[link->templateIndex].controllerSlot;
    creature.creature.skinState = gathering_rules::kHealthy;
    m_world.touch(creature.id, ComponentKey::gatherable);
}

void Gathering::left(Entity& character) {
    m_work.erase(character.characterId);
    m_pending.erase(character.characterId);
    m_rests.erase(character.characterId);
    m_kept.erase(character.characterId);
}

bool Gathering::roll_current(const KeptRoll& roll) const {
    const auto found = m_nodes.find(roll.node);
    const std::uint64_t generation = found != m_nodes.end() ? found->second.generation : 0;
    return generation == roll.generation && (found == m_nodes.end() || !found->second.depleted);
}

void Gathering::load_stored() {
    m_loaded = true;
    if (data() == nullptr) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const std::uint64_t run = m_world.world_time().run_ns();
    const gamedata::InteractionData* catalog = m_world.interactions().data();
    for (const state::GatheringNodeRow& row : m_world.store().list_gathering_nodes()) {
        GatherNode node;
        node.key = row.key;
        bool found = false;
        // Only a lookup in data that the backend can read proves a stale row (code review of stage 3.6a, finding GA-R2). A row whose node
        // the backend cannot check now (no interaction data for a capital node; a region file that is not there, not complete, or not the
        // file of the data) stays in the store with its respawn on the run clock; no node of it can be gathered in this run.
        std::string unchecked;
        if (row.source == kCapitalSource && catalog == nullptr) {
            unchecked = "the backend has no interaction data";
        } else if (row.source == kCapitalSource) {
            const std::optional<std::uint32_t> index = catalog->index_of(row.key);
            const std::optional<gamedata::GatheringLink> link = m_data->capital(row.key);
            if (index && link) {
                node.kind = NodeKind::capital;
                node.objectIndex = *index;
                node.row = link->row;
                node.templateIndex = link->templateIndex;
                node.group = catalog->objects()[*index].group;
                node.position = catalog->objects()[*index].position;
                found = true;
            }
        } else if (row.source.rfind(kDistributionSource, 0) == 0) {
            const std::size_t hash = row.key.find('#');
            const std::optional<std::uint32_t> region = hash != std::string::npos ? m_data->region_index(row.key.substr(0, hash)) : std::nullopt;
            // The same version of the distribution file: a different file has different records (a stored node of an older game copy is
            // not the same node).
            if (region && row.source == std::string(kDistributionSource) + m_data->regions()[*region].version
                && !m_data->region_valid(*region)) {
                unchecked = "the file of the region " + m_data->regions()[*region].name + " is not there, not complete, or not the file of the data";
            } else if (region && row.source == std::string(kDistributionSource) + m_data->regions()[*region].version) {
                std::uint32_t record = 0xFFFFFFFFU;
                try {
                    record = static_cast<std::uint32_t>(std::stoul(row.key.substr(hash + 1)));
                } catch (const std::exception&) {
                    record = 0xFFFFFFFFU; // Not a record number: no node.
                }
                if (const std::optional<gamedata::NaturalNode> natural = m_data->natural_at(*region, record)) {
                    node.kind = NodeKind::natural;
                    node.row = natural->row;
                    node.templateIndex = natural->templateIndex;
                    node.group = natural->group;
                    node.position = natural->position;
                    found = true;
                }
            }
        }
        if (!unchecked.empty()) {
            ++m_counts.keptRows;
            log::write("world: gathering: the stored node %s (%s, %s) cannot be checked now (%s): its row stays", row.key.c_str(),
                       row.source.c_str(), row.row.c_str(), unchecked.c_str());
            continue;
        }
        if (!found || m_data->rows()[node.row].id != row.row) {
            ++m_counts.staleRows;
            log::write("world: gathering: the stored node %s (%s, %s) is not a node of the gathering data now: the row goes", row.key.c_str(),
                       row.source.c_str(), row.row.c_str());
            (void)m_world.store().delete_gathering_node(row.key);
            continue;
        }
        node.source = row.source;
        node.depleted = true;
        node.generation = row.generation;
        node.respawnRunNs = row.respawnRunNs;
        node.depletedAt = row.depletedAt;
        // The remaining time on the run clock (the downtime of the backend does not count: decision of the orchestrator).
        const std::uint64_t left = row.respawnRunNs > run ? row.respawnRunNs - run : 0;
        node.respawn = now + std::chrono::duration_cast<WorldClock::time_point::duration>(std::chrono::nanoseconds(left));
        GatherNode& stored = m_nodes[node.key];
        stored = node;
        m_due.emplace(stored.respawn, Due{stored.key, stored.generation});
        publish(stored);
        ++m_counts.storedNodes;
    }
    if (m_counts.storedNodes != 0 || m_counts.staleRows != 0 || m_counts.keptRows != 0) {
        log::write("world: gathering: %llu depleted node(s) from the store (%llu row(s) of an older source removed, %llu row(s) that the "
                   "backend cannot check now kept)",
                   static_cast<unsigned long long>(m_counts.storedNodes), static_cast<unsigned long long>(m_counts.staleRows),
                   static_cast<unsigned long long>(m_counts.keptRows));
    }
}

void Gathering::body_removed(EntityId creature) {
    m_nodes.erase("body/" + hex64(creature));
}

void Gathering::object_removed(EntityId object) {
    m_claimedObjects.erase(object);
}

void Gathering::respawn(GatherNode& node) {
    if (node.kind == NodeKind::body) {
        // The entry of a skinned body (no respawn of a body by this system). The body is still there: the entry stays, and the due list
        // looks again later (finding GA-R1). The body is gone: the entry goes.
        const Entity* creature = m_world.find(node.group);
        if (creature != nullptr && creature->kind == EntityKind::creature && body_generation(*creature) == node.bodyMade) {
            node.respawn = m_world.clock().now() + gathering_rules::kBodyCheck;
            m_due.emplace(node.respawn, Due{node.key, node.generation});
            return;
        }
        const std::string key = node.key;
        m_nodes.erase(key);
        return;
    }
    if (!m_world.store().delete_gathering_node(node.key)) {
        ++m_counts.storeFailures;
        m_due.emplace(m_world.clock().now() + gathering_rules::kRespawnRetry, Due{node.key, node.generation});
        return;
    }
    node.depleted = false;
    ++m_counts.respawns;
    publish(node);
    log::write("world: gathering: the %s node %s respawns (generation %llu)", node_kind_text(node.kind), node.key.c_str(),
               static_cast<unsigned long long>(node.generation));
}

void Gathering::tick() {
    if (!m_loaded) {
        load_stored();
    }
    // A work that begin prepared and the base did not start (the base calls started() in the same request): it goes.
    m_pending.clear();
    // A work whose use the base ended with no call of the handler (the source went: a body that left the world): the work ends as a
    // cancel (the state Failure). The cost is the number of works (the characters that gather now).
    for (auto work = m_work.begin(); work != m_work.end();) {
        Entity* character = m_world.find_character(work->first);
        if (character != nullptr && character->interaction) {
            ++work;
            continue;
        }
        const std::string key = work->second.node;
        const std::string characterId = work->first;
        work = m_work.erase(work);
        ++m_counts.cancels;
        if (character != nullptr) {
            const std::string mark = log::session_mark(character->owner, character->characterId);
            const log::Context logContext(mark);
            log::write("world: gathering: the use of %s by character %s ended with no handler call (the node went): no reward", key.c_str(),
                       characterId.c_str());
            set_gather(*character, gathering_rules::kStatusFailure, 0.0F, gathering_rules::kNoToolSpeed, false, kNoTarget);
        }
    }
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const Due due = m_due.begin()->second;
        m_due.erase(m_due.begin());
        ++m_counts.dueLooks;
        const auto found = m_nodes.find(due.key);
        if (found == m_nodes.end() || !found->second.depleted || found->second.generation != due.generation || found->second.respawn > now) {
            continue; // A node that a command changed (a new respawn time).
        }
        respawn(found->second);
    }
}

NodeError Gathering::set_respawn(const std::string& key, std::chrono::seconds seconds) {
    if (data() == nullptr) {
        return NodeError::noData;
    }
    if (seconds.count() < 1 || seconds > gathering_rules::kLongestOverride) {
        return NodeError::badTime;
    }
    auto found = m_nodes.find(key);
    if (found == m_nodes.end() && key.size() == 16) {
        // A G in 16 hexadecimal digits: the node of that G.
        for (auto entry = m_nodes.begin(); entry != m_nodes.end(); ++entry) {
            if (hex64(entry->second.group) == key) {
                found = entry;
                break;
            }
        }
    }
    if (found == m_nodes.end()) {
        return NodeError::unknownNode;
    }
    GatherNode& node = found->second;
    if (!node.depleted) {
        return NodeError::notDepleted;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const std::uint64_t run = m_world.world_time().run_ns() + static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(seconds).count());
    if (node.kind != NodeKind::body) {
        state::GatheringNodeRow row;
        row.key = node.key;
        row.source = node.source;
        row.row = m_data->rows()[node.row].id;
        row.generation = node.generation;
        row.respawnRunNs = run;
        row.depletedAt = node.depletedAt;
        if (!m_world.store().write_gathering_node(row)) {
            return NodeError::storeFailed;
        }
    }
    node.respawn = now + seconds;
    node.respawnRunNs = run;
    m_due.emplace(node.respawn, Due{node.key, node.generation});
    log::write("world: gathering: DEVELOPMENT COMMAND: the node %s respawns in %lld s (not the rule of the data)", node.key.c_str(),
               static_cast<long long>(seconds.count()));
    return NodeError::none;
}

GatheringView Gathering::view(const std::string& characterId, float radius) {
    GatheringView view;
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return view;
    }
    view.inWorld = true;
    if (const auto found = m_work.find(characterId); found != m_work.end()) {
        view.work = found->second;
    }
    const WorldClock::time_point now = m_world.clock().now();
    if (data() != nullptr && std::isfinite(radius) && radius >= 0.0F) {
        // The nodes with a state of the server: a command (the development list), not a rule of the world.
        for (const auto& [key, node] : m_nodes) {
            const float distance = std::hypot(node.position.x - character->position.x, node.position.y - character->position.y);
            if (distance > radius) {
                continue;
            }
            NodeView entry;
            entry.key = key;
            entry.kind = node.kind;
            entry.row = m_data->rows()[node.row].id;
            entry.depleted = node.depleted;
            entry.respawnSeconds = node.depleted ? std::chrono::duration<double>(node.respawn - now).count() : 0.0;
            entry.distance = distance;
            entry.generation = node.generation;
            view.nodes.push_back(std::move(entry));
        }
        std::sort(view.nodes.begin(), view.nodes.end(), [](const NodeView& a, const NodeView& b) { return a.distance < b.distance; });
        for (const char* skill : {"Mining", "Logging", "Harvesting", "Skinning"}) {
            gamedata::GatherableRow probe;
            probe.tradeskill = skill;
            probe.type = std::string(skill) == "Mining"    ? gamedata::GatheringType::mining
                         : std::string(skill) == "Logging" ? gamedata::GatheringType::chopping
                         : std::string(skill) == "Skinning" ? gamedata::GatheringType::dressing
                                                            : gamedata::GatheringType::cutting;
            view.skills[skill] = parts_of(*character, probe);
        }
    }
    return view;
}

} // namespace soulwarden::backend::world
