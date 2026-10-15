#include <algorithm>
#include <chrono>
#include <map>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/perk_roll.h"

// The commands of the itemization (stage 1.3b: world/gear.h, world/consumables.h,
// world/runes.h, world/perk_roll.h), in the common layer of the development routes and of the
// admin routes (character_commands.h).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** Largest number of rolls of one perk roll command. */
constexpr std::uint64_t kLargestPerkRolls = 10000;

const char* load_name(std::uint8_t load) {
    switch (load) {
    case 0:
        return "light";
    case 1:
        return "medium";
    case 2:
        return "heavy";
    default:
        return "overburdened";
    }
}

} // namespace

WorldCommand set_item_perks(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto container = arguments.text("container");
    const auto slot = arguments.integer("slot", 0, 10000);
    const auto list = arguments.text("perks");
    world::PerkIds perks{};
    bool valid = slot.has_value() && list.has_value() && (!container || *container == "bag" || *container == "paperdoll");
    // PerkID texts with "," between them; an empty part is a place with no perk (the form of give_item).
    std::size_t place = 0;
    for (std::size_t start = 0; valid && list;) {
        const std::size_t end = std::min(list->find(',', start), list->size());
        if (place >= world::kPerkPlaces || end - start > 200) {
            valid = false;
            break;
        }
        perks[place++] = list->substr(start, end - start);
        if (end >= list->size()) {
            break;
        }
        start = end + 1;
    }
    if (!valid) {
        refusal = message(400, "give slot, perks (at most five PerkID texts with \",\" between them), and when necessary container "
                               "(bag or paperdoll)");
        return {};
    }
    const bool paperdoll = container && *container == "paperdoll";
    return [character, paperdoll, slot = static_cast<std::uint32_t>(*slot), perks](world::World& target) {
        const world::ItemResult result = target.inventory().set_perks(character, paperdoll, slot, perks);
        if (!result.ok()) {
            return message(result.error == world::ItemError::unknownCharacter ? 404 : 400, world::item_error_text(result.error));
        }
        return json_reply(200, json{{"message", "done"}, {"applied", result.inWorld ? "world" : "stored"}}.dump());
    };
}

WorldCommand use_item(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto container = arguments.text("container");
    const auto slot = arguments.integer("slot", 0, 10000);
    if (!slot || (container && *container != "bag" && *container != "paperdoll")) {
        refusal = message(400, "give slot (a slot of the bag, or a quick slot 14 to 19 with container=paperdoll) and when "
                               "necessary container (bag or paperdoll)");
        return {};
    }
    const bool paperdoll = container && *container == "paperdoll";
    return [character, paperdoll, slot = static_cast<std::uint32_t>(*slot)](world::World& target) {
        if (target.find_character(character) == nullptr) {
            return message(target.store().find_character(character) ? 409 : 404, "the character is not in the world");
        }
        const world::UseVerdict verdict = target.consumables().use(character, paperdoll, slot);
        log::write("world: command: use of the item in slot %u of the %s of character %s: %s", slot,
                   paperdoll ? "paperdoll" : "bag", character.c_str(), world::use_verdict_text(verdict));
        json reply = gear_json(target, character);
        reply["message"] = world::use_verdict_text(verdict);
        return json_reply(verdict == world::UseVerdict::used ? 200 : 409, reply.dump());
    };
}

WorldCommand set_rune_charge(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto percent = arguments.number("percent", 0.0F, 100.0F);
    if (!percent) {
        refusal = message(400, "give percent (0 to 100): the heart rune charge");
        return {};
    }
    return [character, percent = *percent](world::World& target) {
        if (!target.runes().set_charge(character, percent)) {
            return message(target.store().find_character(character) ? 409 : 404, "the character is not in the world");
        }
        log::write("world: command: the heart rune charge of character %s is %.0f percent", character.c_str(),
                   static_cast<double>(percent));
        json reply = gear_json(target, character);
        reply["message"] = "done";
        return json_reply(200, reply.dump());
    };
}

WorldCommand activate_rune(const std::string& character, const Arguments&, service::HttpResponse&) {
    return [character](world::World& target) {
        const world::RuneVerdict verdict = target.runes().activate_character(character);
        log::write("world: command: the heart rune of character %s: %s", character.c_str(), world::rune_verdict_text(verdict));
        if (verdict == world::RuneVerdict::noEntity) {
            return message(target.store().find_character(character) ? 409 : 404, "the character is not in the world");
        }
        json reply = gear_json(target, character);
        reply["message"] = world::rune_verdict_text(verdict);
        return json_reply(verdict == world::RuneVerdict::activated ? 200 : 409, reply.dump());
    };
}

WorldCommand roll_item_perks(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto item = arguments.text("item");
    const auto gearScore = arguments.has("gs") ? arguments.integer("gs", 0, 1000) : std::optional<std::uint64_t>(0);
    const auto level = arguments.has("level") ? arguments.integer("level", 1, 1000) : std::optional<std::uint64_t>(60);
    const auto count = arguments.has("count") ? arguments.integer("count", 1, kLargestPerkRolls) : std::optional<std::uint64_t>(1);
    if (!item || item->empty() || !gearScore || !level || !count) {
        refusal = message(400, "give item=ITEM_ID, and when necessary gs (the gear score; the standard of the item), level (the "
                               "level of the character, 1 to 1000; 60), count (1 to 10000 rolls)");
        return {};
    }
    return [character, item = *item, gearScore = static_cast<std::uint32_t>(*gearScore), level = static_cast<std::uint32_t>(*level),
            count = *count](world::World& target) {
        const gamedata::PerkData* perks = target.perk_data();
        const gamedata::ItemCatalog* catalog = target.options().itemCatalog;
        const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item) : nullptr;
        if (perks == nullptr || definition == nullptr) {
            return message(404, "the backend has no perk data, or the item catalog has no such item");
        }
        std::uint32_t score = gearScore;
        if (score == 0) {
            score = definition->gearScoreOverride != 0 ? definition->gearScoreOverride : definition->minGearScore;
        }
        std::map<std::string, std::uint64_t> perkCounts;
        std::map<std::size_t, std::uint64_t> sizes;
        json examples = json::array();
        for (std::uint64_t roll = 0; roll < count; ++roll) {
            world::PerkIds rolled = world::roll_perks(*perks, *definition, score, level, [&target] { return target.random_unit(); });
            perks->apply_swaps(rolled, definition->id, definition->itemClass);
            std::size_t size = 0;
            json list = json::array();
            for (const std::string& perk : rolled) {
                if (!perk.empty()) {
                    ++perkCounts[perk];
                    ++size;
                    list.push_back(perk);
                }
            }
            ++sizes[size];
            if (examples.size() < 10) {
                examples.push_back(list);
            }
        }
        json byPerk = json::object();
        for (const auto& [perk, times] : perkCounts) {
            byPerk[perk] = times;
        }
        json bySize = json::object();
        for (const auto& [size, times] : sizes) {
            bySize[std::to_string(size)] = times;
        }
        log::write("world: command: %llu perk roll(s) of %s at the gear score %u for the level %u%s",
                   static_cast<unsigned long long>(count), definition->id.c_str(), score, level,
                   character.empty() ? "" : (" (character " + character + ")").c_str());
        return json_reply(200, json{{"item", definition->id}, {"gearScore", score}, {"level", level}, {"rolls", count},
                                    {"perkCounts", bySize}, {"perks", byPerk}, {"examples", examples},
                                    {"maxPerks", perks->max_perks(definition->tier, score)}}
                                   .dump());
    };
}

json gear_json(world::World& target, const std::string& character) {
    json reply{{"inWorld", false}};
    world::Entity* entity = target.find_character(character);
    reply["runeCharge"] = target.runes().percent_of(character);
    if (entity == nullptr) {
        return reply;
    }
    reply["inWorld"] = true;
    const world::WorldClock::time_point now = target.clock().now();
    json perks = json::array();
    for (const world::GearPerk& perk : target.gear().perks_of(*entity)) {
        perks.push_back({{"slot", perk.slot}, {"item", perk.itemId}, {"perk", perk.perk->id}, {"original", perk.original},
                         {"potency", perk.potency}, {"condition", perk.perk->condition}});
    }
    json bonus = json::object();
    const char* names[] = {"constitution", "focus", "strength", "dexterity", "intelligence"};
    for (unsigned type = 0; type < world::attribute::kCount; ++type) {
        bonus[names[type]] = entity->attributes.bonus[type];
    }
    json damage = json::object();
    for (const auto& [type, part] : entity->gear.damage) {
        damage[type] = part;
    }
    json absorption = json::object();
    for (const auto& [type, part] : entity->gear.absorption) {
        absorption[type] = part;
    }
    json effects = json::array();
    for (const world::PlayerEffect& effect : entity->effects) {
        if (effect.ends > now) {
            effects.push_back({{"status", effect.status},
                               {"seconds", std::chrono::duration<double>(effect.ends - now).count()},
                               {"potency", effect.potency},
                               {"hitsLeft", effect.hitsLeft}});
        }
    }
    json cooldowns = json::array();
    for (const world::ItemCooldown& cooldown : entity->itemCooldowns) {
        if (cooldown.end > now) {
            cooldowns.push_back({{"itemCrc", cooldown.item}, {"seconds", std::chrono::duration<double>(cooldown.end - now).count()}});
        }
    }
    const world::Combat& combat = target.combat();
    reply["perks"] = perks;
    reply["attributeBonus"] = bonus;
    reply["healthPart"] = entity->gear.healthPart;
    reply["baseDamage"] = entity->gear.baseDamage;
    reply["damage"] = damage;
    reply["physicalArmorPart"] = entity->gear.physicalArmor;
    reply["elementalArmorPart"] = entity->gear.elementalArmor;
    reply["armor"] = {{"physical", combat.armor_of(*entity, gamedata::DamageCategory::physical) * std::max(1.0F + entity->gear.physicalArmor, 0.0F)},
                      {"elemental", combat.armor_of(*entity, gamedata::DamageCategory::elemental) * std::max(1.0F + entity->gear.elementalArmor, 0.0F)}};
    reply["absorption"] = absorption;
    reply["damageReduction"] = entity->gear.damageReduction;
    reply["regeneration"] = entity->gear.regeneration;
    reply["equipLoad"] = entity->gear.equipLoad;
    reply["loadClass"] = load_name(entity->gear.loadClass);
    reply["effects"] = effects;
    reply["itemCooldowns"] = cooldowns;
    reply["health"] = entity->vitals.health;
    reply["healthMax"] = entity->vitals.healthMax;
    reply["mana"] = entity->vitals.mana;
    reply["manaMax"] = entity->vitals.manaMax;
    const world::GearCounts& gear = target.gear().counts();
    const world::RuneCounts& runes = target.runes().counts();
    reply["counts"] = {{"perkHits", gear.perkHits}, {"perkEffects", gear.perkEffects}, {"perkHeals", gear.perkHeals},
                       {"cooldownReductions", gear.cooldownReductions}, {"chainHits", gear.chainHits}, {"tickHits", gear.tickHits},
                       {"runeGains", runes.gains}, {"explosions", runes.explosions}, {"explosionHits", runes.explosionHits},
                       {"explosionsCancelled", runes.cancelled}, {"consumableTicks", target.consumables().counts().ticks},
                       {"consumableHeals", target.consumables().counts().heals}};
    return reply;
}

} // namespace soulwarden::backend::server::http::commands
