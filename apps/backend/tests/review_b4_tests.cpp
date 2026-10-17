// The corrections of the code review of stage B4 (~/Games/soulwarden/work/evidence/bow-build/b4/code-review/FINDINGS.md), corrected in stage
// 3.7a. Made-up ids and the rows of the game data (support/bow_rules_fixture.h, support/gawh_fixture.h); no capture and no id of an account.
// The proof cases of the review (B4P1 to B4P4, the tool case B4T) are the base of these cases.
//
//   BZ1  B4-R1: the normal attribute command reaches constitution 100 and leaves it: the threshold ability Con_Bonus_100_1 (MaxHealth 0.03)
//        comes and goes with the stored change (the gear and the vitals of the new attributes), with no other gear refresh
//   BZ2  B4-R2: the first basic shot from the mark CanAttackCancel of a dodge with a hit near the muzzle: the hit has the exit effects of the
//        dodge (Speedy, the empower of Evasive Tactics): the same damage as a shot after an explicit end of the dodge
//   BZ3  B4-R3: a status of a spell area (Clear Out with Power Cleaner) on a friend: the outgoing time factor of the caster (Foc_Bonus_200_1:
//        1.1), not of the spell entity; the caster itself gets no outgoing factor
//   BZ4  B4-R4: the item catalog tool stops (the old file stays) for a jewelry item whose armor stats row is not in the source, and for jewelry
//        with no armor stats table (check_item_catalog_tool.py: trinket-missing-row, trinket-missing-table)
//   BZ5  B4-R5: an OnHit row with only the outgoing time factor (Global_Weapon_HercyneRaid_1) is listed as not applied

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "support/bow_rules_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gawh_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace rules;

void attribute_threshold(const Options&) {
    RulesShop shop(true);
    shop.ready(kCharacterA, {}, {});
    auto data = shop.host.progression.data();
    for (auto& level : data.levels) {
        level.attributePoints = 100;
    }
    data.constitutionHealth.clear();
    for (unsigned value = 5; value <= 500; ++value) {
        data.constitutionHealth.push_back((value - 5) * 40);
    }
    shop.host.progression.set(data);
    check(shop.host.progression.attribute_maximum() == 500, "control: a progression table with points for constitution 100");
    check(shop.world().progress().set_level(kCharacterA, 6).ok(), "control: level 6");
    check(shop.world().progress().set_attribute(kCharacterA, world::attribute::kConstitution, 100).ok(),
          "control: the attribute command reaches constitution 100");
    const float before = shop.character()->vitals.healthMax;
    check(near(shop.character()->gear.healthPart, 0.03F), "the attribute command applies Con_Bonus_100_1 (health part 0.03) with no other refresh");
    shop.world().gear().refresh(*shop.character());
    check(near(before, shop.character()->vitals.healthMax), "a later gear refresh does not change the maximum health at constitution 100");
    check(shop.world().progress().set_attribute(kCharacterA, world::attribute::kConstitution, 99).ok(),
          "control: the attribute command leaves the threshold");
    const float below = shop.character()->vitals.healthMax;
    check(near(shop.character()->gear.healthPart, 0.0F), "the attribute command removes Con_Bonus_100_1 below 100");
    shop.world().gear().refresh(*shop.character());
    check(near(below, shop.character()->vitals.healthMax), "the maximum health of constitution 99 comes at once (a later refresh changes nothing)");
}

struct MuzzleResult {
    float damage{0.0F};
    bool accepted{false};
    bool haste{false};
    bool empower{false};
};

MuzzleResult muzzle_after_dodge(bool endFirst) {
    RulesShop shop;
    shop.ready(kCharacterA, {kSpeedy}, {kDodgeWeave, kEvasive});
    auto* wolf = shop.wolf_at(1.8F);
    shop.world().set_creature_behavior(false);
    const auto id = wolf->id;
    const float before = wolf->vitals.health;
    shop.walker.idle(20);
    check(shop.dodge() == world::DodgeVerdict::accepted, "control: a dodge with full stamina");
    shop.wait(300);
    if (endFirst) {
        shop.world().stamina().attack_started(*shop.character(), shop.host.clock.now());
    }
    auto shot = BowShop::bow_item("Basic", "TestBowLight");
    wolf = shop.world().creatures().find(id);
    shot.receiverActor = wolf->actor.bytes;
    shot.receiver = id;
    shot.facet = wolf->creature.body->target(id, wolf->creature.body->components[1]);
    shot.point = {wolf->position.x - 0.6F, wolf->position.y, kHeight + 0.6F};
    shop.send_item(shot, {kActionAttack, kActionAttackRelease});
    const bool accepted = shop.world().projectiles().counts().shots[static_cast<unsigned>(world::ShotVerdict::accepted)] == 1
                          && shop.world().projectiles().counts().ends[static_cast<unsigned>(world::ProjectileEnd::nearHit)] == 1;
    return {before - shop.world().creatures().find(id)->vitals.health, accepted, shop.find(kDodgeHaste) != nullptr,
            shop.find(kEvasiveEmpower) != nullptr};
}

void muzzle_hit_after_dodge(const Options&) {
    const MuzzleResult normal = muzzle_after_dodge(false);
    const MuzzleResult control = muzzle_after_dodge(true);
    check(normal.accepted && control.accepted && normal.damage > 0.0F, "control: both shots are accepted hits near the muzzle with damage");
    check(normal.haste && normal.empower && control.haste && control.empower, "control: both shots end the dodge with its exit effects");
    check(near(normal.damage, control.damage, 0.01F),
          "the first hit near the muzzle after the mark CanAttackCancel has Speedy and the empower of Evasive Tactics (" + std::to_string(normal.damage)
              + " and " + std::to_string(control.damage) + ")");
}

void spell_outgoing_factor(const Options&) {
    gawh::GawhShop shop;
    shop.enter_b();
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    auto abilities = shop.host.abilities.tables();
    abilities.abilityRows.push_back({"Foc_Bonus_200_1", cells({{"TargetStatusEffectDurationMult", 0.1F}},
                                                              {{"IgnoreDisabledAttackTypes", "Heartgem"}, {"TargetStatusEffectDurationCats", "Buff"}})});
    check(shop.host.abilities.set(abilities), "control: the focus row of the outgoing buff time");
    auto progression = shop.host.progression.data();
    progression.attributeAbilities["focus"] = {{200, {"Foc_Bonus_200_1"}}};
    shop.host.progression.set(progression);
    shop.hold("TestHammer");
    shop.character()->attributes.spent[world::attribute::kFocus] = 195;
    shop.bar(shop::kWarHammer, {nullptr, nullptr, "Ability_WarHammer_ClearOut"});
    shop.learn({gawh::kFortifyAoe});
    auto* wolf = shop.wolf(-2.0F);
    const auto pressed = shop.press(2);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 23), *wolf);
    shop.walker.idle(2);
    const auto* own = shop.status_of(gawh::kFortifyBuff);
    const auto* friendStatus = shop.status_of(gawh::kFortifyBuff, kCharacterB);
    check(own != nullptr && friendStatus != nullptr, "control: Clear Out gives the fortify to the caster and the friend");
    if (own == nullptr || friendStatus == nullptr) {
        return;
    }
    const float ownTime = std::chrono::duration<float>(own->ends - own->started).count();
    const float friendTime = std::chrono::duration<float>(friendStatus->ends - friendStatus->started).count();
    const float factor = shop.world().passives().outgoing_factor(*shop.character(), *shop.world().find_character(kCharacterB),
                                                                 *shop.host.abilities.status(gawh::kFortifyBuff));
    check(near(factor, 1.1F), "control: the caster has the outgoing buff time factor 1.1");
    check(near(friendTime, 4.0F * factor), "the status of the spell area on the friend has the outgoing factor of the caster (4.4 s, got "
                                               + std::to_string(friendTime) + " s)");
    check(near(ownTime, 4.0F), "the status of the spell area on the caster has no outgoing factor (4 s)");
}

void jewelry_stats_source(const Options& options) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_item_catalog_tool.py";
    const std::filesystem::path work = options.workDirectory / "check-item-catalog-b4";
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / "check-item-catalog-b4.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string() + "\" trinket trinket-missing-row trinket-missing-table > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream file(output);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(text.find("ok    trinket:") != std::string::npos, "control: the complete source exports the three jewelry lines");
    check(status == 0 && text.find("ok    trinket-missing-row:") != std::string::npos,
          "a jewelry item whose armor stats row is not in the source: the tool stops, the old catalog stays");
    check(status == 0 && text.find("ok    trinket-missing-table:") != std::string::npos,
          "jewelry and no armor stats table: the tool stops, the old catalog stays");
}

void event_duration_listed(const Options&) {
    RulesShop shop;
    auto perks = rules_perks();
    perks.perks.push_back(perk("TestB4Hercyne", "Generated", "OnEquip", "Bow", "", {}, {"Global_Weapon_HercyneRaid_1"}));
    perks.abilities["global_weapon_hercyneraid_1"] = cells(
        {{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"CheckStatusEffectsOnTargetOwned", 1.0F}, {"TargetStatusEffectDurationMult", 0.025F},
         {"ActivationCooldown", 1.5F}},
        {{"TargetStatusEffectCategory", "DoT"}, {"TargetStatusEffectDurationCats", "DoT"}});
    check(shop.host.perks.set(perks), "control: the authored Hercyne row");
    shop.ready(kCharacterA, {"TestB4Hercyne"}, {});
    bool listed = false;
    bool applied = true;
    std::string text;
    for (const auto& view : shop.world().passives().describe(*shop.character())) {
        if (view.id == "Global_Weapon_HercyneRaid_1") {
            listed = true;
            applied = view.applied;
            text = view.note;
        }
    }
    check(listed && !applied && text.find("not applied") != std::string::npos,
          "an OnHit row with only the outgoing time factor is listed as not applied (" + text + ")");
}

} // namespace

std::vector<Case> review_b4_cases() {
    return {{"BZ1", "review of stage B4, finding B4-R1: the attribute command changes the threshold ability of the gear and the health at once",
             attribute_threshold},
            {"BZ2", "review of stage B4, finding B4-R2: the first hit near the muzzle after a dodge has the exit effects of the dodge",
             muzzle_hit_after_dodge},
            {"BZ3", "review of stage B4, finding B4-R3: a status of a spell area has the outgoing time factor of its caster", spell_outgoing_factor},
            {"BZ4", "review of stage B4, finding B4-R4: a jewelry item with no armor stats row in the source stops the item catalog tool",
             jewelry_stats_source},
            {"BZ5", "review of stage B4, finding B4-R5: an event row of the outgoing time factor is listed as not applied", event_duration_listed}};
}

} // namespace soulwarden::backend::tests
