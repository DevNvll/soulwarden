#pragma once

// The fixture of the cases of the basic attacks of the melee weapons (stage 3.3b-M1; the review cases of stage 3.3b-M2): a
// world with a wolf of the test, the combat tables of the test (an axe and a rapier with an attack graph, a sword with none),
// the two attack graphs (the form and the times of the great axe and of the rapier of the game data, with made-up ids), a client
// that sends one report for each simulation step, and the damage numbers of a client. No file of the game and no value of a
// capture is here.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamedata/progression_tables.h"
#include "server/rep/combat_messages.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests::attack_fixture {

inline constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
inline constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
inline constexpr float kHeight = 50.0F;
inline constexpr state::WorldPosition kPlace{1000.0F, 1000.0F, kHeight};
inline constexpr std::chrono::microseconds kTick{33333};
inline constexpr unsigned kActionDodge = 322;
inline constexpr std::uint32_t kSlots[] = {5, 9, 15};
inline constexpr float kWolfHealth = 100000.0F;
inline constexpr std::uint32_t kSlash = 1;
inline constexpr const char* kPullNode = "Test_Node_HeavyPull";

inline bool near(float left, float right, float tolerance = 0.001F) {
    return std::abs(left - right) <= tolerance;
}

inline gamedata::CreatureSlice wolf_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/wolf.dynamicslice";
    slice.asset.fill(0x11);
    slice.subId = 2;
    slice.root = 0x1000000000000011ULL;
    slice.interestRadius = 100.0F;
    slice.movementSlot = kSlots[0];
    slice.vitalsSlot = kSlots[1];
    slice.markerSlot = kSlots[2];
    slice.slots = {{kSlots[0], "ActionList", 11}, {kSlots[1], "Vitals", 15}, {kSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"},
                        {slice.root + 3, 0x0000000000000C03ULL, "Head/HitVolume"},
                        {slice.root + 7, 0x0000000000000B02ULL, "Marker/Marker"}};
    gamedata::CreatureSlice::Action reaction;
    reaction.name = "Reaction";
    reaction.fragment = "Fragment";
    reaction.fragmentIndex = 15;
    reaction.serverStarted = true;
    slice.actions.push_back(reaction);
    return slice;
}

inline gamedata::CreatureKind wolf_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Wolf";
    kind.vitals = "Test_Wolf";
    kind.category = "Test_Wolf_Category";
    kind.vitalsId = 0x11112222;
    kind.categoryId = 0x11112223;
    kind.name = "Test_Wolf";
    kind.vitalsName = "Test_Wolf";
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 18;
    kind.bodies = {{0, 100}};
    return kind;
}

/** The combat tables: an axe and a rapier with an attack graph, and a sword with none (the rule of stage 3.2a). */
inline gamedata::CombatData test_combat() {
    gamedata::CombatData data;
    data.constants = {0.025F, 0.0112F, 0.6667F, 5, 100, 500};
    data.damageTypes = {{"True", 1, gamedata::DamageCategory::direct}, {"Slash", 4, gamedata::DamageCategory::physical}};
    data.attributeMinimum = 5;
    for (const unsigned attribute : {world::attribute::kStrength, world::attribute::kDexterity, world::attribute::kIntelligence,
                                     world::attribute::kFocus}) {
        for (int value = 0; value < 96; ++value) {
            data.attributeDamage[attribute].push_back(0.0F);
        }
    }
    data.damageRows = {{"Test_Axe_Primary", "Light", kSlash, 1.0F, 0.0F},      {"Test_Axe_Ender", "Light", kSlash, 1.05F, 0.0F},
                       {"Test_Axe_Heavy", "Heavy", kSlash, 1.2F, 0.0F},        {"Test_Axe_Heavy_Pull", "Heavy", kSlash, 1.25F, 0.0F},
                       {"Test_Axe_Charged", "Heavy", kSlash, 1.6F, 0.0F},      {"Test_Rapier_Primary", "Light", kSlash, 1.0F, 0.0F},
                       {"Test_Rapier_TwoThirds", "Light", kSlash, 0.66F, 0.0F}, {"Test_Sword_Light", "Light", kSlash, 1.0F, 0.0F},
                       {"Test_Sword_Heavy", "Heavy", kSlash, 1.2F, 0.0F}};
    gamedata::WeaponRow axe;
    axe.id = "TestAxeRow";
    axe.melee = true;
    axe.tags = "2H_Melee,TestAxe";
    axe.baseDamage = 100.0F;
    axe.lightRow = 0;
    axe.heavyRow = 2;
    gamedata::WeaponRow rapier = axe;
    rapier.id = "TestRapierRow";
    rapier.tags = "1H_Melee,TestRapier";
    rapier.lightRow = 5;
    rapier.heavyRow = 5;
    gamedata::WeaponRow sword = axe;
    sword.id = "TestSwordRow";
    sword.tags = "1H_Melee,Sword";
    sword.lightRow = 7;
    sword.heavyRow = 8;
    data.weapons = {axe, rapier, sword};
    data.items = {{"testaxe", 0}, {"testrapier", 1}, {"testsword", 2}};
    gamedata::DefenseRow none;
    none.weakness.assign(data.damageTypes.size(), 0.0F);
    none.absorption.assign(data.damageTypes.size(), 0.0F);
    data.defenses = {none};
    data.vitals = {{"test_wolf", 0}};
    return data;
}

inline gamedata::AttackTransition transition(gamedata::TransitionKind kind, float start, float end, gamedata::AttackInput input, const char* to) {
    gamedata::AttackTransition result;
    result.kind = kind;
    result.start = start;
    result.end = end;
    result.input = input;
    result.toName = to;
    result.condition = "test";
    return result;
}

inline gamedata::AttackStrike strike(const char* key, float start, float end, std::vector<gamedata::StrikeRow> rows) {
    gamedata::AttackStrike result;
    result.key = key;
    result.start = start;
    result.end = end;
    result.rows = std::move(rows);
    return result;
}

inline gamedata::StrikeRow row(const char* id, std::vector<std::string> needs = {}, std::vector<std::string> forbids = {}) {
    gamedata::StrikeRow result;
    result.row = id;
    for (const std::string& node : needs) {
        result.needs.push_back(gamedata::progression_id(node));
        result.needNames.push_back(node);
    }
    for (const std::string& node : forbids) {
        result.forbids.push_back(gamedata::progression_id(node));
        result.forbidNames.push_back(node);
    }
    return result;
}

inline gamedata::AttackFragment fragment(const char* name, float length, std::vector<gamedata::AttackTransition> transitions,
                                  std::vector<gamedata::AttackStrike> strikes = {}) {
    gamedata::AttackFragment result;
    result.name = name;
    result.length = length;
    result.transitions = std::move(transitions);
    result.strikes = std::move(strikes);
    return result;
}

/** The two attack graphs (the form and the times of the great axe and of the rapier of the game data). */
inline gamedata::WeaponActionTables test_actions() {
    using gamedata::AttackInput;
    using gamedata::TransitionKind;
    const auto buffered = TransitionKind::buffered;
    const auto instant = TransitionKind::instant;
    gamedata::WeaponActionSet axe;
    axe.tags = {"2H_Melee", "TestAxe"};
    axe.fragments = {
        fragment("Attack_Start_Charge", 0.7F,
                 {transition(instant, 0.0F, 0.333333F, AttackInput::notHeld, "Attack_Primary"),
                  transition(instant, 0.333333F, 0.666667F, AttackInput::released, "Attack_Heavy"),
                  transition(instant, 0.666667F, -1.0F, AttackInput::always, "Attack_Heavy_Charged")}),
        fragment("Attack_Primary", 1.616667F,
                 {transition(buffered, 0.1875F, 0.566667F, AttackInput::virtualInput, "Attack_Start_Charge_C2"),
                  transition(instant, 0.566667F, -1.0F, AttackInput::virtualInput, "Attack_Start_Charge_C2")},
                 {strike("atk1", 0.3F, 0.4F, {row("Test_Axe_Primary")})}),
        fragment("Attack_Heavy", 2.436667F,
                 {transition(buffered, 0.1F, 0.9F, AttackInput::virtualInput, "Attack_Start_Charge_C2"),
                  transition(instant, 0.9F, -1.0F, AttackInput::virtualInput, "Attack_Start_Charge_C2")},
                 {strike("atk1", 0.433333F, 0.566667F, {row("Test_Axe_Heavy", {}, {kPullNode})}),
                  strike("atk2", 0.433333F, 0.566667F, {row("Test_Axe_Heavy_Pull", {kPullNode})})}),
        fragment("Attack_Heavy_Charged", 2.266667F,
                 {transition(buffered, 0.1F, 0.733333F, AttackInput::virtualInput, "Attack_Start_Charge_C2"),
                  transition(instant, 0.733333F, -1.0F, AttackInput::virtualInput, "Attack_Start_Charge_C2")},
                 {strike("atk1", 0.266667F, 0.4F, {row("Test_Axe_Charged")})}),
        fragment("Attack_Start_Charge_C2", 0.7F,
                 {transition(instant, 0.0F, 0.333333F, AttackInput::notHeld, "Attack_Primary_Strike2"),
                  transition(instant, 0.333333F, 0.666667F, AttackInput::released, "Attack_Heavy"),
                  transition(instant, 0.666667F, -1.0F, AttackInput::always, "Attack_Heavy_Charged")}),
        fragment("Attack_Primary_Strike2", 1.933333F,
                 {transition(buffered, 0.366667F, 1.133333F, AttackInput::virtualInput, "Attack_Start_Charge"),
                  transition(instant, 1.133333F, -1.0F, AttackInput::virtualInput, "Attack_Start_Charge")},
                 {strike("atk1", 0.366667F, 0.466667F, {row("Test_Axe_Ender")})}),
    };
    // The dodge windows of the first swing (made-up times in the form of the data: a cancel window from the start, a buffer window, and a
    // later cancel window).
    axe.fragments[1].dodge = {{0.0F, 0.1F, false}, {0.1F, 0.333333F, true}, {0.333333F, -1.0F, false}};
    gamedata::WeaponActionSet rapier;
    rapier.tags = {"1H_Melee", "TestRapier"};
    rapier.fragments = {
        fragment("Attack_Start_Charge", 0.8F,
                 {transition(instant, 0.0F, 0.333333F, AttackInput::notHeld, "Attack_Primary"),
                  transition(instant, 0.333333F, -1.0F, AttackInput::released, "Attack_Primary")}),
        fragment("Attack_Primary", 1.2F, {transition(instant, 0.3F, -1.0F, AttackInput::press, "Attack_Start_Charge_C2")},
                 {strike("atk1", 0.333333F, 0.433333F, {row("Test_Rapier_Primary")})}),
        fragment("Attack_Start_Charge_C2", 0.8F, {transition(instant, 0.0F, -1.0F, AttackInput::notHeld, "Attack_Primary_Strike2")}),
        fragment("Attack_Primary_Strike2", 1.3F, {transition(instant, 0.733333F, -1.0F, AttackInput::press, "Attack_Start_Charge")},
                 {strike("atk1", 0.3F, 0.4F, {row("Test_Rapier_TwoThirds")}), strike("atk2", 0.533333F, 0.633333F, {row("Test_Rapier_TwoThirds")})}),
    };
    gamedata::WeaponActionTables tables;
    tables.weapons = {axe, rapier};
    return tables;
}

inline world::ItemInstance test_item(const char* id) {
    world::ItemInstance item;
    item.instance.fill(0x5A);
    item.itemId = id;
    item.index = 1;
    item.quantity = 1;
    item.gearScore = 100;
    return item;
}

/** @return The amount of each damage number of a client from the message `from` on (the first entry of the body). */
inline std::vector<float> amounts_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<float> amounts;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        const Bytes& body = client.got[index].body;
        if (client.got[index].type != server::rep::kTypeDamageDealt || body.size() < 56) {
            continue;
        }
        const std::uint32_t bits = static_cast<std::uint32_t>(body[52]) << 24 | static_cast<std::uint32_t>(body[53]) << 16
                                   | static_cast<std::uint32_t>(body[54]) << 8 | body[55];
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        amounts.push_back(value);
    }
    return amounts;
}

/** The contact of a weapon with the hit volume of a creature, as a client reports it. */
inline InputContactItem contact_with(const world::Entity& creature) {
    InputContactItem contact;
    contact.groupActor = creature.actor.bytes;
    contact.group = creature.id;
    contact.actor = creature.actor.bytes;
    contact.entity = creature.id;
    contact.facet = creature.creature.body->target(creature.id, creature.creature.body->components[1]);
    contact.point = {creature.position.x - 0.8F, creature.position.y, creature.position.z + 1.0F};
    return contact;
}

/** The input of one test client: one report for each simulation step, with the newest entry and the four entries before it. */
struct Attacker {
    ServerHost& host;
    FakeClient& client;
    state::WorldPosition position;
    std::uint16_t input{10};
    std::deque<InputEntry> history{};
    std::vector<FakeClient*> others{};

    InputEntry entry(std::vector<unsigned> bits = {}) {
        InputEntry made = report_entry(++input, position.x, position.y, position.z);
        made.actionBits = std::move(bits);
        return made;
    }
    void send(const InputEntry& made) {
        history.push_front(made);
        if (history.size() > 5) {
            history.pop_back();
        }
        host.clock.advance(kTick);
        host.datagram(client.peer, client.input_report(std::vector<InputEntry>(history.begin(), history.end())));
        host.tick();
        client.exchange(host);
        for (FakeClient* other : others) {
            other->exchange(host);
        }
    }
    /** One step with these bits and contacts. @return Its input number. */
    std::uint16_t step(std::vector<unsigned> bits = {}, std::vector<InputContactItem> contacts = {}) {
        InputEntry made = entry(std::move(bits));
        made.contacts = std::move(contacts);
        send(made);
        return input;
    }
    void idle(int steps) {
        for (int index = 0; index < steps; ++index) {
            step();
        }
    }
    /** Idle steps until the next step is the input number `target` (16 bits, with wrap). */
    void until(std::uint16_t target) {
        while (static_cast<std::uint16_t>(target - input) > 1 && static_cast<std::uint16_t>(target - input) < 0x8000) {
            step();
        }
    }
    /** A click: the press, then the release in the next step (as the live client). @return The input number of the release. */
    std::uint16_t click() {
        step({kActionAttack});
        return step({kActionAttackRelease});
    }
    /** A hold: the press, `hold` steps with the hold bit, and the release. @return The input number of the press. */
    std::uint16_t hold(int steps) {
        const std::uint16_t start = step({kActionAttack});
        for (int index = 0; index < steps; ++index) {
            step({kActionAttackHold});
        }
        step({kActionAttackRelease});
        return start;
    }
};

/** The world of a case: a character with the axe of the test in weapon slot 1 and the rapier in slot 2, and its client. */
struct Range {
    ServerHost host;
    FakeClient a{27000};
    Player alpha;

    explicit Range(bool actions = true) {
        if (!host.creatures.set({wolf_slice()}, {wolf_kind()}, {}, {{18, kWolfHealth}})) {
            throw std::runtime_error("test: the creature data are not valid");
        }
        if (!host.combat.set(test_combat())) {
            throw std::runtime_error("test: the combat tables are not valid");
        }
        if (actions && !host.weaponActions.set(test_actions())) {
            throw std::runtime_error("test: the weapon actions are not valid: " + host.weaponActions.error());
        }
        alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
        host.enter(a, alpha.registration());
        host.datagram(a.peer, a.report_at(1, kPlace.x, kPlace.y, kPlace.z));
        host.steps(60, {&a});
        arm(kCharacterA, "TestAxe", world::paperdoll::kMainHand1);
        arm(kCharacterA, "TestRapier", world::paperdoll::kMainHand1 + 1);
    }
    void arm(const char* character, const char* item, std::uint32_t slot) {
        host.world.find_character(character)->equipment.slots[slot].item = test_item(item);
    }
    world::Entity* wolf(float east) {
        world::Entity* entity = host.world.creatures().spawn("Test_Wolf", state::WorldPosition{kPlace.x + east, kPlace.y, kHeight}, 0.0F).entity;
        host.steps(60, {&a});
        return entity;
    }
    [[nodiscard]] const world::AttackTrack& track(std::uint32_t session = 1) {
        return host.session(session).attack_track();
    }
    [[nodiscard]] const world::AttackSequences::State* state(const char* character = kCharacterA) {
        return host.world.combat().sequences().state_of(character);
    }
    /** @return The fragment names of the visits of a character, oldest first. */
    std::vector<std::string> visits(const char* character = kCharacterA) {
        std::vector<std::string> names;
        const world::AttackSequences::State* found = state(character);
        if (found != nullptr && found->actions != nullptr) {
            for (const world::AttackVisit& visit : found->visits) {
                names.push_back(found->actions->fragments[visit.fragment].name);
            }
        }
        return names;
    }
};

inline std::string joined(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "" : " ") + name;
    }
    return text;
}

} // namespace soulwarden::backend::tests::attack_fixture
