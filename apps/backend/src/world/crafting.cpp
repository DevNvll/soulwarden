#include "world/crafting.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/logging/log.h"
#include "gamedata/item_catalog.h"
#include "gamedata/progression_tables.h"
#include "state/store.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The options of a crafting station and of a camp (stage 2.5b; uidatabase.uidb: action 20, committed). */
constexpr const char* kStationOptions[] = {"OpenCraftingOption", "OpenCraftingOptionLevel60"};

/**
 * The handler of the station options (stage 3.7a): the handler of the base (stage 2.5b: an object with an Assembly) for the use, and the
 * end of the batch of the character at the end of the use.
 */
class StationUseHandler final : public InteractionHandler {
public:
    StationUseHandler(Crafting& crafting, InteractionHandler* base) : m_crafting(crafting), m_base(base) {}

    [[nodiscard]] const char* name() const override {
        return "crafting station";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        if (m_base != nullptr) {
            return m_base->begin(character, context);
        }
        return context.object != nullptr && context.object->assembly ? InteractionVerdict::accepted : InteractionVerdict::refused;
    }
    void started(Entity& character, const InteractionContext& context) override {
        if (m_base != nullptr) {
            m_base->started(character, context);
        }
    }
    void end(Entity& character, const InteractionContext& context, InteractionEnd cause) override {
        if (m_base != nullptr) {
            m_base->end(character, context, cause);
        }
        m_crafting.use_ended(character, cause);
    }

private:
    Crafting& m_crafting;
    InteractionHandler* m_base;
};

/** The handlers of the four (kind, option) pairs, each with the handler of the base that it replaces. */
class StationHandlers final : public InteractionHandler {
public:
    explicit StationHandlers(Crafting& crafting) : m_crafting(crafting) {}
    [[nodiscard]] const char* name() const override {
        return "crafting stations";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        (void)character;
        (void)context;
        return InteractionVerdict::refused;
    }
    InteractionHandler& add(InteractionHandler* base) {
        m_list.push_back(std::make_unique<StationUseHandler>(m_crafting, base));
        return *m_list.back();
    }

private:
    Crafting& m_crafting;
    std::vector<std::unique_ptr<StationUseHandler>> m_list;
};

} // namespace

const char* craft_verdict_text(CraftVerdict verdict) {
    switch (verdict) {
    case CraftVerdict::accepted:
        return "accepted";
    case CraftVerdict::notInWorld:
        return "the session has no character";
    case CraftVerdict::dead:
        return "the character is dead";
    case CraftVerdict::noData:
        return "no crafting data or no item catalog";
    case CraftVerdict::noStation:
        return "the character uses no crafting station and no camp";
    case CraftVerdict::unknownStation:
        return "the Assembly key of the station has no row of the station database";
    case CraftVerdict::busy:
        return "a craft batch of the character runs";
    case CraftVerdict::unknownRecipe:
        return "the recipe is not a recipe of the data";
    case CraftVerdict::unsupported:
        return "the recipe has a rule that the backend does not have yet";
    case CraftVerdict::wrongStation:
        return "the station row permits no station key of the recipe";
    case CraftVerdict::rank:
        return "the trade skill rank is below the level of the recipe";
    case CraftVerdict::expansion:
        return "the recipe needs an expansion";
    case CraftVerdict::badQuantity:
        return "the count is 0 or above 10000";
    case CraftVerdict::perkItems:
        return "the request has perk items or an upgrade level";
    case CraftVerdict::badIngredients:
        return "the entries do not name the ingredients of the recipe";
    case CraftVerdict::missingMaterials:
        return "the bag and the quick slots do not have the materials";
    case CraftVerdict::bagFull:
        return "the bag has no place for the whole output";
    case CraftVerdict::notEnoughCoin:
        return "the character has less coin than the fee";
    case CraftVerdict::planFailed:
        return "the plan of the items failed";
    case CraftVerdict::cooldown:
        return "the count is above the crafts that are left in the cooldown window of the recipe";
    }
    return "?";
}

const char* craft_end_text(CraftEnd end) {
    switch (end) {
    case CraftEnd::completed:
        return "completed";
    case CraftEnd::clientStop:
        return "the client stopped it (2999)";
    case CraftEnd::useEnded:
        return "the station use ended";
    case CraftEnd::left:
        return "the character left the world";
    case CraftEnd::command:
        return "a command stopped it";
    case CraftEnd::jobRefused:
        return "the checks or the plan of the next job failed";
    case CraftEnd::storeFailed:
        return "the database did not take the rows of the next job";
    }
    return "?";
}

Crafting::Crafting(World& world, const gamedata::CraftingData* data) : m_world(world), m_data(data) {
    // The handler of the station options of both kinds: it keeps the rule of the base for the use (stage 2.5b) and stops the batch of the
    // character at the end of the use.
    auto handlers = std::make_unique<StationHandlers>(*this);
    for (const gamedata::InteractionKind kind : {gamedata::InteractionKind::craftingStation, gamedata::InteractionKind::camp}) {
        for (const char* name : kStationOptions) {
            const std::uint32_t option = gamedata::interaction_option_id(name);
            InteractionHandler* base = world.interactions().handler(kind, option);
            world.interactions().register_handler(kind, option, handlers->add(base));
        }
    }
    m_handler = std::move(handlers);
}

Crafting::~Crafting() = default;

std::uint64_t Crafting::fee_of(const gamedata::Recipe& recipe, bool chargeFee, std::uint64_t count) {
    // Client 0x2E513A0: no fee when the Assembly of the station does not charge one (m_chargeCraftingFee); else the count times CraftingFee
    // times the fee factor of the territory, to an integer. The backend has no territory: the factor is 1 (a decision of an agent). The tax
    // path 0x2E57580 (UseCraftingTax): round(max(1, fee x (1 - the station, territory, and company taxes))); the backend has no tax: the
    // fee is at least 1 (live check L3: the client sent the fee 1 for the counts 1 and 3 of a recipe with CraftingFee 0).
    if (!chargeFee || count == 0) {
        return 0;
    }
    const std::uint64_t base = count * recipe.fee;
    return recipe.tax ? std::max<std::uint64_t>(1, base) : base;
}

CraftVerdict Crafting::check(Entity& character, const gamedata::Recipe*& recipe, const CraftRequest* request, std::uint32_t quantity,
                             std::optional<StationContext>& station, const gamedata::CraftingStation*& row, std::vector<CraftCost>& costs,
                             CraftOutput& output, std::vector<std::string>& selected, std::string& detail, bool needStation) const {
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    if (character.life.dead) {
        return CraftVerdict::dead;
    }
    if (data() == nullptr || catalog == nullptr || !catalog->loaded()) {
        return CraftVerdict::noData;
    }
    // The station of the committed use (the interaction base; never a value of the request).
    station = m_world.interactions().station(character);
    if (station) {
        row = m_data->station(station->assemblyKey);
        if (row == nullptr) {
            detail = station->assemblyKey;
            return CraftVerdict::unknownStation;
        }
    } else if (needStation) {
        return CraftVerdict::noStation;
    }
    if (recipe == nullptr) {
        return CraftVerdict::unknownRecipe;
    }
    if (!recipe->unsupported.empty()) {
        detail = recipe->unsupported.front();
        return CraftVerdict::unsupported;
    }
    if (row != nullptr && !gamedata::CraftingData::permits(*row, *recipe)) {
        detail = row->name;
        return CraftVerdict::wrongStation;
    }
    if (!recipe->skill.empty() && recipe->level > 0) {
        const std::uint32_t rank = character.skills.rank(gamedata::progression_id(recipe->skill));
        if (rank < recipe->level) {
            detail = recipe->skill + " " + std::to_string(rank) + " of " + std::to_string(recipe->level);
            return CraftVerdict::rank;
        }
    }
    if (!recipe->expansion.empty()) {
        detail = recipe->expansion;
        return CraftVerdict::expansion;
    }
    if (quantity == 0 || quantity > crafting_rules::kMaxQuantity) {
        return CraftVerdict::badQuantity;
    }
    if (request != nullptr && (!request->perkItems.empty() || request->upgrade != 0)) {
        return CraftVerdict::perkItems;
    }
    // Stage 3.7b: the crafts that are left in the cooldown window of the recipe (live check L6: the client limits its count to them).
    if (recipe->cooldownQuantity > 0) {
        const std::uint32_t left = cooldown_left(character, *recipe);
        if (quantity > left) {
            detail = std::to_string(quantity) + " of " + std::to_string(left) + " left";
            return CraftVerdict::cooldown;
        }
    }
    // Stage 3.7b: the trade skill experience of the event needs its progression row (the progression tables); else no craft of the recipe.
    if (!recipe->event.empty()) {
        const gamedata::CraftEvent* event = m_data->event(recipe->event);
        const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
        if (event != nullptr && !event->progression.empty() && event->reward > 0.0F
            && (tables == nullptr || !tables->loaded() || tables->progression(event->progression) == nullptr)) {
            detail = "no progression row of " + event->progression;
            return CraftVerdict::noData;
        }
    }
    // The costs: one entry for each ingredient of the recipe, in its order, of the type Item (the client converts each selection to such
    // an entry: 0x5BA9FB0). An item ingredient needs its item, a category ingredient a member of the category. The quantity of an entry is
    // not the cost (the client sends 1): the cost is the quantity of the recipe. A command takes the first member of a category.
    if (request != nullptr && request->ingredients.size() != recipe->ingredients.size()) {
        detail = std::to_string(request->ingredients.size()) + " entries for " + std::to_string(recipe->ingredients.size()) + " ingredients";
        return CraftVerdict::badIngredients;
    }
    for (std::size_t index = 0; index < recipe->ingredients.size(); ++index) {
        const gamedata::RecipeIngredient& ingredient = recipe->ingredients[index];
        std::string item;
        if (ingredient.type == gamedata::IngredientType::item) {
            if (request != nullptr
                && (request->ingredients[index].type != static_cast<std::uint8_t>(gamedata::IngredientType::item)
                    || request->ingredients[index].item != gamedata::item_id_crc(ingredient.id))) {
                detail = "entry " + std::to_string(index + 1) + " is not " + ingredient.id;
                return CraftVerdict::badIngredients;
            }
            item = ingredient.id;
        } else {
            if (request != nullptr) {
                if (request->ingredients[index].type != static_cast<std::uint8_t>(gamedata::IngredientType::item)) {
                    detail = "entry " + std::to_string(index + 1) + " is not an item";
                    return CraftVerdict::badIngredients;
                }
                item = m_data->member(ingredient.id, request->ingredients[index].item);
            } else {
                item = m_data->first_member(ingredient.id);
            }
            if (item.empty()) {
                detail = "entry " + std::to_string(index + 1) + " is not a member of the category " + ingredient.id;
                return CraftVerdict::badIngredients;
            }
        }
        const gamedata::ItemDefinition* definition = catalog->find(item);
        if (definition == nullptr) {
            detail = "the item catalog has no " + item;
            return CraftVerdict::planFailed;
        }
        selected.push_back(definition->id);
        // Two ingredients of one item are one cost: one stack pays a unit once (research crafting-stage.md, Q2).
        const auto same = std::find_if(costs.begin(), costs.end(), [&](const CraftCost& cost) { return cost.itemId == definition->id; });
        if (same != costs.end()) {
            same->quantity += ingredient.quantity;
        } else {
            costs.push_back(CraftCost{definition->id, ingredient.quantity});
        }
    }
    const gamedata::ItemDefinition* result = catalog->find(recipe->item);
    if (result == nullptr) {
        detail = "the item catalog has no " + recipe->item;
        return CraftVerdict::planFailed;
    }
    // Code review of stage U4, finding U4-R1: an output with a gear score range and no override needs the craft gear score rule of stage
    // 3.7c (the tool gives such a recipe a cause; the loader compares the file with the catalog). A file and a catalog that do not agree
    // give no plain item of the lowest gear score: the recipe has a rule that the backend does not have.
    if (result->maxGearScore != 0 && result->gearScoreOverride == 0) {
        detail = "the output " + result->id + " has a gear score range and no override (stage 3.7c)";
        return CraftVerdict::unsupported;
    }
    output = CraftOutput{result->id, recipe->outputQty};
    return CraftVerdict::accepted;
}

CraftYield Crafting::yield_of(const Entity& character, const gamedata::Recipe& recipe, const std::vector<std::string>& selected) const {
    CraftYield yield;
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    if (data() == nullptr || catalog == nullptr || selected.size() != recipe.ingredients.size() || recipe.ingredients.empty()) {
        return yield;
    }
    const auto tier_of = [&](const std::string& item) {
        const gamedata::ItemDefinition* definition = catalog->find(item);
        return definition != nullptr ? static_cast<std::int64_t>(definition->tier) : 0;
    };
    const auto is_category = [](const gamedata::RecipeIngredient& ingredient) {
        return ingredient.type == gamedata::IngredientType::categoryAny || ingredient.type == gamedata::IngredientType::categoryOnly;
    };
    // The chance of the recipe and of the materials (client 0x5C52A40). The base tier: the item of the first ingredient (an item ingredient
    // also adds its ExtraBonusItemChance), or the selected member of a first category ingredient.
    double chance = recipe.bonusChance;
    const std::int64_t base = tier_of(selected.front());
    if (!is_category(recipe.ingredients.front())) {
        chance += static_cast<double>(m_data->material(selected.front()).extraChance);
    }
    for (std::size_t index = 1; index < recipe.ingredients.size(); ++index) {
        if (!is_category(recipe.ingredients[index])) {
            continue; // A later item ingredient adds nothing.
        }
        const std::int64_t difference = tier_of(selected[index]) - base;
        double part = 0.0;
        if (difference > 0 && !recipe.bonusIncrease.empty()) {
            part = recipe.bonusIncrease[std::min<std::size_t>(static_cast<std::size_t>(difference), recipe.bonusIncrease.size()) - 1];
        } else if (difference < 0 && !recipe.bonusIncrease.empty() && !recipe.bonusDecrease.empty()) {
            // The client tests the increase list for a lower tier too (0x5C52A6B..0x5C52C23); an empty decrease list is 0 (agent decision:
            // the client would read before the list; no recipe of the supported build has an empty decrease list with an increase list).
            part = recipe.bonusDecrease[std::min<std::size_t>(static_cast<std::size_t>(-difference), recipe.bonusDecrease.size()) - 1];
        }
        chance += part + static_cast<double>(m_data->material(selected[index]).extraChance);
    }
    yield.materialChance = chance;
    // The roll bonus points (client 0x2E57A70): IngredientBonusPrimary of the first entry and IngredientBonusSecondary of each other one; with
    // no DisallowBonusesToGS the RollBonus of the rank of the trade skill and its ROL effects (the statuses and the gear of the character).
    for (std::size_t index = 0; index < selected.size(); ++index) {
        const gamedata::MaterialBonus bonus = m_data->material(selected[index]);
        yield.materialPoints += index == 0 ? bonus.primary : bonus.secondary;
    }
    if (!recipe.skill.empty() && !recipe.disallowBonuses) {
        const std::uint32_t rank = character.skills.rank(gamedata::progression_id(recipe.skill));
        yield.rankPoints = static_cast<std::int32_t>(std::lround(m_data->roll_bonus(recipe.skill, rank).value_or(0.0F)));
        yield.effectPoints = static_cast<std::int32_t>(std::lround(m_world.gathering().effect_sum(character, "ROL" + recipe.skill)));
    }
    const std::int64_t points = static_cast<std::int64_t>(yield.materialPoints) + yield.rankPoints + yield.effectPoints;
    // The chance of the client is max(0, chance + points x 0.0001) (it shows it x 100). To 6 decimals: the float sums of the client data
    // (-0.25 + 2500 x 0.0001) give no tiny chance (agent decision).
    yield.chance = std::max(0.0, std::round((chance + static_cast<double>(points) * crafting_rules::kRollBonusScale) * 1.0e6) / 1.0e6);
    // Decision D2 of the orchestrator (option A): the expected extra units of one job are OutputQty times the chance; a chance above 1 gives
    // floor(e) units each job and one more with the chance of the rest.
    yield.extraUnits = static_cast<double>(recipe.outputQty) * yield.chance;
    yield.maxExtra = static_cast<std::uint32_t>(std::min(std::ceil(yield.extraUnits), 1.0e6));
    return yield;
}

CraftExperience Crafting::experience_of(const Entity& character, const gamedata::Recipe& recipe) const {
    CraftExperience experience;
    const gamedata::CraftEvent* event = data() != nullptr && !recipe.event.empty() ? m_data->event(recipe.event) : nullptr;
    if (event == nullptr || event->progression.empty() || event->reward <= 0.0F) {
        return experience;
    }
    // The base of one job (client 0x5C47AC0, craftingscreenv4.luac): the reward of the event times the units of the valid ingredients that
    // are not a currency (the tool skips an entry with no type; a currency is a cause). floor: the client shows math.floor of the total.
    std::uint64_t units = 0;
    for (const gamedata::RecipeIngredient& ingredient : recipe.ingredients) {
        units += ingredient.type != gamedata::IngredientType::currency ? ingredient.quantity : 0;
    }
    experience.progression = event->progression;
    experience.perJob = static_cast<std::uint64_t>(std::floor(static_cast<double>(event->reward) * static_cast<double>(units) + 1.0e-6));
    // The first craft: the bonus of the client (javelin.crafting-first-craft-bonus-exp-multiplier - 1) times the base of one job, one time,
    // while the character does not have the achievement of the recipe (the screen reads its bit: IsAchievementUnlocked; live check L4).
    if (!recipe.firstCraftAchievement.empty()) {
        if (const std::optional<std::uint32_t> bit = m_data->achievement_index(recipe.firstCraftAchievement)) {
            if (!std::binary_search(character.achievements.begin(), character.achievements.end(), *bit)) {
                experience.firstCraft = recipe.firstCraftAchievement;
                experience.firstCraftBit = *bit;
                experience.firstCraftExtra = experience.perJob * (crafting_rules::kFirstCraftMultiplier - 1);
            }
        }
    }
    return experience;
}

std::uint32_t Crafting::cooldown_left(const Entity& character, const gamedata::Recipe& recipe) const {
    if (recipe.cooldownQuantity == 0) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    const auto found = character.craftCooldowns.find(recipe.crc);
    if (found == character.craftCooldowns.end() || m_world.clock().now() >= found->second.end) {
        return recipe.cooldownQuantity; // No window, or its end passed (the client ends it itself: live check L6).
    }
    return found->second.used >= recipe.cooldownQuantity ? 0 : recipe.cooldownQuantity - found->second.used;
}

std::uint32_t Crafting::draw_extra(double extraUnits, const std::function<float()>& draw) {
    if (extraUnits <= 0.0) {
        return 0;
    }
    const double whole = std::floor(extraUnits);
    const double rest = extraUnits - whole;
    std::uint32_t extra = static_cast<std::uint32_t>(whole);
    // One draw for each job with a rest (the one random source of the world): one more unit when the draw is below the rest.
    if (rest > 0.0 && static_cast<double>(draw()) < rest) {
        ++extra;
    }
    return extra;
}

CraftVerdict Crafting::refuse(CraftVerdict verdict, Entity* character, SessionId session, const CraftRequest& request, const std::string& detail) {
    ++m_counts.verdicts[static_cast<std::size_t>(verdict)];
    const gamedata::Recipe* recipe = data() != nullptr ? m_data->recipe(request.recipe) : nullptr;
    log::write("world: craft request of character %s refused: %s%s%s (recipe %s %08x, count %u, fee %u, gear score %u, %zu entr%s, %zu "
               "perk item(s), upgrade %u): no change",
               character != nullptr ? character->characterId.c_str() : "?", craft_verdict_text(verdict), detail.empty() ? "" : ": ",
               detail.c_str(), recipe != nullptr ? recipe->id.c_str() : "?", request.recipe, request.quantity, request.coinFee,
               request.gearScore, request.ingredients.size(), request.ingredients.size() == 1 ? "y" : "ies", request.perkItems.size(),
               request.upgrade);
    // The stop form of 2993 ends the crafting of the client (it sends 2999); a second request while a batch runs gets nothing, because
    // the stop would end the crafting of the batch in the client.
    if (character != nullptr && verdict != CraftVerdict::busy && verdict != CraftVerdict::notInWorld) {
        progress(*character, session, request.target, std::string(), 0, 0);
    }
    return verdict;
}

CraftVerdict Crafting::request(SessionId session, const CraftRequest& request) {
    ++m_counts.requests;
    Entity* character = m_world.entity_of(session);
    if (character == nullptr || character->kind != EntityKind::player) {
        return refuse(CraftVerdict::notInWorld, nullptr, session, request, {});
    }
    const std::string mark = log::session_mark(character->owner, character->characterId);
    const log::Context logContext(mark);
    if (character->craftJob) {
        return refuse(CraftVerdict::busy, character, session, request, character->craftJob->recipe);
    }
    const gamedata::Recipe* recipe = data() != nullptr ? m_data->recipe(request.recipe) : nullptr;
    std::optional<StationContext> station;
    const gamedata::CraftingStation* row = nullptr;
    std::vector<CraftCost> costs;
    CraftOutput output;
    std::vector<std::string> selected;
    std::string detail;
    const CraftVerdict verdict = check(*character, recipe, &request, request.quantity, station, row, costs, output, selected, detail, true);
    if (verdict != CraftVerdict::accepted) {
        return refuse(verdict, character, session, request, detail);
    }
    // Stage 3.7b: the bonus item chance of the request (the selected items, the rank and the effects now: the value that the client shows)
    // and the experience of one job. The plan of the batch reserves the largest output of each job (OutputQty and the ceiling of the extra
    // units of decision D2): no paid output can be lost (agent decision).
    const CraftYield yield = yield_of(*character, *recipe, selected);
    const CraftExperience experience = experience_of(*character, *recipe);
    CraftOutput largest = output;
    largest.quantity = output.quantity + yield.maxExtra;
    // The plan of the whole batch on a copy, and the fee of the batch.
    Inventory::Plan plan = m_world.inventory().plan_craft(character->characterId, costs, largest, request.quantity);
    m_counts.slotVisits += character->bag.slots.size() + (paperdoll::kQuickLast - paperdoll::kQuickFirst + 1);
    if (!plan.ok()) {
        const CraftVerdict refusal = plan.error == ItemError::missingMaterials ? CraftVerdict::missingMaterials
                                     : plan.error == ItemError::bagFull           ? CraftVerdict::bagFull
                                                                                  : CraftVerdict::planFailed;
        return refuse(refusal, character, session, request, item_error_text(plan.error));
    }
    const std::uint64_t fee = fee_of(*recipe, station->chargeCraftingFee, request.quantity);
    if (fee > character->wallet.coin) {
        return refuse(CraftVerdict::notEnoughCoin, character, session, request,
                      std::to_string(fee) + " of " + std::to_string(character->wallet.coin));
    }
    if (fee != request.coinFee) {
        ++m_counts.feeClaimsDiffer;
    }
    CraftJob job;
    job.serial = ++m_serial;
    job.recipe = recipe->id;
    job.recipeCrc = recipe->crc;
    job.total = request.quantity;
    job.session = session;
    job.target = request.target;
    job.station = station->objectKey;
    job.stationGeneration = station->objectGeneration;
    job.stationKey = station->assemblyKey;
    job.camp = station->isCamp;
    job.chargeFee = station->chargeCraftingFee;
    for (const CraftCost& cost : costs) {
        job.costs.emplace_back(cost.itemId, cost.quantity);
    }
    job.output = output.itemId;
    job.outputQty = output.quantity;
    job.accepted = m_world.clock().now();
    job.craftAll = recipe->craftAll;
    job.extraUnits = yield.extraUnits;
    job.maxExtra = yield.maxExtra;
    job.progression = experience.progression;
    job.experience = experience.perJob;
    job.firstCraft = experience.firstCraft;
    job.firstCraftExtra = experience.firstCraftExtra;
    job.cooldownSeconds = recipe->cooldownSeconds;
    job.cooldownQuantity = recipe->cooldownQuantity;
    // Code review of stage 3.7a, finding CR7A-R4: a progress that the session did not send yet is a final progress of a request before
    // this one (the stop form of a refusal in the same tick; a batch that runs refuses a request). The client ends its crafting at such a
    // progress and sends 2999, which would stop this batch: the progress goes away. The first job of this batch sends its own progress.
    character->craftProgress = CraftProgress{};
    character->craftJob = job;
    m_active[character->id] = character->characterId;
    ++m_counts.verdicts[static_cast<std::size_t>(CraftVerdict::accepted)];
    ++m_counts.batches;
    std::string costText;
    for (const CraftCost& cost : costs) {
        costText += (costText.empty() ? "" : ", ") + std::to_string(cost.quantity) + " " + cost.itemId;
    }
    log::write("world: craft request of character %s accepted: %s x%u at %s %s (%s, generation %llu): each job takes %s and gives %u %s; "
               "fee of the batch %llu (claim %u), %zu slot(s) of the bag in the plan of the batch",
               character->characterId.c_str(), recipe->id.c_str(), request.quantity, station->isCamp ? "the camp" : "the station",
               station->assemblyKey.c_str(), station->objectKey.c_str(), static_cast<unsigned long long>(station->objectGeneration),
               costText.c_str(), output.quantity, output.itemId.c_str(), static_cast<unsigned long long>(fee), request.coinFee,
               plan.touched.bag.size());
    if (recipe->refining || recipe->craftAll || experience.perJob > 0 || recipe->cooldownQuantity > 0) {
        log::write("world: craft request of character %s: bonus item chance %.4f (materials %.4f, points %d + rank %d + effects %d), %.4f extra "
                   "unit(s) for each job (decision D2); %llu %s experience for each job%s%s%s",
                   character->characterId.c_str(), yield.chance, yield.materialChance, yield.materialPoints, yield.rankPoints,
                   yield.effectPoints, yield.extraUnits, static_cast<unsigned long long>(experience.perJob),
                   experience.progression.empty() ? "trade skill" : experience.progression.c_str(),
                   experience.firstCraft.empty() ? "" : (", first craft " + experience.firstCraft + " +"
                                                         + std::to_string(experience.firstCraftExtra)).c_str(),
                   recipe->cooldownQuantity > 0 ? ", cooldown " : "",
                   recipe->cooldownQuantity > 0
                       ? (std::to_string(cooldown_left(*character, *recipe)) + " of " + std::to_string(recipe->cooldownQuantity) + " left").c_str()
                       : "");
    }
    const std::string characterId = character->characterId;
    const std::uint64_t serial = job.serial;
    m_world.after(crafting_rules::kCraftDuration, [this, characterId, serial](World&) { run(characterId, serial); });
    return CraftVerdict::accepted;
}

void Crafting::run(const std::string& characterId, std::uint64_t serial) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || !character->craftJob || character->craftJob->serial != serial) {
        return;
    }
    const std::string mark = log::session_mark(character->owner, character->characterId);
    const log::Context logContext(mark);
    for (std::uint32_t step = 0; step < crafting_rules::kJobsPerTick; ++step) {
        ++m_counts.jobVisits;
        CraftJob& job = *character->craftJob;
        // The checks again (the use, its station, the data, the life) and the plan of one job (the bag can have changed).
        const gamedata::Recipe* recipe = data() != nullptr ? m_data->recipe(job.recipeCrc) : nullptr;
        std::optional<StationContext> station = m_world.interactions().station(*character);
        std::string detail;
        if (character->life.dead || recipe == nullptr || !station || station->objectKey != job.station
            || station->objectGeneration != job.stationGeneration) {
            detail = character->life.dead ? "the character is dead" : recipe == nullptr ? "no recipe data" : "the station use changed";
            log::write("world: craft batch of character %s: job %u of %u refused before its cost: %s", characterId.c_str(), job.done + 1,
                       job.total, detail.c_str());
            end(*character, CraftEnd::jobRefused, true);
            return;
        }
        std::vector<CraftCost> costs;
        for (const auto& [item, quantity] : job.costs) {
            costs.push_back(CraftCost{item, quantity});
        }
        // Decision D2: the extra units of this job (floor(e), and one more with the chance of the rest: one draw of the generator of the
        // world). The output of a job before the last job of a craft-all batch has the cause ItemAdded (live check L4).
        const std::uint32_t extra = draw_extra(job.extraUnits, [this] { return m_world.random_unit(); });
        CraftOutput output{job.output, job.outputQty + extra};
        output.action = job.craftAll && job.done + 1 < job.total ? sync::kItemAdded : sync::kItemCreated;
        Inventory::Plan plan = m_world.inventory().plan_craft(characterId, costs, output, 1);
        m_counts.slotVisits += character->bag.slots.size() + (paperdoll::kQuickLast - paperdoll::kQuickFirst + 1);
        const std::uint64_t fee = fee_of(*recipe, job.chargeFee, job.done + 1) - fee_of(*recipe, job.chargeFee, job.done);
        if (!plan.ok() || fee > character->wallet.coin) {
            log::write("world: craft batch of character %s: job %u of %u refused before its cost and its output: %s", characterId.c_str(),
                       job.done + 1, job.total, plan.ok() ? "the character has less coin than the fee" : item_error_text(plan.error));
            end(*character, CraftEnd::jobRefused, true);
            return;
        }
        // The trade skill experience of this job, with the first-craft bonus while the character does not have the achievement (its bit;
        // a first craft of the batch writes the achievement in its transaction: the next jobs have no bonus).
        const bool firstCraft = !job.firstCraft.empty()
                                && !(data()->achievement_index(job.firstCraft)
                                     && std::binary_search(character->achievements.begin(), character->achievements.end(),
                                                           *data()->achievement_index(job.firstCraft)));
        const std::uint64_t experience = job.experience + (firstCraft ? job.firstCraftExtra : 0);
        Skills::Plan skills;
        if (experience > 0) {
            skills = m_world.skills().plan_experience(characterId, job.progression, experience);
            if (!skills.ok()) {
                log::write("world: craft batch of character %s: job %u of %u refused before its cost and its output: the experience of %s: %s",
                           characterId.c_str(), job.done + 1, job.total, job.progression.c_str(), progress_error_text(skills.error));
                end(*character, CraftEnd::jobRefused, true);
                return;
            }
        }
        // The cooldown of the recipe: a new window at the first craft after the end of the last window (game data: CooldownSeconds,
        // CooldownQuantity; live check L6), else one more craft in the window. The end on the run clock (the row) and on the world clock.
        std::optional<CraftCooldown> cooldown;
        const WorldClock::time_point now = m_world.clock().now();
        if (job.cooldownQuantity > 0) {
            const auto found = character->craftCooldowns.find(job.recipeCrc);
            if (found != character->craftCooldowns.end() && now < found->second.end) {
                cooldown = found->second;
                ++cooldown->used;
            } else {
                const auto window = std::chrono::seconds(job.cooldownSeconds);
                cooldown = CraftCooldown{job.recipe, 1,
                                         m_world.world_time().run_ns()
                                             + static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(window).count()),
                                         now + window};
            }
        }
        // One transaction: the rows of the items and the coin of the fee of this job, the experience, the first-craft achievement, the
        // cooldown row. The entity changes only after the commit (each part is a plan on a copy).
        const std::optional<std::uint64_t> coin = fee > 0 ? std::optional<std::uint64_t>(character->wallet.coin - fee) : std::nullopt;
        bool stored = false;
        {
            state::Store::Batch batch(m_world.store());
            stored = batch.open() && m_world.inventory().write(plan, coin) && (experience == 0 || m_world.skills().write(skills))
                     && (!firstCraft || m_world.ownership().write_achievements(characterId, {job.firstCraft}))
                     && (!cooldown || m_world.store().write_craft_cooldown(characterId, state::CraftCooldownRow{cooldown->recipe, cooldown->used,
                                                                                                                cooldown->endRunNs}))
                     && batch.commit();
        }
        if (!stored) {
            log::write("world: craft batch of character %s: the database did not take job %u of %u: nothing of it changed",
                       characterId.c_str(), job.done + 1, job.total);
            end(*character, CraftEnd::storeFailed, true);
            return;
        }
        if (coin) {
            m_world.inventory().coin_written(characterId, *coin);
        }
        const std::string what = "craft job " + std::to_string(job.done + 1) + " of " + std::to_string(job.total) + " of " + job.recipe;
        m_world.inventory().finish(plan, what.c_str());
        if (experience > 0) {
            (void)m_world.skills().finish(skills, what.c_str());
        }
        // The entity can have moved in the map of the world: find it again.
        character = m_world.find_character(characterId);
        if (character == nullptr || !character->craftJob) {
            return;
        }
        if (firstCraft) {
            m_world.ownership().achievements_written(*character, {character->craftJob->firstCraft});
            ++m_counts.firstCrafts;
        }
        if (cooldown) {
            character->craftCooldowns[character->craftJob->recipeCrc] = *cooldown;
            m_world.touch(character->id, ComponentKey::crafting);
            ++m_counts.cooldownWrites;
        }
        CraftJob& done = *character->craftJob;
        ++done.done;
        done.feePaid += fee;
        done.bonusGiven += extra;
        done.experienceGiven += experience;
        ++m_counts.jobs;
        m_counts.itemsCreated += done.outputQty + extra;
        m_counts.bonusUnits += extra;
        m_counts.experience += experience;
        m_counts.feesPaid += fee;
        progress(*character, done.session, done.target, done.recipe, done.done, done.total);
        if (done.done >= done.total) {
            end(*character, CraftEnd::completed, false);
            return;
        }
    }
    // The next jobs in the next tick (a timer with no delay waits for the next tick of the world).
    m_world.after(std::chrono::milliseconds(0), [this, characterId, serial](World&) { run(characterId, serial); });
}

void Crafting::end(Entity& character, CraftEnd cause, bool notify) {
    if (!character.craftJob) {
        return;
    }
    const CraftJob job = *character.craftJob;
    character.craftJob.reset();
    m_active.erase(character.id);
    ++m_counts.ends[static_cast<std::size_t>(cause)];
    log::write("world: craft batch of character %s ended (%s): %s, %u of %u job(s) completed, %u %s created (%u base, %u bonus), fee %llu "
               "paid, %llu %s experience",
               character.characterId.c_str(), craft_end_text(cause), job.recipe.c_str(), job.done, job.total,
               job.done * job.outputQty + job.bonusGiven, job.output.c_str(), job.done * job.outputQty, job.bonusGiven,
               static_cast<unsigned long long>(job.feePaid), static_cast<unsigned long long>(job.experienceGiven),
               job.progression.empty() ? "trade skill" : job.progression.c_str());
    // A stop before the last job: the completed jobs are the count of the client (completed = total ends its crafting); with no completed
    // job the stop form.
    if (notify && job.done < job.total) {
        if (job.done > 0) {
            progress(character, job.session, job.target, job.recipe, job.done, job.done);
        } else {
            progress(character, job.session, job.target, std::string(), 0, 0);
        }
    }
}

void Crafting::progress(Entity& character, SessionId session, std::uint64_t target, const std::string& recipe, std::uint32_t completed,
                        std::uint32_t total) {
    character.craftProgress.recipe = recipe;
    character.craftProgress.completed = completed;
    character.craftProgress.total = total;
    character.craftProgress.target = target;
    character.craftProgress.session = session;
    // The number of the progress: the session of the request sends a progress whose number is newer than the last one that it sent.
    character.craftProgress.revision = ++m_progressSerial;
}

bool Crafting::client_stop(SessionId session) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr || !character->craftJob || character->craftJob->session != session) {
        ++m_counts.idleStops;
        return false;
    }
    const std::string mark = log::session_mark(character->owner, character->characterId);
    const log::Context logContext(mark);
    ++m_counts.clientStops;
    end(*character, CraftEnd::clientStop, false);
    return true;
}

void Crafting::use_ended(Entity& character, InteractionEnd cause) {
    if (!character.craftJob) {
        return;
    }
    log::write("world: the station use of character %s ended (%s): its craft batch stops", character.characterId.c_str(),
               interaction_end_text(cause));
    end(character, cause == InteractionEnd::left ? CraftEnd::left : CraftEnd::useEnded, cause != InteractionEnd::left);
}

void Crafting::left(Entity& character) {
    end(character, CraftEnd::left, false);
}

bool Crafting::stop(const std::string& characterId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || !character->craftJob) {
        return false;
    }
    end(*character, CraftEnd::command, true);
    return true;
}

CraftPreview Crafting::preview(const std::string& characterId, const std::string& recipeId, std::uint32_t quantity,
                               std::optional<std::uint64_t> seed) {
    CraftPreview preview;
    preview.quantity = quantity;
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        preview.verdict = CraftVerdict::notInWorld;
        return preview;
    }
    preview.recipe = data() != nullptr ? m_data->recipe_of(recipeId) : nullptr;
    const gamedata::Recipe* recipe = preview.recipe;
    std::vector<std::string> selected;
    preview.verdict = check(*character, recipe, nullptr, quantity, preview.station, preview.stationRow, preview.costs, preview.output,
                            selected, preview.detail, false);
    if (recipe != nullptr && recipe->cooldownQuantity > 0) {
        preview.cooldownLeft = cooldown_left(*character, *recipe);
        const auto found = character->craftCooldowns.find(recipe->crc);
        if (found != character->craftCooldowns.end() && m_world.clock().now() < found->second.end) {
            preview.cooldownSeconds = std::chrono::duration<double>(found->second.end - m_world.clock().now()).count();
        }
    }
    if (preview.verdict != CraftVerdict::accepted) {
        return preview;
    }
    // Stage 3.7b: the bonus item chance (the first member of each category), the experience, and the draws of a seeded batch.
    preview.yield = yield_of(*character, *recipe, selected);
    preview.experience = experience_of(*character, *recipe);
    if (seed) {
        std::uint64_t state = *seed;
        for (std::uint32_t job = 0; job < quantity; ++job) {
            preview.draws.push_back(draw_extra(preview.yield.extraUnits, [&state] { return World::next_unit(state); }));
        }
    }
    if (const gamedata::ItemDefinition* item = m_world.options().itemCatalog->find(preview.output.itemId)) {
        preview.gearScore = item->maxGearScore == 0 ? 0 : (item->gearScoreOverride != 0 ? item->gearScoreOverride : item->minGearScore);
    }
    preview.fee = fee_of(*recipe, preview.station && preview.station->chargeCraftingFee, quantity);
    CraftOutput largest = preview.output;
    largest.quantity += preview.yield.maxExtra;
    Inventory::Plan plan = m_world.inventory().plan_craft(characterId, preview.costs, largest, quantity);
    if (!plan.ok()) {
        preview.verdict = plan.error == ItemError::missingMaterials ? CraftVerdict::missingMaterials
                          : plan.error == ItemError::bagFull           ? CraftVerdict::bagFull
                                                                       : CraftVerdict::planFailed;
        preview.detail = item_error_text(plan.error);
        return preview;
    }
    preview.slots.assign(plan.touched.bag.begin(), plan.touched.bag.end());
    if (preview.fee > character->wallet.coin) {
        preview.verdict = CraftVerdict::notEnoughCoin;
    } else if (!preview.station) {
        preview.verdict = CraftVerdict::noStation;
    }
    return preview;
}

void Crafting::entered(Entity& character) {
    // The cooldowns of the recipes of the character with an active window (the rows; the end on the run clock of the world). A row of a
    // recipe that the data does not have, or of a window that ended, stays in the store with no value of the entity.
    character.craftCooldowns.clear();
    if (data() == nullptr) {
        return;
    }
    const std::uint64_t runNow = m_world.world_time().run_ns();
    const WorldClock::time_point now = m_world.clock().now();
    for (const state::CraftCooldownRow& row : m_world.store().list_craft_cooldowns(character.characterId)) {
        const gamedata::Recipe* recipe = m_data->recipe_of(row.recipe);
        if (recipe == nullptr || recipe->cooldownQuantity == 0 || row.endRunNs <= runNow) {
            continue;
        }
        const auto left = std::chrono::nanoseconds(row.endRunNs - runNow);
        character.craftCooldowns[recipe->crc] = CraftCooldown{recipe->id, row.used, row.endRunNs,
                                                              now + std::chrono::duration_cast<WorldClock::time_point::duration>(left)};
        ++m_counts.cooldownLoads;
    }
    if (!character.craftCooldowns.empty()) {
        m_world.touch(character.id, ComponentKey::crafting);
    }
}

std::string Crafting::set_cooldown(const std::string& characterId, const std::string& recipeId, std::uint32_t used, double seconds) {
    if (data() == nullptr) {
        return "no crafting data";
    }
    const gamedata::Recipe* recipe = m_data->recipe_of(recipeId);
    if (recipe == nullptr) {
        return "the recipe is not a recipe of the data";
    }
    if (recipe->cooldownQuantity == 0) {
        return "the recipe has no cooldown";
    }
    if (used > recipe->cooldownQuantity || !std::isfinite(seconds) || seconds < 0.0 || seconds > 1.0e8) {
        return "used is above the count of the recipe, or the seconds are not 0 to 100000000";
    }
    const auto window = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds));
    const std::uint64_t endRunNs = m_world.world_time().run_ns() + static_cast<std::uint64_t>(window.count());
    if (!m_world.store().write_craft_cooldown(characterId, state::CraftCooldownRow{recipe->id, used, endRunNs})) {
        return "the database did not take the row";
    }
    if (Entity* character = m_world.find_character(characterId)) {
        if (seconds > 0.0) {
            character->craftCooldowns[recipe->crc] =
                CraftCooldown{recipe->id, used, endRunNs, m_world.clock().now() + std::chrono::duration_cast<WorldClock::time_point::duration>(window)};
        } else {
            character->craftCooldowns.erase(recipe->crc);
        }
        m_world.touch(character->id, ComponentKey::crafting);
    }
    log::write("world: the cooldown of %s of character %s is %u of %u used, %.1f s to its end (a command)", recipe->id.c_str(),
               characterId.c_str(), used, recipe->cooldownQuantity, seconds);
    return {};
}

CraftingView Crafting::view(const std::string& characterId) const {
    CraftingView view;
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return view;
    }
    view.inWorld = true;
    view.job = character->craftJob;
    view.station = m_world.interactions().station(*character);
    if (view.station && data() != nullptr) {
        view.stationRow = m_data->station(view.station->assemblyKey);
    }
    return view;
}

} // namespace soulwarden::backend::world
