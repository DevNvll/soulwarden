// Cases for the melee attack of a player on a creature (stage 3.2a): the reader of the three
// lists of an input report, the attack inputs of a client, the combat tables and the
// damage rule, a hit with its bytes, each refusal of the hit check, the death by a hit, the
// weapon key, the case with more than one session, and the remote configuration document of
// the client. All ids, names, and files are made up: no value of a capture is here.

#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/client_input.h"
#include "server/rep/combat_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using nlohmann::json;
using server::rep::InputTail;
using server::rep::read_client_input;
using world::AttackTrack;
using world::HitVerdict;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterC = "cccccccc-0000-4000-8000-00000000000c";
constexpr const char* kSteamIdC = "76561202255233010";
constexpr float kHeight = 50.0F;
/** The place of the character of A in each case. */
constexpr state::WorldPosition kPlace{1000.0F, 1000.0F, kHeight};
/** One simulation step of the client: 30 each second. */
constexpr std::chrono::microseconds kTick{33333};
/** Slots of the two test slices: movement, vitals, marker. */
constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
constexpr std::uint32_t kRabbitSlots[] = {2, 5, 17};
/** The actions of the wolf slice of the test: the index of the action, and the index of its fragment. */
constexpr std::uint32_t kWolfReaction = 25;
constexpr std::int32_t kWolfReactionFragment = 15;
constexpr std::uint32_t kWolfDeath = 27;
constexpr std::int32_t kWolfDeathFragment = 4;
constexpr float kWolfHealth = 1690.875977F;
constexpr float kDummyHealth = 337.5F;
/** The damage of the test sword for a character of the level 1 with no attribute point (the worked example of the research). */
constexpr float kSwordDamage = 64.0F;
/** The same for the test wolf: a light attack (slash), and a heavy attack (thrust, with the weakness of the wolf). */
constexpr float kWolfLight = 64.0F * 0.65F;
constexpr float kWolfHeavy = 64.0F * 1.2F * 0.65F * 1.2F;
/** Indexes of the damage types of the test tables. */
constexpr std::uint32_t kSlash = 1;
constexpr std::uint32_t kThrust = 2;
constexpr std::uint32_t kStrike = 3;

bool near(float left, float right, float tolerance = 0.001F) {
    return std::abs(left - right) <= tolerance;
}

/** A slice with a hit volume, a reaction action, and a death action. */
gamedata::CreatureSlice test_slice(const char* path, std::uint8_t mark, const std::uint32_t slots[3], float radius) {
    gamedata::CreatureSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x1000000000000000ULL + mark;
    slice.interestRadius = radius;
    slice.movementSlot = slots[0];
    slice.vitalsSlot = slots[1];
    slice.markerSlot = slots[2];
    slice.slots = {{slots[0], "ActionList", 11}, {slots[1], "Vitals", 15}, {slots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"},
                        {slice.root + 3, 0x0000000000000C03ULL, "Head/HitVolume"},
                        {slice.root + 7, 0x0000000000000B02ULL, "Marker/Marker"}};
    for (std::uint32_t index = 0; index <= kWolfDeath; ++index) {
        gamedata::CreatureSlice::Action action;
        action.name = index == kWolfDeath ? "Death" : index == kWolfReaction ? "Reaction" : "Action_" + std::to_string(index);
        action.fragment = "Fragment";
        action.fragmentIndex = index == kWolfDeath      ? kWolfDeathFragment
                               : index == kWolfReaction ? kWolfReactionFragment
                                                        : static_cast<std::int32_t>(index);
        action.serverStarted = index == kWolfDeath || index == kWolfReaction;
        slice.actions.push_back(std::move(action));
    }
    return slice;
}

gamedata::CreatureKind test_kind(const char* id, std::uint32_t vitalsId, float factor, std::uint32_t level, std::uint32_t slice) {
    gamedata::CreatureKind kind;
    kind.id = id;
    kind.vitals = id;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = vitalsId;
    kind.categoryId = vitalsId + 1;
    kind.name = id;
    kind.vitalsName = id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = factor;
    kind.level = level;
    kind.bodies = {{slice, 100}};
    return kind;
}

/** The creature data of the cases: a wolf, a rabbit, a dummy with no defense, and a kind with no row in the combat tables. */
bool set_creatures(ServerHost& host) {
    std::vector<gamedata::CreatureSlice> slices{test_slice("slices/test/wolf.dynamicslice", 0x11, kWolfSlots, 100.0F),
                                                test_slice("slices/test/rabbit.dynamicslice", 0x22, kRabbitSlots, 35.0F),
                                                test_slice("slices/test/plain.dynamicslice", 0x33, kWolfSlots, 60.0F)};
    // A slice with no reaction action: the creature keeps its pose.
    slices[2].actions[kWolfReaction].name = "Action_25";
    std::vector<gamedata::CreatureKind> kinds{test_kind("Test_Wolf", 0x11112222, 1.0F, 18, 0),
                                              test_kind("Test_Rabbit", 0x55556666, 0.05F, 1, 1),
                                              test_kind("Test_Dummy", 0x9999AAAA, 1.0F, 1, 2),
                                              test_kind("Test_Stranger", 0xDDDDEEEE, 1.0F, 1, 0)};
    return host.creatures.set(std::move(slices), std::move(kinds), {}, {{1, kDummyHealth}, {18, kWolfHealth}});
}

/**
 * The combat tables of the cases. The sword has the values of the worked example of the
 * research: base damage 64, the factors 0.0085 and 0.0065, a light slash row with the factor
 * 1, and a heavy thrust row with the factor 1.2. The wolf does not take 0.35 of a damage and
 * has the weakness 0.2 for thrust damage.
 */
gamedata::CombatData test_combat() {
    gamedata::CombatData data;
    data.constants = {0.025F, 0.0112F, 0.6667F, 5, 100, 500};
    data.damageTypes = {{"True", 1, gamedata::DamageCategory::direct},
                        {"Slash", 4, gamedata::DamageCategory::physical},
                        {"Thrust", 5, gamedata::DamageCategory::physical},
                        {"Strike", 6, gamedata::DamageCategory::physical},
                        {"Fire", 9, gamedata::DamageCategory::elemental}};
    data.attributeMinimum = 5;
    // A list for each attribute that a weapon can have a factor for: the loader refuses an empty one.
    for (const unsigned attribute : {world::attribute::kStrength, world::attribute::kDexterity,
                                     world::attribute::kIntelligence, world::attribute::kFocus}) {
        for (int value = 0; value < 96; ++value) {
            data.attributeDamage[attribute].push_back(1.25F * static_cast<float>(value));
        }
    }
    data.damageRows = {{"Test_Sword_Light", "Light", kSlash, 1.0F, 0.0F},
                       {"Test_Sword_Heavy", "Heavy", kThrust, 1.2F, 0.0F},
                       {"Test_Hammer_Light", "Light", kStrike, 1.0F, 10.0F},
                       {"Test_Torch_Light", "Light", 4, 1.0F, 0.0F},
                       {"Test_Pure_Light", "Light", 0, 1.0F, 0.0F}};
    gamedata::WeaponRow sword;
    sword.id = "TestSwordRow";
    sword.melee = true;
    sword.tags = "1H_Melee,Sword";
    sword.baseDamage = kSwordDamage;
    sword.scaling[world::attribute::kStrength] = 0.0085F;
    sword.scaling[world::attribute::kDexterity] = 0.0065F;
    sword.lightRow = 0;
    sword.heavyRow = 1;
    gamedata::WeaponRow hammer = sword;
    hammer.id = "TestHammerRow";
    hammer.baseDamage = 93.0F;
    hammer.lightRow = 2;
    hammer.heavyRow.reset();
    gamedata::WeaponRow bow = sword;
    bow.id = "TestBowRow";
    bow.melee = false;
    gamedata::WeaponRow torch = sword;
    torch.id = "TestTorchRow";
    torch.lightRow = 3;
    gamedata::WeaponRow pure = sword;
    pure.id = "TestPureRow";
    pure.lightRow = 4;
    pure.gearScoreRate = 0.02F;
    data.weapons = {sword, hammer, bow, torch, pure};
    data.items = {{"testsword", 0}, {"testhammer", 1}, {"testbow", 2}, {"testtorch", 3}, {"testpure", 4}};
    const std::size_t types = data.damageTypes.size();
    gamedata::DefenseRow none;
    none.weakness.assign(types, 0.0F);
    none.absorption.assign(types, 0.0F);
    gamedata::DefenseRow wolf = none;
    wolf.physical = 0.35F;
    wolf.elemental = 0.5F;
    wolf.weakness[kThrust] = 0.2F;
    wolf.absorption[kStrike] = 0.25F;
    // A row of the game with values above 1: the creature takes no physical damage.
    gamedata::DefenseRow wall = none;
    wall.physical = 2.35F;
    wall.absorption[4] = -1.0F;
    data.defenses = {none, wolf, wall};
    data.vitals = {{"test_dummy", 0}, {"test_wolf", 1}, {"test_rabbit", 1}, {"test_wall", 2}};
    return data;
}

world::ItemInstance test_item(const char* id, std::uint32_t gearScore = 100) {
    world::ItemInstance item;
    item.instance.fill(0x5A);
    item.itemId = id;
    item.index = 1;
    item.quantity = 1;
    item.gearScore = gearScore;
    return item;
}

/** Puts an item into a weapon slot of the paperdoll of a character that is in the world. */
void arm(ServerHost& host, const char* characterId, const char* itemId, std::uint32_t slot = world::paperdoll::kMainHand1,
         std::uint32_t gearScore = 100) {
    host.world.find_character(characterId)->equipment.slots[slot].item = test_item(itemId, gearScore);
}

/** A client enters the world with the character of a player and sends its first report. */
void enter_at(ServerHost& host, FakeClient& client, const Player& player, const state::WorldPosition& position) {
    host.enter(client, player.registration());
    host.datagram(client.peer, client.report_at(1, position.x, position.y, position.z));
    host.steps(60, {&client});
}

/** @return The contact of a weapon with the hit volume of a creature, as a client reports it. */
InputContactItem contact_with(const world::Entity& creature) {
    InputContactItem contact;
    contact.groupActor = creature.actor.bytes;
    contact.group = creature.id;
    contact.actor = creature.actor.bytes;
    contact.entity = creature.id;
    contact.facet = creature.creature.body->target(creature.id, creature.creature.body->components[1]);
    contact.point = {creature.position.x - 0.8F, creature.position.y, creature.position.z + 1.3F};
    return contact;
}

/**
 * The input of one test client, as the live client sends it: one report for each simulation
 * step, with the newest entry and the four entries before it.
 */
struct Attacker {
    ServerHost& host;
    FakeClient& client;
    state::WorldPosition position;
    std::uint16_t input{10};
    std::deque<InputEntry> history{};

    InputEntry entry(std::vector<unsigned> bits = {}) {
        InputEntry entry = report_entry(++input, position.x, position.y, position.z);
        entry.actionBits = std::move(bits);
        return entry;
    }
    /** One simulation step: the clock goes one step on, the report arrives, and the server ticks. */
    void send(const InputEntry& entry) {
        history.push_front(entry);
        if (history.size() > 5) {
            history.pop_back();
        }
        host.clock.advance(kTick);
        host.datagram(client.peer, client.input_report(std::vector<InputEntry>(history.begin(), history.end())));
        host.tick();
        client.exchange(host);
    }
    void idle(int steps) {
        for (int index = 0; index < steps; ++index) {
            send(entry());
        }
    }
    /** The attack input: the button goes down, stays down for `hold` steps, and goes up. @return The input number of the start. */
    std::uint16_t swing(int hold) {
        send(entry({kActionAttack}));
        const std::uint16_t start = input;
        for (int index = 0; index < hold; ++index) {
            send(entry({kActionAttackHold}));
        }
        send(entry({kActionAttackRelease}));
        return start;
    }
    void contact(std::vector<InputContactItem> contacts) {
        InputEntry report = entry();
        report.contacts = std::move(contacts);
        send(report);
    }
    /** A light attack of a sword that touches these targets: the contact comes 12 steps after the start. */
    void light(std::vector<InputContactItem> contacts) {
        swing(1);
        idle(9);
        contact(std::move(contacts));
    }
    /** A heavy attack: the input stays on for 8 steps, and the contact comes 15 steps after the start. */
    void heavy(std::vector<InputContactItem> contacts) {
        swing(8);
        idle(5);
        contact(std::move(contacts));
    }
};

/** @return The bodies of the messages "on damage dealt" that a client got from the message `from` on. */
std::vector<Bytes> numbers_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<Bytes> bodies;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == server::rep::kTypeDamageDealt) {
            bodies.push_back(client.got[index].body);
        }
    }
    return bodies;
}

/** @return The records that a client got from the message `from` on for one interest number. */
std::vector<Record> records_for(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<Record> records;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest == interest) {
                records.push_back(record);
            }
        }
    }
    return records;
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

/** @return The 32-bit float at the start of a field, or a value that is not a number for a short field. */
float float_of(std::span<const std::uint8_t> value) {
    return value.size() >= 4 ? float_at(Bytes(value.begin(), value.end()), 0) : std::numeric_limits<float>::quiet_NaN();
}

/** The world of a hit case: one character with a sword at the place, and its client. */
struct Arena {
    ServerHost host;
    FakeClient a{27000};
    Player alpha;

    explicit Arena(bool tables = true) {
        set_creatures(host);
        if (tables && !host.combat.set(test_combat())) {
            throw std::runtime_error("test: the combat tables are not valid");
        }
        alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
        enter_at(host, a, alpha, kPlace);
        arm(host, kCharacterA, "TestSword");
    }
    world::Entity* spawn(const char* kind, float east) {
        world::Entity* entity =
            host.world.creatures().spawn(kind, state::WorldPosition{kPlace.x + east, kPlace.y, kHeight}, 0.0F).entity;
        host.steps(60, {&a});
        return entity;
    }
    [[nodiscard]] const AttackTrack& track() {
        return host.session(1).attack_track();
    }
};

/** HT1: the reader of an input report with items in its three lists, on fixtures. */
void reader_lists(const Options&) {
    // The contact of the research fixture (made-up ids): provider 203, a target with the
    // entity group id 0x100000004, the point 1001 1000 0.5.
    InputContactItem fixture;
    fixture.provider = 203;
    fixture.actor = {0x04, 0, 0, 0, 0x01, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0};
    fixture.groupActor = fixture.actor;
    fixture.group = 0x0000000100000004ULL;
    fixture.entity = 0x0000000100000004ULL;
    fixture.facet = 0x75BAF23C6AD964C6ULL;
    fixture.point = {1001.0F, 1000.0F, 0.5F};
    fixture.normal = {-1.0F, 0.0F, 0.0F};
    fixture.direction = {1.0F, 0.0F, 0.0F};
    InputEntry newest = report_entry(4321, 8861.25F, 4252.75F, 138.2F);
    newest.contacts = {fixture};
    const Bytes body = input_entry_body(newest);
    const std::string tail = std::string("000001") + "000000cb" + "04000000010000002000000000000000" + "0000000100000004"
                             + "04000000010000002000000000000000" + "0000000100000004" + "75baf23c6ad964c6"
                             + "447a4000447a00003f000000" + "bf8000000000000000000000" + "3f8000000000000000000000";
    auto input = read_client_input(body);
    check(tail.size() == 99 * 2 && hex_text(body).find(tail) != std::string::npos,
          "the fixture has the three list counts 0, 0, 1 and one contact item of 96 bytes: the bytes of the research fixture");
    check(input && input->tail == InputTail::read && input->entries == 1 && input->steps.size() == 1
              && input->steps[0].sequence == 4321 && input->steps[0].contacts.size() == 1 && input->hasCharacter
              && input->characterX == 8861.25F && input->characterY == 4252.75F,
          "entry with one melee contact: the reader reads the entry to its end, with the contact and the position of the "
          "character in the same entry");
    if (input && input->steps.size() == 1 && input->steps[0].contacts.size() == 1) {
        const server::rep::InputContact& contact = input->steps[0].contacts[0];
        check(contact.provider == 203 && contact.actor == fixture.actor && contact.entity == fixture.entity
                  && contact.groupActor == fixture.groupActor && contact.group == fixture.group && contact.facet == fixture.facet
                  && contact.point == fixture.point && contact.normal == fixture.normal && contact.direction == fixture.direction,
              "the contact has the provider, the two references (actor id, entity group id, facet target), the point, the "
              "normal, and the direction of the fixture");
    } else {
        check(false, "the contact of the fixture was not read");
    }

    // Three entries, newest first. The second and the third one do not send their flags:
    // they have the flags of the entry before them, and thus the position field.
    InputContactItem second = fixture;
    second.provider = 0xFFFFFFFFU;
    second.entity = 0x0000000100000006ULL;
    InputEntry one = report_entry(30, 100.0F, 200.0F, kHeight);
    one.contacts = {fixture, second};
    InputEntry two = report_entry(29, 99.0F, 200.0F, kHeight);
    two.actionBits = {kActionAttack};
    two.projectileHits = 2;
    two.shots = 1;
    InputEntry three = report_entry(28, 98.0F, 200.0F, kHeight);
    three.actionBits = {kActionAttackHold, kActionAttackRelease, kActionWeapon2};
    three.object140 = 2;
    input = read_client_input(input_entries_body({one, two, three}));
    check(input && input->entries == 3 && input->steps.size() == 3 && input->sequence == 30 && input->characterX == 100.0F,
          "a report with three entries: the reader reads each one, and the report gives the position of the newest entry");
    if (input && input->steps.size() == 3) {
        const auto& steps = input->steps;
        check(steps[0].sequence == 30 && steps[0].contacts.size() == 2 && steps[0].contacts[1].entity == second.entity
                  && steps[0].contacts[1].provider == 0xFFFFFFFFU && !steps[0].attackPress && steps[0].weaponKey == 0,
              "the newest entry has two contacts and no attack input");
        check(steps[1].sequence == 29 && steps[1].attackPress && !steps[1].attackHold && !steps[1].attackRelease
                  && steps[1].contacts.empty() && steps[1].projectileHits == 2 && steps[1].shots == 1,
              "the second entry (with the flags of the first one) has the attack input, two items in the first list, and "
              "one item in the second list: the reader passes their 104 and 192 bytes");
        check(steps[2].sequence == 28 && !steps[2].attackPress && steps[2].attackHold && steps[2].attackRelease
                  && steps[2].weaponKey == 2 && steps[2].contacts.empty(),
              "the third entry has the object of mask bit 8 with its vectors, the two other attack inputs, and the key of "
              "weapon 2");
    } else {
        check(false, "the three entries were not read");
    }
    InputEntry key = report_entry(31, 100.0F, 200.0F, kHeight);
    key.actionBits = {kActionWeapon1};
    input = read_client_input(input_entry_body(key));
    check(input && input->steps.size() == 1 && input->steps[0].weaponKey == 1, "the key of weapon 1");

    // An entry that the reader cannot read ends the list: the entries after it are differences from it.
    InputEntry bad = two;
    bad.object140 = 3;
    input = read_client_input(input_entries_body({one, bad, three}));
    check(input && input->entries == 3 && input->steps.size() == 1 && input->tail == InputTail::read && input->hasCharacter,
          "a second entry with a value that the client does not send: the report gives the newest entry only");
    // A body that ends in a contact item.
    InputEntry cut = newest;
    cut.value470 = false;
    cut.cut = 10 + 40;
    input = read_client_input(input_entry_body(cut));
    check(input && input->tail == InputTail::shortBody && input->steps.empty() && !input->hasCharacter && input->hasCamera,
          "a body that ends in a contact item: no attack data and no position, and the entry keeps its camera");
    // A count with no item: 100,000 contacts in a body of some hundred bytes.
    InputEntry large = newest;
    large.contacts.clear();
    large.contactCount = 100000;
    input = read_client_input(input_entry_body(large));
    check(input && input->tail == InputTail::shortBody && input->steps.empty(),
          "a list count that is larger than the body: the reader stops and makes no list of that size");
}

/** HT2: the attack inputs of one client: which input a contact can come from. */
void attack_inputs(const Options&) {
    using namespace world::combat_limits;
    ManualClock clock;
    const auto step = [&clock]() {
        clock.advance(kTick);
        return clock.now();
    };
    AttackTrack track;
    check(track.newest() == nullptr && track.press_for(100) == nullptr && track.newer(100) && !track.restarted(100, clock.now()),
          "a new track has no attack input, and each input number is new");
    // A click: the input goes on at 101, stays on at 102, and goes off at 103.
    track.step(100, false, false, false, step());
    track.step(101, true, false, false, step());
    check(track.newest() != nullptr && track.newest()->on && track.newest()->input == 101 && track.presses() == 1
              && track.press_for(101) == nullptr && track.press_for(103) == nullptr && track.press_for(104) == track.newest(),
          "the attack input starts at its input number; a contact can come from it 3 input steps after its start, not sooner");
    track.step(102, false, true, false, step());
    const auto ended = step();
    track.step(103, false, false, true, ended);
    check(!track.newest()->on && track.newest()->holdSteps == 1 && track.newest()->endInput == 103 && track.newest()->ended == ended
              && track.newest()->holdSteps < kHeavyHoldSteps,
          "the input went off at 103 after 1 step: a light attack");
    check(!track.newer(103) && !track.newer(101) && track.newer(104) && !track.restarted(103, clock.now()),
          "an input number that the track got is not new (a client sends an input in more than one report)");

    // The limit between a light and a heavy attack: the input stays on for 7 steps, and for 8 steps.
    std::uint16_t input = 103;
    const auto hold = [&](unsigned steps) {
        for (int idle = 0; idle < 20; ++idle) {
            track.step(++input, false, false, false, step());
        }
        track.step(++input, true, false, false, step());
        for (unsigned index = 0; index < steps; ++index) {
            track.step(++input, false, true, false, step());
        }
        const unsigned on = track.newest()->on ? track.newest()->holdSteps : 0;
        track.step(++input, false, false, true, step());
        return !track.newest()->on && track.newest()->holdSteps == on ? on : 999;
    };
    check(hold(7) == 7 && 7 < kHeavyHoldSteps && hold(8) == 8 && 8 >= kHeavyHoldSteps && track.presses() == 3,
          "an input that stays on for 7 steps is a light attack, and one of 8 steps is a heavy attack (live data of a sword)");
    // A long hold: the input is on, and its steps go on.
    for (int idle = 0; idle < 20; ++idle) {
        track.step(++input, false, false, false, step());
    }
    track.step(++input, true, false, false, step());
    const std::uint16_t start = input;
    for (int index = 0; index < 120; ++index) {
        track.step(++input, false, true, false, step());
    }
    check(track.newest()->on && track.newest()->input == start && track.newest()->holdSteps == 120
              && track.press_for(static_cast<std::uint16_t>(start + 103)) == track.newest(),
          "an input that stays on for 4 s is one attack input: a contact 103 steps after its start comes from it (the client "
          "attacks again and again while the input is on)");
    track.step(++input, false, false, true, step());
    check(!track.newest()->on && track.newest()->holdSteps == 120 && track.newest()->endInput == input, "it goes off at its release");

    // Fast clicks: the contact of a click comes in the step of the next click.
    for (int idle = 0; idle < 20; ++idle) {
        track.step(++input, false, false, false, step());
    }
    track.step(++input, true, false, false, step());
    const std::uint16_t first = input;
    track.step(++input, false, true, false, step());
    track.step(++input, false, false, true, step());
    for (int idle = 0; idle < 8; ++idle) {
        track.step(++input, false, false, false, step());
    }
    track.step(++input, true, false, false, step());
    const AttackTrack::Press* early = track.press_for(input);
    check(track.newest()->input == input && early != nullptr && early != track.newest() && early->input == first && !early->on
              && early->holdSteps == 1 && track.press_for(static_cast<std::uint16_t>(input + 3)) == track.newest(),
          "a second click 11 steps after the first one: a contact in the step of the second click comes from the first "
          "click, and a contact 3 steps later comes from the second one (live data of a hatchet)");
    // An attack input with no release step: the next one ends it.
    track.step(++input, false, true, false, step());
    track.step(++input, true, false, false, step());
    check(track.press_for(static_cast<std::uint16_t>(input + 1)) != nullptr && !track.press_for(static_cast<std::uint16_t>(input + 1))->on
              && track.press_for(static_cast<std::uint16_t>(input + 1))->endInput == input,
          "an attack input whose end the session did not get ends at the start of the next one");

    // One attack hurts one creature one time. The hits are values of the character, not of a
    // session (world::HitLimits): the time rule applies to each session of the character, and
    // the rule of the input steps to the session and the input sequence of the hit.
    world::HitLimits hits;
    check(!hits.same_attack(0x77, 20, 1, 0, clock.now()), "no hit before: a contact is a new attack");
    hits.note_hit(world::HitLimits::Hit{0x77, clock.now(), 20, 1, 0});
    const auto later = clock.now() + std::chrono::milliseconds(300);
    check(hits.same_attack(0x77, 24, 1, 0, later) && !hits.same_attack(0x77, 24, 2, 0, later)
              && !hits.same_attack(0x77, 24, 1, 1, later) && !hits.same_attack(0x78, 24, 1, 0, later)
              && hits.same_attack(0x77, 900, 2, 0, clock.now() + std::chrono::milliseconds(200)),
          "a contact 4 input steps after a hit of the same creature is the same attack in the session and the input "
          "sequence of the hit only; a contact 200 ms after it is the same attack for each session of the character");
    // Combat::claim keeps each hit of a character. See the cases HT5 and HT12.

    // The input number has 16 bits.
    AttackTrack wrap;
    wrap.step(0xFFFE, true, false, false, step());
    wrap.step(0xFFFF, false, true, false, step());
    wrap.step(0x0000, false, true, false, step());
    wrap.step(0x0001, false, true, false, step());
    check(wrap.newest() != nullptr && wrap.newest()->input == 0xFFFE && wrap.newest()->holdSteps == 3 && wrap.newer(2)
              && !wrap.newer(0xFFFF) && wrap.press_for(1) == wrap.newest(),
          "the input number goes from 0xFFFF to 0: the attack input keeps its steps");
    // The input numbers of the client start again.
    check(!wrap.newer(0x9000) && !wrap.restarted(0x9000, clock.now()), "an input number that is not newer is not new at once");
    clock.advance(kSequenceRestart);
    check(!wrap.newer(0x9000) && wrap.restarted(0x9000, clock.now()) && !wrap.restarted(2, clock.now()),
          "after 2 s with no newer input number, a number that is not newer is the start of a new sequence");
    wrap.step(0x9000, false, false, false, clock.now());
    check(wrap.newest() == nullptr && wrap.press_for(0x9010) == nullptr && wrap.newer(0x9001),
          "a new sequence of input numbers has no attack input of the old one");
}

json tables_document() {
    return json{
        {"format", 1},
        {"constants",
         {{"levelDamage", 0.025},
          {"gearScoreRate", 0.0112},
          {"gearScoreSmallerRate", 0.6667},
          {"gearScoreInterval", 5},
          {"gearScoreMinimum", 100},
          {"gearScoreLimit", 500}}},
        {"damageTypes", {{"True", 1, "True"}, {"Slash", 4, "Physical"}, {"Fire", 9, "Elemental"}}},
        {"attributes",
         {{"minimum", 5},
          {"strength", {0.0, 1.25, 2.5}},
          {"dexterity", {0.0, 1.25}},
          {"intelligence", {0.0}},
          {"focus", {0.0, 2.0}}}},
        {"damageRows", {{"Test_Sword_Light", "Sword", "Light", "Slash", 1.0, 0.0}, {"Test_Sword_Heavy", "Sword", "Heavy", "Fire", 1.2, 3.0}}},
        {"weapons",
         {{"TestSwordRow", "Combat Melee", "1H_Melee,Sword", 64.0, 0.0085, 0.0065, 0.0, 0.25, "Test_Sword_Light", "test_sword_heavy", 0.0},
          {"TestBowRow", "Combat Ranged", "Bow", 84.0, 0.004, 0.0095, 0.0, 0.0, "", "", 0.03}}},
        {"items", {{"testsword", 0}, {"testbow", 1}}},
        {"defenses", {{0.35, 0.45, {{"Fire", 0.3}}, {{"Slash", 0.1}}}, {2.35, 0.0, json::object(), {{"Fire", -15.0}}}}},
        {"vitals", {{"test_wolf", 0}, {"test_wall", 1}}}};
}

/** HT3: the combat tables file, and the damage rule with the worked example of the research. */
void tables_and_rule(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "combat-tables";
    std::filesystem::create_directories(folder);
    const auto write = [&folder](const char* name, const json& document) {
        const std::filesystem::path path = folder / name;
        std::ofstream(path, std::ios::binary) << document.dump();
        return path.string();
    };
    gamedata::CombatTables tables;
    check(!tables.load((folder / "none.json").string()) && !tables.loaded() && tables.error() == "the file is not there"
              && tables.weapon_of("TestSword") == nullptr && tables.defense_of("Test_Wolf") == nullptr,
          "no file: no tables, the cause is in the error text, and each lookup finds nothing");
    check(tables.load(write("tables.json", tables_document())) && tables.loaded() && tables.error().empty(),
          "a file in the form of the tool loads");
    const gamedata::WeaponRow* sword = tables.weapon_of("TESTSWORD");
    const gamedata::WeaponRow* bow = tables.weapon_of("TestBow");
    check(sword != nullptr && sword->id == "TestSwordRow" && sword->melee && sword->baseDamage == 64.0F
              && sword->scaling[world::attribute::kStrength] == 0.0085F && sword->scaling[world::attribute::kDexterity] == 0.0065F
              && sword->scaling[world::attribute::kFocus] == 0.25F && sword->scaling[world::attribute::kConstitution] == 0.0F
              && sword->lightRow == 0U && sword->heavyRow == 1U && tables.damage_row(1).added == 3.0F
              && tables.damage_type(tables.damage_row(1).type).wire == 9,
          "the weapon of an item (the compare ignores the letter case): the base damage, the factor of each attribute at "
          "the number of the attribute, and the damage rows of its light and of its heavy attack");
    check(bow != nullptr && !bow->melee && !bow->lightRow && !bow->heavyRow && bow->gearScoreRate == 0.03F
              && tables.weapon_of("TestAxe") == nullptr,
          "a weapon with a different use is not a melee weapon, and an item that the file does not have has no weapon");
    const gamedata::DefenseRow* wolf = tables.defense_of("Test_WOLF");
    const gamedata::DefenseRow* wall = tables.defense_of("test_wall");
    check(wolf != nullptr && wolf->physical == 0.35F && wolf->elemental == 0.45F && wolf->weakness.size() == 3
              && wolf->weakness[2] == 0.3F && wolf->weakness[1] == 0.0F && wolf->absorption[1] == 0.1F && wall != nullptr
              && wall->physical == 2.35F && wall->absorption[2] == -15.0F && tables.defense_of("Test_Rabbit") == nullptr,
          "the defense values of a vitals row: the two mitigation values, and one weakness and one absorption for each "
          "damage type (0 for a type that the row does not name); a value of the game above 1 or below 0 loads");
    check(tables.attribute_damage(world::attribute::kStrength, 5) == 0.0F
              && tables.attribute_damage(world::attribute::kStrength, 6) == 1.25F
              && tables.attribute_damage(world::attribute::kStrength, 400) == 2.5F
              && tables.attribute_damage(world::attribute::kStrength, 3) == 0.0F
              && tables.attribute_damage(world::attribute::kConstitution, 50) == 0.0F
              && tables.attribute_damage(world::attribute::kFocus, 6) == 2.0F,
          "the damage value of an attribute value: the row of the value, the last row for a larger value, and 0 for a value "
          "below the first row and for an attribute with no list");

    // Files that are not complete: no tables at all.
    const auto refused = [&](const char* name, const json& document) {
        gamedata::CombatTables other;
        const bool loaded = other.load(write(name, document));
        if (loaded || other.loaded() || other.error().empty()) {
            note(std::string(name) + " was accepted");
        }
        return !loaded && !other.loaded() && !other.error().empty();
    };
    json noRows = tables_document();
    noRows.erase("damageRows");
    json wrongRow = tables_document();
    wrongRow["weapons"][0][8] = "Test_Row_That_Is_Not_There";
    json wrongType = tables_document();
    wrongType["damageRows"][0][3] = "Ice";
    json wrongWeapon = tables_document();
    wrongWeapon["items"]["testbow"] = 7;
    json wrongDefense = tables_document();
    wrongDefense["vitals"]["test_wall"] = 2;
    json noInterval = tables_document();
    noInterval["constants"]["gearScoreInterval"] = 0;
    json textCell = tables_document();
    textCell["weapons"][0][3] = "64";
    json unknownType = tables_document();
    unknownType["defenses"][0][2] = {{"Ice", 0.5}};
    json shortWeapon = tables_document();
    shortWeapon["weapons"][1] = {"TestBowRow", "Combat Ranged"};
    const std::string notJson = (folder / "not-json.json").string();
    std::ofstream(notJson, std::ios::binary) << "{\"format\": 1, \"weapons\": [";
    gamedata::CombatTables broken;
    check(refused("no-rows.json", noRows) && refused("wrong-row.json", wrongRow) && refused("wrong-type.json", wrongType)
              && refused("wrong-weapon.json", wrongWeapon) && refused("wrong-defense.json", wrongDefense)
              && refused("no-interval.json", noInterval) && refused("text-cell.json", textCell)
              && refused("unknown-type.json", unknownType) && refused("short-weapon.json", shortWeapon)
              && !broken.load(notJson) && !broken.loaded(),
          "a file that is not complete gives no tables: no damage row list, a weapon with a row that is not there, a row "
          "with a damage type that is not there, an item with a weapon index outside the list, a vitals row with a defense "
          "index outside the list, a gear score interval of 0, a number as a text, a defense value for a type that is not "
          "there, a weapon with a value less, and a file that is not JSON");

    // The damage rule, with the tables of the cases.
    Arena arena;
    const world::Combat& combat = arena.host.world.combat();
    world::Entity& attacker = *arena.host.world.find_character(kCharacterA);
    const gamedata::CombatData data = test_combat();
    const gamedata::WeaponRow& swordRow = data.weapons[0];
    const gamedata::DamageRow& light = data.damageRows[0];
    const gamedata::DamageRow& heavy = data.damageRows[1];
    const gamedata::DefenseRow& wolfDefense = data.defenses[1];
    world::Damage damage = combat.damage(attacker, swordRow, 100, light, nullptr);
    check(attacker.progression.level == 1 && attacker.attributes.value(world::attribute::kStrength) == 5
              && damage.weapon == 64.0F && damage.core == 64.0F && damage.raw == 64.0F && damage.dealt == 64.0F && damage.type == 4,
          "the worked example of the research: a sword with the base damage 64 and the gear score 100, the level 1, "
          "strength 5 and dexterity 5, a light slash row with the factor 1, and a target with no defense: 64 slash damage");
    damage = combat.damage(attacker, swordRow, 100, light, &wolfDefense);
    check(near(damage.raw, 64.0F) && near(damage.dealt, 41.6F), "a target that does not take 0.35 of a physical damage: 41.6");
    damage = combat.damage(attacker, swordRow, 100, heavy, &wolfDefense);
    check(damage.type == 5 && near(damage.raw, 76.8F) && near(damage.dealt, 76.8F * 0.65F * 1.2F) && near(damage.dealt, kWolfHeavy),
          "the heavy thrust row with the factor 1.2, and the weakness 0.2 of the target for thrust damage: 59.904");
    damage = combat.damage(attacker, data.weapons[1], 100, data.damageRows[2], &wolfDefense);
    check(damage.type == 6 && near(damage.raw, 103.0F) && near(damage.dealt, 103.0F * 0.65F * 0.75F),
          "a row that adds 10 damage, and the absorption 0.25 of the target for strike damage: (93 + 10) x 0.65 x 0.75");
    damage = combat.damage(attacker, data.weapons[3], 100, data.damageRows[3], &wolfDefense);
    check(damage.type == 9 && near(damage.dealt, 32.0F), "an elemental damage type uses the elemental mitigation (0.5): 32");
    damage = combat.damage(attacker, data.weapons[4], 100, data.damageRows[4], &wolfDefense);
    check(damage.type == 1 && near(damage.dealt, 64.0F), "a damage type with no category (true damage) has no mitigation: 64");
    // The attributes and the level.
    attacker.progression.level = 26;
    attacker.attributes.spent[world::attribute::kStrength] = 3;
    attacker.attributes.spent[world::attribute::kDexterity] = 20;
    damage = combat.damage(attacker, swordRow, 100, light, &wolfDefense);
    const float bonus = 0.0085F * 3.75F + 0.0065F * 25.0F + 0.025F * 25.0F;
    check(near(damage.core, 64.0F * (1.0F + bonus), 0.01F) && near(damage.core, 116.44F, 0.01F) && near(damage.dealt, 75.69F, 0.01F),
          "the level 26, strength 8, and dexterity 25: 64 x (1 + 0.0085 x 3.75 + 0.0065 x 25 + 0.025 x 25) = 116.44, and "
          "75.69 for the target (the values of the live run of stage 3.2a)");
    attacker.progression.level = 1;
    attacker.attributes.spent = {};
    // The gear score.
    check(near(combat.damage(attacker, swordRow, 104, light, nullptr).weapon, 64.0F)
              && near(combat.damage(attacker, swordRow, 105, light, nullptr).weapon, 64.0F * 1.0112F)
              && near(combat.damage(attacker, swordRow, 200, light, nullptr).weapon, 64.0F * std::pow(1.0112F, 20.0F), 0.01F)
              && near(combat.damage(attacker, swordRow, 60, light, nullptr).weapon, 64.0F),
          "the gear score: one factor of 1.0112 for each full interval of 5 above 100, and none below 100");
    const float atLimit = 64.0F * std::pow(1.0112F, 80.0F);
    check(near(combat.damage(attacker, swordRow, 500, light, nullptr).weapon, atLimit, 0.01F)
              && near(combat.damage(attacker, swordRow, 510, light, nullptr).weapon,
                      atLimit * std::pow(1.0F + 0.0112F * 0.6667F, 2.0F), 0.01F),
          "above the gear score 500 each interval has the smaller rate (0.0112 x 0.6667)");
    check(near(combat.damage(attacker, data.weapons[4], 110, data.damageRows[4], nullptr).weapon, 64.0F * 1.02F * 1.02F, 0.01F),
          "a weapon with its own gear score rate uses that rate");
    // Defense values of the game above 1.
    damage = combat.damage(attacker, swordRow, 100, light, &data.defenses[2]);
    const world::Damage fire = combat.damage(attacker, data.weapons[3], 100, data.damageRows[3], &data.defenses[2]);
    check(damage.dealt == 0.0F && near(damage.raw, 64.0F) && near(fire.dealt, 128.0F),
          "a mitigation above 1 gives the damage 0, not a negative damage; an absorption of -1 gives two times the damage");
}

/** HT4: one hit, with the bytes that the observer and the attacker get. */
void one_hit(const Options&) {
    Arena arena;
    ServerHost& host = arena.host;
    FakeClient& a = arena.a;
    const world::Entity* wolf = arena.spawn("Test_Wolf", 2.0F);
    const world::Entity* own = host.world.find_character(kCharacterA);
    const std::uint32_t number = interest_of(host, 1, wolf->id);
    check(wolf != nullptr && number != 0 && own != nullptr && own->position.x == kPlace.x && !own->positionEstimated,
          "the session observes the wolf 2 m from its character, and the world has the exact position of the character");
    const float playerHealth = own->vitals.health;

    // The message of the research fixture (made-up ids).
    world::DamageDealt fixture;
    fixture.target = 0x0000000100000004ULL;
    fixture.targetActor.bytes = {0x04, 0, 0, 0, 0x01, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0};
    fixture.type = 4;
    fixture.amount = 64.0F;
    check(hex_text(server::rep::damage_dealt_body(0x0000000100000002ULL, fixture))
              == std::string("ccc47f93daf6bfc3") + "0000000100000002" + "04000000010000002000000000000000" + "0000000100000004"
                     + "00000000" + "00000000" + "0000" + "01" + "04" + "42800000" + "3f800000",
          "the body of the message \"on damage dealt\" for 64 slash damage is the body of the research fixture (60 bytes)");

    Attacker attacker{host, a, kPlace};
    attacker.idle(5);
    std::size_t from = a.got.size();
    attacker.light({contact_with(*wolf)});
    check(arena.track().count(HitVerdict::accepted) == 1 && arena.track().refused() == 0
              && near(wolf->vitals.health, kWolfHealth - kWolfLight) && !wolf->creature.dead,
          "a light attack whose contact names the wolf: the claim is accepted, and the health of the wolf is 41.6 lower");
    check(log_count("world: hit: Alpha hurts creature") == 1 && log_count("(light attack, row Test_Sword_Light): 41.6 damage of the type Slash") == 1,
          "the log has one line for the hit, with the weapon, the row, and the damage");
    host.steps(10, {&a});
    std::vector<Record> records = records_for(a, number, from);
    check(records.size() == 1 && order_of(records[0]) == "(5,11) (9,15) ",
          "the observer gets one record of the wolf with the movement state and the vitals state");
    if (records.size() == 1 && records[0].fragments.size() == 2) {
        const Fragment& movement = records[0].fragments[0];
        const Fragment& vitals = records[0].fragments[1];
        check(float_of(field_of(vitals, 0, 0)) == wolf->vitals.health && vitals.fields.at({0, 2})[0] == 0
                  && vitals.fields.at({0, 3})[0] == 1 && vitals.fields.size() == 3,
              "the vitals state has the new health, no dead mark, and the change mark 1");
        const Bytes scope = movement.fields.count({0, 33}) != 0 ? movement.fields.at({0, 33}) : Bytes{};
        check(hex_text(scope) == "070001101a3c0000" && hex_text(movement.fields.at({0, 32})) == "01",
              "the movement state has the reaction action of the slice in scope 0: start count 1, fragment index 15 + 1, "
              "action index 25 + 1, the speed 1.0, the option 0");
    } else {
        check(false, "the record of the hit has no two fragments");
    }
    std::vector<Bytes> numbers = numbers_of(a, from);
    const float amount = numbers.size() == 1 && numbers[0].size() == 60 ? float_at(numbers[0], 52) : 0.0F;
    server::hub::Writer expected;
    expected.u64(own->id + server::rep::kPlayerDamageReceiver);
    expected.u64(own->id);
    expected.raw(wolf->actor.bytes);
    expected.u64(wolf->id);
    expected.u32(0);
    expected.u32(0);
    expected.u16(0);
    expected.size(1);
    expected.u8(4);
    expected.f32(amount);
    expected.f32(1.0F);
    check(numbers.size() == 1 && numbers[0] == expected.data() && numbers[0].size() == 60 && near(amount, kWolfLight),
          "the attacker gets one message \"on damage dealt\" (type 2071): the damage receiver facet of its own character, "
          "the actor id and the entity group id of the wolf, the flags 0, and one entry with the type 4 (slash), the "
          "damage, and the part 1");
    check(own->vitals.health == playerHealth, "the health of the player does not change");

    check(wolf->creature.action && wolf->creature.action->action == kWolfReaction && wolf->creature.action->ends,
          "the wolf has the reaction action with an end time");
    // The client sends each entry in 5 reports: 4 more reports with the contact entry follow,
    // the first one after a pause of 3 s (a client that did not run for that time). One hit.
    from = a.got.size();
    host.advance(std::chrono::milliseconds(3000));
    attacker.idle(4);
    host.steps(10, {&a});
    check(arena.track().count(HitVerdict::accepted) == 1 && arena.track().refused() == 0
              && near(wolf->vitals.health, kWolfHealth - kWolfLight) && numbers_of(a, from).empty(),
          "the same contact entry in the next 4 reports changes nothing, also after a pause of 3 s: one hit, and no "
          "second message");
    // The reaction has an end. Then the wolf has no action of the server.
    check(!wolf->creature.action && records_for(a, number, from).empty(),
          "after the time of the reaction (1.5 s) the wolf has no action of the server, and its observers get no record "
          "for this (the client ended the action itself)");
    // A second hit: a heavy attack. The count of the actions goes on.
    from = a.got.size();
    attacker.idle(10);
    const float before = wolf->vitals.health;
    attacker.heavy({contact_with(*wolf)});
    host.steps(10, {&a});
    records = records_for(a, number, from);
    numbers = numbers_of(a, from);
    check(arena.track().count(HitVerdict::accepted) == 2 && near(before - wolf->vitals.health, kWolfHeavy)
              && log_count("(heavy attack, row Test_Sword_Heavy): 59.9 damage of the type Thrust") == 1,
          "a heavy attack (the input stays on for 8 steps): the heavy row of the weapon, 59.904 thrust damage for the wolf");
    check(records.size() == 1 && records[0].fragments.size() == 2
              && hex_text(records[0].fragments[0].fields.at({0, 33})) == "070002101a3c0000" && numbers.size() == 1
              && numbers[0].size() == 60 && numbers[0][51] == 5 && near(float_at(numbers[0], 52), kWolfHeavy),
          "the reaction action has the start count 2, and the message has the type 5 (thrust) with the damage");
    attacker.idle(60);
    // A long hold: the client attacks again and again while the input is on, and one more
    // time after it went off (live data of a sword: contacts 24, 64, and 103 steps after the
    // start, and 10 steps after the release at 120).
    attacker.idle(60);
    const float held = wolf->vitals.health;
    attacker.send(attacker.entry({kActionAttack}));
    const std::uint16_t start = attacker.input;
    const auto hold_to = [&](unsigned step) {
        while (static_cast<std::uint16_t>(attacker.input - start) + 1U < step) {
            attacker.send(attacker.entry({kActionAttackHold}));
        }
    };
    for (const unsigned step : {24U, 64U, 103U}) {
        hold_to(step);
        InputEntry report = attacker.entry({kActionAttackHold});
        report.contacts = {contact_with(*wolf)};
        attacker.send(report);
    }
    hold_to(120);
    attacker.send(attacker.entry({kActionAttackRelease}));
    attacker.idle(9);
    attacker.contact({contact_with(*wolf)});
    check(arena.track().count(HitVerdict::accepted) == 6 && arena.track().refused() == 0
              && near(held - wolf->vitals.health, 4.0F * kWolfHeavy, 0.01F) && arena.track().presses() == 3,
          "an attack input that stays on for 4 s: the three contacts while it is on and the contact 10 steps after it went "
          "off are four heavy attacks of one attack input");
    attacker.idle(30);
    attacker.contact({contact_with(*wolf)});
    check(arena.track().count(HitVerdict::noAttack) == 1 && arena.track().count(HitVerdict::accepted) == 6,
          "a contact 41 steps after the input went off: refused (not more than 30 steps)");
    // Fast clicks: the contact of a click comes in the step of the next click.
    attacker.idle(60);
    const float clicked = wolf->vitals.health;
    attacker.swing(1);
    attacker.idle(8);
    InputEntry second = attacker.entry({kActionAttack});
    second.contacts = {contact_with(*wolf)};
    attacker.send(second);
    attacker.send(attacker.entry({kActionAttackHold}));
    attacker.send(attacker.entry({kActionAttackRelease}));
    attacker.idle(8);
    attacker.contact({contact_with(*wolf)});
    check(arena.track().count(HitVerdict::accepted) == 8 && near(clicked - wolf->vitals.health, 2.0F * kWolfLight, 0.01F),
          "two fast clicks: the contact of the first click comes in the step of the second click, and each click is one "
          "light attack");
    // A slice with no reaction action, and a kind with no row in the combat tables.
    const world::Entity* dummy = arena.spawn("Test_Dummy", -2.0F);
    const world::Entity* stranger = arena.spawn("Test_Stranger", 0.5F);
    attacker.idle(10);
    attacker.light({contact_with(*dummy), contact_with(*stranger)});
    check(near(dummy->vitals.health, kDummyHealth - kSwordDamage) && near(dummy->vitals.health, 273.5F) && !dummy->creature.action
              && near(stranger->vitals.health, kDummyHealth - kSwordDamage) && arena.track().count(HitVerdict::accepted) == 10,
          "one attack hurts two creatures one time each. A creature with no defense takes 64 (the health 337.5 becomes "
          "273.5: the values of the research), and a slice with no reaction action keeps its pose. A kind with no row in "
          "the combat tables takes the damage with no defense");
}

/** HT5: each refusal of the hit check. A refused claim changes nothing. */
void refusals(const Options&) {
    Arena arena;
    ServerHost& host = arena.host;
    FakeClient& a = arena.a;
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    const bool spawned = host.world.spawn_character(kCharacterB, state::WorldPosition{kPlace.x, kPlace.y + 3.0F, kHeight})
                         == world::SpawnError::none;
    const world::Entity* wolf = arena.spawn("Test_Wolf", 2.0F);
    const world::Entity* farWolf = arena.spawn("Test_Wolf", 8.0F);
    const world::Entity* deadWolf = arena.spawn("Test_Wolf", -2.0F);
    const world::Entity* rabbit = arena.spawn("Test_Rabbit", 60.0F);
    const world::Entity* own = host.world.find_character(kCharacterA);
    const world::Entity* other = host.world.find_character(kCharacterB);
    host.world.creatures().kill(deadWolf->id);
    host.steps(60, {&a});
    check(spawned && other != nullptr && interest_of(host, 1, wolf->id) != 0 && interest_of(host, 1, farWolf->id) != 0
              && interest_of(host, 1, deadWolf->id) != 0 && interest_of(host, 1, rabbit->id) == 0,
          "the session observes the two wolves and the dead wolf, and not the rabbit that is 60 m away; the character of a "
          "different player is in the world 3 m away");
    Attacker attacker{host, a, kPlace};
    const std::size_t from = a.got.size();
    const float healths[] = {wolf->vitals.health, farWolf->vitals.health, rabbit->vitals.health, other->vitals.health,
                             own->vitals.health};
    const world::Revision wolfChanged = wolf->changed.count(world::ComponentKey::vitals) != 0
                                            ? wolf->changed.at(world::ComponentKey::vitals)
                                            : 0;
    const auto nothing = [&]() {
        return wolf->vitals.health == healths[0] && farWolf->vitals.health == healths[1] && rabbit->vitals.health == healths[2]
               && other->vitals.health == healths[3] && own->vitals.health == healths[4] && numbers_of(a, from).empty()
               && arena.track().count(HitVerdict::accepted) == 0 && !wolf->creature.action
               && (wolf->changed.count(world::ComponentKey::vitals) != 0 ? wolf->changed.at(world::ComponentKey::vitals) : 0)
                      == wolfChanged;
    };
    const auto refused = [&](HitVerdict verdict, std::uint64_t count, const char* text) {
        const bool counted = arena.track().count(verdict) == count;
        const bool logged = log_count(std::string("rep: hit claim refused (") + world::hit_verdict_text(verdict) + ")") == 1;
        check(counted && logged && nothing(), text);
    };

    // No attack input: a contact with no swing.
    attacker.idle(3);
    attacker.contact({contact_with(*wolf)});
    refused(HitVerdict::noAttack, 1, "a contact with no attack input of the client: refused (no attack)");
    // A contact 2 steps after the start of an attack input, and one 49 steps after its end.
    attacker.send(attacker.entry({kActionAttack}));
    attacker.idle(1);
    attacker.contact({contact_with(*wolf)});
    attacker.idle(47);
    attacker.contact({contact_with(*wolf)});
    refused(HitVerdict::noAttack, 3,
            "a contact 2 input steps after the start of an attack input (not less than 3), and one 49 steps after its end "
            "(not more than 30): refused");
    // The steps fit, and the end of the attack input is too old on the world clock.
    attacker.send(attacker.entry({kActionAttack}));
    attacker.idle(5);
    host.advance(std::chrono::milliseconds(2500));
    attacker.contact({contact_with(*wolf)});
    refused(HitVerdict::noAttack, 4,
            "a contact 5 input steps and 2.7 s after the end of an attack input: refused (an input number is a value of the "
            "client)");

    // The target.
    InputContactItem unknown = contact_with(*wolf);
    unknown.entity = 0x1234;
    unknown.group = 0x1234;
    InputContactItem self = contact_with(*wolf);
    self.actor = own->actor.bytes;
    self.groupActor = own->actor.bytes;
    self.entity = own->id;
    self.group = own->id;
    InputContactItem player = self;
    player.actor = other->actor.bytes;
    player.groupActor = other->actor.bytes;
    player.entity = other->id;
    player.group = other->id;
    InputContactItem wrongActor = contact_with(*wolf);
    wrongActor.actor = farWolf->actor.bytes;
    attacker.idle(10);
    attacker.light({unknown, self, player, wrongActor});
    refused(HitVerdict::notCreature, 4,
            "a target that is not a creature: an entity group id that the world does not have, the own character, the "
            "character of a different player, and the id of a creature with the actor id of a different one");
    InputContactItem wrongFacet = contact_with(*wolf);
    wrongFacet.facet += 1;
    InputContactItem wrongGroup = contact_with(*wolf);
    wrongGroup.group = farWolf->id;
    InputContactItem wrongGroupActor = contact_with(*wolf);
    wrongGroupActor.groupActor = farWolf->actor.bytes;
    attacker.idle(10);
    attacker.light({wrongFacet, wrongGroup, wrongGroupActor});
    refused(HitVerdict::badReference, 3,
            "a reference that does not name a component of the slice of the creature, and a group reference that is not "
            "the group of the receiver");
    attacker.idle(10);
    attacker.light({contact_with(*rabbit)});
    refused(HitVerdict::notObserved, 1, "a creature that the session does not observe (60 m away): refused (not observed)");
    attacker.idle(10);
    attacker.light({contact_with(*deadWolf)});
    refused(HitVerdict::dead, 1, "a dead creature: refused, and it stays dead with the health 0");
    attacker.idle(10);
    attacker.light({contact_with(*farWolf)});
    refused(HitVerdict::tooFar, 1,
            "a creature that is 8 m from the exact position of the character (the limit is 3.5 m and 1.5 m): refused (too far)");
    InputContactItem farPoint = contact_with(*wolf);
    farPoint.point[0] += 10.0F;
    InputContactItem badNumber = contact_with(*wolf);
    badNumber.point[2] = std::numeric_limits<float>::quiet_NaN();
    attacker.idle(10);
    attacker.light({farPoint, badNumber});
    refused(HitVerdict::badPoint, 2, "a contact point that is 10 m from the creature, and a point with a number that is not finite");

    // The weapon.
    world::Entity* armed = host.world.find_character(kCharacterA);
    armed->equipment.slots[world::paperdoll::kMainHand1].item.reset();
    attacker.idle(10);
    attacker.light({contact_with(*wolf)});
    arm(host, kCharacterA, "TestBow");
    attacker.idle(10);
    attacker.light({contact_with(*wolf)});
    arm(host, kCharacterA, "TestHammer");
    attacker.idle(10);
    attacker.heavy({contact_with(*wolf)});
    arm(host, kCharacterA, "TestThing");
    attacker.idle(10);
    attacker.light({contact_with(*wolf)});
    refused(HitVerdict::noWeapon, 4,
            "no weapon in the hand, a weapon that is not a melee weapon (a bow), a heavy attack of a weapon with no heavy "
            "row, and an item that the combat tables do not have: refused (no melee weapon)");
    arm(host, kCharacterA, "TestSword");

    // Too many claims in one second: 14 contacts in one entry. The window takes 12.
    attacker.idle(40);
    const std::uint64_t badBefore = arena.track().count(HitVerdict::badReference);
    attacker.light(std::vector<InputContactItem>(14, wrongFacet));
    check(arena.track().count(HitVerdict::tooMany) == 2 && arena.track().count(HitVerdict::badReference) == badBefore + 12
              && log_count("rep: hit claim refused (too many claims)") == 1 && nothing(),
          "14 claims in one input step: the check does 12 of them, and 2 are refused (too many claims)");

    // The same attack two times: the one accepted claim of this case, and then the refusals.
    attacker.idle(40);
    const std::size_t before = a.got.size();
    attacker.swing(1);
    attacker.idle(9);
    attacker.contact({contact_with(*wolf)});
    attacker.contact({contact_with(*wolf), contact_with(*wolf)});
    attacker.idle(5);
    attacker.contact({contact_with(*wolf)});
    check(arena.track().count(HitVerdict::accepted) == 1 && arena.track().count(HitVerdict::sameAttack) == 3
              && near(wolf->vitals.health, healths[0] - kWolfLight) && numbers_of(a, before).size() == 1
              && log_count("rep: hit claim refused (the same attack)") == 1,
          "four contacts of one attack with one creature (in 7 input steps): the creature takes the damage one time, and "
          "three claims are refused (the same attack: a contact less than 8 steps or less than 250 ms after a hit of the "
          "same creature)");
    host.disconnect(a.peer);
    check(log_count("rep: hit claims of the session:") == 1 && log_count("1 accepted, 37 refused (4 not a creature, 15 not a component of the creature, 1 not observed, 1 dead, 4 no melee weapon, 4 no attack, 3 the same attack, 1 too far, 2 bad point, 2 too many claims, 0 no combat tables)") == 1,
          "the log of the session end has the count of the claims of each cause");

    // A backend with no combat tables.
    Arena bare(false);
    const world::Entity* target = bare.spawn("Test_Wolf", 2.0F);
    Attacker second{bare.host, bare.a, kPlace};
    second.idle(3);
    second.light({contact_with(*target)});
    check(bare.track().count(HitVerdict::noTables) == 1 && bare.track().count(HitVerdict::accepted) == 0
              && target->vitals.health == kWolfHealth && numbers_of(bare.a).empty()
              && log_count("rep: hit claim refused (no combat tables)") == 1,
          "a backend with no combat tables: the claim is refused, and the attack has no effect");
}

/** HT6: the death of a creature by a hit. */
void death_by_hits(const Options&) {
    Arena arena;
    ServerHost& host = arena.host;
    FakeClient& a = arena.a;
    const world::Entity* rabbit = arena.spawn("Test_Rabbit", 1.5F);
    const world::Entity* wolf = arena.spawn("Test_Wolf", 2.0F);
    const world::Entity* own = host.world.find_character(kCharacterA);
    const std::uint32_t rabbitNumber = interest_of(host, 1, rabbit->id);
    const float playerHealth = own->vitals.health;
    Attacker attacker{host, a, kPlace};
    attacker.idle(5);
    std::size_t from = a.got.size();
    const auto died = host.clock.now();
    attacker.light({contact_with(*rabbit)});
    check(near(rabbit->vitals.healthMax, 16.875F) && rabbit->vitals.health == 0.0F && rabbit->creature.dead
              && rabbit->creature.diedAt > died && host.world.creatures().counts().deaths == 1
              && log_count("is dead") == 1 && log_count("health 16.9 to 0.0: dead") == 1,
          "a rabbit with the health 16.875 takes one hit of 41.6: its health is 0 (not below 0), and it is dead");
    host.steps(10, {&a});
    std::vector<Record> records = records_for(a, rabbitNumber, from);
    std::vector<Bytes> numbers = numbers_of(a, from);
    check(records.size() == 1 && order_of(records[0]) == "(2,11) (5,15) ", "the observer gets the movement state and the vitals state");
    if (records.size() == 1 && records[0].fragments.size() == 2) {
        const Fragment& movement = records[0].fragments[0];
        const Fragment& vitals = records[0].fragments[1];
        check(hex_text(movement.fields.at({0, 33})) == "070001051c3c0000" && float_of(field_of(vitals, 0, 0)) == 0.0F
                  && vitals.fields.at({0, 2})[0] == 1 && vitals.fields.at({0, 3})[0] == 1,
              "the movement state has the death action of the slice (not the reaction), and the vitals state has the "
              "health 0 and the dead mark");
    } else {
        check(false, "the record of the death has no two fragments");
    }
    check(numbers.size() == 1 && numbers[0].size() == 60 && numbers[0][48] == 0x08 && numbers[0][49] == 0x00
              && near(float_at(numbers[0], 52), kWolfLight),
          "the attacker gets the message with the flag \"the damage was the death\" (bit 3, low byte first) and the damage "
          "of the attack");
    // More attacks on the body.
    from = a.got.size();
    attacker.idle(10);
    attacker.light({contact_with(*rabbit)});
    attacker.idle(10);
    attacker.heavy({contact_with(*rabbit)});
    host.steps(10, {&a});
    check(arena.track().count(HitVerdict::dead) == 2 && arena.track().count(HitVerdict::accepted) == 1
              && rabbit->vitals.health == 0.0F && rabbit->creature.dead && host.world.creatures().counts().deaths == 1
              && numbers_of(a, from).empty() && records_for(a, rabbitNumber, from).empty(),
          "two more attacks on the body: refused (dead), no change, no message, and no record");
    check(rabbit->creature.action && rabbit->creature.action->action == kWolfDeath && !rabbit->creature.action->ends,
          "the death action has no end: the body stays on the ground");
    host.advance(world::Creatures::kReactionTime * 2);
    host.steps(2, {&a});
    check(rabbit->creature.action.has_value(), "after the time of a reaction the dead creature keeps its death action");

    // The wolf: hits until the health is 0. A reaction that plays does not stop the death action.
    int hits = 0;
    while (!wolf->creature.dead && hits < 60) {
        attacker.idle(10);
        attacker.light({contact_with(*wolf)});
        ++hits;
    }
    check(hits == 41 && wolf->creature.dead && wolf->vitals.health == 0.0F && wolf->creature.action
              && wolf->creature.action->action == kWolfDeath && !wolf->creature.action->ends
              && wolf->creature.action->generation == 41 && host.world.creatures().counts().deaths == 2,
          "41 light attacks of 41.6 take the health of the wolf from 1690.876 to 0: the last hit starts the death action "
          "(the action count 41), not a reaction");
    check(own->vitals.health == playerHealth && arena.track().refused() == 2 && arena.track().presses() == 44,
          "the health of the player does not change, and no claim of these attacks was refused");
}

/** HT7: the weapon in the hand of the character: the first weapon slot, and the slot of a weapon key. */
void weapon_keys(const Options&) {
    Arena arena;
    ServerHost& host = arena.host;
    FakeClient& a = arena.a;
    const world::Entity* dummy = arena.spawn("Test_Dummy", 2.0F);
    world::Entity* own = host.world.find_character(kCharacterA);
    arm(host, kCharacterA, "TestHammer", world::paperdoll::kMainHand1 + 1, 200);
    Attacker attacker{host, a, kPlace};
    attacker.idle(5);
    float before = dummy->vitals.health;
    attacker.light({contact_with(*dummy)});
    check(own->combat.selectedWeapon == 0 && near(before - dummy->vitals.health, kSwordDamage),
          "no weapon key: the weapon in the hand is the first weapon slot with an item (the sword)");
    attacker.idle(10);
    attacker.send(attacker.entry({kActionWeapon2}));
    attacker.idle(10);
    before = dummy->vitals.health;
    attacker.light({contact_with(*dummy)});
    const float hammer = 93.0F * std::pow(1.0112F, 20.0F) + 10.0F;
    check(own->combat.selectedWeapon == world::paperdoll::kMainHand1 + 1 && near(before - dummy->vitals.health, hammer, 0.01F)
              && log_count("with TestHammer (light attack, row Test_Hammer_Light)") == 1,
          "the key of weapon 2: the next attack has the damage of the item of weapon slot 2, with its gear score 200");
    attacker.idle(10);
    attacker.send(attacker.entry({kActionWeapon1}));
    attacker.idle(10);
    before = dummy->vitals.health;
    attacker.light({contact_with(*dummy)});
    check(own->combat.selectedWeapon == world::paperdoll::kMainHand1 && near(before - dummy->vitals.health, kSwordDamage),
          "the key of weapon 1: the sword again");
    // A key for a slot with no item does not change the weapon, and a slot that loses its item is not the weapon.
    own->equipment.slots[world::paperdoll::kMainHand1 + 1].item.reset();
    attacker.send(attacker.entry({kActionWeapon2}));
    check(own->combat.selectedWeapon == world::paperdoll::kMainHand1, "a key for a weapon slot with no item selects nothing");
    arm(host, kCharacterA, "TestHammer", world::paperdoll::kMainHand1 + 1, 100);
    attacker.send(attacker.entry({kActionWeapon2}));
    own->equipment.slots[world::paperdoll::kMainHand1 + 1].item.reset();
    attacker.idle(10);
    before = dummy->vitals.health;
    attacker.light({contact_with(*dummy)});
    check(near(before - dummy->vitals.health, kSwordDamage) && arena.track().refused() == 0,
          "the selected slot lost its item: the weapon in the hand is the first weapon slot with an item");
}

/** HT8: three sessions (rule MP-10): two observe the wolf, one is far away. */
void sessions(const Options&) {
    ServerHost host;
    set_creatures(host);
    check(host.combat.set(test_combat()), "the combat tables of the cases are valid");
    const state::WorldPosition placeB{kPlace.x, kPlace.y + 4.0F, kHeight};
    const state::WorldPosition placeC{kPlace.x + 580.0F, kPlace.y, kHeight};
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", placeB);
    const Player gamma = add_player(host.store, kSteamIdC, "Tester C", kCharacterC, "Gamma", placeC);
    FakeClient a(27000);
    FakeClient b(27001);
    FakeClient c(27002);
    enter_at(host, a, alpha, kPlace);
    enter_at(host, b, beta, placeB);
    enter_at(host, c, gamma, placeC);
    arm(host, kCharacterA, "TestSword");
    arm(host, kCharacterB, "TestSword");
    const world::Entity* wolf = host.world.creatures().spawn("Test_Wolf", state::WorldPosition{kPlace.x + 2.0F, kPlace.y + 2.0F, kHeight}, 0.0F).entity;
    host.steps(60, {&a, &b, &c});
    const std::uint32_t inA = interest_of(host, 1, wolf->id);
    const std::uint32_t inB = interest_of(host, 2, wolf->id);
    check(inA != 0 && inB != 0 && interest_of(host, 3, wolf->id) == 0,
          "sessions 1 and 2 observe the wolf, and session 3 (580 m away) does not");
    const world::Entity* players[] = {host.world.find_character(kCharacterA), host.world.find_character(kCharacterB),
                                      host.world.find_character(kCharacterC)};
    const float healths[] = {players[0]->vitals.health, players[1]->vitals.health, players[2]->vitals.health};
    Attacker attackerA{host, a, kPlace};
    Attacker attackerB{host, b, placeB};
    attackerA.idle(5);
    std::size_t fromA = a.got.size();
    std::size_t fromB = b.got.size();
    std::size_t fromC = c.got.size();
    attackerA.light({contact_with(*wolf)});
    host.steps(10, {&a, &b, &c});
    std::vector<Record> forA = records_for(a, inA, fromA);
    std::vector<Record> forB = records_for(b, inB, fromB);
    check(near(wolf->vitals.health, kWolfHealth - kWolfLight) && forA.size() == 1 && forB.size() == 1
              && order_of(forA[0]) == "(5,11) (9,15) " && order_of(forB[0]) == "(5,11) (9,15) ",
          "A hits the wolf: each of the two observers gets one record with the reaction (movement state) and the health "
          "(vitals state)");
    if (forA.size() == 1 && forB.size() == 1 && forA[0].fragments.size() == 2 && forB[0].fragments.size() == 2) {
        check(forA[0].fragments[0].fields == forB[0].fragments[0].fields && forA[0].fragments[1].fields == forB[0].fragments[1].fields
                  && float_of(field_of(forB[0].fragments[1], 0, 0)) == wolf->vitals.health,
              "the two observers get the same fields: the health change comes from the world, not from the session of the "
              "attacker");
    } else {
        check(false, "the records of the two observers have no two fragments");
    }
    check(numbers_of(a, fromA).size() == 1 && numbers_of(b, fromB).empty() && numbers_of(c, fromC).empty(),
          "only the attacker gets the damage number message");
    bool quiet = true;
    for (std::size_t index = fromC; index < c.got.size(); ++index) {
        if (c.got[index].type != 8) {
            quiet = false;
            continue;
        }
        for (const Record& record : read_bundle(c.got[index].body).records) {
            quiet = quiet && record.interest == 1;
        }
    }
    check(quiet && host.session(3).attack_track().presses() == 0, "the session that is far away gets nothing of the hit");

    // B hits the same wolf: its own swing, its own claim, its own number.
    fromA = a.got.size();
    fromB = b.got.size();
    attackerB.idle(5);
    attackerB.light({contact_with(*wolf)});
    host.steps(10, {&a, &b, &c});
    check(near(wolf->vitals.health, kWolfHealth - 2.0F * kWolfLight) && numbers_of(b, fromB).size() == 1
              && numbers_of(a, fromA).empty() && records_for(a, inA, fromA).size() == 1 && records_for(b, inB, fromB).size() == 1
              && host.session(1).attack_track().count(HitVerdict::accepted) == 1
              && host.session(2).attack_track().count(HitVerdict::accepted) == 1,
          "B hits the wolf: the wolf takes the damage of B too, B gets the number, and each session has its own attack "
          "inputs and its own counts");
    // A claim of a session names the creature, not an attacker: the world finds the attacker from the session.
    check(log_count("world: hit: Alpha hurts creature") == 1 && log_count("world: hit: Beta hurts creature") == 1,
          "the world finds the attacker of a claim from the session");
    check(players[0]->vitals.health == healths[0] && players[1]->vitals.health == healths[1]
              && players[2]->vitals.health == healths[2],
          "the health of each player did not change");
    // The session of the attacker ends: the wolf keeps its health, and the other observer its record.
    const float health = wolf->vitals.health;
    host.disconnect(a.peer);
    host.steps(5, {&b, &c});
    check(host.world.creatures().find(wolf->id) != nullptr && wolf->vitals.health == health && interest_of(host, 2, wolf->id) == inB,
          "the session of an attacker ends: the wolf stays in the world with its health");
}

/** HT9: the remote configuration document of the client, and its switch. */
void remote_configuration(const Options&) {
    const auto get = [](const server::http::Router& router, const char* host, const char* path, const char* method = "GET") {
        service::HttpRequest request;
        request.method = method;
        request.host = host;
        request.path = path;
        request.target = path;
        std::string route;
        service::HttpResponse response = router.dispatch(request, route);
        return std::make_pair(response, route);
    };
    const char* host = "ags-javelin-remote-config.s3.amazonaws.com:443";
    const char* path = "/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless";
    server::http::Router router;
    server::http::register_aws_routes(router, server::http::AwsOptions{});
    auto reply = get(router, host, path);
    check(reply.first.status == 200 && reply.first.contentType == "application/json"
              && reply.first.body == R"({"javelin.enable-client-authoritative-melee":true})" && reply.first.body.size() == 50
              && reply.second == "aws.s3_gameplay_configuration",
          "the request of the client for the gameplay configuration document of the product gets the status 200 and the "
          "document of 50 bytes: the one setting that turns the melee contact report on");
    check(get(router, host, path).first.body == reply.first.body && get(router, "ags-javelin-remote-config.s3.amazonaws.com", path).first.status == 200,
          "each request for the document gets it (the client asks again when it goes back to the main menu), also with "
          "no port in the host");
    bool missing = true;
    for (const char* other :
         {"/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/6031%3B6004151",
          "/applications/publicGameplay/configuration-sets/RegionId/soulwarden-local/versionless",
          "/applications/publicGameplay/configuration-sets/WorldId/a2071063-7300-4a2e-9a01-000000000001/versionless",
          "/applications/public/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless",
          "/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless/more",
          "/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730"}) {
        reply = get(router, host, other);
        missing = missing && reply.first.status == 404 && reply.first.contentType == "application/xml"
                  && reply.first.body.find("<Code>NoSuchKey</Code>") != std::string::npos && reply.second == "aws.s3_missing";
    }
    check(missing,
          "each other document of that host keeps its reply (404, no such key): the versioned document, the documents of "
          "the region and of the world, the document of the class \"public\", and a path that is longer or shorter");
    reply = get(router, "ags-nw-cms.s3.us-west-2.amazonaws.com:443", path);
    const auto prefixed = get(router, "evil-ags-javelin-remote-config.s3.amazonaws.com:443", path);
    const auto posted = get(router, host, path, "POST");
    check(reply.first.status == 404 && reply.second == "aws.s3_missing" && prefixed.first.status == 404
              && prefixed.first.body.find("NoSuchKey") != std::string::npos && posted.first.status == 404,
          "the same path of a different host, of a host with a text before the name, and a request that is not a GET get no "
          "document");
    // The switch.
    server::http::Router off;
    server::http::AwsOptions options;
    options.meleeReport = false;
    server::http::register_aws_routes(off, options);
    reply = get(off, host, path);
    check(reply.first.status == 404 && reply.second == "aws.s3_missing" && reply.first.body.find("NoSuchKey") != std::string::npos,
          "with the switch off (--no-melee-report) the document is not there: the client then reports no melee contact");
}


/**
 * HT10: a combat tables file with a value that is not valid is not used, as a whole (code
 * review of stage 3.2a, finding 1): a damage category that is not a category of the game, and
 * an empty damage list of an attribute.
 */
void review_bad_tables(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "review-bad-combat.json";
    json document = tables_document();
    document["damageTypes"][1][2] = "Physcial";
    std::ofstream(path) << document.dump();
    Arena arena(false);
    const bool loaded = arena.host.combat.load(path.string());
    world::Entity* wolf = arena.spawn("Test_Wolf", 2.0F);
    const float before = wolf->vitals.health;
    Attacker attacker{arena.host, arena.a, kPlace};
    attacker.idle(5);
    attacker.light({contact_with(*wolf)});
    note("a file with the damage category \"Physcial\": loaded " + std::to_string(loaded) + " (" + arena.host.combat.error()
         + "), accepted hits " + std::to_string(arena.track().count(HitVerdict::accepted)) + ", damage "
         + std::to_string(before - wolf->vitals.health));
    check(!loaded && !arena.host.combat.loaded() && arena.track().count(HitVerdict::accepted) == 0
              && arena.track().count(HitVerdict::noTables) == 1 && wolf->vitals.health == before,
          "a damage category that is not Physical, Elemental, or True: the file is not used, and a hit has no effect (before "
          "the correction the damage type had no mitigation: 57.6 damage and not 37.44)");
    for (const std::string member : {"strength", "dexterity", "intelligence", "focus"}) {
        json empty = tables_document();
        empty["attributes"][member] = json::array();
        std::ofstream(path) << empty.dump();
        gamedata::CombatTables tables;
        const bool accepted = tables.load(path.string());
        check(!accepted && !tables.loaded() && !tables.error().empty(),
              "an empty damage list of the attribute " + member + ": the file is not used");
    }
    gamedata::CombatData data = test_combat();
    data.attributeDamage[world::attribute::kFocus].clear();
    gamedata::CombatTables direct;
    check(!direct.set(data) && !direct.loaded(), "the same check for data with no file");
    std::ofstream(path) << tables_document().dump();
    gamedata::CombatTables valid;
    check(valid.load(path.string()) && valid.loaded(), "the file with the three categories of the game and a list for each attribute loads");
    std::filesystem::remove(path);
}

/**
 * HT11: the combat data tool writes no file from sources that are not complete (code review of
 * stage 3.2a, finding 3). The check of the tool is a Python program with made-up data tables
 * (tools/gamedata/check_combat_tool.py): this case runs it.
 */
void review_tool_sources(const Options& options) {
    // The golden folder of the options is REPOSITORY/apps/backend/tests/golden.
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_combat_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the combat data tool is in the tree (the case needs --golden-dir)");
    if (!std::filesystem::is_regular_file(script)) {
        return;
    }
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "combat-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + options.workDirectory.string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 150));
        correct += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && correct == 13 && wrong == 0,
          "the tool writes its file for complete tables and for a row name that the game itself does not have, and it writes "
          "no file (status 1) for a damage row that is not there, a damage type that is not there, a base vitals row that is "
          "not there, a column that is not there, a text in a number cell, a cell that is not finite, a base vitals table "
          "with no weakness column, an armor factor that is not finite, an armor table with no column of the tool, a base "
          "attributes file with no exponent of the armor rule, and a level difference that is not a whole number");
    std::filesystem::remove(output);
}

/**
 * HT12: an entry that ends after its position field gives no position (code review of stage
 * 3.2a, finding 4).
 */
void review_short_position(const Options&) {
    Arena arena;
    world::Entity* own = arena.host.world.find_character(kCharacterA);
    const float before = own->position.x;
    const std::uint64_t accepted = arena.host.session(1).position_track().count(world::PositionVerdict::accepted);
    InputEntry entry = report_entry(20, kPlace.x + 1.0F, kPlace.y, kPlace.z);
    entry.value470 = true;
    entry.cut = 1;
    const auto parsed = read_client_input(input_entries_body({entry}));
    check(parsed && parsed->tail == InputTail::shortBody && parsed->steps.empty() && !parsed->hasCharacter,
          "the reader finds that the last byte of the entry is not there: the entry gives no attack data and no position");
    arena.host.clock.advance(kTick);
    arena.host.datagram(arena.a.peer, arena.a.input_report(entry));
    arena.host.tick();
    note("the position x of the character before " + std::to_string(before) + ", after " + std::to_string(own->position.x));
    check(own->position.x == before
              && arena.host.session(1).position_track().count(world::PositionVerdict::accepted) == accepted,
          "the session gives no position of this entry to the world: the character stays (before the correction it moved 1 m)");
    // The same entry with its last byte moves the character.
    InputEntry whole = report_entry(21, kPlace.x + 0.2F, kPlace.y, kPlace.z);
    arena.host.clock.advance(kTick);
    arena.host.datagram(arena.a.peer, arena.a.input_report(whole));
    check(own->position.x == kPlace.x + 0.2F, "an entry that is complete gives its position");
}

/**
 * HT13: only the exact object path of the configuration host gets the document (code review of
 * stage 3.2a, finding 5).
 */
void review_exact_configuration_path(const Options&) {
    server::http::Router router;
    server::http::register_aws_routes(router, server::http::AwsOptions{});
    const auto get = [&](const std::string& host, const std::string& path) {
        service::HttpRequest request;
        request.method = "GET";
        request.host = host;
        request.path = path;
        request.target = path;
        std::string route;
        const service::HttpResponse response = router.dispatch(request, route);
        return std::make_pair(response, route);
    };
    const std::string host = server::http::kRemoteConfigurationHost;
    const std::string exact = server::http::kGameplayConfigurationPath;
    const auto document = get(host, exact);
    const auto withPort = get(host + ":443", exact);
    check(document.first.status == 200 && document.second == "aws.s3_gameplay_configuration"
              && document.first.body == server::http::kMeleeReportDocument && withPort.first.status == 200,
          "the exact path of the exact host (with a port or with no port) gets the document");
    const std::vector<std::string> others{
        exact + "/", "/applications//publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless", "/" + exact,
        exact + "//", "/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730//versionless"};
    for (const std::string& path : others) {
        const auto reply = get(host, path);
        note("path " + path + ": status " + std::to_string(reply.first.status) + ", route " + reply.second);
        check(reply.first.status == 404 && reply.second == "aws.s3_missing" && reply.first.body.find("NoSuchKey") != std::string::npos,
              "a path with one more \"/\" is a different object: the reply is 404 (no such key)");
    }
    const auto otherHost = get("evil-" + host, exact);
    const auto longerHost = get(host + ".example", exact);
    check(otherHost.first.status == 404 && otherHost.second == "aws.s3_missing" && longerHost.first.status == 404
              && longerHost.first.body.find("javelin.enable") == std::string::npos,
          "a host with a text before or after the name of the configuration host gets no document");
}

/**
 * HT14: the two limits of the hit check are limits of the character (code review of stage
 * 3.2a, finding 6): a new session of the character, and a character that leaves the world and
 * enters it again, do not get new limits.
 */
void review_limits_of_a_character(const Options&) {
    Arena arena;
    gamedata::ItemDefinition definition;
    definition.id = "TestSword";
    definition.type = "Weapon";
    definition.itemClass = "EquippableMainHand+Sword";
    arena.host.catalog.set({definition});
    state::ItemRow item;
    item.instanceId = "0102030405060708090a0b0c0d0e0f10";
    item.container = state::kContainerPaperdoll;
    item.slot = world::paperdoll::kMainHand1;
    item.itemId = "TestSword";
    item.quantity = 1;
    item.gearScore = 100;
    check(arena.host.store.write_items(kCharacterA, {}, {item}), "the character has a stored sword for each new session");
    world::Entity* wolf = arena.spawn("Test_Wolf", 2.0F);
    const float before = wolf->vitals.health;
    const auto start = arena.host.clock.now();
    // One hit of a new session: a press, and a hold with a contact 8 steps later.
    const auto hit = [&](FakeClient& client, std::uint32_t session) {
        InputEntry press = report_entry(10, kPlace.x, kPlace.y, kPlace.z);
        press.actionBits = {kActionAttack};
        arena.host.datagram(client.peer, client.input_report(press));
        arena.host.tick();
        InputEntry hold = report_entry(18, kPlace.x, kPlace.y, kPlace.z);
        hold.actionBits = {kActionAttackHold};
        hold.contacts = {contact_with(*wolf)};
        arena.host.datagram(client.peer, client.input_report(hold));
        return arena.host.session(session).attack_track().count(HitVerdict::accepted);
    };
    // 13 new sessions of the same character, one after the other: each one takes the entity of
    // the session before it. Each session opens 40 ms after the one before it.
    unsigned accepted = 0;
    for (unsigned round = 0; round < 13; ++round) {
        FakeClient replacement(static_cast<std::uint16_t>(28000 + round));
        arena.host.enter(replacement, arena.alpha.registration());
        accepted += static_cast<unsigned>(hit(replacement, round + 2));
    }
    const float elapsed = std::chrono::duration<float>(arena.host.clock.now() - start).count();
    note("13 new sessions of one character in " + std::to_string(elapsed) + " s: accepted hits " + std::to_string(accepted)
         + ", damage " + std::to_string(before - wolf->vitals.health));
    check(elapsed < 1.0F && accepted <= 3,
          "the new sessions do not get past the rule \"one attack hurts one creature one time\": 3 hits at most in this time "
          "(250 ms between two hits of one creature; before the correction each of the 13 sessions hit)");
    check(arena.host.world.combat().limits_of(kCharacterA).hits.size() == accepted
              && arena.host.world.combat().limits_of(kCharacterA).claims.size() <= world::combat_limits::kClaimsInWindow,
          "the hits and the claims of the time window are values of the character");

    // The claim rate: 12 claims in 1 s for the character, also over new sessions. Five wolves,
    // so that the rule of one creature does not hide the rate rule: each new session sends
    // five contacts, 40 ms after the session before it.
    Arena rate;
    check(rate.host.store.write_items(kCharacterA, {}, {item}), "a second world with the same character");
    rate.host.catalog.set({definition});
    std::vector<world::Entity*> wolves;
    for (int index = 0; index < 5; ++index) {
        wolves.push_back(rate.spawn("Test_Wolf", 1.0F + 0.2F * static_cast<float>(index)));
    }
    const auto began = rate.host.clock.now();
    unsigned hits = 0;
    unsigned tooMany = 0;
    for (unsigned round = 0; round < 13; ++round) {
        FakeClient replacement(static_cast<std::uint16_t>(29000 + round));
        rate.host.enter(replacement, rate.alpha.registration());
        InputEntry press = report_entry(10, kPlace.x, kPlace.y, kPlace.z);
        press.actionBits = {kActionAttack};
        rate.host.datagram(replacement.peer, replacement.input_report(press));
        rate.host.tick();
        InputEntry hold = report_entry(18, kPlace.x, kPlace.y, kPlace.z);
        hold.actionBits = {kActionAttackHold};
        for (world::Entity* target : wolves) {
            hold.contacts.push_back(contact_with(*target));
        }
        rate.host.datagram(replacement.peer, replacement.input_report(hold));
        const AttackTrack& track = rate.host.session(round + 2).attack_track();
        hits += static_cast<unsigned>(track.count(HitVerdict::accepted));
        tooMany += static_cast<unsigned>(track.count(HitVerdict::tooMany));
    }
    const float window = std::chrono::duration<float>(rate.host.clock.now() - began).count();
    note("13 new sessions with 5 contacts each (65 claims) in " + std::to_string(window) + " s: accepted hits " + std::to_string(hits)
         + ", refused for the claim rate " + std::to_string(tooMany));
    check(window < 1.0F && hits <= world::combat_limits::kClaimsInWindow && tooMany == 65 - world::combat_limits::kClaimsInWindow
              && rate.host.world.combat().limits_of(kCharacterA).claims.size() == world::combat_limits::kClaimsInWindow,
          "the character has 12 claims in its time window of 1 s, for each number of sessions: the other 53 claims are refused "
          "(too many claims; before the correction each session had its own 12 claims)");

    // A character that leaves the world and enters it again has its limits too.
    Arena leave;
    check(leave.host.store.write_items(kCharacterA, {}, {item}), "a third world with the same character");
    leave.host.catalog.set({definition});
    world::Entity* target = leave.spawn("Test_Wolf", 2.0F);
    Attacker first{leave.host, leave.a, kPlace};
    first.idle(3);
    first.light({contact_with(*target)});
    const float afterFirst = target->vitals.health;
    leave.host.datagram(leave.a.peer, leave.a.carrier_close());
    leave.host.tick();
    FakeClient again(27500);
    leave.host.enter(again, leave.alpha.registration());
    const bool entered = leave.host.world.find_character(kCharacterA) != nullptr;
    InputEntry press = report_entry(10, kPlace.x, kPlace.y, kPlace.z);
    press.actionBits = {kActionAttack};
    leave.host.datagram(again.peer, again.input_report(press));
    leave.host.tick();
    InputEntry hold = report_entry(18, kPlace.x, kPlace.y, kPlace.z);
    hold.actionBits = {kActionAttackHold};
    hold.contacts = {contact_with(*target)};
    leave.host.datagram(again.peer, again.input_report(hold));
    check(entered && afterFirst < kWolfHealth && target->vitals.health == afterFirst,
          "a character that leaves the world and enters it again in less than 250 ms does not hit the same creature a second "
          "time: the limits stay with the character id");
}

} // namespace

std::vector<Case> combat_cases() {
    return {
        {"HT1", "hits: the reader of an input report with items in its three lists", reader_lists},
        {"HT2", "hits: the attack inputs of a client (light and heavy, a long hold, fast clicks, the input numbers)", attack_inputs},
        {"HT3", "hits: the combat tables file, and the damage rule with the worked example", tables_and_rule},
        {"HT4", "hits: one hit, with the bytes of the health change, of the reaction, and of the damage number", one_hit},
        {"HT5", "hits: each refusal of the hit check, and a backend with no combat tables", refusals},
        {"HT6", "hits: the death of a creature by a hit, and attacks on the body", death_by_hits},
        {"HT7", "hits: the weapon in the hand and the weapon keys", weapon_keys},
        {"HT8", "hits: three sessions (two observers of one wolf, one session far away)", sessions},
        {"HT9", "hits: the remote configuration document that turns the contact report on, and its switch", remote_configuration},
        {"HT10", "hits: a combat tables file with a category that is not known or with an empty attribute list (review of stage 3.2a)",
         review_bad_tables},
        {"HT11", "hits: the combat data tool writes no file from sources that are not complete (review of stage 3.2a)",
         review_tool_sources},
        {"HT12", "hits: an input entry that ends after its position field gives no position (review of stage 3.2a)",
         review_short_position},
        {"HT13", "hits: only the exact object path of the configuration host gets the document (review of stage 3.2a)",
         review_exact_configuration_path},
        {"HT14", "hits: the limits of the hit check stay with the character over new sessions (review of stage 3.2a)",
         review_limits_of_a_character},
    };
}

} // namespace soulwarden::backend::tests
