// Tests of the mana of the client (stage 3.5b.2, a task of the orchestrator; docs/systems/combat-and-health.md, protocol notes 4.5).
//
//   MN1  the MaxMana and ManaRate parts of a threshold ability of an attribute (Focus: Foc_Bonus_100_1 of the data has MaxMana 0.1 and
//        ManaRate 0.2): the maximum and the regeneration of the server are the values of the client (live check: the client divides the
//        current mana by its own maximum); full mana stays full; a change of the maximum keeps the part; two sessions (the mana state
//        is for the owner only); no threshold row: the base values. The maximum field of the mana state and ManaMax of the vitals state
//        are the base maximum 100 (live check of the final run: the client multiplies the maximum of its first mana state with its own
//        MaxMana parts; 110 gave 121 and a meter at 91 percent at full mana)
//
// The accounts, the characters, the rows, and the values are made up.

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/interaction_fixture.h"
#include "support/shop_fixture.h"
#include "support/test_progression.h"
#include "server/rep/mana_messages.h"
#include "world/entity.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;

constexpr std::uint32_t kManaSlot = 47;
constexpr std::uint32_t kManaType = 1652;
constexpr std::uint32_t kVitalsSlot = 41;
constexpr std::uint32_t kVitalsType = 15;

float f32_value(std::span<const std::uint8_t> value) {
    std::uint32_t bits = 0;
    for (const std::uint8_t byte : value) {
        bits = (bits << 8) | byte;
    }
    float number = 0.0F;
    std::memcpy(&number, &bits, sizeof(number));
    return number;
}

/** The last mana state of the own character of a client: current, maximum, rate (no value: none since `from`). */
struct WireMana {
    bool found{false};
    float current{0.0F};
    float maximum{0.0F};
    float rate{0.0F};
};

WireMana last_mana(const FakeClient& client, std::size_t from = 0) {
    WireMana mana;
    for (const Seen& seen : fragments_of(client, kManaSlot, kManaType, from)) {
        if (seen.interest != 1) {
            continue;
        }
        mana.found = true;
        mana.current = f32_value(field_of(seen.fragment, 0, 0));
        mana.maximum = f32_value(field_of(seen.fragment, 0, 1));
        mana.rate = f32_value(field_of(seen.fragment, 0, 3));
    }
    return mana;
}

std::size_t observed_mana(const FakeClient& client, std::size_t from = 0) {
    std::size_t count = 0;
    for (const Seen& seen : fragments_of(client, kManaSlot, kManaType, from)) {
        count += seen.interest != 1 ? 1 : 0;
    }
    return count;
}

/** The ManaMax of the last vitals state of the own character of a client that has the mana group (group 1, field 3), or -1. */
float vitals_mana_max(const FakeClient& client) {
    float value = -1.0F;
    for (const Seen& seen : fragments_of(client, kVitalsSlot, kVitalsType)) {
        const auto field = field_of(seen.fragment, 1, 3);
        if (seen.interest == 1 && field.size() == 4) {
            value = f32_value(field);
        }
    }
    return value;
}

bool near(float a, float b, float tolerance = 0.01F) {
    return std::abs(a - b) <= tolerance;
}

/** MN1: the mana parts of the threshold abilities. */
void mana_parts(const Options&) {
    Shop shop(false);
    gamedata::ProgressionData data = test_progression();
    data.attributeAbilities["focus"] = {{25, {"Test_Foc_Bonus_25_1", "Test_Foc_Bonus_25_2"}}};
    data.manaRegenRate = 2.0F;
    data.manaRegenDelay = 1.0F;
    data.manaRegenDelayOnEmpty = 2.0F;
    shop.host.progression.set(data);
    gamedata::AbilityTables tables;
    tables.cooldowns = {{"Test_Cooldown", 1.0F, 0.0F}};
    tables.abilityRows = {{"Test_Foc_Bonus_25_1", tests::shop::cells({{"MaxMana", 0.1F}, {"ManaRate", 0.2F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}})},
                          {"Test_Foc_Bonus_25_2", tests::shop::cells({}, {{"OnEquipStatusEffect", "Test_No_Status"}})}};
    if (!shop.host.abilities.set(tables)) {
        check(false, "the ability data");
        return;
    }
    // A has the focus 25 (the threshold), B the focus 5.
    state::ProgressRow row;
    row.level = 7;
    row.experience = 2100;
    row.spent = {0, 20, 0, 0, 0};
    check(shop.host.store.set_progress(kCharacterA, row), "the progress row of A");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(a.attributes.value(world::attribute::kFocus) == 25 && near(a.vitals.manaMax, 110.0F) && near(a.vitals.mana, 110.0F)
              && near(a.vitals.manaRateFactor, 1.2F),
          "the focus 25 gives the threshold row: the maximum 100 x (1 + 0.1) = 110, full mana stays full (110), the regeneration factor 1.2");
    check(near(b.vitals.manaMax, 100.0F) && near(b.vitals.mana, 100.0F) && near(b.vitals.manaRateFactor, 1.0F),
          "the focus 5 (below the threshold): the base values 100 of 100 and the factor 1");
    const WireMana first = last_mana(shop.clientA);
    check(first.found && near(first.current, 110.0F) && near(first.maximum, 100.0F) && near(vitals_mana_max(shop.clientA), 100.0F),
          "the mana state of the owner A has the current 110 and the base maximum 100, the vitals state ManaMax 100: the client maximum is "
          "100 x 1.1 = 110, and it hides its meter");
    check(observed_mana(shop.clientB) == 0 && observed_mana(shop.clientA) == 0 && near(last_mana(shop.clientB).current, 100.0F),
          "two sessions: B gets its own mana state (100 of 100) and no mana state of A (owner only)");
    // A spend and the regeneration with the factor: 2 x 1.2 = 2.4 mana in one second after the delay of 1 s.
    std::size_t from = shop.clientA.got.size();
    shop.host.world.abilities().change_mana(a, -22.0F, "the test");
    shop.host.steps(10, {&shop.clientA, &shop.clientB});
    const WireMana spent = last_mana(shop.clientA, from);
    check(near(a.vitals.mana, 88.0F) && spent.found && near(spent.current, 88.0F) && near(spent.rate, 2.4F),
          "a spend of 22: 88 of 110, and the state has the rate of the client (2 x 1.2 = 2.4)");
    shop.host.steps(600, {&shop.clientA, &shop.clientB}); // 6 s: the delay (1 s) and 5 s of 2.4
    note("the mana 6 s after the spend: " + std::to_string(a.vitals.mana));
    check(near(a.vitals.mana, 100.0F, 0.3F), "5 s of the regeneration after the delay: 88 + 5 x 2.4 = 100");
    from = shop.clientA.got.size();
    shop.host.steps(500, {&shop.clientA, &shop.clientB});
    const WireMana full = last_mana(shop.clientA, from);
    check(near(a.vitals.mana, 110.0F) && full.found && near(full.current, 110.0F) && near(full.rate, 0.0F),
          "the mana is full again (110 of 110): the state with the rate 0");
    // A change of the maximum keeps the part of the mana (55 of 110 to 50 of 100), as the client does (0x676B310).
    shop.host.world.abilities().change_mana(a, -55.0F, "the test");
    const world::ProgressResult lowered = shop.host.world.progress().set_attribute(kCharacterA, world::attribute::kFocus, 5);
    check(lowered.ok() && near(a.vitals.manaMax, 100.0F) && near(a.vitals.mana, 50.0F) && near(a.vitals.manaRateFactor, 1.0F),
          "below the threshold again: the maximum 100, the mana keeps its part (55 of 110 to 50 of 100), the factor 1");
    const world::ProgressResult raised = shop.host.world.progress().set_attribute(kCharacterA, world::attribute::kFocus, 25);
    check(raised.ok() && near(a.vitals.manaMax, 110.0F) && near(a.vitals.mana, 55.0F, 0.05F),
          "the threshold again: the maximum 110 and the mana 55 (the same part)");
    // No threshold data (an older progression file): the base values.
    data.attributeAbilities.clear();
    shop.host.progression.set(data);
    shop.host.world.progress().refresh_vitals(a);
    check(near(a.vitals.manaMax, 100.0F) && near(a.vitals.manaRateFactor, 1.0F), "no threshold data: the base maximum 100 and the factor 1");
}

/** MN2 (stage 2.2, the open point of stage 3.5b.2): the mana prediction message 4289 with each later mana state. */
void mana_prediction(const Options&) {
    gamedata::SliceIds slice;
    slice.set(0x1111222233334444ULL, {{0x5555666677778888ULL, 0x98C89E858C53376BULL, server::rep::kManaComponentName}});
    Shop shop(false, ":memory:", true, false, &slice);
    gamedata::ProgressionData data = test_progression();
    data.attributeAbilities["focus"] = {{25, {"Test_Foc_Bonus_25_1"}}};
    data.manaRegenRate = 2.0F;
    data.manaRegenDelay = 1.0F;
    data.manaRegenDelayOnEmpty = 2.0F;
    shop.host.progression.set(data);
    gamedata::AbilityTables tables;
    tables.cooldowns = {{"Test_Cooldown", 1.0F, 0.0F}};
    tables.abilityRows = {{"Test_Foc_Bonus_25_1", tests::shop::cells({{"MaxMana", 0.1F}, {"ManaRate", 0.2F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}})}};
    check(shop.host.abilities.set(tables), "setup: the ability data");
    state::ProgressRow row;
    row.level = 7;
    row.experience = 2100;
    row.spent = {0, 20, 0, 0, 0};
    check(shop.host.store.set_progress(kCharacterA, row), "setup: the focus 25 of A");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const std::uint64_t target = slice.target(a.id, *slice.find_named(server::rep::kManaComponentName));
    const auto syncs = [&](const FakeClient& client, std::size_t from) {
        std::vector<std::vector<std::uint8_t>> bodies;
        for (std::size_t index = from; index < client.got.size(); ++index) {
            if (client.got[index].type == server::rep::kTypeManaForceSync) {
                bodies.push_back(client.got[index].body);
            }
        }
        return bodies;
    };
    const auto u64 = [](const std::vector<std::uint8_t>& body, std::size_t at) {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value = (value << 8) | body[at + index];
        }
        return value;
    };
    check(syncs(shop.clientA, 0).empty() && syncs(shop.clientB, 0).empty(),
          "the first record (full mana) has no prediction message: the entry sequence does not change");
    std::size_t from = shop.clientA.got.size();
    shop.host.world.abilities().change_mana(a, -22.0F, "the test");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    const auto spend = syncs(shop.clientA, from);
    check(spend.size() == 1 && spend[0].size() == 32 && u64(spend[0], 0) == target && u64(spend[0], 8) == a.id
              && near(f32_value(std::span(spend[0]).subspan(16, 4)), 88.0F) && near(f32_value(std::span(spend[0]).subspan(20, 4)), 100.0F)
              && near(f32_value(std::span(spend[0]).subspan(24, 4)), 1.0F, 0.05F) && near(f32_value(std::span(spend[0]).subspan(28, 4)), 1.2F),
          "a spend of 22: one message 4289 to A with the Mana facet of A, 88, the base maximum 100, the delay 1 s, the factor 1.2 (live check: "
          "the factor gives the rate of the server)");
    check(syncs(shop.clientB, 0).empty(), "B gets no prediction message of A");
    from = shop.clientA.got.size();
    shop.host.steps(1200, {&shop.clientA, &shop.clientB});
    const auto full = syncs(shop.clientA, from);
    check(near(a.vitals.mana, 110.0F) && !full.empty() && near(f32_value(std::span(full.back()).subspan(16, 4)), 110.0F)
              && near(f32_value(std::span(full.back()).subspan(24, 4)), 0.0F),
          "at the full value: the message 4289 with 110 and no delay");
    // No ids of the player slice: no message, one log line.
    Shop plain(false);
    plain.host.progression.set(data);
    plain.enter();
    plain.host.world.abilities().change_mana(*plain.entity(kCharacterA), -10.0F, "the test");
    plain.host.steps(3, {&plain.clientA, &plain.clientB});
    check(syncs(plain.clientA, 0).empty() && log_count("no mana prediction for the client") == 1,
          "with no ids of the player slice: no message 4289, and one log line");
}

} // namespace

std::vector<Case> mana_cases() {
    return {{"MN1", "mana: the MaxMana and ManaRate parts of a threshold ability; the values of the client; two sessions", mana_parts},
            {"MN2", "mana: the prediction message 4289 of the client with each later mana state (stage 2.2); two sessions", mana_prediction}};
}

} // namespace soulwarden::backend::tests
