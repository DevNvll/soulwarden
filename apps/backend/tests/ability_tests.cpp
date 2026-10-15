// Cases for the abilities of a player (stage 3.3a): the ability data and the input reader, the
// cast and its cooldown, each refusal of a press, the melee abilities (a contact in the window of
// a phase), the area abilities (Shockwave, Path of Destiny, the vortex of Gravity Well), the
// status effects on creatures and the behavior that obeys them, two sessions, the load, and the
// commands. All ids, names, and values are made up, with the id texts of the game for the
// abilities and the effects: no value of a capture is here.

#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "server/rep/client_input.h"
#include "server/rep/combat_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_navigation.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/abilities.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using world::CastVerdict;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr float kHeight = 50.0F;
constexpr state::WorldPosition kPlace{1000.0F, 1000.0F, kHeight};
constexpr std::chrono::microseconds kTick{33333};
constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
constexpr std::uint32_t kStatusSlot = 8;
/** The actions of the test wolf, and the fragments of its reactions and of its stun. */
constexpr std::uint32_t kReaction = 1;
constexpr std::uint32_t kBite = 3;
constexpr std::uint32_t kStun = 5;
constexpr std::int32_t kReactionFragment = 15;
constexpr std::int32_t kKnockdownFragment = 79;
constexpr std::int32_t kStunFragment = 76;
constexpr float kWolfHealth = 10000.0F;
constexpr float kRunSpeed = 5.0F;
/** The weapon damage of the test weapons: base damage 100 at the lowest gear score, level 1, no attribute. */
constexpr float kWeaponDamage = 100.0F;
/** The ids on the wire of the two weapons and of the abilities of the build. */
const std::uint32_t kGreatAxe = gamedata::progression_id("GreatAxeAbilityTable");
const std::uint32_t kWarHammer = gamedata::progression_id("WarHammerAbilityTable");
const std::uint32_t kCharge = gamedata::progression_id("Ability_Greataxe_Charge");
const std::uint32_t kMaelstrom = gamedata::progression_id("Ability_Greataxe_Maelstorm");
const std::uint32_t kGravityWell = gamedata::progression_id("Ability_Greataxe_GravityWell");
const std::uint32_t kWreckingBall = gamedata::progression_id("Ability_WarHammer_WreckingBall");
const std::uint32_t kShockwave = gamedata::progression_id("Ability_WarHammer_Shockwave");
const std::uint32_t kPathOfDestiny = gamedata::progression_id("Ability_WarHammer_PathOfDestiny");
/** The status numbers of the test data (the live numbers of the supported build). */
constexpr std::uint16_t kStunIndex = 1710;
constexpr std::uint16_t kSlowIndex = 1728;
constexpr std::uint16_t kRootIndex = 1059;

const char* const kAxeNodes[] = {"Ability_Greataxe_Charge", "Ability_Greataxe_Maelstorm", "Ability_Greataxe_GravityWell",
                                 "Upgrade_Greataxe_Maelstorm_Rng", "Passive_Greataxe_Mauler_PullDur",
                                 "Upgrade_Greataxe_Maelstorm_SpinAttack", "Upgrade_Greataxe_GravityWell_DmgPerFoe",
                                 "Upgrade_Greataxe_GravityWell_ArmorInWell"};
const char* const kHammerNodes[] = {"Ability_WarHammer_WreckingBall", "Ability_WarHammer_Shockwave", "Ability_WarHammer_PathOfDestiny",
                                    "Upgrade_WarHammer_Wreck_Pushback", "Ult_Passive_WarHammer_SlowOnCC",
                                    "Upgrade_WarHammer_Shockwave_SizeIncrease", "Upgrade_WarHammer_Shockwave_Weaken",
                                    "Upgrade_WarHammer_PathOfDestiny_Stagger", "Upgrade_WarHammer_PathOfDestiny_CDRPerHit"};

bool near(float left, float right, float tolerance = 0.01F) {
    return std::abs(left - right) <= tolerance;
}

state::WorldPosition at(float x, float y, float z = kHeight) {
    return state::WorldPosition{x, y, z};
}

/** The progression tables of the cases: the great axe and the war hammer with the nodes of the build. */
gamedata::ProgressionData ability_progression() {
    gamedata::ProgressionData data = test_progression();
    gamedata::RankTable ranks;
    for (std::uint32_t rank = 0; rank < 20; ++rank) {
        ranks.experience.push_back(10);
        ranks.resetCost.push_back(0);
    }
    data.progressions.push_back({"GreatAxeAbilityTable", 0, gamedata::ProgressionKind::weapon, 19, ranks});
    data.progressions.push_back({"WarHammerAbilityTable", 0, gamedata::ProgressionKind::weapon, 19, ranks});
    std::uint32_t column = 0;
    for (const char* id : kAxeNodes) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = kGreatAxe;
        node.column = column++;
        node.active = std::string(id).rfind("Ability_", 0) == 0;
        data.nodes.push_back(node);
    }
    // A node of a last row (row 5): the other nodes are not in the last row of their tree.
    for (const std::uint32_t table : {kGreatAxe, kWarHammer}) {
        gamedata::AbilityNode last;
        last.id = table == kGreatAxe ? "Test_GreatAxe_Last" : "Test_WarHammer_Last";
        last.table = table;
        last.row = 5;
        data.nodes.push_back(last);
    }
    column = 0;
    for (const char* id : kHammerNodes) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = kWarHammer;
        node.column = column++;
        node.active = std::string(id).rfind("Ability_", 0) == 0;
        data.nodes.push_back(node);
    }
    return data;
}

/** The wolf of the cases: a slice that fights, with a reaction, a stun, the reaction fragments, and a status component. */
gamedata::CreatureSlice wolf_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/wolf.dynamicslice";
    slice.asset.fill(0x11);
    slice.subId = 2;
    slice.root = 0x1000000000000011ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = kWolfSlots[0];
    slice.vitalsSlot = kWolfSlots[1];
    slice.markerSlot = kWolfSlots[2];
    slice.slots = {{kWolfSlots[0], "ActionList", 11},
                   {kStatusSlot, "StatusEffects", 4236},
                   {kWolfSlots[1], "Vitals", 15},
                   {kWolfSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}, {slice.root + 3, 0x0000000000000C03ULL, "Head/HitVolume"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false},       {"Reaction", "r_R2_F", kReactionFragment, true},
                     {"Death", "r_Death", 4, true},            {"Attack_Bite", "Attack_Bite", 7, false},
                     {"Move_Combat", "Move_Combat", 11, false}, {"Stun", "r_Stun_Loop", kStunFragment, false}};
    slice.reactions = {35, 10, 15, 26, kKnockdownFragment};
    gamedata::CreatureBehavior values;
    values.sightAngle = 180.0F;
    values.sightDistance = 12.0F;
    values.combatAngle = 180.0F;
    values.combatDistance = 30.0F;
    values.restLeash = 20.0F;
    values.combatLeash = 70.0F;
    values.walkSpeed = 2.0F;
    values.runSpeed = kRunSpeed;
    slice.behavior = values;
    gamedata::CreatureContact contact;
    contact.time = 0.5F;
    contact.reach = 2.2F;
    contact.factor = 0.2F;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Test_Bite";
    slice.attacks = {gamedata::CreatureAttack{kBite, 1.4F, {contact}}};
    return slice;
}

gamedata::CreatureKind wolf_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Wolf";
    kind.vitals = kind.id;
    kind.category = "Test_Wolf_Category";
    kind.vitalsId = 0x11112222;
    kind.categoryId = 0x11112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 1;
    kind.bodies = {{0, 100}};
    return kind;
}

/** The combat tables of the cases: the great axe, the war hammer, a sword with no mastery, and the damage rows of the abilities. */
gamedata::CombatData ability_combat() {
    gamedata::CombatData data;
    data.constants = {0.025F, 0.0112F, 0.6667F, 5, 100, 500};
    data.damageTypes = {{"True", 1, gamedata::DamageCategory::direct},
                        {"Slash", 4, gamedata::DamageCategory::physical},
                        {"Strike", 6, gamedata::DamageCategory::physical},
                        {"Corruption", 11, gamedata::DamageCategory::elemental}};
    data.attributeMinimum = 5;
    for (const unsigned attribute : {world::attribute::kStrength, world::attribute::kDexterity, world::attribute::kIntelligence,
                                     world::attribute::kFocus}) {
        data.attributeDamage[attribute] = {0.0F, 1.25F};
    }
    const auto row = [&](const char* id, std::uint32_t type, float factor) {
        data.damageRows.push_back({id, "Ability", type, factor, 0.0F});
    };
    row("Test_Light", 1, 1.0F);
    for (const char* id : {"GreatAxe_Engage1"}) {
        row(id, 1, 1.2F);
    }
    for (const char* id : {"GreatAxe_Maelstrom", "GreatAxe_Maelstrom_Reach", "GreatAxe_Maelstrom_RP2", "GreatAxe_Maelstrom_Reach_RP2",
                           "GreatAxe_Maelstrom_NoReprieve", "GreatAxe_Maelstrom_NoReprieve_RP2"}) {
        row(id, 1, 1.1F);
    }
    row("GreatAxe_GravityWell", 1, 0.25F);
    row("GreatAxe_GravityWell_Burst", 3, 1.0F);
    // The pull row of the vortex (stage 3.3b-S4): no damage. With no impact here, the cases of stage 3.3a move no creature.
    row("GreatAxe_GravityWell_Vortex", 3, 0.0F);
    row("Warhammer_Damage_WreckingBall", 2, 0.9F);
    row("Warhammer_Damage_WreckingBall_Push", 2, 0.9F);
    row("Warhammer_Damage_Shockwave", 2, 0.6F);
    row("Warhammer_Damage_Shockwave_L2", 2, 0.6F);
    for (const char* id : {"Warhammer_Damage_PathOfDestiny", "Warhammer_Damage_PathOfDestiny_L2", "Warhammer_Damage_PathOfDestiny_L3"}) {
        row(id, 2, 0.7F);
    }
    gamedata::WeaponRow axe;
    axe.id = "TestAxeRow";
    axe.melee = true;
    axe.baseDamage = kWeaponDamage;
    axe.lightRow = 0;
    axe.mastery = "GreatAxeAbilityTable";
    gamedata::WeaponRow hammer = axe;
    hammer.id = "TestHammerRow";
    hammer.mastery = "WarHammerAbilityTable";
    gamedata::WeaponRow sword = axe;
    sword.id = "TestSwordRow";
    sword.mastery.clear();
    data.weapons = {axe, hammer, sword};
    data.items = {{"testaxe", 0}, {"testhammer", 1}, {"testsword", 2}};
    gamedata::DefenseRow none;
    none.weakness.assign(data.damageTypes.size(), 0.0F);
    none.absorption.assign(data.damageTypes.size(), 0.0F);
    data.defenses = {none};
    data.vitals = {{"test_wolf", 0}};
    return data;
}

/** The ability data of the cases: the cooldowns, the effects, and the spells of the six abilities (values of the game data). */
gamedata::AbilityTables ability_tables() {
    gamedata::AbilityTables tables;
    tables.cooldowns = {{"Ability_Greataxe_Charge", 20.0F, 0.0F},        {"Ability_Greataxe_Maelstorm", 20.0F, 0.0F},
                        {"Ability_Greataxe_GravityWell", 30.0F, 0.0F},    {"Ability_WarHammer_WreckingBall", 15.0F, 0.0F},
                        {"Ability_WarHammer_Shockwave", 20.0F, 0.0F},     {"Ability_WarHammer_PathOfDestiny", 22.0F, 2.0F}};
    tables.statusEffects = {{"WarHammer_Shockwave_Stun", kStunIndex, 2.0F, 0.0F, true, false, 1, true, 0.0F, {}},
                            {"WarHammer_Ult_SlowOnCC", kSlowIndex, 4.0F, -0.2F, false, false, 0, true, 0.0F, {}},
                            {"WarHammer_Shockwave_Weaken", 240, 10.0F, 0.0F, false, false, 0, true, -0.1F, {}},
                            {"GreatAxe_Vortex_Root", kRootIndex, 1.0F, 0.0F, false, true, 0, true, 0.0F, {}},
                            {"GreatAxe_Vortex", 3399, 3.0F, -0.6F, false, false, 0, false, 0.0F, {}},
                            {"Status_Passive_GreatAxe_Gravity_Root_Hold", 2182, 0.4F, 0.0F, false, true, 0, true, 0.0F, {}}};
    tables.spells = {{"WarHammer_Spell_Shockwave", 3.0F, 0.0F, 0.0F, 0, 0.0F, 6000.0F, {}},
                     {"WarHammer_Spell_Shockwave_L2", 4.0F, 0.0F, 0.0F, 0, 0.0F, 6000.0F, {}},
                     {"WarHammer_Spell_PathOfDestiny", 4.0F, 2.0F, 3.8F, 3, 200.0F, 770.0F, {}},
                     {"WarHammer_Spell_PathOfDestiny_L2", 4.0F, 2.0F, 3.8F, 3, 200.0F, 770.0F, {}},
                     {"WarHammer_Spell_PathOfDestiny_L3", 4.0F, 2.0F, 3.8F, 3, 200.0F, 770.0F, {}},
                     {"Spell_GreatAxe_GravityWell_Projectile", 0.0F, 0.0F, 10.0F, 0, 0.0F, 2000.0F, {}},
                     {"Spell_GreatAxe_GravityWell_Vortex", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}},
                     {"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}}};
    return tables;
}

world::ItemInstance weapon_item(const char* id) {
    world::ItemInstance item;
    item.instance.fill(0x5A);
    item.itemId = id;
    item.index = 1;
    item.quantity = 1;
    item.gearScore = 100;
    return item;
}

/** Gives a character the two weapons and each node and the bars of the build (the skill system is not a part of these cases). */
void give_build(world::Entity& character, bool hammer = true) {
    character.equipment.slots[world::paperdoll::kMainHand1].item = weapon_item("TestAxe");
    character.equipment.slots[(world::paperdoll::kMainHand1 + 1)].item = weapon_item("TestHammer");
    character.combat.selectedWeapon = hammer ? (world::paperdoll::kMainHand1 + 1) : world::paperdoll::kMainHand1;
    for (const char* id : kAxeNodes) {
        character.skills.pools[kGreatAxe][gamedata::progression_id(id)].level = 1;
    }
    for (const char* id : kHammerNodes) {
        character.skills.pools[kWarHammer][gamedata::progression_id(id)].level = 1;
    }
    character.skills.bars[kGreatAxe] = {kCharge, kMaelstrom, kGravityWell};
    character.skills.bars[kWarHammer] = {kWreckingBall, kShockwave, kPathOfDestiny};
}

/** @return The contact of a weapon with the hit volume of a creature, as a client reports it. */
InputContactItem contact_with(const world::Entity& creature) {
    InputContactItem contact;
    contact.groupActor = creature.actor.bytes;
    contact.group = creature.id;
    contact.actor = creature.actor.bytes;
    contact.entity = creature.id;
    contact.facet = creature.creature.body->target(creature.id, creature.creature.body->components[1]);
    contact.point = {creature.position.x + 0.5F, creature.position.y, creature.position.z + 1.0F};
    return contact;
}

/** One client of a case: its input reports, each with the newest entry and the four entries before it. */
struct Caster {
    ServerHost& host;
    FakeClient& client;
    state::WorldPosition position;
    std::uint16_t input{10};
    float yaw{1.5708F};
    std::deque<InputEntry> history{};

    InputEntry entry(std::vector<unsigned> bits = {}) {
        InputEntry entry = report_entry(++input, position.x, position.y, position.z);
        entry.actionBits = std::move(bits);
        entry.hasLook = true;
        entry.yaw = yaw;
        return entry;
    }
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
    /** A key press of the bar (place 0, 1, 2): the press with the virtual bit, the hold, the release. @return The input of the press. */
    std::uint16_t press(unsigned place) {
        const unsigned keys[] = {kActionAbility1, kActionAbility2, kActionAbility3};
        send(entry({keys[place], kActionAbility1Virtual}));
        const std::uint16_t pressed = input;
        send(entry({kActionAbility1Hold}));
        send(entry({kActionAbility1Release}));
        return pressed;
    }
    void contact(std::vector<InputContactItem> contacts) {
        InputEntry report = entry();
        report.contacts = std::move(contacts);
        send(report);
    }
    void shot(const InputShotItem& item) {
        InputEntry report = entry();
        report.shotItems = {item};
        send(report);
    }
    /** Input steps until `steps` steps after the input `from`. */
    void until(std::uint16_t from, unsigned steps) {
        while (static_cast<std::uint16_t>(input - from) + 1 < steps) {
            send(entry());
        }
    }
};

/** The world of an ability case: one character (A, with the build and the war hammer in its hand) and its client, B with no build. */
struct Arena {
    ServerHost host;
    FakeClient a{27000};
    FakeClient b{27001};
    Player alpha;
    Player beta;
    Caster caster{host, a, kPlace};

    explicit Arena(bool data = true, bool behavior = false) {
        host.progression.set(ability_progression());
        if (!host.creatures.set_full({wolf_slice()}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}})) {
            throw std::runtime_error("ability test: the creature data is not valid");
        }
        if (!host.combat.set(ability_combat())) {
            throw std::runtime_error("ability test: the combat tables are not valid");
        }
        if (data && !host.abilities.set(ability_tables())) {
            throw std::runtime_error("ability test: the ability data is not valid");
        }
        host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
        host.world.set_creature_behavior(behavior);
        alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
        host.enter(a, alpha.registration());
        caster.idle(20);
        give_build(*character());
    }
    world::Entity* character() {
        return host.world.find_character(kCharacterA);
    }
    world::Abilities& abilities() {
        return host.world.abilities();
    }
    world::Entity* wolf(float east, float north = 0.0F) {
        world::Entity* entity = host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
        caster.idle(10);
        return entity;
    }
    std::uint64_t accepted() {
        return abilities().counts().presses[static_cast<std::size_t>(CastVerdict::accepted)];
    }
    std::uint64_t refused(CastVerdict verdict) {
        return abilities().counts().presses[static_cast<std::size_t>(verdict)];
    }
    /** Input steps of the caster for this time. */
    void pass(float seconds) {
        caster.idle(static_cast<int>(std::lround(seconds * 30.0F)));
    }
    /** B enters near A with a client: the second session of a case. */
    void enter_b() {
        beta = add_player(host.store, "76561202255233009", "Tester B", kCharacterB, "Beta", at(kPlace.x - 1.0F, kPlace.y + 1.0F));
        host.enter(b, beta.registration());
        for (int index = 0; index < 20; ++index) {
            host.clock.advance(kTick);
            host.datagram(b.peer, b.report_at(static_cast<std::uint16_t>(100 + index), kPlace.x - 1.0F, kPlace.y + 1.0F, kHeight));
            host.tick();
            b.exchange(host);
            a.exchange(host);
        }
    }
};

/** @return The fragments of one slot and type that a client got from the message `from` on (for each interest). */
std::vector<Fragment> fragments_of(const FakeClient& client, std::uint32_t slot, std::uint32_t type, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot && fragment.type == type) {
                    found.push_back(fragment);
                }
            }
        }
    }
    return found;
}

/** One entry of a cooldown map of the wire: the ability, its end, and its minimum end (microseconds). */
struct WireCooldown {
    std::uint32_t ability{0};
    std::uint64_t end{0};
    std::uint64_t minEnd{0};
};

/** @return The entries of a map (field 0, 1, or 2) of a cooldown fragment. */
std::vector<WireCooldown> cooldown_entries(const Fragment& fragment, unsigned field) {
    std::vector<WireCooldown> entries;
    const auto found = fragment.fields.find({0, field});
    if (found == fragment.fields.end()) {
        return entries;
    }
    server::hub::Reader reader(found->second);
    const auto count = progress_wire::read_full_form(reader);
    for (std::uint32_t index = 0; count && index < *count; ++index) {
        WireCooldown entry;
        entry.ability = reader.u32();
        entry.end = reader.u64();
        entry.minEnd = reader.u64();
        entries.push_back(entry);
    }
    return entries;
}

/** @return The status numbers of the compact map of a status fragment of a creature. */
std::vector<std::uint16_t> status_entries(const Fragment& fragment) {
    std::vector<std::uint16_t> entries;
    const auto found = fragment.fields.find({3, 1});
    if (found == fragment.fields.end()) {
        return entries;
    }
    server::hub::Reader reader(found->second);
    const auto count = progress_wire::read_full_form(reader);
    for (std::uint32_t index = 0; count && index < *count; ++index) {
        entries.push_back(reader.u16());
        reader.u8();
    }
    return entries;
}

std::uint64_t microseconds_of(world::WorldClock::time_point time) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count());
}

std::size_t damage_numbers(const FakeClient& client, std::size_t from = 0) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        count += client.got[index].type == server::rep::kTypeDamageDealt ? 1 : 0;
    }
    return count;
}

bool has_effect(const world::Entity& creature, const char* status) {
    for (const world::CreatureEffect& effect : creature.creature.effects) {
        if (effect.status == status) {
            return true;
        }
    }
    return false;
}

/** AB1: the ability data file (each rule of the loader) and the input reader (the ability keys and the shots). */
void data_and_reader(const Options& options) {
    const auto path = options.workDirectory / "ability-data.json";
    const json good = {{"format", 1},
                       {"statusRows", 4517},
                       {"counts", {{"cooldowns", 1}, {"statusEffects", 1}, {"spells", 1}}},
                       {"cooldowns", {{"Ability_WarHammer_WreckingBall", 15.0, 0.0}}},
                       {"statusEffects", {{"WarHammer_Shockwave_Stun", 1710, 2.0, 0.0, true, false, 1, true, 0.0}}},
                       {"spells", {{"WarHammer_Spell_Shockwave_L2", 4.0, 0.0, 0.0, 0, 0.0, 6000.0}}}};
    const auto load = [&](const json& document) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        gamedata::AbilityData data;
        const bool loaded = data.load(path.string());
        return std::make_pair(loaded, std::move(data));
    };
    {
        auto [loaded, data] = load(good);
        check(loaded && data.cooldown("ability_warhammer_wreckingball") != nullptr && data.cooldown("Ability_WarHammer_WreckingBall")->time == 15.0F
                  && data.status("WARHAMMER_SHOCKWAVE_STUN")->index == 1710 && data.status("WarHammer_Shockwave_Stun")->stunned
                  && data.spell("WarHammer_Spell_Shockwave_L2")->radius == 4.0F,
              "a complete file loads; the lookups ignore the case of the letters");
    }
    json wrong = good;
    wrong["statusEffects"][0][1] = 70000;
    check(!load(wrong).first, "a status number above 16 bits: the file is refused as a whole");
    wrong = good;
    wrong["cooldowns"].push_back({"ABILITY_WARHAMMER_WRECKINGBALL", 10.0, 0.0});
    wrong["counts"]["cooldowns"] = 2;
    check(!load(wrong).first, "two rows with one id: the file is refused");
    wrong = good;
    wrong["counts"]["spells"] = 2;
    check(!load(wrong).first, "a list with fewer rows than its count: the file is refused");
    wrong = good;
    wrong["cooldowns"][0][1] = -1.0;
    check(!load(wrong).first, "a negative cooldown: the file is refused");
    wrong = good;
    wrong["spells"][0][1] = "4";
    check(!load(wrong).first, "a number with the form of a text: the file is refused");
    gamedata::AbilityData none;
    check(!none.load((options.workDirectory / "no-such-ability-data.json").string()) && !none.loaded(), "no file: no data");

    // The reader: the press bits of the three keys, and a shot with its values.
    InputEntry first = report_entry(500, 1000.0F, 1000.0F, kHeight);
    first.actionBits = {kActionAbility2, 571};
    first.shotItems = {InputShotItem{{1001.0F, 1002.0F, 51.5F}, {0.0F, 1.0F, 0.0F}, 0x8E14366BU}};
    InputEntry second = report_entry(499, 1000.0F, 1000.0F, kHeight);
    second.actionBits = {kActionAbility1Hold};
    InputEntry third = report_entry(498, 1000.0F, 1000.0F, kHeight);
    third.actionBits = {kActionAbility3, 523};
    const auto input = server::rep::read_client_input(input_entries_body({first, second, third}));
    check(input && input->steps.size() == 3 && input->steps[0].abilityKey == 2 && input->steps[1].abilityKey == 0
              && input->steps[2].abilityKey == 3,
          "the reader gives the place of the key of a press (R: 2, F: 3), and nothing for a hold bit alone");
    check(input && input->steps.size() == 3 && input->steps[0].shotItems.size() == 1 && input->steps[0].shotItems[0].attack == 0x8E14366BU
              && input->steps[0].shotItems[0].origin[1] == 1002.0F && input->steps[0].shotItems[0].direction[1] == 1.0F
              && input->steps[0].shots == 1,
          "the reader gives the origin, the direction, and the attack of a shot of the second list");
    InputEntry q = report_entry(600, 1000.0F, 1000.0F, kHeight);
    q.actionBits = {kActionAbility1, kActionAbility1Virtual};
    const auto pressed = server::rep::read_client_input(input_entry_body(q));
    check(pressed && pressed->steps.size() == 1 && pressed->steps[0].abilityKey == 1, "the press bit of Q (402) is the place 0");
}

/** AB2: a press of a key casts the ability of the bar of the weapon in the hand, and starts the cooldown of the data; the owner gets the countdown. */
void cast_and_cooldown(const Options&) {
    Arena arena;
    const std::size_t from = arena.a.got.size();
    const world::WorldClock::time_point start = arena.host.clock.now();
    arena.caster.press(0);
    check(arena.accepted() == 1, "a press of Q with the war hammer in the hand casts Wrecking Ball (place 0 of its bar)");
    const world::Entity* own = arena.character();
    const auto timer = own->cooldowns.find(kWreckingBall);
    check(timer != own->cooldowns.end() && timer->second.end - timer->second.start == std::chrono::seconds(15)
              && timer->second.start >= start && timer->second.start - start < std::chrono::milliseconds(100),
          "the cooldown of the data (15 s) starts at the press, on the world clock, as a value of the character");
    arena.caster.idle(2);
    const auto states = fragments_of(arena.a, 45, 2932, from);
    check(!states.empty(), "the owner gets the cooldown state (slot 45, type 2932)");
    if (!states.empty()) {
        const auto map0 = cooldown_entries(states.back(), 0);
        check(map0.size() == 1 && map0[0].ability == kWreckingBall && map0[0].end == microseconds_of(timer->second.end)
                  && map0[0].minEnd == microseconds_of(timer->second.minEnd) && cooldown_entries(states.back(), 1).empty()
                  && cooldown_entries(states.back(), 2).empty(),
              "field 0 (the map of place 0) has the ability id with its end and its minimum end in microseconds of the server "
              "time; the maps of the places 1 and 2 are empty (live check L4)");
    }
    // The press is in five reports: one cast for each input sequence.
    arena.caster.idle(6);
    check(arena.accepted() == 1, "the client sends the entry of a press in five reports: one cast");
    // A press in the cooldown: no cast, no new cooldown.
    const auto end = timer->second.end;
    arena.pass(5.0F);
    arena.caster.press(0);
    check(arena.accepted() == 1 && arena.refused(CastVerdict::cooldown) == 1 && own->cooldowns.at(kWreckingBall).end == end,
          "a press in the cooldown makes no cast and does not change the cooldown");
    arena.pass(10.5F);
    arena.caster.press(0);
    check(arena.accepted() == 2, "a press after the end of the cooldown casts the ability again");
    // The cooldown of a different place: field 1.
    arena.pass(1.0F);
    const std::size_t later = arena.a.got.size();
    arena.caster.press(1);
    arena.caster.idle(2);
    const auto shockwave = fragments_of(arena.a, 45, 2932, later);
    check(arena.accepted() == 3 && !shockwave.empty() && cooldown_entries(shockwave.back(), 1).size() == 1
              && cooldown_entries(shockwave.back(), 1)[0].ability == kShockwave && cooldown_entries(shockwave.back(), 0).size() == 1,
          "Shockwave (place 1) is in field 1, and the map of place 0 keeps Wrecking Ball");
    // The cooldowns are values of the character: a new session gets them. The bars are rows of
    // the character, so that the first record of the new session has them.
    state::SkillRows rows;
    for (const char* id : kHammerNodes) {
        rows.nodes.push_back({kWarHammer, gamedata::progression_id(id), id, 1});
    }
    rows.bars = {{kWarHammer, 0, kWreckingBall, "Ability_WarHammer_WreckingBall"}, {kWarHammer, 1, kShockwave, "Ability_WarHammer_Shockwave"}, {kWarHammer, 2, kPathOfDestiny, "Ability_WarHammer_PathOfDestiny"}};
    check(arena.host.store.write_skills(kCharacterA, rows), "setup: the rows of the bars");
    arena.host.disconnect(arena.a.peer);
    arena.host.steps(5, {});
    FakeClient again(27005);
    arena.host.enter(again, arena.alpha.registration());
    Caster second{arena.host, again, kPlace, 2000};
    second.idle(20);
    give_build(*arena.character());
    const auto restored = fragments_of(again, 45, 2932);
    check(arena.character()->cooldowns.count(kShockwave) == 1 && !restored.empty()
              && cooldown_entries(restored.front(), 1).size() == 1,
          "a new session of the character gets its cooldowns in its first record (MP-1: not a value of the session)");
    second.press(1);
    check(arena.accepted() == 3 && arena.refused(CastVerdict::cooldown) == 2, "the new session cannot cast in the cooldown");
}

/** AB3: each refusal of a press: no cast, no cooldown, no hit. */
void refusals(const Options&) {
    {
        Arena arena(false);
        arena.caster.press(0);
        check(arena.accepted() == 0 && arena.refused(CastVerdict::noData) == 1 && arena.character()->cooldowns.empty(),
              "no ability data: the press is refused");
    }
    Arena arena;
    world::Entity* own = arena.character();
    own->equipment.slots[world::paperdoll::kMainHand3].item = weapon_item("TestSword");
    own->combat.selectedWeapon = world::paperdoll::kMainHand3;
    arena.caster.press(0);
    check(arena.refused(CastVerdict::noWeapon) == 1, "a weapon with no ability table in the hand: no weapon");
    own->combat.selectedWeapon = (world::paperdoll::kMainHand1 + 1);
    own->skills.bars[kWarHammer] = {0, kShockwave, kPathOfDestiny};
    arena.caster.press(0);
    check(arena.refused(CastVerdict::emptyPlace) == 1, "an empty place of the bar: no cast");
    own->skills.bars[kWarHammer] = {kWreckingBall, kShockwave, kPathOfDestiny};
    own->skills.pools[kWarHammer][kWreckingBall].level = 0;
    arena.caster.press(0);
    check(arena.refused(CastVerdict::notLearned) == 1, "an ability on the bar whose node the character does not have: no cast");
    own->skills.pools[kWarHammer][kWreckingBall].level = 1;
    // A cast plays: a press of a different ability in its time is refused.
    arena.caster.press(1);
    arena.caster.press(0);
    check(arena.accepted() == 1 && arena.refused(CastVerdict::casting) == 1 && own->cooldowns.count(kWreckingBall) == 0,
          "a press while a different cast plays makes no cast and no cooldown");
    // A dead character.
    arena.pass(1.0F);
    (void)arena.host.world.health().hurt(kCharacterA, own->vitals.health + 1.0F);
    arena.caster.press(0);
    check(own->life.dead && arena.refused(CastVerdict::dead) == 1, "a dead character casts nothing");
    (void)arena.host.world.health().heal(kCharacterA);
    // The lock of the input: a travel that the client did not confirm.
    (void)arena.host.world.travel(kCharacterA, at(kPlace.x + 500.0F, kPlace.y), world::TravelScreen::off);
    arena.host.steps(1, {&arena.a});
    world::Entity* wolf = arena.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 500.0F, kPlace.y), 0.0F).entity;
    arena.host.steps(5, {&arena.a});
    const float health = wolf->vitals.health;
    InputEntry stale = arena.caster.entry({kActionAbility1, kActionAbility1Virtual});
    arena.caster.send(stale);
    check(arena.refused(CastVerdict::locked) == 1 && own->cooldowns.count(kWreckingBall) == 0,
          "a press before the client confirmed the place of a travel (its loading screen) is refused");
    arena.caster.contact({contact_with(*wolf)});
    check(wolf->vitals.health == health, "a refused press makes no hit: a contact after it hurts nothing");
}

/** AB4: the melee abilities: a contact in the window of a phase hurts with the row of the phase, one hit for each phase. */
void melee_phases(const Options&) {
    Arena arena;
    world::Entity* wolf = arena.wolf(2.5F);
    const float full = wolf->vitals.health;
    const std::size_t numbers = damage_numbers(arena.a);
    const std::uint16_t pressed = arena.caster.press(0);
    arena.caster.until(pressed, 24);
    arena.caster.contact({contact_with(*wolf)});
    check(near(full - wolf->vitals.health, kWeaponDamage * 0.9F), "Wrecking Ball: a contact 24 steps after the press hurts with the row "
                                                                  "of the ability (factor 0.9 of the weapon damage)");
    check(damage_numbers(arena.a, numbers) == 1, "the owner gets the damage number");
    check(wolf->creature.knockedUntil.has_value() && wolf->creature.action && wolf->creature.action->action == kReaction
              && wolf->creature.action->fragment == static_cast<std::uint32_t>(kKnockdownFragment),
          "the knockdown: the creature plays the reaction of power 4 (the fragment r_R4_F of its slice)");
    check(has_effect(*wolf, "WarHammer_Ult_SlowOnCC"), "Aftershock: the hit of an ability of the hammer slows the creature");
    const float afterHit = wolf->vitals.health;
    arena.caster.contact({contact_with(*wolf)});
    check(wolf->vitals.health == afterHit, "a second contact of the same phase hurts the creature no more");
    arena.caster.until(pressed, 60);
    arena.caster.contact({contact_with(*wolf)});
    check(wolf->vitals.health == afterHit, "a contact after the window of the phase is not a hit of the ability (and no attack input fits it)");

    // Maelstrom: two spins, one hit each; Gravity: a root after the hit.
    arena.character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    world::Entity* second = arena.wolf(-2.0F);
    const float secondFull = second->vitals.health;
    const std::uint16_t spin = arena.caster.press(1);
    arena.caster.until(spin, 14);
    arena.caster.contact({contact_with(*second)});
    arena.caster.contact({contact_with(*second)});
    check(near(secondFull - second->vitals.health, kWeaponDamage * 1.1F), "Maelstrom: the first spin hurts one time (factor 1.1)");
    arena.caster.idle(11);
    check(has_effect(*second, "Status_Passive_GreatAxe_Gravity_Root_Hold") && second->creature.rooted(arena.host.clock.now()),
          "Gravity: the creature has the root 0.33 s after the hit");
    arena.caster.until(spin, 26);
    arena.caster.contact({contact_with(*second)});
    check(near(secondFull - second->vitals.health, 2.0F * kWeaponDamage * 1.1F), "No Reprieve: the second spin is a second phase and "
                                                                                "a second hit");
    // Charge: the hit check of a melee attack stays (the distance).
    arena.pass(1.0F);
    world::Entity* far = arena.wolf(9.0F);
    const float farFull = far->vitals.health;
    const std::uint16_t charge = arena.caster.press(0);
    arena.caster.until(charge, 14);
    arena.caster.contact({contact_with(*far)});
    check(far->vitals.health == farFull, "Charge: a contact with a creature 9 m from the position of the character is refused (too far)");
    // The run of the charge: 0.5 m in each input step (15 m/s) to the creature.
    while (arena.caster.position.x < kPlace.x + 7.5F) {
        arena.caster.position.x += 0.5F;
        arena.caster.idle(1);
    }
    arena.caster.contact({contact_with(*far)});
    check(near(farFull - far->vitals.health, kWeaponDamage * 1.2F), "Charge: at the creature its contact hurts with the row 1.2");
}

/** AB5: Shockwave: the creatures in the area around the caster, the stun, the weaken, a wall, a creature on its way back. */
void area_around(const Options& options) {
    Arena arena;
    // A wall 1.6 m to the north of the place (navigation data of the case).
    const auto folder = options.workDirectory / "ability-wall";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    region.tiles[{15, 15}] = [](int cellX, int cellY, std::vector<TestFloor>& floors) {
        const float y = 15.0F * 64.0F + (static_cast<float>(cellY) + 0.5F) * 0.25F;
        const float x = 15.0F * 64.0F + (static_cast<float>(cellX) + 0.5F) * 0.25F;
        if (y > 1001.5F && y < 1001.8F && x > 990.0F && x < 1010.0F) {
            floors.clear();
            floors.push_back({kHeight + 3.0F, gamedata::NavFloor::kBlocked});
        }
    };
    region.write(folder);
    check(arena.host.navigation.set_directory(folder.string()), "setup: the wall data loads");
    world::Entity* near1 = arena.wolf(2.0F);
    world::Entity* near2 = arena.wolf(-3.5F);
    world::Entity* outside = arena.wolf(5.0F);
    world::Entity* behindWall = arena.wolf(0.0F, 3.0F);
    world::Entity* immune = arena.wolf(0.0F, -2.0F);
    immune->creature.mind.mode = world::CreatureMode::home;
    const float full = near1->vitals.health;
    arena.caster.press(1);
    arena.pass(1.0F);
    check(near(full - near1->vitals.health, kWeaponDamage * 0.6F) && near(full - near2->vitals.health, kWeaponDamage * 0.6F),
          "Shockwave with Meteoric Crater: the two creatures in 4 m take the damage of the row L2 (0.6)");
    check(outside->vitals.health == full, "a creature 5 m away takes nothing");
    check(behindWall->vitals.health == full && arena.abilities().counts().blocked >= 1, "a wall between the caster and a creature stops the area");
    check(immune->vitals.health == full && immune->creature.effects.empty(), "a creature on its way back takes no damage and no effect");
    check(has_effect(*near1, "WarHammer_Shockwave_Stun") && has_effect(*near1, "WarHammer_Shockwave_Weaken")
              && has_effect(*near1, "WarHammer_Ult_SlowOnCC") && near1->creature.held(arena.host.clock.now()),
          "each creature of the area has the stun, the weaken (Frailty), and the slow (Aftershock)");
    check(near1->creature.action && near1->creature.action->action == kStun, "a stunned creature plays the action Stun of its slice");
    check(near(near1->creature.damage_factor(arena.host.clock.now()), 0.9F), "the weaken lowers the damage of the creature by 10 percent");
    // The next hit ends the stun (StopOnHitCount 1).
    (void)arena.host.world.creatures().hurt(near2->id, 1.0F, arena.character()->id);
    arena.caster.idle(1);
    check(!has_effect(*near2, "WarHammer_Shockwave_Stun") && !near2->creature.held(arena.host.clock.now()),
          "the next hit ends the stun");
    // The end of the stun on the world clock.
    arena.pass(2.0F);
    check(!has_effect(*near1, "WarHammer_Shockwave_Stun") && !near1->creature.held(arena.host.clock.now())
              && !(near1->creature.action && near1->creature.action->action == kStun),
          "the stun ends after 2 s (the data): the creature has no stun action");
    arena.pass(8.5F);
    check(near1->creature.effects.empty(), "each effect ends at its time (the weaken after 10 s)");
}

/** AB6: Path of Destiny: the circles of the path in front of the caster, one hit for each creature, the stagger, and the shorter cooldown. */
void area_path(const Options&) {
    Arena arena;
    // The caster looks west (yaw +90 degrees): the path goes to the west.
    world::Entity* first = arena.wolf(-4.0F);
    world::Entity* second = arena.wolf(-9.0F, 0.5F);
    world::Entity* behind = arena.wolf(4.5F);
    const float full = first->vitals.health;
    arena.caster.press(2);
    const auto start = arena.character()->cooldowns.at(kPathOfDestiny).start;
    arena.pass(1.6F);

    check(near(full - first->vitals.health, kWeaponDamage * 0.7F) && near(full - second->vitals.health, kWeaponDamage * 0.7F),
          "the two creatures on the path take the damage of the row L3 one time (three circles, one phase)");
    check(behind->vitals.health == full, "a creature behind the caster takes nothing");
    check(first->creature.knockedUntil.has_value(), "Seismic Waves: a hit is a stagger");
    const auto& timer = arena.character()->cooldowns.at(kPathOfDestiny);
    check(timer.end - start == std::chrono::milliseconds(22000) - std::chrono::milliseconds(2200),
          "Stimulated Reduction: each hit takes 5 percent of the cooldown away (two hits: 22 s - 2.2 s)");
}

/** AB7: Gravity Well: the swing, the projectile of the shot, the vortex (root and slow), the burst with Crowded Well, and the throw of the server. */
void gravity_well(const Options&) {
    Arena arena;
    arena.character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    world::Entity* first = arena.wolf(-5.0F);
    world::Entity* second = arena.wolf(-6.0F, 1.5F);
    world::Entity* outside = arena.wolf(-12.0F, 4.0F);
    const float full = first->vitals.health;
    const std::uint16_t pressed = arena.caster.press(2);
    arena.caster.until(pressed, 15);
    arena.caster.shot(InputShotItem{{kPlace.x - 0.5F, kPlace.y, kHeight + 1.5F}, {-1.0F, 0.0F, 0.0F}, gamedata::progression_id("GravityWell")});
    arena.pass(0.5F);
    check(arena.abilities().counts().shots == 1 && arena.abilities().counts().vortexes == 1, "the shot of the client throws the projectile");
    check(has_effect(*first, "GreatAxe_Vortex_Root") && has_effect(*first, "GreatAxe_Vortex") && has_effect(*second, "GreatAxe_Vortex")
              && first->creature.rooted(arena.host.clock.now()) && near(first->creature.speed_factor(arena.host.clock.now()), 0.4F),
          "the vortex at the first creature on the line: the root (1 s) and the slow of 60 percent for each creature in 4 m");
    check(outside->creature.effects.empty(), "a creature outside the vortex takes nothing");
    arena.pass(3.2F);
    check(near(full - first->vitals.health, kWeaponDamage * 1.2F) && near(full - second->vitals.health, kWeaponDamage * 1.2F),
          "the burst 3 s after the start of the vortex: the row of the burst (1.0) with Crowded Well (two creatures: 1.2)");
    // No shot: the server throws the projectile itself.
    arena.pass(30.0F);
    world::Entity* third = arena.wolf(-4.0F);
    arena.caster.press(2);
    arena.pass(1.0F);
    check(arena.abilities().counts().vortexes == 2 && has_effect(*third, "GreatAxe_Vortex_Root"),
          "with no shot of the client the server throws the projectile in the direction of the press");
}

/** AB8: the behavior obeys the effects: no movement and no attack in a stun, no movement in a root, a lower speed in a slow. */
void behavior_obeys(const Options&) {
    Arena arena(true, true);
    world::Entity* own = arena.character();
    own->vitals.healthMax = own->vitals.health = 100000.0F;
    // A slow: 20 percent slower. The way of one step of the behavior (200 ms) before and in the
    // slow, of a wolf 25 m away that runs to the character.
    world::Entity* runner = arena.wolf(25.0F);
    (void)arena.host.world.creatures().hurt(runner->id, 1.0F, own->id);
    const auto one_step = [&] {
        const std::uint64_t steps = arena.host.world.creatures().behavior().counts().steps;
        const state::WorldPosition before = runner->position;
        for (int index = 0; index < 40 && arena.host.world.creatures().behavior().counts().steps == steps; ++index) {
            arena.caster.idle(1);
        }
        return std::hypot(runner->position.x - before.x, runner->position.y - before.y);
    };
    (void)one_step();
    const float fullWay = one_step();
    check(arena.abilities().apply_status(*runner, "WarHammer_Ult_SlowOnCC", own->id), "setup: the slow");
    (void)one_step();
    const float slowWay = one_step();
    note("the way of one step: " + std::to_string(fullWay) + " m, in the slow " + std::to_string(slowWay) + " m");
    check(fullWay > 0.5F && near(slowWay, fullWay * 0.8F, 0.08F), "a slowed creature runs slower (its speed times 0.8)");
    (void)arena.host.world.creatures().remove(runner->id);
    // A wolf 10 m away that runs to the character.
    world::Entity* wolf = arena.wolf(10.0F);
    (void)arena.host.world.creatures().hurt(wolf->id, 1.0F, own->id);
    arena.pass(0.4F);
    check(wolf->creature.mind.mode == world::CreatureMode::fight, "setup: the wolf has the character as its target");
    // A stun: the wolf does not move.
    check(arena.abilities().apply_status(*wolf, "WarHammer_Shockwave_Stun", own->id), "setup: the stun");
    const state::WorldPosition stunned = wolf->position;
    arena.pass(1.5F);
    check(wolf->position.x == stunned.x && wolf->position.y == stunned.y && !wolf->creature.mind.attack,
          "a stunned creature does not move and does not attack");
    arena.pass(1.0F);
    check(wolf->position.x != stunned.x, "after the stun the creature runs again");
    // A root: the wolf does not move, and it attacks in its reach.
    check(arena.abilities().apply_status(*wolf, "GreatAxe_Vortex_Root", own->id, 3.0F), "setup: the root");
    const state::WorldPosition rooted = wolf->position;
    arena.pass(1.0F);
    check(wolf->position.x == rooted.x && wolf->position.y == rooted.y, "a rooted creature does not move");
    // An attack that plays stops at a stun: its contact does nothing.
    own->vitals.health = own->vitals.healthMax;
    for (int index = 0; index < 200 && !wolf->creature.mind.attack; ++index) {
        arena.caster.idle(1);
        wolf->creature.effects.clear();
    }
    check(wolf->creature.mind.attack.has_value(), "setup: the wolf attacks");
    const float health = own->vitals.health;
    check(arena.abilities().apply_status(*wolf, "WarHammer_Shockwave_Stun", own->id), "setup: the stun in the attack");
    arena.pass(1.0F);
    check(own->vitals.health == health && !wolf->creature.mind.attack, "a stun stops the attack that plays: its contact does no damage");
    // A knockdown: no movement, no attack, for its time.
    arena.pass(2.0F);
    check(arena.host.world.creatures().knock(*wolf, 4, std::chrono::milliseconds(2000)), "setup: a knockdown");
    const state::WorldPosition knocked = wolf->position;
    arena.pass(1.5F);
    check(wolf->position.x == knocked.x && !wolf->creature.mind.attack, "a knocked creature does not move and does not attack");
    // A rooted creature on its way back waits: it is not at its anchor (finding F6 of the review of stage 3.5a).
    arena.pass(1.0F);
    check(arena.abilities().apply_status(*wolf, "GreatAxe_Vortex_Root", own->id, 6.0F), "setup: a long root");
    arena.host.world.creatures().behavior().set_ignored(kCharacterA, true);
    // The attack that plays ends first (1.4 s).
    arena.pass(1.6F);
    check(wolf->creature.mind.mode == world::CreatureMode::home && wolf->creature.immune(), "setup: the wolf goes back, immune");
    const state::WorldPosition waiting = wolf->position;
    arena.pass(2.0F);
    check(wolf->creature.mind.mode == world::CreatureMode::home && wolf->position.x == waiting.x && wolf->creature.immune(),
          "a rooted creature on its way back waits at its place: no arrival at its anchor, and it stays immune");
    check(!arena.abilities().apply_status(*wolf, "WarHammer_Shockwave_Stun", own->id), "a creature on its way back takes no new effect");
}

/** AB9: two sessions: the observer gets the effect and the reaction, only the owner gets the cooldown and the damage number. */
void two_sessions(const Options&) {
    Arena arena;
    arena.enter_b();
    world::Entity* wolf = arena.wolf(2.0F);
    arena.host.steps(10, {&arena.a, &arena.b});
    const std::size_t fromA = arena.a.got.size();
    const std::size_t fromB = arena.b.got.size();
    arena.caster.press(1);
    for (int index = 0; index < 40; ++index) {
        arena.caster.idle(1);
        arena.b.exchange(arena.host);
    }
    check(has_effect(*wolf, "WarHammer_Shockwave_Stun"), "setup: the Shockwave of A stuns the wolf");
    const auto statusB = fragments_of(arena.b, kStatusSlot, 4236, fromB);
    const auto statusA = fragments_of(arena.a, kStatusSlot, 4236, fromA);
    const auto has_stun = [](const std::vector<Fragment>& fragments) {
        for (const Fragment& fragment : fragments) {
            for (const std::uint16_t index : status_entries(fragment)) {
                if (index == kStunIndex) {
                    return true;
                }
            }
        }
        return false;
    };
    check(has_stun(statusB) && has_stun(statusA), "each observer of the wolf (A and B) gets its compact status map with the stun "
                                                  "(slot of the slice, group 3, field 1)");
    check(!fragments_of(arena.b, kWolfSlots[0], 11, fromB).empty(), "B gets the movement state of the wolf (the stun action)");
    check(fragments_of(arena.b, 45, 2932, fromB).empty() && !fragments_of(arena.a, 45, 2932, fromA).empty(),
          "only the owner gets the cooldown state");
    check(damage_numbers(arena.b, fromB) == 0 && damage_numbers(arena.a, fromA) == 1, "only the owner gets the damage number");
    // The end of the effect: each observer gets the empty map.
    const std::size_t laterB = arena.b.got.size();
    for (int index = 0; index < 400; ++index) {
        arena.caster.idle(1);
        arena.b.exchange(arena.host);
    }
    const auto cleared = fragments_of(arena.b, kStatusSlot, 4236, laterB);
    check(!cleared.empty() && status_entries(cleared.back()).empty(), "at the end of the effects the observer gets the empty map");
    // B presses a key with no build: refused, and nothing changes for A.
    InputEntry press = report_entry(300, kPlace.x - 1.0F, kPlace.y + 1.0F, kHeight);
    press.actionBits = {kActionAbility1, kActionAbility1Virtual};
    arena.host.datagram(arena.b.peer, arena.b.input_report(press));
    arena.host.steps(2, {&arena.a, &arena.b});
    check(arena.refused(CastVerdict::noWeapon) == 1 && arena.host.world.find_character(kCharacterB)->cooldowns.empty(),
          "a press of B (no weapon in its hand) is a press of the character of B: refused");
}

/** AB10: the load: area abilities and effects near many creatures, in a world of many creatures, with a time limit. */
void load(const Options&) {
    Arena arena;
    world::Entity* own = arena.character();
    // 4,000 creatures in the world, far from the character, and 40 near it.
    for (int index = 0; index < 4000; ++index) {
        (void)arena.host.world.creatures().spawn("Test_Wolf", at(3000.0F + static_cast<float>(index % 100) * 3.0F,
                                                                 3000.0F + static_cast<float>(index / 100) * 3.0F),
                                                 0.0F);
    }
    for (int index = 0; index < 40; ++index) {
        (void)arena.host.world.creatures().spawn("Test_Wolf", at(kPlace.x - 3.0F + static_cast<float>(index % 8) * 0.8F,
                                                                 kPlace.y - 3.0F + static_cast<float>(index / 8) * 1.2F),
                                                 0.0F);
    }
    arena.caster.idle(5);
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t lookedBefore = arena.abilities().counts().areaLooked;
    for (int cast = 0; cast < 100; ++cast) {
        // The test client has no cooldown of its own: the reset ends the cooldown of the client too.
        (void)arena.abilities().reset_cooldowns(kCharacterA, true);
        arena.caster.press(1);
        arena.caster.idle(27);
        own->vitals.health = own->vitals.healthMax;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const std::uint64_t looked = arena.abilities().counts().areaLooked - lookedBefore;
    note("100 Shockwaves near 40 creatures in a world of 4,040 creatures: " + std::to_string(seconds) + " s, "
         + std::to_string(looked) + " entities looked at, " + std::to_string(arena.abilities().counts().areaHits) + " area hits");
    check(arena.accepted() == 100 && arena.abilities().counts().areaHits >= 100 * 32, "setup: 100 casts, each with a full area");
    check(looked <= 100 * 60, "an area looks only at the entities near its center (the position index), not at each creature of the world");
    check(seconds < 5.0, "100 casts with 3,000 ticks and their effects in less than 5 s");
}

/** AB11: the commands: the node and bar commands (the rules of the tree), the cooldown commands, the effect command. */
void commands(const Options&) {
    Arena arena;
    namespace commands = server::http::commands;
    const auto run = [&](commands::Command command, const json& values) {
        const commands::JsonArguments arguments(values);
        service::HttpResponse refusal;
        const auto step = command(kCharacterA, arguments, refusal);
        return step ? step(arena.host.world) : refusal;
    };
    // A fresh tree of the war hammer through the skill system.
    (void)arena.host.world.skills().set_rank(kCharacterA, "WarHammerAbilityTable", gamedata::ProgressionKind::weapon, 19);
    world::Entity* own = arena.character();
    own->skills.pools.erase(kWarHammer);
    own->skills.bars.erase(kWarHammer);
    check(run(commands::assign_node, {{"weapon", "warhammer"}, {"node", "Ability_WarHammer_WreckingBall"}}).status == 200
              && own->skills.pools[kWarHammer][kWreckingBall].level == 1,
          "the node command spends one mastery point through the skill system");
    check(run(commands::assign_node, {{"weapon", "warhammer"}, {"node", "Ability_WarHammer_WreckingBall"}}).status == 400,
          "a node that the character has: refused");
    check(run(commands::assign_node, {{"weapon", "warhammer"}, {"node", "No_Such_Node"}}).status == 404, "an unknown node: 404");
    check(run(commands::set_ability_bar, {{"weapon", "warhammer"}, {"slot", 0}, {"ability", "Ability_WarHammer_Shockwave"}}).status == 400,
          "the bar command refuses an ability whose node the character does not have");
    check(run(commands::set_ability_bar, {{"weapon", "warhammer"}, {"slot", 0}, {"ability", "Ability_WarHammer_WreckingBall"}}).status == 200
              && own->skills.bars[kWarHammer][0] == kWreckingBall,
          "the bar command puts the ability on its place");
    arena.caster.press(0);
    check(arena.accepted() == 1 && own->cooldowns.count(kWreckingBall) == 1, "setup: a cast with the bar of the command");
    check(run(commands::reset_cooldowns, json::object()).status == 200 && own->cooldowns.empty(), "the reset command ends each cooldown");
    const json view = commands::abilities_json(arena.host.world, kCharacterA);
    check(view["loaded"] == true && view["presses"]["accepted"] == 1 && view["cooldowns"].empty(), "the view of the abilities");
    world::Entity* wolf = arena.wolf(3.0F);
    const json effect{{"creature", [&] {
                           char text[17];
                           std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(wolf->id));
                           return std::string(text);
                       }()},
                      {"status", "GreatAxe_Vortex_Root"},
                      {"seconds", 5}};
    const commands::JsonArguments arguments(effect);
    service::HttpResponse refusal;
    const auto step = commands::give_creature_effect("", arguments, commands::CreatureReach::world, refusal);
    check(step && step(arena.host.world).status == 200 && wolf->creature.rooted(arena.host.clock.now()),
          "the effect command gives a creature an effect of the data, with the time of the command");
}

/** AB12: the plan of each ability for the nodes of the build, and without them. */
void plans(const Options&) {
    world::SkillSet full;
    world::Entity holder;
    give_build(holder);
    full = holder.skills;
    const auto maelstrom = world::Abilities::plan(kMaelstrom, full);
    check(maelstrom && maelstrom->phases.size() == 2 && maelstrom->phases[0].row == "GreatAxe_Maelstrom_Reach_RP2"
              && maelstrom->phases[1].row == "GreatAxe_Maelstrom_NoReprieve_RP2" && maelstrom->phases[0].lastStep < maelstrom->phases[1].firstStep,
          "Maelstrom of the build: two phases, the rows of Storm's Reach and Gravity, two windows that do not overlap");
    world::SkillSet bare;
    bare.pools[kGreatAxe][kMaelstrom].level = 1;
    bare.pools[kWarHammer][kShockwave].level = 1;
    bare.pools[kWarHammer][kPathOfDestiny].level = 1;
    const auto plain = world::Abilities::plan(kMaelstrom, bare);
    check(plain && plain->phases.size() == 1 && plain->phases[0].row == "GreatAxe_Maelstrom" && plain->phases[0].effects.empty(),
          "Maelstrom with no upgrade: one phase, the base row, no root");
    const auto shockwave = world::Abilities::plan(kShockwave, bare);
    check(shockwave && shockwave->phases[0].spell == "WarHammer_Spell_Shockwave" && shockwave->phases[0].effects.size() == 1,
          "Shockwave with no upgrade: radius 3, only the stun");
    const auto path = world::Abilities::plan(kPathOfDestiny, bare);
    check(path && path->phases[0].row == "Warhammer_Damage_PathOfDestiny" && !path->phases[0].cooldownPerHit,
          "Path of Destiny with no upgrade: the base row, no shorter cooldown");
    check(!world::Abilities::plan(gamedata::progression_id("Ability_Sword_WhirlingBlade"), full),
          "an ability of a different weapon has no rule in this stage");
}

// The review of stage 3.3a (findings F1 to F10, stage 1.3b): the proof tests of the review, each one a case that
// failed before its correction. The review wrote them; the corrections are in world/abilities.cpp, world/combat.cpp,
// world/creatures.cpp, gamedata/ability_data.cpp, and server/rep/states/cooldown_state.cpp.

void early_contacts(const Options&) {
    Arena arena;
    arena.character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    auto* wolf = arena.wolf(2);
    const float full = wolf->vitals.health;
    arena.caster.idle(20);
    arena.caster.send(arena.caster.entry({kActionAbility2}));
    const auto started = arena.host.clock.now();
    for (int step=1;step<=26;++step) {
        auto entry=arena.caster.entry();
        if(step==14 || step==26) entry.contacts={contact_with(*wolf)};
        arena.host.clock.advance(std::chrono::milliseconds(1));
        arena.host.datagram(arena.a.peer,arena.a.input_report(entry));
        arena.host.tick();
        arena.a.exchange(arena.host);
    }
    note("elapsed us=" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(arena.host.clock.now()-started).count())
         + ", contact hits=" + std::to_string(arena.abilities().counts().contactHits) + ", damage=" + std::to_string(full-wolf->vitals.health));
    const auto& track=arena.host.session(arena.character()->owner).attack_track();
    for(unsigned i=1;i<world::kHitVerdictCount;++i) {
        const auto v=static_cast<world::HitVerdict>(i);
        if(track.count(v)) note(std::string(world::hit_verdict_text(v))+"="+std::to_string(track.count(v)));
    }
    check(arena.accepted() == 1, "setup: one accepted Maelstrom press through the session");
    check(track.count(world::HitVerdict::notObserved)==0,"setup: the session observes the creature");
    check(arena.abilities().counts().contactHits <= 1, "the second spin cannot hit before its world-clock window starts");
}

void shot_origin_wall(const Options& options) {
    Arena arena;
    arena.character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    const auto folder = options.workDirectory / "review-shot-wall";
    TestNavRegion region;
    region.tiles[{15,15}] = [](int cx, int, std::vector<TestFloor>& floors) {
        const float x=960+(cx+0.5F)*0.25F;
        if(x>998 && x<998.3F) floors={{kHeight+4, gamedata::NavFloor::kBlocked}};
    };
    region.write(folder);
    check(arena.host.navigation.set_directory(folder.string()), "setup: navigation loads");
    auto* wolf=arena.wolf(-6);
    check(!arena.host.world.navigation().sees(kPlace,wolf->position), "setup: a wall blocks the caster from the target");
    arena.caster.press(2);
    arena.caster.shot({{996,1000,51.5F},{-1,0,0},gamedata::progression_id("GravityWell")});
    arena.pass(0.3F);
    note("shots="+std::to_string(arena.abilities().counts().shots)+", rooted="+std::to_string(wolf->creature.rooted(arena.host.clock.now())));
    check(!wolf->creature.rooted(arena.host.clock.now()), "an origin beyond the wall cannot launch the well through the wall");
}

void phase_cap(const Options&) {
    Arena arena;
    std::vector<world::EntityId> ids;
    for(int group=0;group<2;++group) for(int i=0;i<20;++i) {
        auto* e=arena.host.world.creatures().spawn("Test_Wolf",at(kPlace.x-(group?11.4F:3.8F),kPlace.y),0).entity;
        ids.push_back(e->id);
    }
    arena.caster.idle(20);
    arena.caster.press(2);
    arena.pass(1.6F);
    std::size_t hurt=0;
    for(auto id:ids) hurt += arena.host.world.find(id)->vitals.health<kWolfHealth;
    note("one path phase hurt="+std::to_string(hurt));
    check(arena.accepted()==1, "setup: one accepted Path of Destiny");
    check(hurt<=world::ability_rules::kAreaMost, "one phase hurts at most 32 creatures across all circles");
}

void overlap_control(const Options&) {
    Arena arena;
    auto* wolf=arena.wolf(2);
    check(arena.abilities().apply_status(*wolf,"WarHammer_Shockwave_Stun",arena.character()->id,5),"setup: a five-second stun");
    check(arena.host.world.creatures().knock(*wolf,4,std::chrono::milliseconds(1000)),"setup: a shorter knockdown");
    arena.pass(1.2F);
    note("held="+std::to_string(wolf->creature.held(arena.host.clock.now()))+", action="+(wolf->creature.action?std::to_string(wolf->creature.action->action):"none"));
    check(wolf->creature.held(arena.host.clock.now()),"setup: the stun still holds the creature");
    arena.enter_b();
    int action=-3;
    const auto records=fragments_of(arena.b,kWolfSlots[0],11);
    if(!records.empty()) {
        const auto it=records.front().fields.find({0,33});
        if(it!=records.front().fields.end()) {
            server::hub::Reader reader(it->second);
            if(reader.size()==0) action=-1;
            else { reader.u16(); reader.size(); action=static_cast<int>(reader.size())-1; }
        }
    }
    note("new observer wire action="+std::to_string(action));
    check(action==static_cast<int>(kStun),"a new observer gets the remaining Stun action");
    check(wolf->creature.action && wolf->creature.action->action==kStun,"the remaining stun restores its Stun record after the knockdown");
}

void knock_without_fragment(const Options&) {
    Arena arena;
    auto slice=wolf_slice();
    slice.actions[kReaction].fragmentIndex=-1;
    slice.reactions.fill(-1);
    check(arena.host.creatures.set_full({slice},{wolf_kind()},{},{{1,kWolfHealth,100,50}}),"setup: a valid slice with no reaction fragment");
    auto* wolf=arena.wolf(2);
    check(!wolf->creature.body->hitAction,"setup: the slice has no usable Reaction action");
    const auto press=arena.caster.press(0);
    arena.caster.until(press,24);
    arena.caster.contact({contact_with(*wolf)});
    note("damage="+std::to_string(kWolfHealth-wolf->vitals.health)+", held="+std::to_string(wolf->creature.held(arena.host.clock.now())));
    check(wolf->vitals.health<kWolfHealth,"setup: Wrecking Ball hit the creature");
    check(wolf->creature.held(arena.host.clock.now()),"a missing reaction fragment does not remove the knockdown rule");
}

void missing_status(const Options&) {
    Arena arena;
    auto data=ability_tables();
    data.statusEffects.clear();
    check(arena.host.abilities.set(data),"setup: the loader accepts cooldowns and spells with no status rows");
    auto* wolf=arena.wolf(2);
    const float full=wolf->vitals.health;
    arena.caster.press(1);
    arena.pass(1);
    note("accepted="+std::to_string(arena.accepted())+", damage="+std::to_string(full-wolf->vitals.health)+", effects="+std::to_string(wolf->creature.effects.size()));
    check(arena.accepted()==0 && wolf->vitals.health==full && arena.character()->cooldowns.empty(),"an incomplete Shockwave plan is refused before its cooldown or damage");
}

void status_indices(const Options& options) {
    json document={{"format",1},{"statusRows",1},{"counts",{{"cooldowns",1},{"statusEffects",2},{"spells",0}}},
        {"cooldowns",{{"Ability_WarHammer_Shockwave",20.0,0.0}}},
        {"statusEffects",{{"WarHammer_Shockwave_Stun",1710,2.0,0.0,true,false,1,true,0.0},
                          {"GreatAxe_Vortex_Root",1710,1.0,0.0,false,true,0,true,0.0}}},{"spells",json::array()}};
    const auto path=options.workDirectory/"review-status-indices.json";
    std::ofstream(path)<<document.dump();
    gamedata::AbilityData data;
    const bool loaded=data.load(path.string());
    note("loaded="+std::to_string(loaded)+", declared status rows=1, both wire indices=1710");
    check(!loaded,"the loader refuses status indices that do not fit the declared row list");
}

void cooldown_bar_move(const Options&) {
    Arena arena;
    arena.caster.press(1);
    arena.pass(1);
    const auto from=arena.a.got.size();
    check(arena.host.world.skills().set_bar(kCharacterA,kWarHammer,0,kShockwave).ok(),"setup: move the cooling Shockwave from R to Q");
    arena.caster.idle(3);
    const auto states=fragments_of(arena.a,45,2932,from);
    note("cooldown fragments after bar move="+std::to_string(states.size())+", running cooldowns="+std::to_string(arena.character()->cooldowns.size()));
    check(!states.empty(),"moving an ability on its bar sends its timer on the new key");
}

void refused_contact(const Options&) {
    for(bool refused : {false,true}) {
        Arena arena;
        auto* wolf=arena.wolf(2);
        const float full=wolf->vitals.health;
        arena.caster.press(0);
        if(refused) {
            arena.caster.press(2);
            arena.caster.idle(8);
        } else {
            arena.pass(0.85F);
            arena.caster.send(arena.caster.entry({kActionAttack}));
            arena.caster.send(arena.caster.entry({kActionAttackRelease}));
            arena.caster.idle(5);
        }
        arena.caster.contact({contact_with(*wolf)});
        note(std::string(refused?"refused ability":"later light attack")+": contact hits="+std::to_string(arena.abilities().counts().contactHits)+", damage="+std::to_string(full-wolf->vitals.health));
        if(refused) {
            check(arena.refused(CastVerdict::casting)==1,"setup: the server refused the second ability press");
            check(wolf->vitals.health==full,"a contact following the refused press cannot borrow the earlier cast");
        } else {
            check(near(full-wolf->vitals.health,kWeaponDamage),"a light attack after the cast uses its own damage row");
            check(!wolf->creature.knockedUntil,"a light attack after Wrecking Ball does not add a knockdown");
        }
    }
}

void shorten_knock(const Options&) {
    Arena arena;
    auto* wolf=arena.wolf(2);
    check(arena.host.world.creatures().knock(*wolf,4,std::chrono::milliseconds(2000)),"setup: a two-second knockdown");
    const auto firstEnd=*wolf->creature.knockedUntil;
    arena.pass(0.3F);
    check(arena.host.world.creatures().knock(*wolf,2,std::chrono::milliseconds(1000)),"setup: a one-second stagger during the knockdown");
    note("old end minus replacement end ms="+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(firstEnd-*wolf->creature.knockedUntil).count()));
    arena.pass(1.1F);
    note("old knockdown still due="+std::to_string(arena.host.clock.now()<firstEnd)+", held="+std::to_string(wolf->creature.held(arena.host.clock.now())));
    check(wolf->creature.held(arena.host.clock.now()),"a shorter new stagger does not end the earlier knockdown");
}

void positive_controls(const Options&) {
    {
        Arena arena;
        auto* wolf=arena.wolf(2);
        arena.caster.idle(20);
        arena.caster.contact({contact_with(*wolf)});
        check(wolf->vitals.health==kWolfHealth,"a contact with no press or basic attack hurts nothing");
        arena.host.clock.advance(std::chrono::seconds(3));
        arena.caster.history.clear();
        arena.caster.input=65530;
        arena.caster.send(arena.caster.entry());
        const auto press=arena.caster.press(0);
        arena.caster.until(press,24);
        arena.caster.contact({contact_with(*wolf)});
        check(arena.accepted()==1 && arena.abilities().counts().contactHits==1,"one cast and one hit across input-number wrap");
        const auto after=wolf->vitals.health;
        arena.caster.contact({contact_with(*wolf)});
        check(wolf->vitals.health==after,"a second contact in the same phase is refused after wrap");
    }
    for(bool nonfinite : {false,true}) {
        Arena arena;
        arena.character()->combat.selectedWeapon=world::paperdoll::kMainHand1;
        auto* wolf=arena.wolf(-5);
        InputShotItem shot{{9000,9000,9000},{-100,0,0},gamedata::progression_id("GravityWell")};
        if(nonfinite) shot.direction[0]=std::numeric_limits<float>::quiet_NaN();
        arena.caster.shot(shot);
        check(arena.abilities().counts().shots==0,"a shot with no press has no effect");
        arena.caster.press(2);
        arena.caster.shot(shot);
        arena.caster.shot(shot);
        arena.pass(0.4F);
        check(arena.abilities().counts().shots==1 && arena.abilities().counts().vortexes==1,"many shots make one well; a far origin is replaced by the caster origin");
        check(wolf->creature.rooted(arena.host.clock.now()),nonfinite?"a nonfinite flat direction uses the caster yaw":"a nonunit flat direction follows the normalized line");
    }
    for(bool leave : {false,true}) {
        Arena arena;
        auto* wolf=arena.wolf(2);
        arena.caster.press(1);
        if(leave) arena.host.disconnect(arena.a.peer);
        else (void)arena.host.world.health().hurt(kCharacterA,arena.character()->vitals.health+1);
        arena.host.clock.advance(std::chrono::seconds(1));
        arena.host.tick();
        check(wolf->vitals.health==kWolfHealth,leave?"a caster that left causes no delayed area damage":"a dead caster causes no delayed area damage");
    }
}

} // namespace

std::vector<Case> ability_cases() {
    return {{"AB1", "abilities: the ability data file and the input reader (keys, shots)", data_and_reader},
            {"AB2", "abilities: a cast starts the cooldown of the data; the owner gets the countdown; a new session keeps it", cast_and_cooldown},
            {"AB3", "abilities: each refusal of a press (no cast, no cooldown, no hit)", refusals},
            {"AB4", "abilities: the melee phases (Wrecking Ball, Maelstrom, Charge), one hit for each phase", melee_phases},
            {"AB5", "abilities: Shockwave (the area, a wall, the stun, the weaken, the way back)", area_around},
            {"AB6", "abilities: Path of Destiny (the path, one hit, the stagger, the shorter cooldown)", area_path},
            {"AB7", "abilities: Gravity Well (the shot, the vortex, the burst, the throw of the server)", gravity_well},
            {"AB8", "abilities: the behavior obeys a stun, a slow, a root, a knockdown (and finding F6)", behavior_obeys},
            {"AB9", "abilities: two sessions (the observer gets the effect, only the owner the cooldown and the number)", two_sessions},
            {"AB10", "abilities: the load of area abilities near many creatures, with a time limit", load},
            {"AB11", "abilities: the node, bar, cooldown, and effect commands", commands},
            {"AB12", "abilities: the plan of each ability for the nodes of the character", plans},
            {"RA1", "review of 3.3a, F1: a contact before the world time of its phase is not a hit", early_contacts},
            {"RA2", "review of 3.3a, F3: a shot origin behind a wall does not throw the well through the wall", shot_origin_wall},
            {"RA3", "review of 3.3a, F4: one phase of an area hurts the cap of creatures at most, across its circles", phase_cap},
            {"RA4", "review of 3.3a, F7: the stun action comes back after a shorter knockdown", overlap_control},
            {"RA5", "review of 3.3a, F5: a slice with no reaction fragment gets the knockdown hold", knock_without_fragment},
            {"RA6", "review of 3.3a, F9: a cast whose status rows are not in the data is refused", missing_status},
            {"RA7", "review of 3.3a, F10: the loader refuses status numbers that do not fit together", status_indices},
            {"RA8", "review of 3.3a, F8: a bar change sends the running cooldown on its new key", cooldown_bar_move},
            {"RA9", "review of 3.3a, F2: a later attack and a refused press do not take the row of an earlier cast", refused_contact},
            {"RA10", "review of 3.3a, F6: a shorter stagger does not end an earlier knockdown", shorten_knock},
            {"RA11", "review of 3.3a: the controls of the review (input wrap, shots, a caster that left or died)", positive_controls}};
}

} // namespace soulwarden::backend::tests
