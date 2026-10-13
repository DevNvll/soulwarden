#include "server/http/character_commands.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "server/http/router.h"

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

constexpr std::array<const char*, world::attribute::kCount> kAttributeNames{"constitution", "focus", "strength",
                                                                             "dexterity", "intelligence"};
/** The id of the mastery of a weapon is the id of its ability table: "<Weapon>AbilityTable". */
constexpr const char* kWeaponSuffix = "AbilityTable";

int status_of(world::ProgressError error) {
    return error == world::ProgressError::unknownCharacter ? 404 : 400;
}

/**
 * @return The id text of the weapon mastery or trade skill that a command names: the text of
 *         the command when the tables have it, or that text with the end of a weapon id.
 */
std::string progression_name(const gamedata::ProgressionTables* tables, const std::string& text) {
    if (tables != nullptr && tables->progression(text) == nullptr && tables->progression(text + kWeaponSuffix) != nullptr) {
        return text + kWeaponSuffix;
    }
    return text;
}

/** The two commands that set a rank: a weapon mastery (the level is the rank plus 1) and a trade skill. */
WorldCommand set_rank(const std::string& character,
                      const Arguments& arguments,
                      service::HttpResponse& refusal,
                      gamedata::ProgressionKind kind,
                      const char* parameter) {
    const bool weapon = kind == gamedata::ProgressionKind::weapon;
    const auto name = arguments.text(parameter);
    const auto level = arguments.integer("level", weapon ? 1 : 0, 100000);
    std::uint64_t experience = 0;
    if (arguments.has("experience")) {
        const auto value = arguments.integer("experience", 0, world::kSkillExperienceLimit);
        if (!value) {
            refusal = message(400, "experience must be a number from 0 to 10000000");
            return {};
        }
        experience = *value;
    }
    if (!name || name->empty() || !level) {
        refusal = message(400, weapon ? "give weapon=NAME and level=N (1 to 20)" : "give skill=NAME and level=N");
        return {};
    }
    const std::uint32_t rank = static_cast<std::uint32_t>(weapon ? *level - 1 : *level);
    return [character, name = *name, kind, rank, experience](world::World& target) {
        const std::string id = progression_name(target.options().progressionTables, name);
        return skill_reply(target.skills().set_rank(character, id, kind, rank, experience));
    };
}

} // namespace

std::optional<std::uint64_t> Arguments::integer(const std::string& name, std::uint64_t lowest, std::uint64_t highest) const {
    const auto value = text(name);
    if (!value || value->empty() || value->size() > 20 || value->find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    errno = 0;
    const unsigned long long number = std::strtoull(value->c_str(), nullptr, 10);
    return errno == 0 && number >= lowest && number <= highest ? std::optional<std::uint64_t>(number) : std::nullopt;
}

std::optional<float> Arguments::number(const std::string& name, float lowest, float highest) const {
    const auto value = text(name);
    if (!value || value->empty() || value->size() > 32) {
        return std::nullopt;
    }
    char* end = nullptr;
    const float number = std::strtof(value->c_str(), &end);
    return end == value->c_str() + value->size() && std::isfinite(number) && number >= lowest && number <= highest
               ? std::optional<float>(number)
               : std::nullopt;
}

std::optional<std::string> QueryArguments::text(const std::string& name) const {
    const std::string& query = m_request.query;
    const std::string needle = name + "=";
    std::size_t position = 0;
    while (position < query.size()) {
        const std::size_t end = query.find('&', position);
        const std::string pair = query.substr(position, end == std::string::npos ? end : end - position);
        if (pair.rfind(needle, 0) == 0) {
            return pair.substr(needle.size());
        }
        if (end == std::string::npos) {
            break;
        }
        position = end + 1;
    }
    return std::nullopt;
}

std::optional<std::string> JsonArguments::text(const std::string& name) const {
    if (!m_object.is_object()) {
        return std::nullopt;
    }
    const auto member = m_object.find(name);
    if (member == m_object.end()) {
        return std::nullopt;
    }
    if (member->is_string()) {
        return member->get<std::string>();
    }
    if (member->is_boolean()) {
        return std::string(member->get<bool>() ? "1" : "0");
    }
    // A number has its JSON text: a whole number that is not negative has only digits, and the
    // number reader refuses each other form.
    if (member->is_number()) {
        return member->dump();
    }
    return std::nullopt;
}

service::HttpResponse message(int status, const std::string& text) {
    return json_reply(status, json{{"message", text}}.dump());
}

service::HttpResponse in_world(const Services& services, WorldCommand command) {
    world::CallStatus status = world::CallStatus::done;
    auto reply = services.world.call<service::HttpResponse>(std::move(command), kWorldCallLimit, &status);
    if (reply) {
        return *reply;
    }
    return status == world::CallStatus::notStarted
               ? message(503, "the world did not start the command: nothing changed")
               : message(500, "the command stopped with an error: the state is not known");
}

json item_json(const world::ItemInstance& item, std::uint32_t slot) {
    return {
        {"slot", slot},
        {"item", item.itemId},
        {"index", item.index},
        {"quantity", item.quantity},
        {"gearScore", item.gearScore},
        {"durability", item.durability},
        {"maxDurability", item.maxDurability},
        {"bound", (item.flags & world::kItemBound) != 0},
        {"locked", (item.flags & world::kItemLocked) != 0},
        {"instance", world::instance_text(item.instance)},
    };
}

service::HttpResponse item_reply(const world::ItemResult& result) {
    if (!result.ok()) {
        const int status = result.error == world::ItemError::unknownCharacter ? 404 : 400;
        return json_reply(status, json{{"message", world::item_error_text(result.error)}}.dump());
    }
    return json_reply(200,
                      json{{"message", "done"},
                           {"applied", result.inWorld ? "world" : "stored"},
                           {"slots", result.slots},
                           {"equipmentSlots", result.equipmentSlots}}
                          .dump());
}

service::HttpResponse progress_reply(const world::ProgressResult& result) {
    if (!result.ok()) {
        return json_reply(status_of(result.error), json{{"message", world::progress_error_text(result.error)}}.dump());
    }
    return json_reply(200,
                      json{{"message", "done"},
                           {"applied", result.inWorld ? "world" : "stored"},
                           {"level", result.level},
                           {"experience", result.experience},
                           {"freeAttributePoints", result.unspent},
                           {"levelsGained", result.levelsGained}}
                          .dump());
}

service::HttpResponse skill_reply(const world::SkillResult& result) {
    if (!result.ok()) {
        return json_reply(status_of(result.error), json{{"message", world::progress_error_text(result.error)}}.dump());
    }
    return json_reply(200,
                      json{{"message", "done"},
                           {"applied", result.inWorld ? "world" : "stored"},
                           {"rank", result.rank},
                           {"experience", result.points},
                           {"freePoints", result.freePoints}}
                          .dump());
}

WorldCommand give_item(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto item = arguments.text("item");
    // An argument that is not necessary: with no argument the default applies. An argument
    // that is there with a wrong form (a JSON array, object, or null) is a wrong request.
    const auto quantity = arguments.has("quantity") ? arguments.integer("quantity", 1, world::kGiveLimit)
                                                    : std::optional<std::uint64_t>(1);
    if (!item || item->empty() || !quantity) {
        refusal = message(400, "give item=ITEM and quantity=N (1 to 100000)");
        return {};
    }
    world::GiveOptions options;
    if (arguments.has("gearScore")) {
        const auto gearScore = arguments.integer("gearScore", 0, 65535);
        if (!gearScore) {
            refusal = message(400, "gearScore must be a number from 0 to 65535");
            return {};
        }
        options.gearScore = static_cast<std::uint32_t>(*gearScore);
    }
    if (arguments.has("action")) {
        const auto action = arguments.integer("action", 0, world::sync::kLargest);
        if (!action) {
            refusal = message(400, "action must be a number from 0 to 63");
            return {};
        }
        options.action = static_cast<std::uint8_t>(*action);
    }
    return [character, item = *item, quantity = static_cast<std::uint32_t>(*quantity), options](world::World& target) {
        return item_reply(target.inventory().give(character, item, quantity, options));
    };
}

WorldCommand remove_item(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto slot = arguments.integer("slot", 0, world::kMaxContainerSlots - 1);
    // With no quantity the command removes the full stack. A quantity with a wrong form must
    // not have that effect.
    std::optional<std::uint32_t> quantity;
    if (arguments.has("quantity")) {
        const auto value = arguments.integer("quantity", 1, world::kMaxItemQuantity);
        if (!value) {
            refusal = message(400, "quantity must be a number from 1 to 10000");
            return {};
        }
        quantity = static_cast<std::uint32_t>(*value);
    }
    if (!slot) {
        refusal = message(400, "give slot=N (0 to 499)");
        return {};
    }
    return [character, slot = static_cast<std::uint32_t>(*slot), quantity](world::World& target) {
        return item_reply(target.inventory().remove(character, slot, quantity));
    };
}

WorldCommand set_coin(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto amount = arguments.integer("amount", 0, world::kWalletLimit);
    if (!amount) {
        refusal = message(400, "give amount=N: hundredths of a coin, 0 to 100000000");
        return {};
    }
    return [character, amount = *amount](world::World& target) {
        return item_reply(target.inventory().set_coin(character, amount));
    };
}

WorldCommand set_level(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto level = arguments.integer("level", 1, 1000);
    if (!level) {
        refusal = message(400, "give level=N: the level of the screen (1 is the first level)");
        return {};
    }
    return [character, level = static_cast<std::uint32_t>(*level)](world::World& target) {
        return progress_reply(target.progress().set_level(character, level));
    };
}

WorldCommand add_experience(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto amount = arguments.integer("amount", 1, world::kExperienceLimit);
    if (!amount) {
        refusal = message(400, "give amount=N (1 to 10000000)");
        return {};
    }
    return [character, amount = *amount](world::World& target) {
        return progress_reply(target.progress().add_experience(character, amount));
    };
}

WorldCommand set_attribute(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto name = arguments.text("attribute");
    const auto value = arguments.integer("value", 0, 100000);
    std::optional<unsigned> type;
    for (unsigned index = 0; name && index < world::attribute::kCount; ++index) {
        if (*name == kAttributeNames[index] || *name == std::to_string(index)) {
            type = index;
        }
    }
    if (!type || !value) {
        refusal = message(400, "give attribute=NAME (constitution, focus, strength, dexterity, intelligence) and value=N");
        return {};
    }
    return [character, type = *type, value = static_cast<std::uint32_t>(*value)](world::World& target) {
        return progress_reply(target.progress().set_attribute(character, type, value));
    };
}

WorldCommand set_weapon_mastery(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    return set_rank(character, arguments, refusal, gamedata::ProgressionKind::weapon, "weapon");
}

WorldCommand set_trade_skill(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    return set_rank(character, arguments, refusal, gamedata::ProgressionKind::tradeSkill, "skill");
}

WorldCommand add_skill_experience(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto name = arguments.text("id");
    const auto amount = arguments.integer("amount", 1, world::kSkillExperienceLimit);
    if (!name || name->empty() || !amount) {
        refusal = message(400, "give id=NAME (a weapon or a trade skill) and amount=N (1 to 10000000)");
        return {};
    }
    return [character, name = *name, amount = *amount](world::World& target) {
        const std::string id = progression_name(target.options().progressionTables, name);
        return skill_reply(target.skills().add_experience(character, id, amount));
    };
}

std::optional<json> items_view(world::World& target, const std::string& character) {
    const auto holdings = target.inventory().holdings(character);
    if (!holdings) {
        return std::nullopt;
    }
    json items = json::array();
    for (std::size_t slot = 0; slot < holdings->bag.slots.size(); ++slot) {
        if (holdings->bag.slots[slot].item) {
            items.push_back(item_json(*holdings->bag.slots[slot].item, static_cast<std::uint32_t>(slot)));
        }
    }
    // The equipment: the slot of an item is its slot number of the paperdoll.
    json equipment = json::array();
    for (const auto& [slot, content] : holdings->equipment.slots) {
        if (content.item) {
            equipment.push_back(item_json(*content.item, slot));
        }
    }
    return json{{"character", character},
                {"inWorld", holdings->inWorld},
                {"slots", holdings->bag.slots.size()},
                {"coin", holdings->wallet.coin},
                {"items", items},
                {"equipment", equipment}};
}

std::optional<json> progress_view(world::World& target, const std::string& character) {
    const auto progress = target.progress().view(character);
    const auto skills = target.skills().view(character);
    if (!progress || !skills) {
        return std::nullopt;
    }
    const gamedata::ProgressionTables* tables = target.options().progressionTables;
    const bool loaded = tables != nullptr && tables->loaded();
    json attributes = json::object();
    for (unsigned type = 0; type < world::attribute::kCount; ++type) {
        attributes[kAttributeNames[type]] = {{"value", progress->attributes.value(type)},
                                             {"spent", progress->attributes.spent[type]}};
    }
    const auto node_name = [&](std::uint32_t id) {
        const gamedata::AbilityNode* node = loaded ? tables->node(id) : nullptr;
        char text[16];
        std::snprintf(text, sizeof(text), "%08X", id);
        return node != nullptr ? node->id : std::string(text);
    };
    json weapons = json::array();
    json tradeSkills = json::array();
    if (loaded) {
        for (const gamedata::ProgressionRow& row : tables->progressions()) {
            const auto found = skills->skills.progressions.find(row.wireId);
            const world::ProgressionRank value =
                found == skills->skills.progressions.end() ? world::ProgressionRank{} : found->second;
            const std::uint32_t next = value.rank < row.maxRank && value.rank < row.ranks.experience.size()
                                           ? row.ranks.experience[value.rank]
                                           : 0;
            if (row.kind == gamedata::ProgressionKind::tradeSkill) {
                tradeSkills.push_back({{"id", row.id},
                                       {"level", value.rank},
                                       {"experience", value.points},
                                       {"nextLevel", next},
                                       {"maxLevel", row.maxRank}});
                continue;
            }
            json nodes = json::array();
            const auto pool = skills->skills.pools.find(row.wireId);
            if (pool != skills->skills.pools.end()) {
                for (const auto& [node, state] : pool->second) {
                    if (state.level != 0) {
                        nodes.push_back(node_name(node));
                    }
                }
            }
            json bar = json::array();
            const auto places = skills->skills.bars.find(row.wireId);
            for (std::uint32_t slot = 0; slot < world::kBarSlots; ++slot) {
                const std::uint32_t ability = places == skills->skills.bars.end() ? 0 : places->second[slot];
                bar.push_back(ability == 0 ? std::string{} : node_name(ability));
            }
            // The screen shows the rank of a weapon plus 1.
            weapons.push_back({{"id", row.id},
                               {"level", value.rank + 1},
                               {"experience", value.points},
                               {"nextLevel", next},
                               {"freePoints", skills->skills.free_points(row.wireId)},
                               {"spentPoints", skills->skills.spent_points(row.wireId)},
                               {"nodes", nodes},
                               {"bar", bar}});
        }
    }
    return json{{"character", character},
                {"inWorld", progress->inWorld},
                {"tables", loaded},
                {"level", progress->progression.level},
                {"experience", progress->progression.experience},
                {"levelExperience", progress->levelExperience},
                {"nextLevelExperience", progress->nextLevelExperience},
                {"attributes", attributes},
                {"freeAttributePoints", progress->attributes.unspent},
                {"health", progress->vitals.health},
                {"healthMax", progress->vitals.healthMax},
                {"weapons", weapons},
                {"tradeSkills", tradeSkills}};
}

} // namespace soulwarden::backend::server::http::commands
