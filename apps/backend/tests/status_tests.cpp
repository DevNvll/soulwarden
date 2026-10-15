// Cases for the status effects of a player (stage 3.3b-S1, world/statuses.h): the data of format 3 (the status categories
// and the caster effect rows), the rules of an application (stacks, refresh, the maximum duration, DontReplaceStack,
// ApplicationCooldown), the end and the end status, the removal causes (a death, hits, a newer status of the source, a
// leave), a new session of the same character, the values with the limits of the categories (each value counts one
// time), the consumables and the heart rune through the one owner, the caster effects of a learned node, the wire bytes
// of the owner view and of the observer view and the clock pair of the Player state, two sessions (rule MP-10), the speed
// of the position check, the commands, and the load. All ids, names, and values are made up, with the id texts of the
// game where a rule of the backend names them.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using namespace shop;

/** Decodes the bytes of one state. The result has no field when the bytes do not decode to the last byte. */
Fragment decode_state(std::uint32_t type, const Bytes& data) {
    Fragment fragment;
    fragment.type = type;
    std::size_t position = 0;
    if (!detail::read_state(data, position, fragment) || position != data.size()) {
        fragment.fields.clear();
    }
    return fragment;
}

constexpr std::uint32_t kStatusWireSlot = 44;
constexpr std::uint32_t kStatusType = 4236;

/** The ability data of the cases: the rows of the itemization cases and the rows of the status rules (numbers in CRC order). */
gamedata::AbilityTables status_tables() {
    gamedata::AbilityTables tables = shop_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    rows.push_back(status("WarHammer_WreckingBall_Fortify", 7.0F,
                          cells({{"PhysicalArmor", 0.2F}, {"ElementalArmor", 0.2F}, {"DurationMax", 14.0F}, {"StackMax", 1.0F},
                                 {"ShowInUITray", 1.0F}, {"RemoveOnDeath", 1.0F}, {"TickRate", 1.0F}},
                                {{"EffectCategories", "Buff+ArmorFortify"}}),
                          true));
    rows.push_back(status("Status_Passive_Greataxe_Reaper_DmgOnLight", 5.0F,
                          cells({{"DMGSlash", 0.05F}, {"DurationMax", 10.0F}, {"StackMax", 3.0F}, {"ShowInUITray", 1.0F}, {"RemoveOnDeath", 1.0F}},
                                {{"EffectCategories", "Buff+Empower"}}),
                          true));
    rows.push_back(status("Test_Refresh", 4.0F, cells({{"RefreshDuration", 1.0F}, {"StackMax", 2.0F}, {"ShowInUITray", 1.0F}})));
    rows.push_back(status("Test_Haste_A", 10.0F, cells({}, {{"EffectCategories", "Haste"}})));
    rows.push_back(status("Test_Haste_B", 10.0F, cells({}, {{"EffectCategories", "Haste"}})));
    rows.push_back(status("Test_Free_Speed", 10.0F, cells({})));
    rows.push_back(status("Test_NoReplace", 10.0F, cells({{"StackMax", 2.0F}, {"DontReplaceStack", 1.0F}})));
    rows.push_back(status("Test_AppCooldown", 10.0F, cells({{"ApplicationCooldown", 1.0F}})));
    rows.push_back(status("Test_Hits", 30.0F, cells({}), false, 2));
    rows.push_back(status("Test_EndChain", 2.0F, cells({}, {{"OnEndStatusEffect", "Test_Ended"}})));
    rows.push_back(status("Test_Ended", 5.0F, cells({{"ShowInUITray", 1.0F}})));
    rows.push_back(status("Test_EndNoRemove", 2.0F, cells({{"DontApplyOnEndEffectOnRemove", 1.0F}}, {{"OnEndStatusEffect", "Test_Ended"}})));
    rows.push_back(status("Test_Public", 20.0F, cells({{"AlwaysReplicate", 1.0F}})));
    rows.push_back(status("Test_Private", 20.0F, cells({{"ShowInUITray", 1.0F}})));
    rows.push_back(status("Test_Plate", 20.0F, cells({{"ShowInUITray", 1.0F}}), true));
    rows.push_back(status("Test_Strong_Armor", 20.0F, cells({{"PhysicalArmor", 1.5F}}, {{"EffectCategories", "ArmorFortify"}})));
    tables.statusEffects = numbered(std::move(rows));
    for (gamedata::StatusRow& row : tables.statusEffects) {
        if (row.id == "Test_Haste_A" || row.id == "Test_Haste_B") {
            row.moveSpeed = 0.3F;
        } else if (row.id == "Test_Free_Speed") {
            row.moveSpeed = 0.1F;
        } else if (row.id == "Rune_Detonate_Major" || row.id == "Rune_Detonate_Dominator") {
            row.moveSpeed = 0.2F;
        }
    }
    // The caster effect rows of two learned nodes (the cells of the game data: research abilities-shared.md, Q6).
    gamedata::CellValues fortify = cells({{"OnHit", 1.0F}, {"InSequenceTime", 0.1F}},
                                         {{"DamageTableRow", "Warhammer_Damage_WreckingBall,Warhammer_Damage_WreckingBall_Push"},
                                          {"InSequence", "Ability_WarHammer_WreckingBall"},
                                          {"SelfApplyStatusEffect", "WarHammer_WreckingBall_Fortify"},
                                          {"WeaponTag", "Warhammer"},
                                          {"RequiredAbilityId", "Ability_WarHammer_WreckingBall"},
                                          {"CooldownId", "Ability_WarHammer_WreckingBall"}});
    gamedata::CellValues greed = cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}},
                                       {{"AttackType", "Light"}, {"SelfApplyStatusEffect", "Status_Passive_Greataxe_Reaper_DmgOnLight"},
                                        {"WeaponTag", "GreatAxe"}});
    tables.abilityRows = {{"Upgrade_WarHammer_Wreck_Fortify", fortify}, {"Passive_Greataxe_Reaper_DmgOnLight", greed}};
    return tables;
}

/** The progression tables of the cases with the two learned nodes of the caster effects. */
gamedata::ProgressionData status_progression() {
    gamedata::ProgressionData data = shop_progression();
    gamedata::AbilityNode fortify;
    fortify.id = "Upgrade_WarHammer_Wreck_Fortify";
    fortify.table = kWarHammer;
    fortify.column = 10;
    data.nodes.push_back(fortify);
    gamedata::AbilityNode greed;
    greed.id = "Passive_Greataxe_Reaper_DmgOnLight";
    greed.table = kGreatAxe;
    greed.column = 10;
    data.nodes.push_back(greed);
    return data;
}

/** The world of a case with the status data. */
struct StatusShop : Shop {
    explicit StatusShop(bool observe = false) : Shop(true, false, observe) {
        if (!host.abilities.set(status_tables())) {
            throw std::runtime_error("status test: the ability data is not valid: " + host.abilities.error());
        }
        host.progression.set(status_progression());
    }
    world::StatusVerdict give(const char* id, float seconds = 0.0F, float potency = 1.0F, world::Entity* target = nullptr) {
        world::StatusApply how;
        how.seconds = seconds;
        how.potency = potency;
        how.source = world::StatusSource{world::StatusSourceKind::command, "test", 0, 0};
        return world().statuses().apply(target != nullptr ? *target : *character(), id, how);
    }
    const world::ActiveStatus* find(const char* id, world::Entity* target = nullptr) {
        const world::Entity& entity = target != nullptr ? *target : *character();
        for (const world::ActiveStatus& status : entity.statuses) {
            if (status.status == id) {
                return &status;
            }
        }
        return nullptr;
    }
    float seconds_left(const char* id) {
        const world::ActiveStatus* status = find(id);
        return status == nullptr ? -1.0F : std::chrono::duration<float>(status->ends - host.clock.now()).count();
    }
};

/** @return The groups of a status state fragment (the bits of its group mask). */
std::uint8_t groups_of(const Fragment& fragment) {
    std::uint8_t mask = 0;
    for (const auto& [key, value] : fragment.fields) {
        mask = static_cast<std::uint8_t>(mask | (1U << key.first));
    }
    return mask;
}

/** ST1: the ability data of format 3: the status categories and the caster effect rows; the refusals of the loader. */
void data_format(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "status-ability-data.json";
    const nlohmann::json good{
        {"format", 3},
        {"statusRows", 2},
        {"counts", {{"cooldowns", 1}, {"statusEffects", 2}, {"spells", 0}, {"manaCosts", 0}, {"staminaCosts", 0}, {"abilityCosts", 0},
                    {"abilityRows", 1}, {"statusCategories", 1}}},
        {"cooldowns", {{"Test", 1, 0}}},
        // The numbers are the positions of the CRC values of the two ids in the unsigned order.
        {"statusEffects", {{"StatusA", gamedata::progression_id("statusa") < gamedata::progression_id("statusb") ? 0 : 1, 7, 0, false, false, 0, true, 0,
                            {{"PhysicalArmor", 0.2}}},
                           {"StatusB", gamedata::progression_id("statusa") < gamedata::progression_id("statusb") ? 1 : 0, 3, 0.2, false, false, 0, false, 0,
                            {{"EffectCategories", "Haste"}}}}},
        {"spells", nlohmann::json::array()},
        {"manaCosts", nlohmann::json::array()},
        {"staminaCosts", nlohmann::json::array()},
        {"abilityCosts", nlohmann::json::array()},
        {"abilityRows", nlohmann::json::array({nlohmann::json::array({"Upgrade_Test", {{"OnHit", 1}, {"SelfApplyStatusEffect", "StatusA"}}})})},
        {"statusCategories", nlohmann::json::array({nlohmann::json::array({"Haste", {{"MoveSpeedMod", 0.4}, {"SprintSpeedMod", 0.4}}})})}};
    const auto load = [&](const nlohmann::json& document, gamedata::AbilityData& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        return data.load(path.string());
    };
    gamedata::AbilityData data;
    check(load(good, data) && data.category("haste") != nullptr && data.category("Haste")->limits.at("MoveSpeedMod") == 0.4F
              && data.ability_row("upgrade_test") != nullptr && data.ability_row("Upgrade_Test")->cells.text("SelfApplyStatusEffect") == "StatusA",
          "a file of format 3 loads: the limits of a category and the cells of a caster effect row (the lookups ignore the case)");
    nlohmann::json bad = good;
    bad["abilityRows"][0][1]["SelfApplyStatusEffect"] = "NoSuchStatus";
    check(!load(bad, data), "a caster effect row that names a status that the data does not have: the file is refused");
    bad = good;
    bad["statusCategories"][0][1]["MoveSpeedMod"] = "fast";
    check(!load(bad, data), "a limit that is not a number: the file is refused");
    bad = good;
    bad["counts"]["statusCategories"] = 2;
    check(!load(bad, data), "a count that is not the size of its list: the file is refused");
    bad = good;
    bad["format"] = 2;
    bad.erase("abilityRows");
    bad.erase("statusCategories");
    bad["counts"].erase("abilityRows");
    bad["counts"].erase("statusCategories");
    check(load(bad, data) && data.tables().statusCategories.empty(), "a file of format 2 still loads, with no category and no caster row");
}

/** ST2: the rules of an application: a new status, the stacks, the refresh, the maximum duration, the later end, DontReplaceStack, ApplicationCooldown. */
void application_rules(const Options&) {
    StatusShop shop;
    check(shop.give("WarHammer_WreckingBall_Fortify") == world::StatusVerdict::applied && near(shop.seconds_left("WarHammer_WreckingBall_Fortify"), 7.0F, 0.05F)
              && shop.find("WarHammer_WreckingBall_Fortify")->stacks == 1 && shop.find("WarHammer_WreckingBall_Fortify")->tray,
          "a new status: BaseDuration 7 s, one stack, the tray mark of ShowInUITray");
    shop.pass(3.0F);
    check(shop.give("WarHammer_WreckingBall_Fortify") == world::StatusVerdict::renewed && near(shop.seconds_left("WarHammer_WreckingBall_Fortify"), 11.0F, 0.1F)
              && shop.find("WarHammer_WreckingBall_Fortify")->stacks == 1,
          "the same status 3 s later: DurationMax 14 above the duration 7: the time adds up (4 + 7 = 11 s left), StackMax 1");
    (void)shop.give("WarHammer_WreckingBall_Fortify");
    check(near(shop.seconds_left("WarHammer_WreckingBall_Fortify"), 14.0F, 0.1F), "again: not more than DurationMax from now (14 s)");
    for (int index = 0; index < 4; ++index) {
        (void)shop.give("Status_Passive_Greataxe_Reaper_DmgOnLight");
    }
    check(shop.find("Status_Passive_Greataxe_Reaper_DmgOnLight")->stacks == 3, "four applications of a row with StackMax 3: three stacks");
    (void)shop.give("Test_Refresh");
    shop.pass(3.0F);
    (void)shop.give("Test_Refresh");
    check(near(shop.seconds_left("Test_Refresh"), 4.0F, 0.1F) && shop.find("Test_Refresh")->stacks == 2,
          "RefreshDuration: a new application starts the time again (4 s), and adds a stack");
    (void)shop.give("Test_Free_Speed", 9.0F);
    shop.pass(1.0F);
    (void)shop.give("Test_Free_Speed", 2.0F);
    check(near(shop.seconds_left("Test_Free_Speed"), 8.0F, 0.1F), "no RefreshDuration and no DurationMax: the later end stays (8 s, not 2 s)");
    check(shop.give("Test_NoReplace") == world::StatusVerdict::applied && shop.give("Test_NoReplace") == world::StatusVerdict::renewed
              && shop.give("Test_NoReplace") == world::StatusVerdict::full,
          "DontReplaceStack: at StackMax 2 a new application changes nothing");
    check(shop.give("Test_AppCooldown") == world::StatusVerdict::applied && shop.give("Test_AppCooldown") == world::StatusVerdict::cooldown,
          "ApplicationCooldown 1 s: a second application in that time is refused");
    shop.pass(1.1F);
    check(shop.give("Test_AppCooldown") == world::StatusVerdict::renewed, "after the ApplicationCooldown the application is taken");
    shop.character()->vitals.health = 100.0F;
    check(shop.give("PotionHealT5") == world::StatusVerdict::instant && shop.character()->vitals.health > 100.0F && shop.find("PotionHealT5") == nullptr,
          "a row with no duration: one heal now, and no status stays");
    check(shop.give("No_Such_Status") == world::StatusVerdict::unknown, "a status that the data does not have: nothing changes");
}

/** ST3: the end on the world clock and the end status; the removal causes; a new session; a restart of the world has none. */
void end_rules(const Options&) {
    StatusShop shop;
    (void)shop.give("Test_EndChain");
    shop.pass(2.2F);
    check(shop.find("Test_EndChain") == nullptr && shop.find("Test_Ended") != nullptr,
          "at its end on the world clock the status goes away, and its OnEndStatusEffect comes on the character");
    (void)shop.world().statuses().remove(*shop.character(), [](const world::ActiveStatus&) { return true; }, world::StatusEnd::command);
    (void)shop.give("Test_EndNoRemove");
    (void)shop.world().statuses().remove(*shop.character(), [](const world::ActiveStatus& status) { return status.status == "Test_EndNoRemove"; },
                                         world::StatusEnd::command);
    check(shop.find("Test_EndNoRemove") == nullptr && shop.find("Test_Ended") == nullptr,
          "a removal before the end of a row with DontApplyOnEndEffectOnRemove: no end status");
    (void)shop.give("Test_EndChain");
    (void)shop.world().statuses().remove(*shop.character(), [](const world::ActiveStatus& status) { return status.status == "Test_EndChain"; },
                                         world::StatusEnd::command);
    check(shop.find("Test_Ended") != nullptr, "a removal of a row with no DontApplyOnEndEffectOnRemove: the end status comes");
    (void)shop.give("Test_Hits");
    shop.world().statuses().hit_taken(*shop.character());
    const bool afterOne = shop.find("Test_Hits") != nullptr;
    shop.world().statuses().hit_taken(*shop.character());
    check(afterOne && shop.find("Test_Hits") == nullptr, "StopOnHitCount 2: the status ends at the second hit that the character takes");
    (void)shop.give("WarHammer_WreckingBall_Fortify");
    (void)shop.give("Test_Public");
    (void)shop.world().health().hurt(kCharacterA, 1.0e7F);
    check(shop.character()->life.dead && shop.find("WarHammer_WreckingBall_Fortify") == nullptr && shop.find("Test_Public") != nullptr,
          "a death ends the statuses with RemoveOnDeath, and the other statuses stay");
    check(shop.give("Test_Private") == world::StatusVerdict::dead, "a dead character takes no status");
    (void)shop.world().health().heal(kCharacterA);
    // A new session of the same character: the statuses are values of the character.
    const float left = shop.seconds_left("Test_Public");
    check(shop.world().leave(shop.character()->owner), "setup: the character leaves the world");
    shop.host.clock.advance(std::chrono::seconds(2));
    shop.host.tick();
    check(shop.world().spawn_character(kCharacterA, kPlace) == world::SpawnError::none, "setup: the character is in the world again");
    check(shop.find("Test_Public") != nullptr && near(shop.seconds_left("Test_Public"), left - 2.0F, 0.1F),
          "a new entity of the character has its statuses, with the time that ran while it was not in the world");
    // A restart of the backend: a new world object with the same database has no status (a rule of this project).
    world::World fresh(shop.host.clock, shop.host.store, shop.host.tables, shop.world().options());
    check(fresh.spawn_character(kCharacterA, kPlace) == world::SpawnError::none && fresh.find_character(kCharacterA)->statuses.empty(),
          "a new world (a restart) has no status of the character: the statuses are not in the database");
}

/** ST4: the values of the statuses with the limits of their categories; each value counts one time in the gear summary. */
void status_values(const Options&) {
    StatusShop shop;
    world::Entity& own = *shop.character();
    const float baseArmor = own.gear.physicalArmor;
    (void)shop.give("Test_Haste_A");
    check(near(own.gear.moveSpeed, 0.3F), "a haste of 0.3 (category Haste): the speed part 0.3");
    (void)shop.give("Test_Haste_B");
    check(near(own.gear.moveSpeed, 0.4F), "two hastes of 0.3: the sum is not above the limit of the category Haste (0.4)");
    (void)shop.give("Test_Free_Speed");
    check(near(own.gear.moveSpeed, 0.5F), "a speed with no category adds to the limited sum (0.4 + 0.1)");
    (void)shop.give("WarHammer_WreckingBall_Fortify");
    check(near(own.gear.physicalArmor, baseArmor + 0.2F) && near(own.gear.elementalArmor, 0.2F),
          "the armor of a fortify: PhysicalArmor and ElementalArmor 0.2, one time in the gear summary");
    (void)shop.give("Test_Strong_Armor");
    check(near(own.gear.physicalArmor, baseArmor + 1.7F), "two statuses of ArmorFortify: 0.2 + 1.5 (below the limit 2.0 of the category)");
    (void)shop.give("Test_Strong_Armor", 20.0F);
    (void)shop.give("FoodCONT5");
    check(own.attributes.bonus[world::attribute::kConstitution] == 10, "the attribute of a food: MODConstitution 10 in the attribute bonus");
    for (int index = 0; index < 3; ++index) {
        (void)shop.give("Status_Passive_Greataxe_Reaper_DmgOnLight");
    }
    check(near(own.gear.damage["Slash"], 0.15F), "the damage of three stacks: DMGSlash 0.05 times 3");
    // One owner: the gear summary is the values of the gear and of the statuses, not a second copy.
    const world::StatusValues values = shop.world().statuses().values(own);
    check(near(values.physicalArmor, own.gear.physicalArmor - baseArmor) && near(values.moveSpeed, own.gear.moveSpeed),
          "the status part of the summary is the value of the status system");
    shop.pass(10.5F);
    check(near(own.gear.moveSpeed, 0.0F) && near(own.gear.physicalArmor, baseArmor + 1.5F),
          "at their ends the values go away from the summary (the hastes and the fortify; the second armor status runs)");
}

/** ST5: the consumables and the heart rune use the one owner; a regeneration potion heals at its ticks; the gear status. */
void users_of_the_owner(const Options&) {
    StatusShop shop;
    world::Entity& own = *shop.character();
    world::GiveOptions options;
    (void)shop.world().inventory().give(kCharacterA, "TestFood", 2, options);
    (void)shop.world().consumables().use(kCharacterA, false, 0);
    const world::ActiveStatus* food = shop.find("FoodCONT5");
    check(food != nullptr && food->source.kind == world::StatusSourceKind::consumable && food->source.id == "TestFood"
              && own.attributes.bonus[world::attribute::kConstitution] == 10,
          "a food: its status is a status of the status system, with the source of the item");
    (void)shop.world().inventory().give(kCharacterA, "TestRegenPotion", 1, options);
    std::uint32_t slot = 0;
    for (std::uint32_t index = 0; index < own.bag.slots.size(); ++index) {
        if (own.bag.slots[index].item && own.bag.slots[index].item->itemId == "TestRegenPotion") {
            slot = index;
        }
    }
    own.vitals.health = 100.0F;
    const world::UseVerdict used = shop.world().consumables().use(kCharacterA, false, slot);
    shop.pass(0.1F);
    const float afterUse = own.vitals.health;
    shop.pass(3.0F);
    note(std::string("the regeneration potion: ") + world::use_verdict_text(used) + ", health 100 then " + std::to_string(afterUse) + " then "
         + std::to_string(own.vitals.health));
    check(used == world::UseVerdict::used && afterUse > 100.0F && own.vitals.health > afterUse && shop.find("GrandPotionRegen") != nullptr,
          "a regeneration potion: a heal at the first tick of the world after the use and at each tick (TickRate 1) of its status");
    shop.pass(3.0F);
    const std::uint64_t ticks = shop.world().statuses().counts().ticks;
    shop.pass(2.0F);
    check(shop.find("GrandPotionRegen") == nullptr && shop.world().statuses().counts().ticks == ticks && ticks == 5,
          "the status of the potion ends after its time (5 s): five ticks (0 to 4 s), and no tick after its end");
    // The heart rune: the status of the activation is a status of the status system; the end status casts the explosion.
    check(shop.wear("TestRune", world::paperdoll::kHeartRune).ok(), "setup: the heart rune");
    (void)shop.world().runes().set_charge(kCharacterA, 100.0F);
    check(shop.world().runes().activate_character(kCharacterA) == world::RuneVerdict::activated, "setup: an activation");
    const world::ActiveStatus* rune = nullptr;
    for (const world::ActiveStatus& status : own.statuses) {
        if (status.source.kind == world::StatusSourceKind::rune) {
            rune = &status;
        }
    }
    check(rune != nullptr && rune->status == "Rune_Detonate_Dominator" && near(own.gear.moveSpeed, 0.2F),
          "the status of the variant (Dominator) is a status with the source rune; its haste is the speed part 0.2");
    const world::EntityId wolf = shop.wolf(2)->id;
    const float before = shop.world().creatures().find(wolf)->vitals.health;
    shop.pass(3.3F);
    check(shop.world().runes().counts().explosions == 1 && shop.world().creatures().find(wolf)->vitals.health < before
              && near(own.gear.moveSpeed, 0.0F),
          "at the end of the status: its end status Rune_Detonate_CastAoE casts Rune_Detonate_AoE, the explosion; the haste goes away");
}

/** ST6: the wire: the owner view, the observer view, the first record, the removal, the clock pair of the Player state. */
void wire_bytes(const Options&) {
    ManualClock clock;
    world::Entity entity;
    entity.kind = world::EntityKind::player;
    entity.changed[world::ComponentKey::status] = 5;
    world::ActiveStatus privateStatus;
    privateStatus.status = "Test_Private";
    privateStatus.index = 0x0123;
    privateStatus.trayKey = gamedata::progression_id("test_private");
    privateStatus.stacks = 2;
    privateStatus.ends = clock.now() + std::chrono::seconds(30);
    privateStatus.tray = true;
    world::ActiveStatus publicStatus;
    publicStatus.status = "Test_Public";
    publicStatus.index = 0x0456;
    publicStatus.potency = 1.5F;
    publicStatus.ends = clock.now() + std::chrono::seconds(20);
    publicStatus.observed = true;
    publicStatus.nameplate = true;
    world::ActiveStatus gearStatus;
    gearStatus.status = "Test_Gear";
    gearStatus.index = 0x0789;
    gearStatus.permanent = true;
    gearStatus.replicated = false;
    entity.statuses = {privateStatus, publicStatus, gearStatus};
    const server::rep::PartsRequest request{0, 900, &clock};
    const auto owner = server::rep::encode_player_status_state(entity, server::rep::View::owner, request);
    const auto observer = server::rep::encode_player_status_state(entity, server::rep::View::observer, request);
    check(owner.size() == 1
              && hex_text(owner[0])
                     == "020700010502012318dae909c4dd2c003c000200045618dae90770d148003e0001000001050000010500",
          "owner view: group 1, the full map (index, wall end, potency f16, stacks, flags 0) of each status on the wire, an empty "
          "lightweight map, and an empty tray (the full map gives the icon: live run s1-live); no status of the gear (the bytes of "
          "an independent encoder)");
    check(observer.size() == 1 && hex_text(observer[0]) == "080300010501045618dae90770d148003e00010000010501045681",
          "observer view: group 3, the full map and the compact map of the public status only: no tray, no owner field");
    check(gamedata::progression_id("rune_detonate_major") == 0x41891F91U,
          "the tray key of Rune_Detonate_Major is 41891F91 (live check L1: the icon showed with this key)");
    world::Entity empty;
    empty.kind = world::EntityKind::player;
    check(server::rep::encode_player_status_state(empty, server::rep::View::owner, request).empty()
              && server::rep::encode_player_status_state(empty, server::rep::View::observer, request).empty(),
          "a first record of a character with no status has no status fragment");
    empty.changed[world::ComponentKey::status] = 9;
    const auto removal = server::rep::encode_player_status_state(empty, server::rep::View::owner, server::rep::PartsRequest{3, 900, &clock});
    check(removal.size() == 1 && hex_text(removal[0]) == "02070001090000010900000109" "00",
          "a later state with no status: three empty lists with the newer stamp (the client removes each entry)");
    const server::hub::Bytes player = server::rep::encode_player_state(entity, server::rep::View::owner, clock);
    const Fragment decoded = decode_state(server::rep::kTypePlayerComponentState, player);
    check(decoded.fields.count({1, 6}) == 1 && hex_text(decoded.fields.at({1, 6})) == "18dae902c8b98000"
              && decoded.fields.count({1, 7}) == 1 && hex_text(decoded.fields.at({1, 7})) == "00038d7ea4c68000",
          "the Player state of the owner: sessionStartWallClockTimePoint (Unix ns of the world clock) and sessionStartTimePoint "
          "(the server ns) from one sample");
    const Fragment remote = decode_state(server::rep::kTypePlayerComponentState,
                                         server::rep::encode_player_state(entity, server::rep::View::observer, clock));
    check(remote.fields.count({1, 6}) == 0 && remote.fields.count({1, 7}) == 0, "the Player state of an observer has no field of group 1");
}

/** ST7: two sessions (rule MP-10): the owner gets its full map and an empty tray, the observer only the public maps; a status of A does not change B. */
void two_sessions(const Options&) {
    StatusShop shop(true);
    shop.enter_b();
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    (void)shop.give("Test_Private");
    (void)shop.give("Test_Plate");
    shop.walker.idle(3);
    for (int index = 0; index < 3; ++index) {
        shop.host.clock.advance(kTick);
        shop.host.tick();
        shop.b.exchange(shop.host);
    }
    const std::vector<Fragment> ownFragments = fragments_of(shop.a, kStatusWireSlot, kStatusType, fromA);
    const std::vector<Fragment> seenFragments = fragments_of(shop.b, kStatusWireSlot, kStatusType, fromB);
    bool ownTray = false;
    for (const Fragment& fragment : ownFragments) {
        if (fragment.fields.count({1, 0}) == 1 && fragment.fields.count({1, 2}) == 1) {
            server::hub::Reader map(fragment.fields.at({1, 0}));
            server::hub::Reader tray(fragment.fields.at({1, 2}));
            const auto mapCount = progress_wire::read_full_form(map);
            const auto trayCount = progress_wire::read_full_form(tray);
            ownTray = ownTray || (mapCount && *mapCount == 2 && trayCount && *trayCount == 0);
        }
    }
    bool seenPrivate = false;
    bool seenPlate = false;
    for (const Fragment& fragment : seenFragments) {
        seenPrivate = seenPrivate || (groups_of(fragment) & 0x02U) != 0;
        if (fragment.fields.count({3, 1}) == 1) {
            server::hub::Reader reader(fragment.fields.at({3, 1}));
            const auto count = progress_wire::read_full_form(reader);
            seenPlate = seenPlate || (count && *count == 1);
        }
    }
    check(!ownFragments.empty() && ownTray, "A (the owner) gets the status state with its full map (two statuses) and the empty tray");
    check(!seenFragments.empty() && !seenPrivate && seenPlate,
          "B (an observer of A) gets the status state of A with group 3 only: the compact map has the status with ShowInNameplates; no tray, no owner map");
    const world::Entity* beta = shop.world().find_character(kCharacterB);
    check(beta != nullptr && beta->statuses.empty() && near(beta->gear.moveSpeed, 0.0F), "the statuses of A are not values of B");
    (void)shop.give("Test_Haste_A", 0.0F, 1.0F, shop.world().find_character(kCharacterB));
    check(near(shop.world().find_character(kCharacterB)->gear.moveSpeed, 0.3F) && near(shop.character()->gear.moveSpeed, 0.0F),
          "a haste of B changes the speed of B, not of A");
}

/** ST8: the caster effects of the learned nodes: Safety Measures on a hit of Wrecking Ball (InSequence), Greed on a light hit. */
void caster_effects(const Options&) {
    StatusShop shop;
    world::Entity& own = *shop.character();
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1).ok(), "setup: the hammer is in the hand");
    own.skills.pools[kWarHammer][gamedata::progression_id("Upgrade_WarHammer_Wreck_Fortify")].level = 1;
    world::Entity& wolf = *shop.wolf(2);
    const gamedata::WeaponRow* hammer = shop.host.combat.weapon_of("TestHammer");
    const world::HitWeapon light{"TestHammer", world::paperdoll::kMainHand1, hammer, 100, {}};
    (void)shop.world().combat().strike(own, wolf, light, *shop.host.combat.row_index("Test_Light"), 1.0F, "a test hit");
    check(shop.find("WarHammer_WreckingBall_Fortify") == nullptr, "a light attack of the hammer: no fortify (the row and InSequence do not pass)");
    const world::HitWeapon ball{"TestHammer", world::paperdoll::kMainHand1, hammer, 100, "Ability_WarHammer_WreckingBall"};
    (void)shop.world().combat().strike(own, wolf, ball, *shop.host.combat.row_index("Warhammer_Damage_WreckingBall"), 1.0F, "a test hit");
    const world::ActiveStatus* fortify = shop.find("WarHammer_WreckingBall_Fortify");
    check(fortify != nullptr && fortify->source.kind == world::StatusSourceKind::ability && fortify->source.id == "Upgrade_WarHammer_Wreck_Fortify"
              && near(own.gear.physicalArmor, 0.2F, 0.01F),
          "a hit of Wrecking Ball with Safety Measures: the fortify on the caster, a status of the status system with the source of the node");
    const world::HitWeapon wrongCast{"TestHammer", world::paperdoll::kMainHand1, hammer, 100, "Ability_WarHammer_Shockwave"};
    own.skills.pools[kWarHammer].clear();
    own.skills.pools[kWarHammer][gamedata::progression_id("Upgrade_WarHammer_Wreck_Fortify")].level = 0;
    (void)shop.world().combat().strike(own, wolf, wrongCast, *shop.host.combat.row_index("Warhammer_Damage_WreckingBall"), 1.0F, "a test hit");
    check(shop.world().gear().counts().casterEffects == 1, "a node that the character does not have gives nothing");
    // Greed: a light hit of the great axe, three stacks at most.
    check(shop.wear("TestAxe", world::paperdoll::kMainHand1 + 1).ok(), "setup: the axe in the second weapon slot");
    shop.world().combat().select_weapon(own.owner, 2);
    own.skills.pools[kGreatAxe][gamedata::progression_id("Passive_Greataxe_Reaper_DmgOnLight")].level = 1;
    const gamedata::WeaponRow* axe = shop.host.combat.weapon_of("TestAxe");
    const world::HitWeapon axeLight{"TestAxe", world::paperdoll::kMainHand1 + 1, axe, 100, {}};
    for (int index = 0; index < 4; ++index) {
        (void)shop.world().combat().strike(own, wolf, axeLight, *shop.host.combat.row_index("Test_Light"), 1.0F, "a test hit");
    }
    const world::ActiveStatus* greed = shop.find("Status_Passive_Greataxe_Reaper_DmgOnLight");
    check(greed != nullptr && greed->stacks == 3 && near(own.gear.damage["Slash"], 0.15F),
          "Greed: each light hit of the axe gives a stack of its empower, three at most (StackMax 3)");
    (void)shop.world().combat().strike(own, wolf, world::HitWeapon{"TestAxe", world::paperdoll::kMainHand1 + 1, axe, 100, {}},
                                       *shop.host.combat.row_index("Test_Heavy"), 1.0F, "a test hit");
    check(shop.world().gear().counts().casterEffects == 5, "a heavy hit gives no stack of Greed (AttackType Light)");
}

/** ST9: the speed of the statuses in the position check: a haste makes the window limit larger. */
void speed_check(const Options&) {
    using world::PositionVerdict;
    const auto run = [](float speedFactor) {
        world::PositionTrack track;
        const state::WorldPosition start = at(100.0F, 100.0F);
        auto time = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
        std::size_t accepted = 0;
        std::size_t refused = 0;
        // 11 m/s for 2 s, one report each 1/30 s: above the window limit of 8 m/s with its margin of 2 m.
        for (int step = 0; step <= 60; ++step) {
            const world::PositionReport report{static_cast<std::uint16_t>(10 + step), at(100.0F + 11.0F * static_cast<float>(step) / 30.0F, 100.0F),
                                               wire_height(kHeight)};
            const PositionVerdict verdict = track.judge(report, start, time, speedFactor);
            (verdict == PositionVerdict::accepted ? accepted : refused) += 1;
            time += std::chrono::microseconds(33333);
        }
        return std::make_pair(accepted, refused);
    };
    const auto plain = run(1.0F);
    const auto haste = run(1.4F);
    note("a run of 11 m/s: " + std::to_string(plain.second) + " report(s) refused with no haste, " + std::to_string(haste.second)
         + " with a haste of 0.4");
    check(plain.second > 0 && haste.second == 0, "the position check refuses a run of 11 m/s with no haste, and takes it with the haste of a status (factor 1.4)");
}

/** ST10: the commands of the development routes and of the admin interface (apply_status, remove_status, status_json). */
void commands(const Options&) {
    StatusShop shop;
    nlohmann::json arguments{{"status", "Test_Public"}, {"seconds", 12}, {"count", 2}};
    service::HttpResponse refusal;
    const server::http::commands::WorldCommand apply =
        server::http::commands::apply_status(kCharacterA, server::http::commands::JsonArguments(arguments), refusal);
    check(static_cast<bool>(apply), "setup: the arguments are correct");
    const service::HttpResponse reply = apply(shop.world());
    const nlohmann::json body = nlohmann::json::parse(reply.body, nullptr, false);
    check(reply.status == 200 && body.is_object() && body["statuses"].size() == 1 && body["statuses"][0]["status"] == "Test_Public"
              && near(body["statuses"][0]["seconds"].get<float>(), 12.0F, 0.1F),
          "apply_status: the status with the seconds of the command; the reply has the statuses of the character");
    nlohmann::json bad{{"status", "Test_Public"}, {"count", 99}};
    service::HttpResponse badRefusal;
    check(!server::http::commands::apply_status(kCharacterA, server::http::commands::JsonArguments(bad), badRefusal) && badRefusal.status == 400,
          "apply_status: a count above 25 is refused with 400");
    nlohmann::json remove{{"status", "all"}};
    const service::HttpResponse removed =
        server::http::commands::remove_status(kCharacterA, server::http::commands::JsonArguments(remove), refusal)(shop.world());
    check(removed.status == 200 && shop.character()->statuses.empty(), "remove_status all: each status (not of the gear) ends");
}

/** ST11: the load: the end and the ticks look only at the due list; 200 characters with statuses in a world of 4,000 creatures. */
void load(const Options&) {
    StatusShop shop;
    for (int index = 0; index < 4000; ++index) {
        (void)shop.world().creatures().spawn("Test_Wolf", at(5000.0F + static_cast<float>(index % 100) * 3.0F, 5000.0F + static_cast<float>(index / 100) * 3.0F), 0.0F);
    }
    std::vector<std::string> characters;
    for (int index = 0; index < 200; ++index) {
        char id[64];
        std::snprintf(id, sizeof(id), "dddddddd-3333-4000-8000-%012d", index);
        char steam[32];
        std::snprintf(steam, sizeof(steam), "765612022553%05d", 50000 + index);
        char name[32];
        std::snprintf(name, sizeof(name), "Status%c%c", 'a' + index / 26, 'a' + index % 26);
        const state::WorldPosition place = at(kPlace.x + static_cast<float>(index % 20) * 10.0F, kPlace.y + static_cast<float>(index / 20) * 10.0F);
        (void)add_player(shop.host.store, steam, "Load", id, name, place);
        (void)shop.world().spawn_character(id, place);
        characters.emplace_back(id);
    }
    const std::uint64_t dueBefore = shop.world().statuses().counts().dueTaken;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < characters.size(); ++index) {
        world::Entity* character = shop.world().find_character(characters[index]);
        (void)shop.give("WarHammer_WreckingBall_Fortify", 0.0F, 1.0F, character);
        (void)shop.give("Test_Haste_A", 1.0F + static_cast<float>(index % 5), 1.0F, character);
        (void)shop.give("GrandPotionRegen", 5.0F, 1.0F, character);
        (void)shop.give("FoodCONT5", 30.0F, 1.0F, character);
        (void)shop.give("Test_EndChain", 0.0F, 1.0F, character);
    }
    for (int tick = 0; tick < 300; ++tick) {
        shop.host.clock.advance(kTick);
        shop.host.tick();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const world::StatusCounts& counts = shop.world().statuses().counts();
    const std::uint64_t taken = counts.dueTaken - dueBefore;
    note("200 characters, 1,000 statuses, 300 ticks of the world (10 s) in a world of 4,000 creatures: " + std::to_string(taken)
         + " due entries taken, " + std::to_string(counts.ticks) + " ticks of a status, " + std::to_string(seconds) + " s");
    check(counts.ticks >= 200 * 5 && taken <= 200 * 20, "the due list has only the ends and the ticks of the statuses (not each creature or character)");
    check(seconds < 2.0, "1,000 statuses with their ticks and ends, and 300 ticks of the world, in less than 2 s");
}

} // namespace

std::vector<Case> status_cases() {
    return {{"ST1", "status effects: the ability data of format 3 (the categories, the caster effect rows) and its refusals", data_format},
            {"ST2", "status effects: the rules of an application (stacks, refresh, DurationMax, later end, DontReplaceStack, ApplicationCooldown)",
             application_rules},
            {"ST3", "status effects: the end and the end status, the removal causes, a new session, a restart", end_rules},
            {"ST4", "status effects: the values with the limits of the categories, one time in the gear summary", status_values},
            {"ST5", "status effects: the consumables and the heart rune through the one owner; the ticks of a regeneration", users_of_the_owner},
            {"ST6", "status effects: the wire bytes of the owner and the observer views, the removal, the clock pair of the Player state", wire_bytes},
            {"ST7", "status effects: two sessions (the owner maps only to the owner, the public maps to an observer)", two_sessions},
            {"ST8", "status effects: the caster effects of learned nodes (Safety Measures with InSequence, Greed with stacks)", caster_effects},
            {"ST9", "status effects: the speed of a haste in the position check", speed_check},
            {"ST10", "status effects: the commands apply_status and remove_status", commands},
            {"ST11", "status effects: the load of 200 characters with 1,000 statuses in a world of 4,000 creatures, with a time limit", load}};
}

} // namespace soulwarden::backend::tests
