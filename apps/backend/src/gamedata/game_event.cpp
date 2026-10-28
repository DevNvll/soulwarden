#include "gamedata/game_event.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace soulwarden::backend::gamedata {
namespace {

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

std::string cell(const EventCells& cells, const char* name) {
    const auto found = cells.find(name);
    return found != cells.end() ? found->second : std::string{};
}

/** @return A number of a cell text (text after the number is allowed), or no value for a blank or not finite text. */
std::optional<double> number(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

/** Code review of stage 1.8d, finding QR8D-R6: @return The number of a whole text (no trailing text after the number), or no value. */
std::optional<double> whole_number(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::uint64_t> azoth_amount(const std::string& text) {
    // Code review of stage QG2a, finding QG2R-R5: the whole text is the number ("37junk" is no amount), and a negative cell (-0.5) has no
    // rule: both stay unpaid.
    const std::optional<double> value = whole_number(trimmed(text));
    if (!value || *value < 0.0) {
        return std::nullopt;
    }
    // The cell is a float32 (numeric column kind 2); the integer getter 0x5AC0ED0 truncates it toward zero (cvttss2si at 0x5AC0F7B), and
    // the event loader keeps the low 16 bits (0x31E802A). A negative amount has no rule here (no row of the build has one).
    const auto cellValue = static_cast<float>(*value);
    if (!(cellValue > -1.0F)) {
        return std::nullopt;
    }
    const double truncated = std::trunc(static_cast<double>(cellValue));
    if (truncated >= 2147483648.0) {
        return std::nullopt; // cvttss2si gives the indefinite integer: no rule.
    }
    return static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int64_t>(truncated)));
}

QuestGameEvent read_game_event(const std::string& key, const EventCells& cells) {
    QuestGameEvent event;
    event.id = cell(cells, "id");
    if (event.id.empty()) {
        event.id = cell(cells, "EventID");
    }
    if (event.id.empty()) {
        event.id = key;
    }
    const auto whole = [&](const char* column) -> std::uint64_t {
        const std::optional<double> value = number(cell(cells, column));
        return value && *value > 0.0 ? static_cast<std::uint64_t>(std::min(*value, 1e12)) : 0U;
    };
    event.experience = whole("UniversalExpAmount");
    // Stage QG2a: CurrencyReward is a whole text (a salvage event of the data has "5000-10000": a range with no known rule stays unpaid; a
    // number with a text after it must not pay its first number).
    if (const std::string text = trimmed(cell(cells, "CurrencyReward")); !text.empty()) {
        const std::optional<double> value = whole_number(text);
        // Code review of stage QG2a, finding QG2R-R4: a fraction (2500.75) is not a whole number of the units of the wallet: unpaid.
        if (value && *value >= 0.0 && std::floor(*value) == *value) {
            event.coin = static_cast<std::uint64_t>(std::min(*value, 1e12));
        } else {
            event.unpaid.push_back("CurrencyReward " + text);
        }
    }
    if (const std::optional<double> chance = number(cell(cells, "CoinRewardChance"))) {
        event.coinChance = std::clamp(*chance, 0.0, 1.0);
    }
    event.item = trimmed(cell(cells, "ItemReward"));
    if (const std::optional<double> quantity = number(cell(cells, "ItemRewardQty"))) {
        // The sign stays (code review of stage 1.8b, finding QR8B-R10): a negative quantity refuses the hand-in.
        event.itemQty = static_cast<std::int64_t>(std::clamp(*quantity, -1e6, 1e6));
    }
    // Stage 1.8d: GearScoreRange (client 0x31E43A0: split at "-"; one number is both ends). A text that is not one or two whole numbers from
    // 0 to 65535 keeps the event with the column as a reward that the backend cannot pay.
    if (const std::string text = trimmed(cell(cells, "GearScoreRange")); !text.empty()) {
        const std::size_t dash = text.find('-');
        const std::optional<double> low = whole_number(trimmed(text.substr(0, dash)));
        const std::optional<double> high = dash == std::string::npos ? low : whole_number(trimmed(text.substr(dash + 1)));
        if (low && high && *low >= 0.0 && *high >= *low && *high <= 65535.0 && std::floor(*low) == *low && std::floor(*high) == *high) {
            event.gearScoreRange = std::make_pair(static_cast<std::uint16_t>(*low), static_cast<std::uint16_t>(*high));
        } else {
            event.unpaid.push_back("GearScoreRange " + text);
        }
    }
    // Stage 1.8d: TerritoryStanding has an owner (the standing of a territory). A text that is not a whole number of 0 or more (the data has
    // one "#REF!") stays a reward that the backend cannot pay.
    if (const std::string text = trimmed(cell(cells, "TerritoryStanding")); !text.empty()) {
        const std::optional<double> value = whole_number(text); // Finding QR8D-R6: the whole text.
        if (value && *value >= 0.0 && std::floor(*value) == *value && *value <= 1e9) {
            event.standing = static_cast<std::uint64_t>(*value);
        } else {
            event.unpaid.push_back("TerritoryStanding " + text);
        }
    }
    event.achievement = trimmed(cell(cells, "AchievementId"));
    event.whisper = trimmed(cell(cells, "WhisperId"));
    if (event.whisper.empty()) {
        event.whisper = trimmed(cell(cells, "WhisperID"));
    }
    // Stage QG2a: AzothReward is the amount of the client (azoth_amount: 37.5 gives 37), with any AzothRewardChance (a blank chance loads as
    // 0, and the offer and the journal still show the amount: research quest-gaps-qg2b.md section 2). A project rule of the orchestrator:
    // the hand-in pays the amount that the client shows. A text that is not a number, and a negative amount, stay unpaid.
    if (const std::string text = trimmed(cell(cells, "AzothReward")); !text.empty()) {
        if (const std::optional<std::uint64_t> amount = azoth_amount(text)) {
            event.azoth = *amount;
        } else {
            event.unpaid.push_back("AzothReward " + text);
        }
    }
    // The rewards with no owner in the backend yet (research quest-engine.md Q6; stage 1.8d and later).
    for (const char* column : {"FactionReputation", "FactionTokens", "CategoricalProgressionReward", "UmbralCurrency", "PvpXp", "PVPXP",
                               "SeasonsXp", "AzothSalt", "CursedSilvers", "CursedCrowns"}) {
        const std::string text = trimmed(cell(cells, column));
        const std::optional<double> value = number(text);
        if (!text.empty() && (!value || *value != 0.0)) {
            event.unpaid.push_back(std::string(column) + " " + text);
        }
    }
    // Stage QG2a: LootTags and LootLimitId (a loot limit of a salvage event of the data: the PUG caches, the Crassus box) have no rule.
    for (const char* column : {"EntitlementId", "StatusEffectId", "LootTags", "LootLimitId"}) {
        if (const std::string text = trimmed(cell(cells, column)); !text.empty()) {
            event.unpaid.push_back(std::string(column) + " " + text);
        }
    }
    return event;
}

} // namespace soulwarden::backend::gamedata
