#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// One game event row (GameEventData; research quest-engine.md Q6, quest-engine-3.md Q3): the rewards of the success event of an objective,
// the whisper of the game event of a task, and (stage QG2a) the salvage event of a loot container item (SalvageGameEventID: the coin purses,
// Coin_Stack_500). One reader of the reward cells (read_game_event): the quest data and the loot data use it, so that one cell has one rule.

namespace soulwarden::backend::gamedata {

/** The cells of one game event row: the source text of each cell that is not blank, by its column name. */
using EventCells = std::map<std::string, std::string>;

/** The rewards of one game event. The cells keep the source text; the reader reads the ones of the engine. */
struct QuestGameEvent {
    std::string id;
    /** UniversalExpAmount and CurrencyReward (the units of the wallet: 150 is 1.50 coin). */
    std::uint64_t experience{0};
    std::uint64_t coin{0};
    /** CoinRewardChance: the loader of the client sets 1 when the cell is blank (0x31E70B0, row +0x38). */
    double coinChance{1.0};
    /**
     * ItemReward and ItemRewardQty (a blank quantity: 1, the client loader 0x31E7129; an explicit 0 stays 0). The quantity keeps its sign
     * (code review of stage 1.8b, finding QR8B-R10: a negative quantity is a removal with no rule, and the hand-in refuses it).
     */
    std::string item;
    std::int64_t itemQty{1};
    /**
     * Stage 1.8d: GearScoreRange (client parser 0x31E43A0: the text split at "-", one number is the smallest and the largest value). The
     * gear score of the item of the event. No value: a blank cell.
     */
    std::optional<std::pair<std::uint16_t, std::uint16_t>> gearScoreRange;
    /** Stage 1.8d: TerritoryStanding: the standing of a territory (stage QG1: the territory of the hand-in). */
    std::uint64_t standing{0};
    /**
     * Stage 1.8e: AzothReward: points of the Azoth balance (the categorical progression Azoth_Currency of stage 3.7c, research
     * quest-engine-3.md Q4). Stage QG2a (research quest-gaps-qg2b.md section 2): the amount of the client: the float cell truncated toward
     * zero (cvttss2si 0x5AC0F7B), then 16 bits (0x31E802A: ax at row +0xC). 37.5 gives 37. AzothRewardChance has no effect (the offer
     * and the journal show the amount with no chance; a project rule of the orchestrator: the hand-in pays what the client shows).
     */
    std::uint64_t azoth{0};
    /** AchievementId: an achievement of the event. */
    std::string achievement;
    /** WhisperId or WhisperID: the whisper of the event (166 of 170 TaskTriggerGameEvent rows name an event with one). */
    std::string whisper;
    /**
     * The rewards of the event that the backend cannot pay yet (stage 1.8d and later: FactionReputation, FactionTokens, a categorical
     * progression, the other currencies, an entitlement, a status effect, a TerritoryStanding, a GearScoreRange, or a CurrencyReward that is
     * not a whole number, a negative AzothReward, a loot limit of LootTags or LootLimitId): "COLUMN VALUE" for each cell with a value that
     * is not 0.
     */
    std::vector<std::string> unpaid;
};

/** @return The rewards of one game event row. `key` is the id when the cells have no EventID or id. */
[[nodiscard]] QuestGameEvent read_game_event(const std::string& key, const EventCells& cells);

/** Stage QG2a: @return The Azoth amount of the client for an AzothReward text (truncation toward zero, 16 bits), or no value (not a number, below 0). */
[[nodiscard]] std::optional<std::uint64_t> azoth_amount(const std::string& text);

} // namespace soulwarden::backend::gamedata
