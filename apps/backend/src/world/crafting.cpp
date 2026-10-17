#include "world/crafting.h"

#include <algorithm>
#include <cmath>

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
                             CraftOutput& output, std::string& detail, bool needStation) const {
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
    output = CraftOutput{result->id, recipe->outputQty};
    return CraftVerdict::accepted;
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
    std::string detail;
    const CraftVerdict verdict = check(*character, recipe, &request, request.quantity, station, row, costs, output, detail, true);
    if (verdict != CraftVerdict::accepted) {
        return refuse(verdict, character, session, request, detail);
    }
    // The plan of the whole batch on a copy, and the fee of the batch.
    Inventory::Plan plan = m_world.inventory().plan_craft(character->characterId, costs, output, request.quantity);
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
        Inventory::Plan plan = m_world.inventory().plan_craft(characterId, costs, CraftOutput{job.output, job.outputQty}, 1);
        m_counts.slotVisits += character->bag.slots.size() + (paperdoll::kQuickLast - paperdoll::kQuickFirst + 1);
        const std::uint64_t fee = fee_of(*recipe, job.chargeFee, job.done + 1) - fee_of(*recipe, job.chargeFee, job.done);
        if (!plan.ok() || fee > character->wallet.coin) {
            log::write("world: craft batch of character %s: job %u of %u refused before its cost and its output: %s", characterId.c_str(),
                       job.done + 1, job.total, plan.ok() ? "the character has less coin than the fee" : item_error_text(plan.error));
            end(*character, CraftEnd::jobRefused, true);
            return;
        }
        // One transaction: the rows of the items and the coin of the fee of this job.
        const std::optional<std::uint64_t> coin = fee > 0 ? std::optional<std::uint64_t>(character->wallet.coin - fee) : std::nullopt;
        bool stored = false;
        {
            state::Store::Batch batch(m_world.store());
            stored = batch.open() && m_world.inventory().write(plan, coin) && batch.commit();
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
        // The entity can have moved in the map of the world: find it again.
        character = m_world.find_character(characterId);
        if (character == nullptr || !character->craftJob) {
            return;
        }
        CraftJob& done = *character->craftJob;
        ++done.done;
        done.feePaid += fee;
        ++m_counts.jobs;
        m_counts.itemsCreated += done.outputQty;
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
    log::write("world: craft batch of character %s ended (%s): %s, %u of %u job(s) completed, %u %s created, fee %llu paid",
               character.characterId.c_str(), craft_end_text(cause), job.recipe.c_str(), job.done, job.total, job.done * job.outputQty,
               job.output.c_str(), static_cast<unsigned long long>(job.feePaid));
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

CraftPreview Crafting::preview(const std::string& characterId, const std::string& recipeId, std::uint32_t quantity) {
    CraftPreview preview;
    preview.quantity = quantity;
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        preview.verdict = CraftVerdict::notInWorld;
        return preview;
    }
    preview.recipe = data() != nullptr ? m_data->recipe_of(recipeId) : nullptr;
    const gamedata::Recipe* recipe = preview.recipe;
    preview.verdict = check(*character, recipe, nullptr, quantity, preview.station, preview.stationRow, preview.costs, preview.output,
                            preview.detail, false);
    if (preview.verdict != CraftVerdict::accepted) {
        return preview;
    }
    if (const gamedata::ItemDefinition* item = m_world.options().itemCatalog->find(preview.output.itemId)) {
        preview.gearScore = item->maxGearScore == 0 ? 0 : (item->gearScoreOverride != 0 ? item->gearScoreOverride : item->minGearScore);
    }
    preview.fee = fee_of(*recipe, preview.station && preview.station->chargeCraftingFee, quantity);
    Inventory::Plan plan = m_world.inventory().plan_craft(characterId, preview.costs, preview.output, quantity);
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
