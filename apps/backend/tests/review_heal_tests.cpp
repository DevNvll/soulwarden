// Cases for the corrections of the code review of stage 3.3b-S6 (stage 3.3b-M1): the findings S6-R1 to S6-R8, each with the proof of the
// reviewer (made-up accounts, characters, and navigation tiles; no file of the game). Each case fails on the tree of stage 3.3b-S6
// (commit 959f252) and passes with the correction. S6-R9 (the load case HE9 with no periodic heal) is in tests/heal_tests.cpp.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/heal_fixture.h"
#include "support/passive_fixture.h"
#include "support/projectile_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace heal;

/** Runs a check script of the tools (a path below the repository) and @return its output lines. */
std::vector<std::string> run_tool(const Options& options, const std::string& script) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + "-rw.log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + path.lexically_normal().string() + "\" > \"" + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

/** RW1 (S6-R1): no tool edits a settings file of a Proton prefix; a development run has the startup setting -sys_audio_disable 1. */
void silence(const Options& options) {
    const std::vector<std::string> lines = run_tool(options, "tools/check_dev_sound.py");
    const auto ok = [&](const char* name) {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.rfind(std::string("ok ") + name + ":", 0) == 0; });
    };
    check(ok("no settings write"), "no file mute in the tree: a swap of folders has no write to redirect (the two-stage swap of the reviewer)");
    check(ok("game argument") && ok("launcher line") && ok("sound override"),
          "the development run gives the game -sys_audio_disable 1 (and SOULWARDEN_DEV_SOUND=1 none)");
}

/** RW2 (S6-R2): an old aim of Divine Embrace cannot confirm Sacred Ground after a change of the bar. */
void changed_aim_ability(const Options&) {
    HealShop shop;
    shop.staff(*shop.character(), kDivineNode, {kSacredNode});
    shop.character()->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.walker.press(0);
    const auto table = gamedata::progression_id(kLifeMagic);
    check(shop.world().skills().set_bar(kCharacterA, table, 0, gamedata::progression_id(kSacredNode)).ok(),
          "control: the normal skill command changes Q from Divine Embrace to Sacred Ground");
    shop.walker.bits({kSelfTargetBit});
    shop.pass(1.2F);
    note("Divine aim, Sacred bar, confirm: confirms=" + std::to_string(shop.ability_counts().confirms)
         + ", spells=" + std::to_string(shop.spells().size()) + ", mana=" + std::to_string(shop.character()->vitals.mana));
    check(shop.ability_counts().confirms == 0 && shop.spells().empty(),
          "a confirm cannot cast a different ability from the one that opened the aim");
}

/** RW3 (S6-R3): a full friend does not take the heal and its cost; the hurt caster gets the heal. */
void zero_gain_cost(const Options&) {
    HealShop shop(true);
    shop.enter_b_near(at(kPlace.x - 1.0F, kPlace.y));
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    shop.staff(*shop.character(), kDivineNode);
    shop.character()->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    world::Entity* friendTarget = shop.world().find_character(kCharacterB);
    friendTarget->vitals.health = friendTarget->vitals.healthMax;
    const float mana = shop.character()->vitals.mana;
    shop.cast(false);
    shop.pass(1.2F);
    note("Full friend, injured caster: successful heals=" + std::to_string(shop.heals().heals)
         + ", counted friend casts=" + std::to_string(shop.ability_counts().healsFriend)
         + ", mana paid=" + std::to_string(mana - shop.character()->vitals.mana));
    check(shop.heals().heals > 0 || near(shop.character()->vitals.mana, mana, 0.01F),
          "a full friend must not consume the heal and its cost without any health gain");
}

/** RW4 (S6-R4): Sacred Ground does not heal a friend behind a wall. */
void aura_wall(const Options& options) {
    HealShop shop(true);
    const auto folder = options.workDirectory / "independent-heal-wall";
    TestNavRegion region;
    region.tiles[{31, 31}] = [](int column, int, std::vector<TestFloor>& floors) {
        const float east = 1984.0F + (static_cast<float>(column) + 0.5F) * 0.25F;
        if (east > 1998.0F && east < 1998.3F) {
            floors = {{kHeight + 4.0F, gamedata::NavFloor::kBlocked}};
        }
    };
    region.write(folder);
    check(shop.host.navigation.set_directory(folder.string()), "control: navigation contains the wall");
    const auto targetPlace = at(kPlace.x - 2.5F, kPlace.y);
    shop.enter_b_near(targetPlace);
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    shop.staff(*shop.character(), kSacredNode);
    world::Entity* target = shop.world().find_character(kCharacterB);
    target->vitals.health = 100.0F;
    shop.world().health().fought(*target);
    auto origin = shop.character()->position;
    origin.z += 1.5F;
    auto chest = target->position;
    chest.z += 1.5F;
    check(!shop.world().navigation().sees(origin, chest), "control: the ally is behind the wall");
    shop.cast(true);
    shop.pass(2.1F);
    target = shop.world().find_character(kCharacterB);
    note("Sacred Ground through wall: health=" + std::to_string(target->vitals.health)
         + ", heal status=" + std::to_string(has_status(*target, kSacredStatus)));
    check(near(target->vitals.health, 100.0F), "an aura cannot heal an ally through the wall");
}

world::EntityId make_aura(HealShop& shop, world::Entity& caster, float base) {
    world::SpellRequest request;
    request.spellId = kSacredSpell;
    request.caster = caster.id;
    request.role = world::SpellRole::aura;
    request.position = shop.character()->position;
    request.radius = 3.0F;
    request.life = std::chrono::milliseconds(5000);
    request.statuses = {kSacredStatus};
    request.heal.healer = caster.id;
    request.heal.healerCharacter = caster.characterId;
    request.heal.base = base;
    request.heal.table = gamedata::progression_id(kLifeMagic);
    const world::Entity* created = shop.world().spells().make(request);
    check(created != nullptr, "control: create a valid healing aura");
    return created != nullptr ? created->id : 0;
}

/** RW5 (S6-R5): the aura that stays gives its own healer and heal base to the status. */
void aura_source_transfer(const Options&) {
    HealShop shop(true);
    shop.enter_b_near(at(kPlace.x - 1.0F, kPlace.y));
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    shop.staff(*shop.character(), kSacredNode);
    world::Entity* other = shop.world().find_character(kCharacterB);
    shop.staff(*other, kSacredNode);
    shop.character()->vitals.health = 100.0F;
    other->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.world().health().fought(*other);
    const auto first = make_aura(shop, *shop.character(), 100.0F);
    shop.pass(0.1F);
    const auto second = make_aura(shop, *shop.world().find_character(kCharacterB), 500.0F);
    shop.pass(0.1F);
    shop.world().spells().end(first, world::SpellEnd::time);
    const auto found = std::find_if(shop.character()->statuses.begin(), shop.character()->statuses.end(),
                                    [](const auto& status) { return status.status == kSacredStatus; });
    check(found != shop.character()->statuses.end(), "control: the overlapping aura retains the status");
    if (found == shop.character()->statuses.end()) {
        return;
    }
    const auto healer = shop.world().find_character(kCharacterB)->id;
    const bool sourceUpdated = found->heal.healer == healer;
    note("Aura transfer: source is second aura=" + std::to_string(found->source.entity == second)
         + ", heal still belongs to first healer=" + std::to_string(found->heal.healer == shop.character()->id)
         + ", base=" + std::to_string(found->heal.base));
    const float before = shop.character()->vitals.health;
    const auto noticesBefore = notices_of(shop.b).size();
    const auto oldNoticesBefore = notices_of(shop.a).size();
    shop.pass(1.0F);
    shop.b_steps(3, at(kPlace.x - 1.0F, kPlace.y)); // The client of B takes its messages (the step `pass` exchanges only the client of A).
    const float gain = shop.character()->vitals.health - before;
    note("After first aura ends: A gain=" + std::to_string(gain)
         + ", notices to surviving healer=" + std::to_string(notices_of(shop.b).size() - noticesBefore)
         + ", notices to ended healer=" + std::to_string(notices_of(shop.a).size() - oldNoticesBefore));
    check(sourceUpdated && near(gain, 80.0F, 0.1F),
          "the surviving aura must supply its own healer and heal base");
    check(notices_of(shop.b).size() > noticesBefore, "the surviving healer receives the heal notice");
    check(notices_of(shop.a).size() == oldNoticesBefore, "the ended healer receives no further heal notice");
}

/** A control of the review (not a finding): a different weapon in the hand ends the aim. */
void replaced_cast_weapon(const Options&) {
    HealShop shop;
    shop.staff(*shop.character(), kDivineNode);
    shop.character()->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.walker.press(0);
    shop.learn(*shop.character(), "TestAxe", "GreatAxeAbilityTable", "Ability_Greataxe_Charge");
    shop.walker.bits({kSelfTargetBit});
    shop.pass(1.2F);
    note("Staff replaced by axe during aim: confirms=" + std::to_string(shop.ability_counts().confirms)
         + ", accepted casts=" + std::to_string(shop.presses(world::CastVerdict::accepted)));
    check(shop.ability_counts().confirms == 0 && shop.presses(world::CastVerdict::accepted) == 0,
          "replacing the equipped weapon must cancel its previous heal aim");
}

/** RW6 (S6-R6): a body stop near the muzzle does not become a headshot beyond the hit tolerance. */
void head_after_nearer_body(const Options&) {
    using namespace passive;
    projectile::ProjectileShop shop;
    check(shop.host.creatures.set_full({crit_wolf()}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}}),
          "control: use the exported wolf collision volumes");
    gamedata::CombatData combat = shop.host.combat.data();
    for (auto& weapon : combat.weapons) {
        if (weapon.id == "TestBowRow") {
            weapon.critical = true;
            weapon.critChance = 0.0F;
            weapon.critMultiplier = 1.3F;
        }
    }
    check(shop.host.combat.set(combat), "control: only a head contact can force this bow critical");
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(1.3F);
    wolf->creature.yaw = 0.0F;
    const auto center = wolf->position;
    const auto body = world::capsule_of(*wolf);
    const auto head = world::head_of(*wolf);
    check(head.has_value(), "control: the wolf has its head volume");
    if (!head) {
        return;
    }
    bool falseHead = false;
    state::WorldPosition stopped;
    std::array<float, 3> shotDirection{};
    for (int angle = 0; angle < 360 && !falseHead; ++angle) {
        const float radians = static_cast<float>(angle) * 0.0174532925F;
        const float east = std::sin(radians);
        const float north = std::cos(radians);
        for (int height = 80; height <= 160 && !falseHead; ++height) {
            const float elevation = static_cast<float>(height) * 0.01F;
            const state::WorldPosition from{center.x - east * 3.0F, center.y - north * 3.0F, center.z + elevation};
            const state::WorldPosition end{center.x + east * 3.0F, center.y + north * 3.0F, center.z + elevation};
            const auto bodyAt = world::sweep(from, end, body, 0.15F);
            const auto headAt = world::sweep(from, end, *head, 0.15F);
            if (!bodyAt || !headAt || (*headAt - *bodyAt) * 6.0F <= world::projectile_rules::kHitTolerance) {
                continue;
            }
            const state::WorldPosition point{from.x + east * 6.0F * *bodyAt, from.y + north * 6.0F * *bodyAt, from.z};
            if (world::head_contact(*wolf, point, {east, north, 0.0F}, 0.15F)) {
                note("Head behind body: angle=" + std::to_string(angle) + ", height=" + std::to_string(elevation)
                     + ", body distance=" + std::to_string(*bodyAt * 6.0F) + ", head distance=" + std::to_string(*headAt * 6.0F));
                falseHead = true;
                stopped = point;
                shotDirection = {east, north, 0.0F};
            }
        }
    }
    check(!falseHead, "a body stop cannot become a later headshot beyond the hit tolerance");
    if (falseHead) {
        auto shot = projectile::ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", shotDirection);
        shot.origin = {stopped.x, stopped.y, stopped.z};
        shot.receiverActor = wolf->actor.bytes;
        shot.receiver = wolf->id;
        shot.facet = wolf->creature.body->target(wolf->id, wolf->creature.body->components[1]);
        shot.point = {stopped.x, stopped.y, stopped.z};
        const auto from = shop.a.got.size();
        shop.shoot(shot);
        check(shop.verdicts(world::ShotVerdict::accepted) == 1 && shop.ends(world::ProjectileEnd::nearHit) == 1,
              "control: the ordinary input path accepts this near-muzzle body impact");
        bool headNumber = false;
        for (std::size_t index = from; index < shop.a.got.size(); ++index) {
            if (shop.a.got[index].type != server::rep::kTypeDamageDealt) {
                continue;
            }
            server::hub::Reader reader(shop.a.got[index].body);
            reader.u64();
            reader.u64();
            reader.raw(16);
            reader.u64();
            reader.u32();
            reader.u32();
            const std::uint16_t low = reader.u8();
            const std::uint16_t high = reader.u8();
            const std::uint16_t flags = low | (high << 8);
            headNumber = headNumber || (flags & world::damage_flags::kHeadshot) != 0;
        }
        note("Near-muzzle input: accepted=" + std::to_string(shop.verdicts(world::ShotVerdict::accepted))
             + ", headshot damage number=" + std::to_string(headNumber));
        check(!headNumber, "the accepted body impact must not emit a headshot damage number");
    }
}

/** RW7 (S6-R7): a gain with no number does not add to the next number of the owner. */
void silent_gain_feedback(const Options&) {
    HealShop shop;
    shop.character()->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.world().touch(shop.character()->id, world::ComponentKey::vitals);
    shop.walker.idle(3);
    const auto from = shop.a.got.size();
    world::HealRequest hidden;
    hidden.amount = 50.0F;
    hidden.kind = world::HealKind::regeneration;
    hidden.number = false;
    hidden.what = "independent silent regeneration";
    check(near(shop.world().health().heal(*shop.character(), hidden).gained, 50.0F), "control: a silent heal gains 50");
    world::HealRequest shown;
    shown.amount = 100.0F;
    shown.what = "independent numbered heal";
    check(near(shop.world().health().heal(*shop.character(), shown).gained, 100.0F), "control: the numbered heal gains 100");
    shop.walker.idle(3);
    float prior = 100.0F;
    float numbered = 0.0F;
    for (const auto& [health, flags] : vitals_of(shop.a, from)) {
        if ((flags & 2) != 0) {
            numbered += std::max(health - prior, 0.0F);
        }
        prior = health;
    }
    note("Silent gain=50, shown gain=100, gain carried by numbered owner state=" + std::to_string(numbered));
    check(near(numbered, 100.0F), "a silent heal must not add to the later heal number");
}

/** RW8 (S6-R8): the confirm of an aim uses the lock of the session now (the loading screen of a travel). */
void travel_aim_lock(const Options&) {
    HealShop shop;
    shop.staff(*shop.character(), kDivineNode);
    shop.character()->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.walker.press(0);
    const auto result = shop.world().travel(kCharacterA, at(kPlace.x + 1000.0F, kPlace.y), world::TravelScreen::on);
    check(result.error == world::TravelError::none, "control: a travel creates the session loading lock");
    shop.walker.bits({kSelfTargetBit});
    shop.walker.idle(45);
    const auto confirms = shop.ability_counts().confirms;
    const auto healed = shop.ability_counts().healsSelf;
    const auto locked = shop.presses(world::CastVerdict::locked);
    shop.walker.press(0);
    check(shop.presses(world::CastVerdict::locked) == locked + 1,
          "control: the ordinary ability key is still refused by the loading lock");
    note("Confirm during loading: confirms=" + std::to_string(confirms) + ", self heals=" + std::to_string(healed));
    check(confirms == 0 && healed == 0, "confirm must use the current session lock, not the old aim lock");
}

} // namespace

std::vector<Case> review_heal_cases() {
    return {{"RW1", "review of 3.3b-S6, S6-R1: the silence of a development run edits no settings file (check_dev_sound.py)", silence},
            {"RW2", "review of 3.3b-S6, S6-R2: an old aim cannot confirm a different ability of the bar", changed_aim_ability},
            {"RW3", "review of 3.3b-S6, S6-R3: a friend at full health does not take a direct heal and its cost", zero_gain_cost},
            {"RW4", "review of 3.3b-S6, S6-R4: an area heal does not go through a wall", aura_wall},
            {"RW5", "review of 3.3b-S6, S6-R5: the aura that stays gives its own heal source", aura_source_transfer},
            {"RW6", "review of 3.3b-S6, S6-R6: a body stop near the muzzle is no later headshot", head_after_nearer_body},
            {"RW7", "review of 3.3b-S6, S6-R7: a silent gain does not add to the next heal number", silent_gain_feedback},
            {"RW8", "review of 3.3b-S6, S6-R8: a heal confirm respects the lock of a travel now", travel_aim_lock},
            {"RW9", "review of 3.3b-S6 (control, not a finding): a different weapon ends the aim", replaced_cast_weapon}};
}

} // namespace soulwarden::backend::tests
