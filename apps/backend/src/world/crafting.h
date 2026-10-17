#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/crafting_data.h"
#include "world/entity.h"
#include "world/interactions.h"
#include "world/inventory.h"

// The crafting of fixed recipes at a station and at a camp (stage 3.7a, docs/systems/crafting.md, protocol notes 4.50), and from stage 3.7b
// refining and the reward progress (protocol notes 4.51): the bonus items of decision D2, the trade skill experience of the event, the
// first-craft bonus with its achievement, and the cooldown of a recipe (the Crafting state 205).
//
// The client opens its crafting screen at a station use (the interaction base: action 20, OpenCrafting; the Assembly gives the station
// key) and sends request 2997 for a craft: the recipe (CRC-32 of its id), the count, the fee and the gear score that it computed, the perk
// items, the upgrade level, and the item that it selected for each ingredient (type Item, quantity 1: the client converts each selection
// to such an entry, 0x5BA9FB0). The server takes the quantities of the recipe, not the entries (research crafting.md 4.1). The checks, in
// this sequence (each refusal changes nothing; the source of each rule is in the document):
//   1. The character of the session lives, and the crafting data and the item catalog are there.
//   2. The character uses a crafting station or a camp (the committed use of the interaction base), and its Assembly key is a row of
//      the station database (client lookup 0x2E804E0 has no alias). No batch of the character runs (one batch for each character).
//   3. The recipe is a recipe of the data with no cause (the rules that the backend has for this stage), a station key of the recipe is
//      a type of the station row (client 0x2E5ED00), the trade skill rank is at RecipeLevel or more, the recipe needs no expansion.
//   4. The count is 1 to 10,000 (the reader of the client), there are no perk items and no upgrade, and the entries name, in the order
//      of the recipe, the item of each item ingredient or a member of each category ingredient (one entry for each ingredient).
//   5. The plan of the whole batch on a copy (Inventory::plan_craft), job by job in the order of the jobs (code review of stage 3.7a,
//      finding CR7A-R1): the costs of each job from the unlocked stacks of the bag and of the quick slots, then its output into the bag;
//      and the coin of the fee of the batch.
// An accepted request is a batch (decision D5 of the orchestrator, option A): the server reserves nothing but checks the whole batch, and
// each job of the batch is one transaction (its costs, its output, its part of the fee). The first job completes one craft time after the
// request (the client setting javelin.crafting-duration-seconds, 1.0 s; the server facet schedules its delayed execution with it after
// one check of the whole count, client 0x2E41E10), and the next jobs complete in the next world ticks (kJobsPerTick in each tick). Before
// each job the checks 1 to 3 and the plan of that job run again: a failure (the bag changed, the use ended) stops the batch before the next
// cost and output, and the completed jobs stay. A stop (message 2999 of the client, the end of the station use, the death, the leave)
// keeps the completed jobs. A batch is a value of the world (the entity): a restart or a crash ends it, and each completed job is in the
// store (the crash recovery: no job is half written, no job is written twice).
//
// The result: the output slots get the cause ItemCreated (3; live check L3: the result screen of the client shows the item), and after the
// record of the items the session sends progress 2993 with the count of the completed jobs (completed = total ends the crafting of the
// client, which then sends 2999). A refusal sends the stop form of 2993 (an empty recipe, 0 of 0; live check L3: the client stops its
// crafting and shows "Crafting attempt failed." at the end of its own 10 s timer). A second request while a batch runs gets no message.
// An accepted batch drops a progress of the character that the session did not send yet (code review of stage 3.7a, finding CR7A-R4: the
// stop form of a refused request before it in the same tick would end the crafting of the client, and its 2999 the new batch).
//
// Stage 3.7b, for each job of a batch (one transaction with its costs and its output): the extra units of decision D2 (the expected extra
// units e of the request: floor(e) and one more with the chance of the rest, from the one random source of the world), the trade skill
// experience (the reward of the event times the units of the ingredients; the first job of a recipe whose first-craft achievement the
// character does not have adds (3 - 1) times that and writes the achievement), and the cooldown row of the recipe. The request checks the
// crafts that are left in the cooldown window. The output of a job before the last job of a craft-all batch has the cause ItemAdded.
//
// The cost depends on the requests and the active batches, not on the size of the world (MP-8): a request finds its recipe, its station
// row, and a category member in hash maps, the station of the use through the active interaction of the character, and the plan looks only
// at the bag and the quick slots of that character; a batch is one timer of the world. World thread only.

namespace soulwarden::backend::world {

class World;

/** Rules of the client and of this project for the crafting (docs/systems/crafting.md). */
namespace crafting_rules {
/**
 * The time from an accepted request to its first job: the default of the client setting javelin.crafting-duration-seconds (1.0 s,
 * registered at 0x6171204; the server facet of the client schedules its delayed execution with it at 0x2E4204F).
 */
inline constexpr std::chrono::milliseconds kCraftDuration{1000};
/** The jobs of one batch that complete in one world tick after the first one (a decision of an agent for D5 and the cost rule). */
inline constexpr std::uint32_t kJobsPerTick = 20;
/** The limits of the request reader of the client (research crafting.md 4.1). */
inline constexpr std::uint32_t kMaxQuantity = 10000;
inline constexpr std::uint32_t kMaxFee = 100000000;
inline constexpr std::uint32_t kMaxGearScore = 10000;
inline constexpr std::size_t kMaxPerkItems = 4;
inline constexpr std::uint8_t kMaxUpgrade = 5;
inline constexpr std::size_t kMaxIngredients = 7;
inline constexpr std::uint32_t kMaxIngredientQuantity = 50000;
/**
 * The first-craft multiplier of the trade skill experience (stage 3.7b): the default of the client setting
 * javelin.crafting-first-craft-bonus-exp-multiplier (3, registered at 0x6171316 and 0x617133F). The crafting screen adds one job's base
 * experience times (multiplier - 1) to a batch (craftingscreenv4.luac; live check L4: "Base 132 XP, First Craft 88 XP (x2), Total 220").
 */
inline constexpr std::uint64_t kFirstCraftMultiplier = 3;
/** The scale of the roll bonus points in the bonus item chance (client 0x2E4FF50: chance + points x 0.0001). */
inline constexpr double kRollBonusScale = 0.0001;
} // namespace crafting_rules

/**
 * The bonus item chance of a craft (stage 3.7b; client 0x2E4FF50 with 0x5C52A40 and 0x2E57A70; live check L4) and the extra output of
 * decision D2 of the orchestrator (option A).
 */
struct CraftYield {
    /**
     * The chance of the recipe and of the materials (0x5C52A40): BonusItemChance; the ExtraBonusItemChance of the item of the first
     * ingredient when it is an item; for each later category ingredient, the tier list value of the tier of its selected item above
     * (BonusItemChanceIncrease) or below (BonusItemChanceDecrease) the base tier (the item of the first ingredient), and its
     * ExtraBonusItemChance.
     */
    double materialChance{0.0};
    /**
     * The roll bonus points (0x2E57A70): the materials (IngredientBonusPrimary of the first entry, IngredientBonusSecondary of each other
     * one), and with no DisallowBonusesToGS the RollBonus of the rank of the trade skill and the ROL effects of the trade skill.
     */
    std::int32_t materialPoints{0};
    std::int32_t rankPoints{0};
    std::int32_t effectPoints{0};
    /** max(0, materialChance + points x 0.0001), to 6 decimals: the client shows it x 100 ("%.0f%%"). */
    double chance{0.0};
    /** Decision D2: the expected extra units of one job (OutputQty x chance) and the largest extra of one job (its ceiling). */
    double extraUnits{0.0};
    std::uint32_t maxExtra{0};
};

/** The trade skill experience of one job of a recipe for a character (stage 3.7b; the reward of the event of the recipe). */
struct CraftExperience {
    /** The progression of the event (a trade skill; empty: no experience). */
    std::string progression;
    /** floor(the reward of the event x the units of the ingredients of one job): the base of one job (live check L4: 11 x 4 = 44). */
    std::uint64_t perJob{0};
    /** The first-craft achievement of the recipe that the character does not have (empty: none), its bit, and its extra experience. */
    std::string firstCraft;
    std::uint32_t firstCraftBit{0};
    std::uint64_t firstCraftExtra{0};
};

/** One ingredient entry of request 2997 (a claim of the client). */
struct CraftIngredientClaim {
    std::uint8_t type{0};
    std::uint32_t quantity{0};
    std::uint32_t item{0};
};

/** Request 2997 (CraftingComponentServerFacet_SetCraftingDetails): each value is a claim of the client. */
struct CraftRequest {
    /** The facet target of the Crafting component in the body: the progress messages of the batch use it. */
    std::uint64_t target{0};
    std::uint32_t recipe{0};
    std::uint32_t quantity{0};
    std::uint32_t coinFee{0};
    std::uint16_t gearScore{0};
    std::vector<std::uint32_t> perkItems;
    std::uint8_t upgrade{0};
    std::vector<CraftIngredientClaim> ingredients;
};

enum class CraftVerdict : std::uint8_t {
    accepted,
    notInWorld,
    dead,
    /** The backend has no crafting data or no item catalog. */
    noData,
    /** The character has no committed use of a crafting station or a camp. */
    noStation,
    /** The Assembly key of the station has no row of the station database. */
    unknownStation,
    /** A batch of the character runs. */
    busy,
    unknownRecipe,
    /** The recipe has a cause of the data (a rule that the backend does not have yet). */
    unsupported,
    /** No station key of the recipe is a type of the station row. */
    wrongStation,
    /** The trade skill rank is below RecipeLevel. */
    rank,
    /** The recipe needs an expansion (the backend does not check it yet). */
    expansion,
    /** The count is 0 or above 10,000. */
    badQuantity,
    /** Perk items or an upgrade level (stage 3.7c). */
    perkItems,
    /** The entries do not name the ingredients of the recipe (count, type, item). */
    badIngredients,
    /** The bag and the quick slots do not have the unlocked quantity. */
    missingMaterials,
    /** The bag has no place for the whole output. */
    bagFull,
    notEnoughCoin,
    /** A different item error of the plan (the catalog has no output item). */
    planFailed,
    /** Stage 3.7b: the count is above the crafts that are left in the cooldown window of the recipe. */
    cooldown,
};
inline constexpr std::size_t kCraftVerdictCount = 20;

[[nodiscard]] const char* craft_verdict_text(CraftVerdict verdict);

/** Why a batch ended. */
enum class CraftEnd : std::uint8_t {
    completed,
    /** Message 2999 of the client. */
    clientStop,
    /** The station use ended (2473, 3746, the range, a new use, a travel, the death). */
    useEnded,
    /** The character left the world. */
    left,
    /** A command (tools/world craft stop). */
    command,
    /** The checks or the plan of the next job failed (the bag changed, the coin, the data): before its cost and its output. */
    jobRefused,
    /** The database did not take the rows of the next job: nothing of it changed. */
    storeFailed,
};

[[nodiscard]] const char* craft_end_text(CraftEnd end);

/** The plan of a recipe for a character (a command; also the base of a request). */
struct CraftPreview {
    CraftVerdict verdict{CraftVerdict::accepted};
    std::string detail;
    const gamedata::Recipe* recipe{nullptr};
    std::uint32_t quantity{0};
    /** The station of the use (no value: none) and its row of the station database. */
    std::optional<StationContext> station;
    const gamedata::CraftingStation* stationRow{nullptr};
    /** The costs of one job (the items of the entries, or the first member of a category for a command) and the output of one job. */
    std::vector<CraftCost> costs;
    CraftOutput output;
    /** The fee of the batch (wire hundredths of a coin) and the gear score of the output instance. */
    std::uint64_t fee{0};
    std::uint32_t gearScore{0};
    /** The slots of the bag that the plan of the batch changes (no value: no plan). */
    std::vector<std::uint32_t> slots;
    /** Stage 3.7b: the bonus item chance and the extra units of one job, the experience of one job, the crafts left in the cooldown window. */
    CraftYield yield;
    CraftExperience experience;
    /** The crafts that are left in the cooldown window (no value: the recipe has no cooldown) and the seconds to its end (0: no window). */
    std::optional<std::uint32_t> cooldownLeft;
    double cooldownSeconds{0.0};
    /** A seeded preview: the extra units of each job of the batch with the generator of the world at that seed (no change of the world). */
    std::vector<std::uint32_t> draws;
};

struct CraftingCounts {
    std::uint64_t requests{0};
    std::array<std::uint64_t, kCraftVerdictCount> verdicts{};
    std::uint64_t jobs{0};
    std::uint64_t batches{0};
    std::array<std::uint64_t, 7> ends{};
    std::uint64_t itemsCreated{0};
    std::uint64_t feesPaid{0};
    std::uint64_t clientStops{0};
    /** Messages 2999 with no batch (the answer of the client to a final progress). */
    std::uint64_t idleStops{0};
    /** The jobs that the timers looked at, and the slots of the bags and of the quick slots that the plans looked at (case CRA-LOAD). */
    std::uint64_t jobVisits{0};
    std::uint64_t slotVisits{0};
    /** The requests whose fee claim differs from the fee of the server (the server takes its own fee). */
    std::uint64_t feeClaimsDiffer{0};
    /** Stage 3.7b: the extra units of decision D2, the trade skill experience, the first crafts, the cooldown rows that the jobs wrote. */
    std::uint64_t bonusUnits{0};
    std::uint64_t experience{0};
    std::uint64_t firstCrafts{0};
    std::uint64_t cooldownWrites{0};
    /** The cooldown rows that the world entries read (each one of an active window), and the jobs that the timers ran. */
    std::uint64_t cooldownLoads{0};
};

/** The crafting of a character for a reader (a command). */
struct CraftingView {
    bool inWorld{false};
    std::optional<CraftJob> job;
    std::optional<StationContext> station;
    const gamedata::CraftingStation* stationRow{nullptr};
};

class Crafting {
public:
    /** @param data The crafting data (game data), or null: each request is refused. */
    Crafting(World& world, const gamedata::CraftingData* data);
    ~Crafting();
    Crafting(const Crafting&) = delete;
    Crafting& operator=(const Crafting&) = delete;

    [[nodiscard]] const gamedata::CraftingData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /** Request 2997 of the client of a session (the world finds the character from the session: MP-5). */
    CraftVerdict request(SessionId session, const CraftRequest& request);
    /** Message 2999 of the client of a session: its batch stops (the completed jobs stay). @return True when a batch stopped. */
    bool client_stop(SessionId session);
    /** The station use of a character ended (the handler of the station options): its batch stops. */
    void use_ended(Entity& character, InteractionEnd cause);
    /** The world calls it at the leave of a character: its batch stops. */
    void left(Entity& character);
    /** A command: the batch of a character stops. @return False: no batch. */
    bool stop(const std::string& characterId);
    /** The entity of a character comes into the world: the cooldowns of its recipes (their rows) with an active window (stage 3.7b). */
    void entered(Entity& character);
    /**
     * A command (stage 3.7b, the harness): sets the cooldown of a recipe of a character: `used` crafts in a window that ends `seconds` from
     * now (0: no window). The row first, then the entity and its Crafting state. @return An empty text, or the cause of a refusal.
     */
    std::string set_cooldown(const std::string& characterId, const std::string& recipeId, std::uint32_t used, double seconds);

    /**
     * @return The bonus item chance and the extra units of one job of a recipe for a character with these selected items (the item of
     *         each ingredient, in the order of the recipe): the rules of the client (CraftYield).
     */
    [[nodiscard]] CraftYield yield_of(const Entity& character, const gamedata::Recipe& recipe, const std::vector<std::string>& selected) const;
    /** @return The trade skill experience of one job of a recipe for a character (CraftExperience). */
    [[nodiscard]] CraftExperience experience_of(const Entity& character, const gamedata::Recipe& recipe) const;
    /** @return The crafts that are left in the cooldown window of a recipe for a character (CooldownQuantity with no active window). */
    [[nodiscard]] std::uint32_t cooldown_left(const Entity& character, const gamedata::Recipe& recipe) const;
    /**
     * @return The extra units of one job (decision D2): floor(e), and one more unit when one draw of the generator is below the rest (no
     *         draw for a whole e).
     */
    [[nodiscard]] static std::uint32_t draw_extra(double extraUnits, const std::function<float()>& draw);

    /**
     * @return The plan of a recipe for a character (a command, the development routes and the tool window): the checks of a request with
     *         the station of its use (no station: the verdict noStation, and the plan with no station check), the first member of each
     *         category, and the plan of the batch on a copy. No change.
     */
    [[nodiscard]] CraftPreview preview(const std::string& characterId, const std::string& recipeId, std::uint32_t quantity,
                                       std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] CraftingView view(const std::string& characterId) const;

    [[nodiscard]] const CraftingCounts& counts() const {
        return m_counts;
    }
    /** @return The number of batches that run. */
    [[nodiscard]] std::size_t batch_count() const {
        return m_active.size();
    }
    /** @return The fee of a count of a recipe at a station (wire hundredths of a coin; client 0x2E513A0, 0x2E57580). */
    [[nodiscard]] static std::uint64_t fee_of(const gamedata::Recipe& recipe, bool chargeFee, std::uint64_t count);

private:
    /** The checks 1 to 4 of a request (and of a command), and the costs and the output of one job. */
    CraftVerdict check(Entity& character, const gamedata::Recipe*& recipe, const CraftRequest* request, std::uint32_t quantity,
                       std::optional<StationContext>& station, const gamedata::CraftingStation*& row, std::vector<CraftCost>& costs,
                       CraftOutput& output, std::vector<std::string>& selected, std::string& detail, bool needStation) const;

    CraftVerdict refuse(CraftVerdict verdict, Entity* character, SessionId session, const CraftRequest& request, const std::string& detail);
    /** The timer of a batch: its next jobs. */
    void run(const std::string& characterId, std::uint64_t serial);
    /** Ends the batch of a character and sends the final progress when `notify`. */
    void end(Entity& character, CraftEnd cause, bool notify);
    /** Sets the progress of a character for its session (the session sends 2993 after the record of its items). */
    void progress(Entity& character, SessionId session, std::uint64_t target, const std::string& recipe, std::uint32_t completed,
                  std::uint32_t total);

    World& m_world;
    const gamedata::CraftingData* m_data;
    std::unique_ptr<InteractionHandler> m_handler;
    /** The characters with a batch (MP-8: a stop of the world visits only them). */
    std::map<EntityId, std::string> m_active;
    std::uint64_t m_serial{0};
    std::uint64_t m_progressSerial{0};
    CraftingCounts m_counts;
};

} // namespace soulwarden::backend::world
