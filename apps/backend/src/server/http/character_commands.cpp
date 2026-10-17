#include "server/http/character_commands.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

#include "server/http/router.h"
#include "world/behavior.h"
#include "world/creatures.h"
#include "world/projectiles.h"
#include "world/stamina.h"

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
            // The value with its escapes %XX decoded (stage 3.6a: the key of a gathering node has "#" and "+"). A "+" stays a "+". A
            // malformed escape gives no value.
            const std::string raw = pair.substr(needle.size());
            std::string value;
            for (std::size_t index = 0; index < raw.size(); ++index) {
                if (raw[index] != '%') {
                    value += raw[index];
                    continue;
                }
                const auto digit = [](char letter) {
                    return letter >= '0' && letter <= '9'   ? letter - '0'
                           : letter >= 'a' && letter <= 'f' ? letter - 'a' + 10
                           : letter >= 'A' && letter <= 'F' ? letter - 'A' + 10
                                                            : -1;
                };
                if (index + 2 >= raw.size()) {
                    return std::nullopt;
                }
                const int high = digit(raw[index + 1]);
                const int low = digit(raw[index + 2]);
                if (high < 0 || low < 0) {
                    return std::nullopt;
                }
                value += static_cast<char>(high * 16 + low);
                index += 2;
            }
            return value;
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
        // The perks of the five places (stage 1.3b): the PerkID texts and the indexes on the wire.
        {"perks", item.perks},
        {"perkIndexes", item.perkIndex},
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
    if (arguments.has("perks")) {
        // The perks of the five places (stage 1.3b): PerkID texts with "," between them; an empty
        // part is a place with no perk ("PerkID_A,,PerkID_Gem_VoidDMG4"). The world checks each perk.
        const auto list = arguments.text("perks");
        world::PerkIds perks{};
        std::size_t place = 0;
        bool valid = list.has_value();
        if (valid) {
            std::size_t start = 0;
            while (valid) {
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
        }
        if (!valid) {
            refusal = message(400, "perks must be at most five PerkID texts with \",\" between them");
            return {};
        }
        options.perks = perks;
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

WorldCommand reset_cooldowns(const std::string& character, const Arguments&, service::HttpResponse&) {
    return [character](world::World& target) {
        if (target.store().find_character(character) == std::nullopt) {
            return message(404, "the store has no character with this id");
        }
        const bool inWorld = target.abilities().reset_cooldowns(character);
        log::write("world: command: each cooldown of the abilities of character %s ended", character.c_str());
        return json_reply(200, json{{"message", "done"}, {"applied", inWorld ? "world" : "stored"}}.dump());
    };
}

WorldCommand cast_ability(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto place = arguments.integer("place", 0, world::kBarSlots - 1);
    const auto yaw = arguments.has("yaw") ? arguments.number("yaw", -360.0F, 360.0F) : std::optional<float>(0.0F);
    if (!place || !yaw) {
        refusal = message(400, "give place=N (0 to 2) and yaw=DEGREES (-360 to 360, from north to the east)");
        return {};
    }
    return [character, place = static_cast<unsigned>(*place), yaw = *yaw](world::World& target) {
        world::AbilityPress press;
        press.place = place;
        // The yaw of a press: radians from north, positive to the left (the camera of the input report).
        press.yaw = -yaw * 3.14159265358979F / 180.0F;
        const world::CastVerdict verdict = target.abilities().press_character(character, press);
        log::write("world: command: character %s presses place %u of its bar: %s", character.c_str(), place, world::cast_verdict_text(verdict));
        return json_reply(verdict == world::CastVerdict::accepted ? 200 : 409,
                          json{{"message", world::cast_verdict_text(verdict)}, {"applied", "world"}}.dump());
    };
}

WorldCommand shoot(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto yaw = arguments.has("yaw") ? arguments.number("yaw", -360.0F, 360.0F) : std::optional<float>(0.0F);
    const auto pitch = arguments.has("pitch") ? arguments.number("pitch", -89.0F, 89.0F) : std::optional<float>(0.0F);
    const auto heavy = arguments.has("heavy") ? arguments.integer("heavy", 0, 1) : std::optional<std::uint64_t>(0);
    if (!yaw || !pitch || !heavy) {
        refusal = message(400, "give yaw=DEGREES (-360 to 360, from north to the east), pitch=DEGREES (-89 to 89, up) and heavy=0 or 1");
        return {};
    }
    return [character, shot = world::ServerShot{*yaw, *pitch, *heavy == 1}](world::World& target) {
        const world::ShotResult result = target.projectiles().launch_character(character, shot);
        log::write("world: command: character %s shoots (yaw %.0f, pitch %.0f, %s): %s",
                   character.c_str(),
                   shot.yaw,
                   shot.pitch,
                   shot.heavy ? "heavy" : "light",
                   world::shot_verdict_text(result.verdict));
        return json_reply(result.verdict == world::ShotVerdict::accepted ? 200 : 409,
                          json{{"message", world::shot_verdict_text(result.verdict)}, {"projectile", result.projectile}, {"applied", "world"}}.dump());
    };
}

json basic_attacks_json(world::World& target) {
    const world::SequenceCounts& counts = target.combat().sequences().counts();
    json ends = json::object();
    for (std::size_t why = 0; why < world::kSequenceEndCount; ++why) {
        if (counts.ends[why] != 0) {
            ends[world::sequence_end_text(static_cast<world::SequenceEnd>(why))] = counts.ends[why];
        }
    }
    return {{"characters", target.combat().sequences().size()}, {"visits", counts.visits}, {"strikes", counts.strikes},
            {"alternates", counts.alternates}, {"staminaPayments", counts.staminaPayments}, {"manaPayments", counts.manaPayments},
            {"paymentsWithCondition", counts.paymentsWithCondition}, {"delayedEvents", counts.delayedEvents}, {"ends", ends}};
}

json projectiles_json(world::World& target) {
    json flying = json::array();
    for (const world::ProjectileView& view : target.projectiles().active()) {
        const world::Entity* caster = target.find_character(view.characterId);
        flying.push_back({{"number", view.number},
                          {"caster", caster != nullptr ? caster->name : view.characterId},
                          {"attack", view.attack},
                          {"row", view.row},
                          {"travelled", view.travelled},
                          {"age", view.age.count() / 1000.0},
                          {"position", {view.position.x, view.position.y, view.position.z}}});
    }
    const world::ProjectileCounts& counts = target.projectiles().counts();
    json shots = json::object();
    for (std::size_t verdict = 0; verdict < world::kShotVerdictCount; ++verdict) {
        if (counts.shots[verdict] != 0) {
            shots[world::shot_verdict_text(static_cast<world::ShotVerdict>(verdict))] = counts.shots[verdict];
        }
    }
    json ends = json::object();
    for (std::size_t end = 0; end < world::kProjectileEndCount; ++end) {
        if (counts.ends[end] != 0) {
            ends[world::projectile_end_text(static_cast<world::ProjectileEnd>(end))] = counts.ends[end];
        }
    }
    // The shots of the creatures (stage 3.4f-A).
    const auto id_text = [](world::EntityId id) {
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
        return std::string(text);
    };
    json creatureFlying = json::array();
    for (const world::Projectiles::FlightView& view : target.projectiles().creature_active()) {
        creatureFlying.push_back({{"number", view.number},
                                  {"creature", id_text(view.creature)},
                                  {"entity", id_text(view.entity)},
                                  {"attack", view.attack},
                                  {"travelled", view.travelled},
                                  {"age", view.age.count() / 1000.0},
                                  {"position", {view.position.x, view.position.y, view.position.z}}});
    }
    const world::CreatureShotCounts& shotCounts = target.projectiles().creature_counts();
    json creatureEnds = json::object();
    for (std::size_t end = 0; end < world::kCreatureShotEndCount; ++end) {
        if (shotCounts.ends[end] != 0) {
            creatureEnds[world::creature_shot_end_text(static_cast<world::CreatureShotEnd>(end))] = shotCounts.ends[end];
        }
    }
    const world::BehaviorCounts& behavior = target.creatures().behavior().counts();
    const json creatures{{"flying", creatureFlying},
                         {"holds", target.projectiles().creature_holds()},
                         {"shots", behavior.shots},
                         {"releases", behavior.releases},
                         {"releasesCancelled", behavior.releasesCancelled},
                         {"retreats", behavior.retreats},
                         {"launches", shotCounts.launches},
                         {"refused", shotCounts.refused},
                         {"ends", creatureEnds},
                         {"damage", shotCounts.damage},
                         {"evaded", shotCounts.evaded},
                         {"steps", shotCounts.steps},
                         {"looked", shotCounts.looked},
                         {"wallTests", shotCounts.wallTests},
                         {"largest", shotCounts.largest}};
    const world::NavigationCounts& navigation = target.navigation().counts();
    return json{{"data", target.projectiles().has_data()},
                {"creatures", creatures},
                {"flying", flying},
                {"shots", shots},
                {"ends", ends},
                {"damage", counts.damage},
                {"steps", counts.steps},
                {"looked", counts.looked},
                {"largest", counts.largest},
                {"ledger", target.projectiles().ledger_size()},
                {"lineTests", navigation.lineTests},
                {"lineCells", navigation.lineCells},
                {"lineStopped", navigation.lineStopped}};
}

json stamina_json(world::World& target) {
    const world::Stamina& stamina = target.stamina();
    const world::StaminaCounts& counts = stamina.counts();
    json dodges = json::object();
    for (std::size_t verdict = 0; verdict < world::kDodgeVerdictCount; ++verdict) {
        if (counts.dodges[verdict] != 0) {
            dodges[world::dodge_verdict_text(static_cast<world::DodgeVerdict>(verdict))] = counts.dodges[verdict];
        }
    }
    const world::WorldClock::time_point now = target.clock().now();
    using Seconds = std::chrono::duration<double>;
    json characters = json::array();
    target.for_each_character([&](const world::Entity& entity) {
        json item{{"character", entity.characterId},
                  {"stamina", entity.vitals.stamina},
                  {"maximum", entity.vitals.staminaMax},
                  {"exhausted", entity.vitals.exhausted},
                  {"regeneration", stamina.regeneration(entity)}};
        if (const std::optional<world::Stamina::DodgeView> dodge = stamina.dodge_of(entity.characterId)) {
            item["dodge"] = {{"fragment", dodge->fragment},
                             {"cost", dodge->cost},
                             {"age", Seconds(now - dodge->start).count()},
                             {"paid", dodge->paid},
                             {"immune", stamina.immune(entity, now)}};
        }
        characters.push_back(item);
    });
    return json{{"data", stamina.has_data()},
                {"dodges", dodges},
                {"payments", counts.payments},
                {"paymentsCancelled", counts.paymentsCancelled},
                {"gains", counts.gains},
                {"regenerationSteps", counts.regenerationSteps},
                {"emptied", counts.emptied},
                {"evaded", counts.evaded},
                {"characters", characters}};
}

WorldCommand end_spells(const std::string& character, const Arguments&, service::HttpResponse&) {
    return [character](world::World& target) {
        const world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(404, "the character is not in the world");
        }
        const std::size_t ended = target.spells().end_caster(entity->id, world::SpellEnd::command);
        log::write("world: command: %zu spell(s) of character %s end", ended, character.c_str());
        return json_reply(200, json{{"message", std::to_string(ended) + " spell(s) ended"}, {"applied", "world"}}.dump());
    };
}

json spells_json(world::World& target) {
    json spells = json::array();
    const world::WorldClock::time_point now = target.clock().now();
    using Seconds = std::chrono::duration<double>;
    for (const auto& [id, entity] : target.entities()) {
        if (entity.kind != world::EntityKind::spell) {
            continue;
        }
        char group[17];
        std::snprintf(group, sizeof(group), "%016llx", static_cast<unsigned long long>(id));
        const world::Entity* caster = target.find(entity.spell.caster);
        json item{{"entity", group},
                  {"spell", entity.spell.spellId},
                  {"role", world::spell_role_text(entity.spell.role)},
                  {"caster", caster != nullptr ? caster->name : std::string()},
                  {"age", Seconds(now - entity.spell.started).count()},
                  {"left", Seconds(entity.spell.ends - now).count()},
                  {"record", entity.spell.slice != nullptr},
                  {"ending", entity.spell.ending},
                  {"position", {entity.position.x, entity.position.y, entity.position.z}}};
        // Stage 3.3b-S2b: the health of a summon or a trap, the entity that an aura follows, and the friends that have its statuses.
        if (entity.spell.vitalsRow != nullptr) {
            item["health"] = {entity.vitals.health, entity.vitals.healthMax};
        }
        if (entity.spell.attached != 0) {
            const world::Entity* attached = target.find(entity.spell.attached);
            item["attached"] = attached != nullptr ? attached->name : std::string("gone");
        }
        if (entity.spell.role == world::SpellRole::aura) {
            json members = json::array();
            for (const world::EntityId member : target.spells().members(id)) {
                const world::Entity* friendly = target.find(member);
                members.push_back(friendly != nullptr ? friendly->name : std::string("gone"));
            }
            item["members"] = members;
        }
        spells.push_back(item);
    }
    const world::SpellCounts& counts = target.spells().counts();
    json ended = json::object();
    for (std::size_t cause = 0; cause < world::kSpellEndCount; ++cause) {
        ended[world::spell_end_text(static_cast<world::SpellEnd>(cause))] = counts.ended[cause];
    }
    return {{"data", target.spells().has_data()}, {"spells", spells}, {"made", counts.made}, {"ended", ended},
            {"phases", counts.phases}, {"phasesCancelled", counts.phasesCancelled}, {"dueTaken", counts.dueTaken},
            {"largest", counts.largest}, {"shots", counts.shots}, {"summonHits", counts.summonHits}, {"triggers", counts.triggers},
            {"trapHits", counts.trapHits}, {"auraGiven", counts.auraGiven}, {"auraTaken", counts.auraTaken}, {"looked", counts.looked}};
}

json abilities_json(world::World& target, const std::string& character) {
    json cooldowns = json::array();
    const world::WorldClock::time_point now = target.clock().now();
    if (const auto* timers = target.abilities().cooldowns_of(character)) {
        for (const auto& [ability, timer] : *timers) {
            // A held cooldown (stage 3.3b-S2b: the Pylon while it lives) has no end: "held" and no seconds.
            if (timer.indefinite || timer.end > now) {
                const gamedata::ProgressionTables* tables = target.options().progressionTables;
                const gamedata::AbilityNode* node = tables != nullptr ? tables->node(ability) : nullptr;
                cooldowns.push_back({{"ability", node != nullptr ? node->id : std::to_string(ability)},
                                     {"seconds", timer.indefinite ? 0.0 : std::chrono::duration<double>(timer.end - now).count()},
                                     {"held", timer.indefinite}});
            }
        }
    }
    const world::AbilityCounts& counts = target.abilities().counts();
    json presses = json::object();
    for (std::size_t verdict = 0; verdict < world::kCastVerdictCount; ++verdict) {
        presses[world::cast_verdict_text(static_cast<world::CastVerdict>(verdict))] = counts.presses[verdict];
    }
    return {{"loaded", target.abilities().has_data()},
            {"cooldowns", cooldowns},
            {"presses", presses},
            {"contactHits", counts.contactHits},
            {"areaHits", counts.areaHits},
            {"areaLooked", counts.areaLooked},
            {"shots", counts.shots},
            {"vortexes", counts.vortexes},
            {"effects", counts.effects},
            {"effectsRefused", counts.effectsRefused},
            {"blocked", counts.blocked},
            // Stage 3.3b-S4: the counters, and the forced movements of the creatures (the pulls and the pushes).
            {"counters", counts.counters},
            {"counterHits", counts.counterHits},
            {"forced",
             {{"moving", target.creatures().forced().moving_count()},
              {"started", target.creatures().forced().counts().started},
              {"replaced", target.creatures().forced().counts().replaced},
              {"refused", target.creatures().forced().counts().refused},
              {"steps", target.creatures().forced().counts().steps},
              {"blocked", target.creatures().forced().counts().blocked},
              {"largest", target.creatures().forced().counts().largest}}}};
}

WorldCommand assign_node(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto weapon = arguments.text("weapon");
    const auto node = arguments.text("node");
    if (!weapon || weapon->empty() || !node || node->empty()) {
        refusal = message(400, "give weapon=NAME and node=ID (the id text of a node of the tree)");
        return {};
    }
    return [character, weapon = *weapon, node = *node](world::World& target) {
        const gamedata::ProgressionTables* tables = target.options().progressionTables;
        const gamedata::ProgressionRow* row = tables != nullptr ? tables->progression(progression_name(tables, weapon)) : nullptr;
        const gamedata::AbilityNode* found = tables != nullptr ? tables->node(node) : nullptr;
        if (row == nullptr || found == nullptr) {
            return message(404, "the progression tables have no such weapon or node");
        }
        return skill_reply(target.skills().assign(character, row->wireId, found->wireId, 1));
    };
}

WorldCommand set_ability_bar(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto weapon = arguments.text("weapon");
    const auto slot = arguments.integer("slot", 0, world::kBarSlots - 1);
    const auto ability = arguments.text("ability");
    if (!weapon || weapon->empty() || !slot || !ability || ability->empty()) {
        refusal = message(400, "give weapon=NAME, slot=N (0 to 2), and ability=ID (or none)");
        return {};
    }
    return [character, weapon = *weapon, slot = static_cast<std::uint32_t>(*slot), ability = *ability](world::World& target) {
        const gamedata::ProgressionTables* tables = target.options().progressionTables;
        const gamedata::ProgressionRow* row = tables != nullptr ? tables->progression(progression_name(tables, weapon)) : nullptr;
        const gamedata::AbilityNode* found = tables != nullptr && ability != "none" ? tables->node(ability) : nullptr;
        if (row == nullptr || (ability != "none" && found == nullptr)) {
            return message(404, "the progression tables have no such weapon or node");
        }
        return skill_reply(target.skills().set_bar(character, row->wireId, slot, found != nullptr ? found->wireId : 0));
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

std::optional<json> ownership_view(world::World& target, const std::string& character) {
    const auto view = target.ownership().view(character);
    if (!view) {
        return std::nullopt;
    }
    json expansion = nullptr;
    if (view->expansion != nullptr) {
        expansion = {{"id", view->expansion->id},
                     {"maxLevel", view->expansion->maxLevel},
                     {"maxEquipGearScore", view->expansion->maxEquipGearScore},
                     {"maxCraftGearScore", view->expansion->maxCraftGearScore},
                     {"maxTradeSkillLevel", view->expansion->maxTradeSkillLevel},
                     {"entitlement", view->expansion->entitlement}};
    }
    return json{{"character", character},
                {"inWorld", view->inWorld},
                {"expansion", expansion},
                {"entitlements", view->entitlements},
                {"achievements", view->achievements},
                {"rewards", view->rewards}};
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
                                             {"spent", progress->attributes.spent[type]},
                                             {"bonus", progress->attributes.bonus[type]}};
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
            const std::uint32_t cap = tables->rank_cap(row);
            const std::uint32_t next = value.rank < cap && value.rank < row.ranks.experience.size()
                                           ? row.ranks.experience[value.rank]
                                           : 0;
            if (row.kind == gamedata::ProgressionKind::tradeSkill) {
                tradeSkills.push_back({{"id", row.id},
                                       {"level", value.rank},
                                       {"experience", value.points},
                                       {"nextLevel", next},
                                       {"maxLevel", cap}});
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
