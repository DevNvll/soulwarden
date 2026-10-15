// Cases for the spell entities of stage 3.3b-S2a (world/spells.h, server/rep/states/spell_state.cpp, gamedata/spell_data.h)
// and for the speed table of a character (S2a-I10, states/stat_multiplier_state.cpp). The vortex slice of the cases has the
// structure of the slice of the game (the slots, the delays, and the event types of greataxe_gravitywell_vortex) with a
// made-up asset id. All ids, names, and values are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
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
namespace {

using namespace shop;
using std::chrono::milliseconds;

constexpr const char* kVortex = "Spell_GreatAxe_GravityWell_Vortex";
constexpr std::array<std::uint8_t, 16> kVortexAsset{0x51, 0x50, 0xE1, 0x10, 0x00, 0x00, 0x40, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
const std::uint32_t kGravityWellNode = gamedata::progression_id("Ability_Greataxe_GravityWell");

/** The vortex slice of the cases: the structure of the slice of the game (slots, delays, event types). */
gamedata::SpellSlice vortex_slice(float burst = 3.0F) {
    gamedata::SpellSlice slice;
    slice.path = "slices/items/weapons/test_vortex.dynamicslice";
    slice.asset = kVortexAsset;
    slice.subId = 2;
    slice.root = 0x0000025ACFDAA308ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = 1;
    slice.slots = {{1, "Spell", 2912}, {2, "Ownership", 3217}, {3, "TemporaryAffiliation", 3563}};
    for (std::uint32_t slot = 4; slot <= 9; ++slot) {
        slice.slots.push_back({slot, "DelayedEvent", 3139});
    }
    slice.events = {{4, 3.0F, gamedata::spell_event::kDeactivate, "VFX_Foe", "", false},
                    {5, 3.0F, gamedata::spell_event::kDeactivate, "VFX_Vortex", "", false},
                    {6, 3.1F, gamedata::spell_event::kDeactivate, "DetectionVolume", "", false},
                    {7, burst, gamedata::spell_event::kActivate, "Burst", "GreatAxe_GravityWell_Burst", false},
                    {8, 1.0F, gamedata::spell_event::kDeactivate, "VortexRoot", "GreatAxe_GravityWell_Vortex_Root", false},
                    {9, 3.5F, gamedata::spell_event::kTrigger, "Destroy", "", true}};
    return slice;
}

gamedata::SpellTables spell_tables(float burst = 3.0F) {
    gamedata::SpellTables tables;
    tables.slices = {vortex_slice(burst)};
    tables.spells = {{kVortex, 0}, {"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 0}};
    return tables;
}

/** The ability data of the shop with the rows of Gravity Well (the values of the game data: the root 1 s, the slow -0.6 for 3 s). */
gamedata::AbilityTables gravity_tables(bool endOnDeath = false) {
    gamedata::AbilityTables tables = shop_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    gamedata::StatusRow root = status("GreatAxe_Vortex_Root", 1.0F, {}, true);
    root.root = true;
    rows.push_back(root);
    gamedata::StatusRow slow = status("GreatAxe_Vortex", 3.0F, {});
    slow.moveSpeed = -0.6F;
    rows.push_back(slow);
    rows.push_back(status("Review_Haste40", 30.0F, cells({{"SprintSpeedMod", 0.4F}}, {{"EffectCategories", "Haste"}})));
    rows.back().moveSpeed = 0.4F;
    tables.statusEffects = numbered(std::move(rows));
    gamedata::SpellRow vortex{kVortex, 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}};
    if (endOnDeath) {
        vortex.cells.numbers["EndOnCasterDeath"] = 1.0F;
    }
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Projectile", 0.0F, 0.0F, 10.0F, 0, 0.0F, 2000.0F, {}});
    tables.spells.push_back(vortex);
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    return tables;
}

/** The world of a case: A with the great axe and Gravity Well on place F of the bar; B (enter_b, or at a place). */
struct SpellShop : Shop {
    explicit SpellShop(bool spellData = true, bool endOnDeath = false, float burst = 3.0F) : Shop(true, false, false) {
        if (!host.abilities.set(gravity_tables(endOnDeath))) {
            throw std::runtime_error("spell test: the ability data is not valid: " + host.abilities.error());
        }
        if (spellData && !host.spells.set(spell_tables(burst))) {
            throw std::runtime_error("spell test: the spell data is not valid: " + host.spells.error());
        }
        if (!wear("TestAxe", world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error("spell test: the axe");
        }
        give_well(*character());
    }
    static void give_well(world::Entity& character) {
        character.skills.pools[kGreatAxe][kGravityWellNode].level = 1;
        character.skills.bars[kGreatAxe] = {0, 0, kGravityWellNode};
        character.combat.selectedWeapon = world::paperdoll::kMainHand1;
    }
    /** B enters at a place with a client, and stays there for some ticks. */
    void enter_b_at(const state::WorldPosition& place) {
        beta = add_player(host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", place);
        host.enter(b, beta.registration());
        b_steps(20, place);
    }
    std::uint16_t bInput{100};
    /** Ticks of the world with an input report of B at a place (and an idle step of A). */
    void b_steps(int count, const state::WorldPosition& place) {
        for (int index = 0; index < count; ++index) {
            host.clock.advance(kTick);
            host.datagram(b.peer, b.report_at(bInput++, place.x, place.y, place.z));
            host.tick();
            b.exchange(host);
            a.exchange(host);
        }
    }
    /** The spell entities of the world. */
    std::vector<const world::Entity*> spells() {
        std::vector<const world::Entity*> found;
        for (const auto& [id, entity] : world().entities()) {
            if (entity.kind == world::EntityKind::spell) {
                found.push_back(&entity);
            }
        }
        return found;
    }
    /** Presses F (Gravity Well) and waits until the spell is in the world (the throw of the server and the flight). */
    const world::Entity* cast_well() {
        walker.press(2);
        for (int step = 0; step < 60 && spells().empty(); ++step) {
            walker.idle(1);
        }
        return spells().empty() ? nullptr : spells().front();
    }
};

/** @return The interest numbers of the records of a client that have a fragment of this type, from the message `from` on. */
std::vector<std::uint32_t> interests_with(const FakeClient& client, std::uint32_t type, std::size_t from = 0) {
    std::vector<std::uint32_t> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (std::any_of(record.fragments.begin(), record.fragments.end(), [&](const Fragment& fragment) { return fragment.type == type; })) {
                found.push_back(record.interest);
            }
        }
    }
    return found;
}

/** @return The interest numbers of the stops that a client got from the message `from` on. */
std::vector<std::uint32_t> stops_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::uint32_t> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == 8) {
            const Bundle bundle = read_bundle(client.got[index].body);
            found.insert(found.end(), bundle.stops.begin(), bundle.stops.end());
        }
    }
    return found;
}

/** @return The remaining time (seconds) and the completed mark of the delayed event state of a slot (the first one from `from` on). */
std::pair<float, int> delayed_of(const FakeClient& client, std::uint32_t slot, std::size_t from = 0) {
    const std::vector<Fragment> fragments = fragments_of(client, slot, server::rep::kTypeDelayedEventState, from);
    if (fragments.empty() || !fragments.front().fields.contains({0, 0}) || !fragments.front().fields.contains({0, 1})) {
        return {-1.0F, -1};
    }
    const Bytes& half = fragments.front().fields.at({0, 0});
    const std::uint16_t bits = static_cast<std::uint16_t>((half[0] << 8) | half[1]);
    const int exponent = (bits >> 10) & 0x1F;
    const float mantissa = static_cast<float>(bits & 0x3FF);
    const float value = exponent == 0 ? 0.0F : std::ldexp(1.0F + mantissa / 1024.0F, exponent - 15);
    return {value, fragments.front().fields.at({0, 1})[0]};
}

bool has_status(const world::Entity& creature, const char* status) {
    return std::any_of(creature.creature.effects.begin(), creature.creature.effects.end(),
                       [&](const world::CreatureEffect& effect) { return effect.status == status; });
}

std::string hex(const Bytes& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

/** SP1 (S2a-I3): the spell data: a complete set, its lookups, each refusal, a file, and no file. */
void data_and_loader(const Options& options) {
    gamedata::SpellData data;
    check(data.set(spell_tables()) && data.loaded(), "a complete set of the spell data is accepted");
    const gamedata::SpellSlice* slice = data.slice_of("spell_greataxe_gravitywell_vortex");
    check(slice != nullptr && slice->end_delay() == 3.5F && slice->activation_delay("GreatAxe_GravityWell_Burst") == 3.0F
              && !slice->activation_delay("No_Row"),
          "the lookup ignores the case of the letters; the end is the trigger of the Destroy entity (3.5 s), the burst the activation of the "
          "entity with the burst row (3 s)");
    const auto refused = [](const char* what, auto change) {
        gamedata::SpellTables tables = spell_tables();
        change(tables);
        gamedata::SpellData other;
        check(!other.set(std::move(tables)) && !other.loaded() && !other.error().empty(), std::string("refused: ") + what);
    };
    refused("the null asset id", [](gamedata::SpellTables& tables) { tables.slices[0].asset = {}; });
    refused("a Spell slot that is not of the type 2912", [](gamedata::SpellTables& tables) { tables.slices[0].spellSlot = 2; });
    refused("an event slot that is not a DelayedEvent slot", [](gamedata::SpellTables& tables) { tables.slices[0].events[0].slot = 3; });
    refused("two components with one slot", [](gamedata::SpellTables& tables) { tables.slices[0].slots.push_back({4, "DelayedEvent", 3139}); });
    refused("a delay that is not a number", [](gamedata::SpellTables& tables) { tables.slices[0].events[0].delay = std::nanf(""); });
    refused("two spells with one id", [](gamedata::SpellTables& tables) { tables.spells.push_back({"SPELL_GREATAXE_GRAVITYWELL_VORTEX", 0}); });
    refused("a spell with no slice", [](gamedata::SpellTables& tables) { tables.spells[0].second = 5; });
    {
        gamedata::SpellTables tables = spell_tables();
        tables.slices[0].events[0].delay = -6.0F;
        tables.slices[0].events[1].delay = 0.0001F;
        gamedata::SpellData game;
        check(game.set(std::move(tables)), "the delays of the game -6 s and 0.0001 s are accepted (the world takes them as the start)");
    }
    // The file of the tool.
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "sp1-spell-data.json";
    nlohmann::json entry{{"path", "slices/items/weapons/test_vortex.dynamicslice"},
                         {"asset", "5150E110-0000-4000-8000-000000000001"},
                         {"sub", 2},
                         {"root", "0000025ACFDAA308"},
                         {"interestRadius", 50.0},
                         {"spellSlot", 1},
                         {"slots", nlohmann::json::array({nlohmann::json::array({1, "Spell", 2912}), nlohmann::json::array({9, "DelayedEvent", 3139})})},
                         {"events", nlohmann::json::array({nlohmann::json::array({9, 3.5, 0, "Destroy", "", true})})},
                         {"components", nlohmann::json::array({nlohmann::json::array({"0000025ACFDAA308", "DF4D13D4C4E6DB89", "Vortex/Spell"})})}};
    nlohmann::json document{{"format", 1},
                            {"counts", {{"spells", 1}, {"slices", 1}, {"spellsWithNoSlice", 0}, {"slicesWithNoSpellComponent", 0}}},
                            {"slices", nlohmann::json::array({entry})},
                            {"spells", nlohmann::json::array({nlohmann::json::array({kVortex, 0})})}};
    std::ofstream(path) << document.dump();
    gamedata::SpellData file;
    const bool loaded = file.load(path.string());
    if (!loaded) {
        note("the file: " + file.error());
    }
    check(loaded && file.slice_of(kVortex) != nullptr && file.slice_of(kVortex)->asset == kVortexAsset
              && file.slice_of(kVortex)->components.size() == 1,
          "the file of the tool loads: the asset id, the slots, the events, and the components");
    document["counts"]["spells"] = 2;
    std::ofstream(path) << document.dump();
    gamedata::SpellData wrong;
    check(!wrong.load(path.string()), "a file whose count is not the size of its list is refused");
    gamedata::SpellData none;
    check(!none.load((options.workDirectory / "sp1-no-such-file.json").string()) && !none.loaded() && none.error() == "the file is not there",
          "no file: no data, and the cause is in the error text");
}

/** SP2 (S2a-I2): the spell entity: its id, its caster, its place, its row, its times, its record, and its stop at its end. */
void entity_and_record(const Options&) {
    SpellShop shop;
    world::Entity* wolf = shop.wolf(-5.0F);
    const state::WorldPosition landing = wolf->position;
    const std::size_t from = shop.a.got.size();
    const world::Entity* spell = shop.cast_well();
    check(spell != nullptr && shop.spells().size() == 1, "a cast of Gravity Well makes one spell entity");
    if (spell == nullptr) {
        return;
    }
    const world::EntityId id = spell->id;
    const world::ActorId actor = spell->actor;
    check(spell->kind == world::EntityKind::spell && (actor.bytes[0] & 1U) == 0 && id == world::entity_group_id(actor)
              && id != shop.character()->id && id != wolf->id,
          "the entity has the kind spell and its own actor id of the id allocator (an even first byte; the group id is its key)");
    check(spell->spell.caster == shop.character()->id && spell->spell.casterActor == shop.character()->actor
              && spell->spell.casterCharacter == kCharacterA && spell->spell.spellId == kVortex,
          "its caster is the entity of A, and its row is the vortex of the data");
    check(near(spell->position.x, landing.x) && near(spell->position.y, landing.y),
          "its place is the landing of the projectile (the wolf on the line)");
    check(spell->spell.ends - spell->spell.started == milliseconds(3500), "its times on the world clock: the end 3.5 s after its start (the data)");
    shop.walker.idle(2);
    const std::vector<Fragment> metadata = fragments_of(shop.a, 0, server::rep::kTypeGdeMetadataState, from);
    const auto spellRecord = std::find_if(metadata.begin(), metadata.end(), [&](const Fragment& fragment) {
        return fragment.fields.contains({0, 1}) && std::equal(actor.bytes.begin(), actor.bytes.end(), fragment.fields.at({0, 1}).begin());
    });
    check(spellRecord != metadata.end() && std::equal(kVortexAsset.begin(), kVortexAsset.end(), spellRecord->fields.at({0, 0}).begin()),
          "the session of the caster gets the record: the metadata state with the asset of the slice and the actor of the spell");
    const std::vector<Fragment> states = fragments_of(shop.a, 1, server::rep::kTypeSpellState, from);
    check(states.size() == 1 && states[0].fields.size() == 11, "one Spell state (slot 1 of the slice) with the eleven fields");
    if (states.size() == 1 && states[0].fields.size() == 11) {
        const world::Entity& own = *shop.character();
        Bytes caster(own.actor.bytes.begin(), own.actor.bytes.end());
        for (int shift = 56; shift >= 0; shift -= 8) {
            caster.push_back(static_cast<std::uint8_t>(own.id >> shift));
        }
        check(hex(states[0].fields.at({0, 0})) == "b5715a91" && states[0].fields.at({0, 7}) == caster
                  && hex(states[0].fields.at({0, 2})) == "07" && hex(states[0].fields.at({0, 5})).substr(32) == "00000000ffffffff"
                  && hex(states[0].fields.at({0, 9})) == "00000000",
              "the key B5715A91 (CRC-32 of the lower-case id), the caster reference (actor and group of A), the identity rotation, no "
              "attachment, the count 0");
    }
    const auto [rootLeft, rootDone] = delayed_of(shop.a, 8, from);
    const auto [destroyLeft, destroyDone] = delayed_of(shop.a, 9, from);
    check(fragments_of(shop.a, 4, server::rep::kTypeDelayedEventState, from).size() == 1 && rootDone == 0 && rootLeft > 0.8F && rootLeft <= 1.0F
              && destroyDone == 0 && destroyLeft > 3.3F && destroyLeft <= 3.5F,
          "one delayed event state for each event of the slice, with the time that is left (the root 1 s, the destroy 3.5 s)");
    std::uint32_t interest = 0;
    for (const std::uint32_t number : interests_with(shop.a, server::rep::kTypeSpellState, from)) {
        interest = number;
    }
    const std::size_t before = shop.a.got.size();
    shop.pass(3.6F);
    check(shop.spells().empty() && shop.world().find(id) == nullptr, "the entity goes away at its end");
    const std::vector<std::uint32_t> stops = stops_of(shop.a, before);
    check(interest != 0 && std::count(stops.begin(), stops.end(), interest) == 1, "the session gets the stop of the interest number of the spell");
}

/** SP3 (S2a-I4): the hits follow the entity: the vortex effects at its start, the burst at the event of the data, one time each. */
void hits_follow_entity(const Options&) {
    for (const float burst : {3.0F, 2.0F}) {
        SpellShop shop(true, false, burst);
        world::Entity* wolf = shop.wolf(-5.0F);
        world::Entity* second = shop.wolf(-6.0F, 1.0F);
        const float full = wolf->vitals.health;
        const world::Entity* spell = shop.cast_well();
        check(spell != nullptr, "setup: the spell");
        if (spell == nullptr) {
            return;
        }
        const auto started = spell->spell.started;
        check(has_status(*wolf, "GreatAxe_Vortex_Root") && has_status(*wolf, "GreatAxe_Vortex") && has_status(*second, "GreatAxe_Vortex"),
              "the vortex phase at the start of the entity: the root and the slow on each wolf in its radius");
        while (shop.host.clock.now() - started < milliseconds(static_cast<long long>(burst * 1000.0F)) - milliseconds(100)) {
            shop.walker.idle(1);
        }
        check(wolf->vitals.health == full, std::string("no burst before the time of the data (") + std::to_string(burst) + " s)");
        shop.pass(0.5F);
        const float once = full - wolf->vitals.health;
        check(once > 0.0F && near(full - second->vitals.health, once), "the burst at the event of the data hurts each wolf of the vortex");
        shop.pass(4.0F);
        check(near(full - wolf->vitals.health, once), "one burst for each wolf: no second hit (no area timer of stage 3.3a)");
        check(shop.world().spells().counts().phases == 2 && shop.world().abilities().counts().vortexes == 1,
              "the spell ran its two phases one time");
    }
}

/** SP4 (S2a-I4): a cancel: the leave of the caster ends the spell (no burst, the stop); the death follows EndOnCasterDeath of the row. */
void cancel(const Options&) {
    {
        SpellShop shop;
        shop.enter_b_at(at(kPlace.x - 2.0F, kPlace.y + 2.0F));
        world::Entity* wolf = shop.wolf(-5.0F);
        const float full = wolf->vitals.health;
        const world::Entity* spell = shop.cast_well();
        check(spell != nullptr, "setup: the spell");
        if (spell == nullptr) {
            return;
        }
        const world::EntityId id = spell->id;
        shop.b_steps(5, at(kPlace.x - 2.0F, kPlace.y + 2.0F));
        const std::vector<std::uint32_t> seen = interests_with(shop.b, server::rep::kTypeSpellState);
        const std::size_t before = shop.b.got.size();
        check(shop.world().leave(shop.character()->owner), "setup: the caster leaves the world (its session ends)");
        shop.b_steps(5, at(kPlace.x - 2.0F, kPlace.y + 2.0F));
        check(shop.world().find(id) == nullptr && shop.world().spells().counts().ended[static_cast<std::size_t>(world::SpellEnd::casterLeft)] == 1,
              "the leave of the caster ends its spell (a cancel)");
        const std::vector<std::uint32_t> stops = stops_of(shop.b, before);
        check(!seen.empty() && std::count(stops.begin(), stops.end(), seen.front()) == 1, "the observer B gets the stop of the spell");
        shop.b_steps(120, at(kPlace.x - 2.0F, kPlace.y + 2.0F));
        check(wolf->vitals.health == full, "no burst after the cancel");
    }
    for (const bool endOnDeath : {false, true}) {
        SpellShop shop(true, endOnDeath);
        world::Entity* wolf = shop.wolf(-5.0F);
        const float full = wolf->vitals.health;
        const world::Entity* spell = shop.cast_well();
        check(spell != nullptr, "setup: the spell");
        if (spell == nullptr) {
            return;
        }
        const world::EntityId id = spell->id;
        shop.world().health().hurt(kCharacterA, 1.0e7F);
        check(shop.character()->life.dead, "setup: the caster dies");
        shop.pass(3.6F);
        if (endOnDeath) {
            check(shop.world().spells().counts().ended[static_cast<std::size_t>(world::SpellEnd::casterDied)] == 1 && wolf->vitals.health == full,
                  "a spell row with EndOnCasterDeath ends at the death of the caster: no burst");
        } else {
            check(shop.world().find(id) == nullptr && shop.world().spells().counts().ended[static_cast<std::size_t>(world::SpellEnd::time)] == 1
                      && wolf->vitals.health < full,
                  "the vortex row has no EndOnCasterDeath (game data): it lives to its end after the death, and its burst hits");
        }
    }
}

/** SP5 (S2a-I5): a session that comes later gets the age; a session that leaves and comes back gets no second entity and no second burst. */
void late_observer(const Options&) {
    SpellShop shop;
    const state::WorldPosition far = at(kPlace.x + 90.0F, kPlace.y);
    const state::WorldPosition near_ = at(kPlace.x - 3.0F, kPlace.y + 3.0F);
    shop.enter_b_at(far);
    world::Entity* wolf = shop.wolf(-5.0F);
    const float full = wolf->vitals.health;
    const world::Entity* spell = shop.cast_well();
    check(spell != nullptr, "setup: the spell");
    if (spell == nullptr) {
        return;
    }
    const world::EntityId id = spell->id;
    const auto started = spell->spell.started;
    shop.b_steps(3, far);
    check(interests_with(shop.b, server::rep::kTypeSpellState).empty(), "B at 90 m does not observe the spell (the radius of the slice: 50 m)");
    while (shop.host.clock.now() - started < milliseconds(2500)) {
        shop.b_steps(1, far);
    }
    const std::size_t from = shop.b.got.size();
    check(shop.world().travel(kCharacterB, near_, world::TravelScreen::off).error == world::TravelError::none, "setup: B travels near the spell");
    shop.b_steps(3, near_);
    const auto [burstLeft, burstDone] = delayed_of(shop.b, 7, from);
    const auto [rootLeft, rootDone] = delayed_of(shop.b, 8, from);
    const auto [destroyLeft, destroyDone] = delayed_of(shop.b, 9, from);
    note("B at the age 2.6 s: burst " + std::to_string(burstLeft) + " s left, root completed " + std::to_string(rootDone) + ", destroy "
         + std::to_string(destroyLeft) + " s left");
    check(burstDone == 0 && burstLeft > 0.3F && burstLeft < 0.5F && rootDone == 1 && destroyDone == 0 && destroyLeft > 0.8F && destroyLeft < 1.0F,
          "the record of the late observer has the age of the spell: the burst in 0.4 s, the root completed, the end in 0.9 s");
    // The burst comes; B goes away and comes back before the end: a new record, no second entity, no second burst.
    while (shop.host.clock.now() - started < milliseconds(3100)) {
        shop.b_steps(1, near_);
    }
    const float once = full - wolf->vitals.health;
    check(once > 0.0F, "setup: the burst hit the wolf");
    const std::vector<std::uint32_t> first = interests_with(shop.b, server::rep::kTypeSpellState, from);
    const std::size_t away = shop.b.got.size();
    check(shop.world().travel(kCharacterB, far, world::TravelScreen::off).error == world::TravelError::none, "setup: B travels away");
    shop.b_steps(2, far);
    const std::vector<std::uint32_t> stops = stops_of(shop.b, away);
    check(!first.empty() && std::count(stops.begin(), stops.end(), first.front()) == 1, "B leaves the interest: the stop");
    const std::size_t back = shop.b.got.size();
    check(shop.world().travel(kCharacterB, near_, world::TravelScreen::off).error == world::TravelError::none, "setup: B comes back");
    shop.b_steps(2, near_);
    const auto [heldLeft, heldDone] = delayed_of(shop.b, 7, back);
    check(interests_with(shop.b, server::rep::kTypeSpellState, back).size() == 1 && shop.spells().size() == 1 && shop.spells().front()->id == id,
          "the new record is the same spell entity (no second entity)");
    check(heldDone == 0 && heldLeft > 1.0F, "the burst that passed is held (not completed): the client does not play it again (live check L4)");
    shop.b_steps(30, near_);
    check(near(full - wolf->vitals.health, once) && shop.world().spells().counts().phases == 2, "no second burst for the new record");
}

/** SP6 (S2a-I6, MP-10): two sessions observe the same vortex, each with its own interest number; only the caster gets the damage numbers. */
void two_sessions(const Options&) {
    SpellShop shop;
    const state::WorldPosition placeB = at(kPlace.x - 40.0F, kPlace.y);
    shop.enter_b_at(placeB);
    // B gets an interest number for a wolf that A does not observe (75 m from A, 35 m from B; the radius of the wolf: 60 m):
    // the numbers of A and of B differ.
    shop.wolf(-75.0F);
    shop.b_steps(5, placeB);
    world::Entity* wolf = shop.wolf(-5.0F);
    shop.b_steps(5, placeB);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    const world::Entity* spell = shop.cast_well();
    check(spell != nullptr, "setup: the spell");
    if (spell == nullptr) {
        return;
    }
    shop.b_steps(5, placeB);
    const std::vector<std::uint32_t> numberA = interests_with(shop.a, server::rep::kTypeSpellState, fromA);
    const std::vector<std::uint32_t> numberB = interests_with(shop.b, server::rep::kTypeSpellState, fromB);
    note("the interest number of the spell: A " + (numberA.empty() ? std::string("none") : std::to_string(numberA.front())) + ", B "
         + (numberB.empty() ? std::string("none") : std::to_string(numberB.front())));
    check(numberA.size() == 1 && numberB.size() == 1, "each session gets one record of the spell");
    const std::vector<Fragment> stateA = fragments_of(shop.a, 1, server::rep::kTypeSpellState, fromA);
    const std::vector<Fragment> stateB = fragments_of(shop.b, 1, server::rep::kTypeSpellState, fromB);
    check(!stateA.empty() && !stateB.empty() && stateA.front().fields.at({0, 0}) == stateB.front().fields.at({0, 0})
              && stateA.front().fields.at({0, 7}) == stateB.front().fields.at({0, 7}),
          "the two sessions get the same Spell state (the observer view: the row and the caster)");
    check(numberA.size() == 1 && numberB.size() == 1 && numberA.front() != numberB.front(),
          "each session has its own interest number for the spell");
    const std::size_t numbersA = damage_entries(shop.a, fromA).size();
    const std::size_t numbersB = damage_entries(shop.b, fromB).size();
    shop.b_steps(120, placeB);
    check(damage_entries(shop.a, fromA).size() > numbersA && damage_entries(shop.b, fromB).size() == numbersB && wolf->vitals.health < wolf->vitals.healthMax,
          "the burst gives the damage number to the caster A only");
}

/** SP7 (S2a-I7): many spells far away and a few near: the work depends on the spells near the players and on the due times. */
void load(const Options&) {
    SpellShop shop;
    // A character with no session far away is the caster of the far spells.
    const auto beta = add_player(shop.host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", at(kPlace.x + 4000.0F, kPlace.y));
    check(shop.world().spawn_character(kCharacterB, at(kPlace.x + 4000.0F, kPlace.y)) == world::SpawnError::none, "setup: a far caster");
    const world::EntityId far = shop.world().find_character(kCharacterB)->id;
    const world::EntityId own = shop.character()->id;
    const auto request = [](world::EntityId caster, const state::WorldPosition& place) {
        world::SpellRequest spell;
        spell.spellId = kVortex;
        spell.caster = caster;
        spell.position = place;
        spell.fallbackEnd = milliseconds(3500);
        return spell;
    };
    constexpr int kFar = 5000;
    constexpr int kNear = 10;
    const auto made = std::chrono::steady_clock::now();
    for (int index = 0; index < kFar; ++index) {
        (void)shop.world().spells().make(request(far, at(kPlace.x + 2000.0F + static_cast<float>(index % 100) * 20.0F,
                                                         kPlace.y + static_cast<float>(index / 100) * 20.0F)));
    }
    for (int index = 0; index < kNear; ++index) {
        (void)shop.world().spells().make(request(own, at(kPlace.x + static_cast<float>(index), kPlace.y + 2.0F)));
    }
    const double makeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - made).count();
    const auto start = std::chrono::steady_clock::now();
    shop.pass(3.0F);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::size_t observed = 0;
    for (const auto& [id, known] : shop.host.server->session(1)->observer().known()) {
        const world::Entity* entity = shop.world().find(id);
        observed += entity != nullptr && entity->kind == world::EntityKind::spell ? 1 : 0;
    }
    note(std::to_string(kFar) + " spells 2 to 4 km away and " + std::to_string(kNear) + " near: " + std::to_string(makeSeconds)
         + " s to make them, 90 ticks of the world with the session in " + std::to_string(seconds) + " s, " + std::to_string(observed)
         + " spells in the interest table, " + std::to_string(shop.world().spells().counts().dueTaken) + " due entries taken before the end");
    check(observed == static_cast<std::size_t>(kNear), "the session observes only the spells near its character");
    check(seconds < 1.0 && shop.world().spells().counts().dueTaken == 0,
          "90 ticks of the world before the end of the spells: no due entry, and less than 1 s (the far spells cost nothing)");
    const auto end = std::chrono::steady_clock::now();
    shop.pass(1.0F);
    const double endSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - end).count();
    note("the ends of " + std::to_string(kFar + kNear) + " spells (their due entries): " + std::to_string(endSeconds) + " s");
    check(shop.world().spells().count() == 0 && endSeconds < 2.0, "each spell ends at its time; the ends of 5,010 spells in less than 2 s");
    (void)beta;
}

/** SP8 (S2a-I3): with no spell data the backend works: the vortex has its hits on the times of the ability rule and no record. */
void no_data(const Options&) {
    SpellShop shop(false);
    world::Entity* wolf = shop.wolf(-5.0F);
    const float full = wolf->vitals.health;
    const std::size_t from = shop.a.got.size();
    const world::Entity* spell = shop.cast_well();
    check(spell != nullptr && spell->spell.slice == nullptr, "the spell entity is in the world with no slice");
    check(has_status(*wolf, "GreatAxe_Vortex_Root"), "the vortex phase");
    shop.pass(3.3F);
    check(wolf->vitals.health < full, "the burst at the time of the ability rule (3 s)");
    check(fragments_of(shop.a, 1, server::rep::kTypeSpellState, from).empty() && interests_with(shop.a, server::rep::kTypeSpellState, from).empty(),
          "no session gets a record of a spell with no slice");
    shop.pass(1.0F);
    check(shop.spells().empty(), "the entity goes away after the last phase");
}

/** SP9: the commands cast_ability (a character with no client) and end_spells (the stop). */
void commands(const Options&) {
    SpellShop shop;
    const auto beta = add_player(shop.host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", at(kPlace.x - 2.0F, kPlace.y));
    check(shop.world().spawn_character(kCharacterB, at(kPlace.x - 2.0F, kPlace.y)) == world::SpawnError::none, "setup: B with no client");
    world::Entity& remote = *shop.world().find_character(kCharacterB);
    remote.equipment.slots[world::paperdoll::kMainHand1].item = shop.character()->equipment.slots[world::paperdoll::kMainHand1].item;
    SpellShop::give_well(remote);
    const std::size_t from = shop.a.got.size();
    world::AbilityPress press;
    press.place = 2;
    press.yaw = 1.5708F;
    check(shop.world().abilities().press_character(kCharacterB, press) == world::CastVerdict::accepted, "a press for a character with no client");
    for (int step = 0; step < 60 && shop.spells().empty(); ++step) {
        shop.walker.idle(1);
    }
    shop.walker.idle(2);
    check(shop.spells().size() == 1 && shop.spells().front()->spell.caster == remote.id, "the server throws the projectile: the spell of B");
    check(interests_with(shop.a, server::rep::kTypeSpellState, from).size() == 1, "A observes the spell of B (the record of a spell of a different caster)");
    const std::size_t before = shop.a.got.size();
    check(shop.world().spells().end_caster(remote.id, world::SpellEnd::command) == 1, "end_spells: the spell of B ends");
    shop.walker.idle(2);
    check(!stops_of(shop.a, before).empty() && shop.spells().empty(), "A gets the stop");
    (void)beta;
}

/** SV1 (S2a-I10): the speed table of slot 43: no fragment at 1, the multipliers of a haste, the empty table at its end, owner only. */
void speed_table(const Options&) {
    SpellShop shop;
    shop.enter_b_at(at(kPlace.x - 1.0F, kPlace.y + 1.0F));
    check(fragments_of(shop.a, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState).empty(),
          "the first record of a character with no speed status has no speed table (the stored bytes of T2 do not change)");
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    world::StatusApply how;
    how.source = {world::StatusSourceKind::command, "test", 0, shop.character()->id};
    check(shop.world().statuses().apply(*shop.character(), "Review_Haste40", how) == world::StatusVerdict::applied, "setup: a haste 0.4 for 30 s");
    shop.b_steps(3, at(kPlace.x - 1.0F, kPlace.y + 1.0F));
    const std::vector<Fragment> tables = fragments_of(shop.a, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromA);
    check(tables.size() == 1 && tables.front().fields.contains({1, 0}), "the owner gets the speed table (group 1 field 0)");
    if (!tables.empty() && tables.front().fields.contains({1, 0})) {
        const std::string table = hex(tables.front().fields.at({1, 0}));
        note("the table: " + table);
        check(table.substr(0, 4) == "0001" && table.substr(table.size() - 26) == "02340000" "36b000350000" "36b000",
              "a full table with a sequence: the keys 0x34 and 0x35 at 14000 (1.4 times 10000), m_syncVitals 0");
    }
    check(fragments_of(shop.b, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromB).empty(),
          "the observer B gets no speed table of A");
    const std::size_t end = shop.a.got.size();
    shop.world().statuses().remove(*shop.character(), [](const world::ActiveStatus& entry) { return entry.status == "Review_Haste40"; },
                                   world::StatusEnd::command);
    shop.walker.idle(3);
    const std::vector<Fragment> empty = fragments_of(shop.a, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, end);
    check(empty.size() == 1 && hex(empty.front().fields.at({1, 0})).substr(0, 4) == "0001" && hex(empty.front().fields.at({1, 0})).substr(
                                   hex(empty.front().fields.at({1, 0})).size() - 2) == "00",
          "the end of the haste: the empty table with a newer sequence (the client has 1.0 again: live check L2)");
}

/** SV2 (S2a-I10, F1): the movement check accepts a faster run only while the client has the multiplier of its table. */
void speed_check(const Options&) {
    const auto run = [](SpellShop& shop, world::PositionTrack& track, std::uint16_t first) {
        unsigned refused = 0;
        const state::WorldPosition start = shop.character()->position;
        for (unsigned index = 0; index <= 60; ++index) {
            const world::PositionReport report{static_cast<std::uint16_t>(first + index), at(start.x + 11.0F * index / 30.0F, start.y),
                                               wire_height(kHeight)};
            if (shop.world().report_position(shop.character()->owner, report, track) != world::PositionVerdict::accepted) {
                ++refused;
            }
            shop.host.clock.advance(std::chrono::microseconds(33333));
        }
        return refused;
    };
    SpellShop shop;
    world::StatusApply how;
    how.source = {world::StatusSourceKind::command, "test", 0, shop.character()->id};
    check(shop.world().statuses().apply(*shop.character(), "Review_Haste40", how) == world::StatusVerdict::applied, "setup: a haste 0.4");
    world::PositionTrack unsent;
    const unsigned before = run(shop, unsent, 5000);
    world::PositionTrack sent;
    sent.set_client_speed(1.4F, shop.host.clock.now());
    const unsigned after = run(shop, sent, 6000);
    note("11 m/s for 2 s with the haste: refusals with no table sent " + std::to_string(before) + ", with the table 1.4 sent " + std::to_string(after));
    check(before > 0, "with no table that the client has, 11 m/s is refused (finding F1)");
    check(after == 0, "with the table 1.4 that the client has, 11 m/s is accepted");
    sent.set_client_speed(1.0F, shop.host.clock.now());
    check(near(sent.client_speed(shop.host.clock.now()), 1.4F), "a lower value counts only one window after its time (the client gets the table later)");
    shop.host.clock.advance(std::chrono::milliseconds(1100));
    check(near(sent.client_speed(shop.host.clock.now()), 1.0F), "after the window the check has the new value");
    // The session sets the value when the table goes out (the real path of a session).
    shop.walker.idle(3);
    check(near(shop.host.server->session(1)->position_track().client_speed(shop.host.clock.now()), 1.4F),
          "the session of A gives the check the multiplier of the table that it sent");
}

/** SV3 (S2a-I10, MP-10): the haste of A changes the table and the check of A only. */
void speed_two_sessions(const Options&) {
    SpellShop shop;
    shop.enter_b_at(at(kPlace.x - 1.0F, kPlace.y + 1.0F));
    const std::size_t fromB = shop.b.got.size();
    world::StatusApply how;
    how.source = {world::StatusSourceKind::command, "test", 0, shop.character()->id};
    (void)shop.world().statuses().apply(*shop.character(), "Review_Haste40", how);
    shop.b_steps(5, at(kPlace.x - 1.0F, kPlace.y + 1.0F));
    float speedA = 0.0F;
    float speedB = 0.0F;
    for (const std::uint32_t id : {1U, 2U}) {
        const server::rep::RepSession* session = shop.host.server->session(id);
        if (session != nullptr) {
            (session->character_id() == kCharacterA ? speedA : speedB) = session->position_track().client_speed(shop.host.clock.now());
        }
    }
    check(near(speedA, 1.4F) && near(speedB, 1.0F), "the check of A has 1.4, the check of B stays 1.0");
    check(near(shop.character()->sprintMultiplier, 1.4F) && near(shop.world().find_character(kCharacterB)->sprintMultiplier, 1.0F)
              && fragments_of(shop.b, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromB).empty(),
          "the multipliers are values of the character of A; B gets no table");
}

} // namespace

std::vector<Case> spell_cases() {
    return {{"SP1", "spell entities: the spell data (spell-data.json), its lookups and refusals, a file, no file", data_and_loader},
            {"SP2", "spell entities: the entity of the vortex (id, caster, place, row, times), its record, its stop at its end", entity_and_record},
            {"SP3", "spell entities: the vortex and the burst follow the entity, at the times of the data, one time each", hits_follow_entity},
            {"SP4", "spell entities: a cancel at the leave of the caster; the death and EndOnCasterDeath", cancel},
            {"SP5", "spell entities: a later observer gets the age; a leave and a return give no second entity and no second burst", late_observer},
            {"SP6", "spell entities: two sessions observe one vortex with their own interest numbers (MP-10)", two_sessions},
            {"SP7", "spell entities: the load of 5,000 far spells and 10 near ones, with a time limit", load},
            {"SP8", "spell entities: no spell data: the hits stay, no record", no_data},
            {"SP9", "spell entities: a cast for a character with no client, and the end of its spells", commands},
            {"SV1", "speed table (S2a-I10): slot 43 with the multipliers of a haste, the empty table at its end, owner only", speed_table},
            {"SV2", "speed table (S2a-I10, F1): the movement check takes the multiplier that the client has", speed_check},
            {"SV3", "speed table (S2a-I10): two sessions, the haste of A changes only A (MP-10)", speed_two_sessions}};
}

} // namespace soulwarden::backend::tests
