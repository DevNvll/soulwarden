#include <cctype>
#include <string>
#include <string_view>

#include "gamedata/progression_tables.h"
#include "server/http/character_commands.h"

// The views of the evaluator of the passive rules (stage 3.3b-S5, world/passives.h), in the common layer of the development
// routes (character_commands.h): the rules of a character with what the backend does with each one, and the rules of a whole
// weapon tree (the list of the rules that the evaluator does not apply yet).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

const char* origin_name(world::RuleOrigin origin) {
    switch (origin) {
    case world::RuleOrigin::node:
        return "node";
    case world::RuleOrigin::active:
        return "active";
    case world::RuleOrigin::perk:
        return "perk";
    case world::RuleOrigin::helper:
        return "helper of a node";
    case world::RuleOrigin::status:
        return "row of a status";
    }
    return "?";
}

json rule_json(const world::RuleView& view) {
    return {{"id", view.id}, {"origin", origin_name(view.origin)}, {"kinds", view.kinds}, {"applied", view.applied}, {"note", view.note}};
}

} // namespace

json passives_json(world::World& target, const std::string& character) {
    json reply{{"inWorld", false}};
    world::Entity* entity = target.find_character(character);
    if (entity == nullptr) {
        return reply;
    }
    reply["inWorld"] = true;
    json rules = json::array();
    for (const world::RuleView& view : target.passives().describe(*entity)) {
        rules.push_back(rule_json(view));
    }
    reply["rules"] = rules;
    // The critical values of the weapon in the hand (stage 3.3b-S5): its chance and its multiplier (game data), the fixed parts of
    // the rules of its tree and of the perks, and the parts of the statuses. The rules of a hit add their parts at the hit.
    json critical{{"weapon", ""}};
    const world::ItemInstance* item = world::Combat::weapon_in_hand(*entity);
    const gamedata::CombatTables* tables = target.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    if (weapon != nullptr) {
        float fixedChance = 0.0F;
        float fixedDamage = 0.0F;
        const std::uint32_t table = weapon->mastery.empty() ? 0 : gamedata::progression_id(weapon->mastery);
        for (const world::PassiveRule& rule : target.passives().rules_of(*entity, table)) {
            if (world::Passives::fixed(*rule.cells)) {
                fixedChance += rule.cells->number("CritChance") * rule.potency;
                fixedDamage += rule.cells->number("CritDamage") * rule.potency;
            }
        }
        critical = {{"weapon", weapon->id},
                    {"hasValues", weapon->critical},
                    {"weaponChance", weapon->critChance},
                    {"weaponMultiplier", weapon->critMultiplier},
                    {"fixedChance", fixedChance},
                    {"fixedDamage", fixedDamage},
                    {"statusChance", entity->gear.critChance},
                    {"statusDamage", entity->gear.critDamage},
                    {"chance", std::clamp(weapon->critChance + fixedChance + entity->gear.critChance, 0.0F, 1.0F)},
                    {"multiplier", weapon->critMultiplier + fixedDamage + entity->gear.critDamage}};
    }
    reply["critical"] = critical;
    const world::PassiveCounts& counts = target.passives().counts();
    reply["counts"] = {{"hits", counts.hits},
                       {"rulesLooked", counts.rulesLooked},
                       {"applied", counts.applied},
                       {"casterStatuses", counts.casterStatuses},
                       {"targetStatuses", counts.targetStatuses},
                       {"heals", counts.heals},
                       {"cooldownReductions", counts.cooldownReductions},
                       {"aroundSteps", counts.aroundSteps},
                       {"aroundLooked", counts.aroundLooked},
                       {"aroundStatuses", counts.aroundStatuses},
                       {"hitTakenRules", counts.hitTakenRules}};
    return reply;
}

json passives_table_json(world::World& target, const std::string& table) {
    // The id of the table, or a short name of the weapon ("greataxe": GreatAxeAbilityTable).
    std::string name = table;
    if (const gamedata::ProgressionTables* tables = target.options().progressionTables;
        tables != nullptr && tables->progression(name) == nullptr && tables->progression(name + "AbilityTable") != nullptr) {
        name = tables->progression(name + "AbilityTable")->id;
    }
    json rules = json::array();
    std::size_t applied = 0;
    for (const world::RuleView& view : target.passives().describe_table(gamedata::progression_id(name))) {
        applied += view.applied ? 1 : 0;
        rules.push_back(rule_json(view));
    }
    return {{"table", name}, {"rules", rules}, {"applied", applied}, {"count", rules.size()}};
}

json passives_stamina_json(world::World& target) {
    // Each ability row of the data with a part of the stamina or of the dodge (stage 3.4f-B): a column whose name has "Stamina", the
    // event OnHitTakenWhileInvulnerable, the condition EquipLoadCategory, or a text that names a dodge.
    const gamedata::AbilityData* abilities = target.options().abilityData;
    const gamedata::ProgressionTables* progression = target.options().progressionTables;
    const auto lower = [](std::string_view text) {
        std::string out(text);
        for (char& letter : out) {
            letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
        }
        return out;
    };
    json rules = json::array();
    std::size_t applied = 0;
    if (abilities != nullptr) {
        for (const gamedata::AbilityRow& row : abilities->tables().abilityRows) {
            bool found = row.cells.has("OnHitTakenWhileInvulnerable") || row.cells.has("EquipLoadCategory");
            for (const auto& [column, value] : row.cells.numbers) {
                found = found || lower(column).find("stamina") != std::string::npos;
            }
            for (const auto& [column, value] : row.cells.texts) {
                found = found || lower(column).find("stamina") != std::string::npos || lower(value).find("dodge") != std::string::npos;
            }
            if (!found) {
                continue;
            }
            const gamedata::AbilityNode* node = progression != nullptr ? progression->node(row.id) : nullptr;
            const world::RuleOrigin origin = node == nullptr ? world::RuleOrigin::perk : node->active ? world::RuleOrigin::active : world::RuleOrigin::node;
            const world::RuleView view = target.passives().describe_row(row.id, &row.cells, origin);
            applied += view.applied ? 1 : 0;
            rules.push_back(rule_json(view));
        }
    }
    return {{"table", "stamina"}, {"rules", rules}, {"applied", applied}, {"count", rules.size()}};
}

} // namespace soulwarden::backend::server::http::commands
