// Cases of stage 2.5b: the server children (the stations of a settlement, the camps, the fixtures), the timed use, the ends of a use,
// and the service handoff (world/interactions.h, gamedata/interaction_data.h, server/rep/states/object_states.cpp). The data has the
// form of the file of tools/gamedata/interactions.py (format 2): a station template with an Assembly and a forge child, a forge
// template, the keg template with the timed option Trigger (5 s), a camp template, a station template with no Assembly, and a station
// child of a made-up settlement 2 m north of the characters. All keys, ids, and positions are made up.
//
//   SC1  I8: the object entity kind: an actor of the allocator, the position index, the slice descriptor (the slots), the metadata
//        codecs of a player, a creature, and an object; the record of a station (metadata, place with its rotation, Complete, Interact)
//        and of its forge; the stop and the re-entry of interest; the spawn rule (150 m, 200 m) and a new child after a removal
//   SC2  I9, I10, I11, IT-STATION: the request with the ids of the child (K = root XOR G), the StationContext (the Assembly key, the fee
//        flag, the Assembly reference, the parent settlement), a key with no row, a station with no Assembly, a camp, the forge (no bound)
//   SC3  I12, IT-TIME: the timed use on the world clock: no completion before 5 s, one completion after it, none after a release, a
//        2473, or a repeat of the block; a new use after the release passes the checks again and completes; a disabled source at the end
//   SC4  I13, IT-END: the server cancel, the removal of the source, the loss of the range, damage (a timed use and an animation end;
//        a station does not), a travel and a death in a timed use, a conversation that the server ends (6705 still gets 6709)
//   SC5  I14, IT-SERVICE: two sessions: the grant stays after the 2473 of the conversation; the offer of each character; the range, a
//        new source, the removal of the source, the death, the leave, and the deadline end it
//   SC6  IT-MP: two sessions: an unlimited station (users 2, 1, 0 for each observer), the owner replies, the Interactor state of the
//        observer, a keg of one place (the second character: full), no leak of a station context
//   SC7  IT-LOAD: far server children do not change the work of the spawn rule near a character; the time of the scans and of 150 timed
//        uses; no work with no character
//   SC8  I9: the loader of format 2: a complete file, each refusal (a cycle of children, a child of itself, a fixture with no bound, a
//        child with no template, a rotation that is not a unit, no PositionInTheWorld slot, a count), format 1
//   SC9  the development commands: a fixture, the list with the server child, the removal

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/lock_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/interaction_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;

/** The option rows of stage 2.5b after the rows of the fixture of 2.5a (kReadLore to kPet). */
enum StationOption : std::uint32_t { kCraft = 6, kTrigger = 7, kBind = 8 };
/** The templates of the cases. */
enum TemplateIndex : std::uint32_t { kStationTemplate, kForgeTemplate, kKegTemplate, kCampTemplate, kBareTemplate, kNoRowTemplate };

constexpr std::uint64_t kStationRoot = 0x3000;
constexpr std::uint64_t kFunctional = 0x3001;
constexpr std::uint64_t kStationBound = 0x3002;
constexpr std::uint64_t kAssemblyComponent = 0x698F02D45D873F1AULL;
constexpr std::uint64_t kStationInteract = 0x6001;
constexpr std::uint64_t kKegRoot = 0x4000;
constexpr std::uint64_t kKegEntity = 0x4001;
constexpr std::uint64_t kKegInteract = 0x7000;
constexpr const char* kSettlementKey = "5e771e00-0000-4000-8000-000000000001";
const std::string kStationKey = std::string(kSettlementKey) + "/0000000000002001";
const std::string kFarStationKey = std::string(kSettlementKey) + "/0000000000002002";
constexpr std::array<std::uint8_t, 16> kSettlementActor{0x0B, 0x5E, 0x77, 0x1E, 'S', 'E', 'T', 'L', 0, 0, 0, 0, 0, 0, 0, 0};
/** The station 2 m north of the place of A, turned by 90 degrees: its bound (1, 0, 0.25 in the slice) is 3 m north of A. */
constexpr state::WorldPosition kStationPlace{100.0F, 202.0F, 30.0F};
const gamedata::Rotation kTurn{0.0F, 0.0F, std::sqrt(0.5F), std::sqrt(0.5F)};
constexpr std::uint32_t kPlaceType = 13;
constexpr std::uint32_t kBuildableType = 1594;
constexpr std::uint32_t kOwnershipType = 3217;

std::array<std::uint8_t, 16> asset_of(std::uint8_t tag) {
    return {0xA5, 0x5E, 0x75, tag, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
}

gamedata::InteractionBound template_bound(std::uint32_t slot, std::uint64_t entity, std::uint64_t component, state::WorldPosition local,
                                          std::vector<gamedata::BoundOption> options, std::int32_t capacity = -1) {
    gamedata::InteractionBound bound;
    bound.slot = slot;
    bound.entity = entity;
    bound.component = component;
    bound.position = local;
    bound.rangeSq = 25.0F;
    bound.capacity = capacity;
    bound.arc = 180.0F;
    bound.options = std::move(options);
    return bound;
}

struct StationParts {
    DataParts base;
    std::vector<gamedata::InteractionTemplate> templates;
    std::vector<gamedata::SpawnedPlacement> spawned;
    std::map<std::string, std::uint32_t> fixtures;
};

StationParts station_parts() {
    StationParts parts;
    parts.base = data_parts();
    parts.base.options.push_back(option_row("OpenCraftingOption", 20, true));
    gamedata::InteractionOption trigger = option_row("Trigger", 33, true);
    trigger.delay = 5.0F;
    trigger.hold = 0.0F;
    parts.base.options.push_back(trigger);
    parts.base.options.push_back(option_row("BindToCampOption", 73, false));
    gamedata::InteractionTemplate station;
    station.slice = "slices/test/station.dynamicslice";
    station.kind = InteractionKind::craftingStation;
    station.asset = asset_of(1);
    station.root = kStationRoot;
    station.positionSlot = 2;
    station.buildableSlot = 1;
    station.buildableState = 5;
    station.assembly = gamedata::InteractionAssembly{"Blacksmith5", true, kFunctional, kAssemblyComponent, true};
    station.bounds = {template_bound(4, kStationBound, kStationInteract, {1.0F, 0.0F, 0.25F}, {{kCraft, kFunctional}})};
    station.children = {gamedata::TemplateChild{kForgeTemplate, {0.0F, 0.0F, 0.0F}, gamedata::kNoRotation}};
    gamedata::InteractionTemplate forge;
    forge.slice = "slices/test/forge.dynamicslice";
    forge.asset = asset_of(2);
    forge.root = 0x5000;
    forge.positionSlot = 1;
    gamedata::InteractionTemplate keg;
    keg.slice = "slices/test/keg.dynamicslice";
    keg.asset = asset_of(3);
    keg.root = kKegRoot;
    keg.positionSlot = 6;
    keg.ownershipSlot = 3;
    keg.permissionRule = 4;
    keg.buildableSlot = 4;
    keg.buildableState = 5;
    keg.bounds = {template_bound(10, kKegEntity, kKegInteract, {0.0F, 0.0F, 0.0F}, {{kTrigger, kKegEntity}}, 1)};
    gamedata::InteractionTemplate camp;
    camp.slice = "slices/test/camp.dynamicslice";
    camp.kind = InteractionKind::camp;
    camp.asset = asset_of(4);
    camp.root = 0x8000;
    camp.positionSlot = 1;
    camp.ownershipSlot = 4;
    camp.permissionRule = 1;
    camp.assembly = gamedata::InteractionAssembly{"Camp5", false, 0x8001, kAssemblyComponent, true};
    camp.bounds = {template_bound(5, 0x8002, 0x8100, {0.0F, -0.8F, 0.1F}, {{kCraft, 0x8001}, {kBind, 0x8003}})};
    gamedata::InteractionTemplate bare = station;
    bare.slice = "slices/test/bare.dynamicslice";
    bare.asset = asset_of(5);
    bare.assembly.reset();
    bare.children.clear();
    gamedata::InteractionTemplate noRow = station;
    noRow.slice = "slices/test/norow.dynamicslice";
    noRow.asset = asset_of(6);
    noRow.assembly = gamedata::InteractionAssembly{"Carpentry2", true, kFunctional, kAssemblyComponent, false};
    noRow.children.clear();
    parts.templates = {station, forge, keg, camp, bare, noRow};
    gamedata::SpawnedPlacement near;
    near.key = kStationKey;
    near.parent = kSettlementKey;
    near.parentActor = kSettlementActor;
    near.parentSlice = "slices/test/settlement.dynamicslice";
    near.spawner = "Public_Blacksmith";
    near.templateIndex = kStationTemplate;
    near.region = "r_+00_+00";
    near.position = kStationPlace;
    near.rotation = kTurn;
    gamedata::SpawnedPlacement far = near;
    far.key = kFarStationKey;
    far.spawner = "Public_Engineering";
    far.position = state::WorldPosition{100.0F, 900.0F, 30.0F};
    parts.spawned = {near, far};
    parts.fixtures = {{"powderKeg", kKegTemplate}, {"camp", kCampTemplate}, {"bare", kBareTemplate}, {"noRow", kNoRowTemplate}};
    return parts;
}

bool set_stations(gamedata::InteractionData& data, StationParts parts = station_parts()) {
    return data.set(std::move(parts.base.options), std::move(parts.base.achievements), std::move(parts.base.objects), std::move(parts.templates),
                    std::move(parts.spawned), std::move(parts.fixtures));
}

/** A host with the data of the stations; A and B near the station (the settlement child is made by the first scans). */
struct Stations : Shop {
    explicit Stations(bool withB = true) : Shop(false, ":memory:", withB) {
        if (!set_stations(host.interactions)) {
            throw std::runtime_error("station test: the data was not set: " + host.interactions.error());
        }
    }
    [[nodiscard]] world::Interactions& interactions() {
        return host.world.interactions();
    }
    [[nodiscard]] world::Entity* station() {
        return host.world.find(interactions().spawned_entity(kStationKey));
    }
    /** A fixture of the data near A (dy m north), with a permission rule (-1: the rule of the data). */
    world::Entity* fixture(const char* name, float dy = 2.0F, int permission = 1) {
        const world::FixtureResult result = interactions().make_fixture(kCharacterA, name, {0.0F, dy, 0.0F}, 0.0F, permission);
        host.steps(60, {&clientA, &clientB});
        return result.entity;
    }
};

/** @return The block of a request of an option of the first bound of a server child, as the client makes it (live check L4). */
InteractionBlock child_block(const gamedata::InteractionData& data, const world::Entity& entity, std::uint32_t option) {
    const gamedata::InteractionTemplate& slice = data.templates()[entity.object.index];
    const std::uint64_t k = slice.root ^ entity.id;
    const gamedata::InteractionBound& bound = slice.bounds.front();
    const gamedata::InteractionOption& row = data.options()[option];
    InteractionBlock block;
    block.actor = entity.actor.bytes;
    block.group = entity.id;
    block.target = (bound.entity ^ k) + bound.component;
    block.action = row.action;
    block.baseAction = row.committed ? row.action : 0;
    for (const gamedata::BoundOption& item : bound.options) {
        if (item.option == option) {
            block.optionEntity = item.entity ^ k;
        }
    }
    block.committed = row.committed;
    block.optionId = row.id;
    block.text88 = "@Test_Station";
    return block;
}

/** The 2473 of a client with the reference of a block. */
void send_cancel(Shop& shop, FakeClient& client, const char* character, const InteractionBlock& block) {
    world::Entity* entity = shop.entity(character);
    server::hub::Writer cancel;
    cancel.u64(entity->id + server::rep::kInteractorComponent);
    cancel.u64(entity->id);
    cancel.raw(block.actor);
    cancel.u64(block.group);
    cancel.u64(block.target);
    shop.host.datagram(client.peer, client.component_message(kCancelMessage, entity->actor.bytes, cancel.data()));
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
}

/** @return The interest number of the record whose metadata names this actor (0: none), from a message index on. */
std::uint32_t interest_of(const FakeClient& client, const std::array<std::uint8_t, 16>& actor, std::size_t from = 0) {
    std::uint32_t interest = 0;
    for (const Seen& seen : fragments_of(client, 0, 10, from)) {
        const auto ref = field_of(seen.fragment, 0, 1);
        if (ref.size() == 16 && std::equal(ref.begin(), ref.end(), actor.begin())) {
            interest = seen.interest;
        }
    }
    return interest;
}

std::vector<Fragment> fragments_for(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest == interest) {
                found.insert(found.end(), record.fragments.begin(), record.fragments.end());
            }
        }
    }
    return found;
}

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

/** @return The (enabled, users) states of slot 4 (the station bound) for an interest. */
std::vector<std::pair<bool, std::uint32_t>> station_states(const FakeClient& client, std::uint32_t interest, std::uint32_t slot = 4,
                                                           std::size_t from = 0) {
    std::vector<std::pair<bool, std::uint32_t>> found;
    for (const Fragment& fragment : fragments_for(client, interest, from)) {
        if (fragment.slot == slot && fragment.type == kInteractType) {
            found.emplace_back(field_of(fragment, 0, 0)[0] != 0, static_cast<std::uint32_t>(u64_of(field_of(fragment, 0, 1))));
        }
    }
    return found;
}

/** The expected place state bytes (an independent writer: x and y as floats, the height over -100 to 2000, the rotation of 0x87B250). */
Bytes expected_place(const state::WorldPosition& place, const gamedata::Rotation& q) {
    Bytes bytes{0x01, 0x03};
    const auto put32 = [&](float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        for (int shift = 24; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
        }
    };
    put32(place.x);
    put32(place.y);
    const auto height = static_cast<std::uint16_t>(std::lround((place.z + 100.0F) / 2100.0F * 65535.0F));
    bytes.push_back(static_cast<std::uint8_t>(height >> 8));
    bytes.push_back(static_cast<std::uint8_t>(height));
    std::uint8_t header = 0;
    Bytes values;
    for (int axis = 0; axis < 3; ++axis) {
        if (q[static_cast<std::size_t>(axis)] == 0.0F) {
            header |= static_cast<std::uint8_t>(1U << axis);
        } else {
            const auto value = static_cast<std::uint16_t>((q[static_cast<std::size_t>(axis)] + 1.0F) * 32767.5F);
            values.push_back(static_cast<std::uint8_t>(value >> 8));
            values.push_back(static_cast<std::uint8_t>(value));
        }
    }
    bytes.push_back(header);
    bytes.insert(bytes.end(), values.begin(), values.end());
    return bytes;
}

/** ST1: the object entity kind (I8). */
void object_entity(const Options&) {
    Stations shop(false);
    shop.enter(false);
    world::Entity* station = shop.station();
    check(station != nullptr && station->kind == world::EntityKind::object && station->object.spawned
              && shop.interactions().spawned_entity(kFarStationKey) == 0 && shop.interactions().instance_count() == 2,
          "the spawn rule made the station 2 m away and its forge child; not the station 700 m away");
    if (station == nullptr) {
        return;
    }
    const std::array<std::uint8_t, 4> soul{'S', 'O', 'U', 'L'};
    check((station->actor.bytes[0] & 1U) == 0 && std::equal(soul.begin(), soul.end(), station->actor.bytes.begin() + 4)
              && station->id == world::entity_group_id(station->actor) && station->object.positionSlot == 2 && station->object.buildableSlot == 1
              && station->object.buildableState == 5 && station->object.ownershipSlot == 0 && station->object.bounds.size() == 1
              && station->object.bounds[0].slot == 4 && station->object.asset == asset_of(1),
          "the actor is an id of the allocator (an even first byte), and the body is the slice descriptor: the asset, the slots, Complete");
    bool indexed = false;
    shop.host.world.for_each_near(kStationPlace, 0.5F, [&](world::Entity& entity) { indexed = indexed || entity.id == station->id; });
    check(indexed, "the station is in the position index of the world at its place");
    // The metadata codec tells a player, a creature, and an object apart.
    gamedata::CreatureSlice slice;
    slice.asset = {0xC0, 0xEA, 0x70, 0x1E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x07};
    slice.subId = 2;
    world::Entity creature;
    creature.kind = world::EntityKind::creature;
    creature.creature.body = &slice;
    creature.actor = world::make_actor_id(0x20000123, world::ActorKind::otherEntity);
    const world::Entity& player = *shop.entity(kCharacterA);
    const Bytes ofPlayer = server::rep::encode_metadata_state(player, server::rep::View::observer, shop.host.clock);
    const Bytes ofCreature = server::rep::encode_metadata_state(creature, server::rep::View::observer, shop.host.clock);
    const Bytes ofObject = server::rep::encode_metadata_state(*station, server::rep::View::observer, shop.host.clock);
    const auto asset_in = [](const Bytes& state, const std::array<std::uint8_t, 16>& asset) {
        return state.size() == 38 && std::equal(asset.begin(), asset.end(), state.begin() + 2);
    };
    const auto actor_in = [](const Bytes& state, const world::ActorId& actor) {
        return state.size() == 38 && std::equal(actor.bytes.begin(), actor.bytes.end(), state.begin() + 22);
    };
    check(asset_in(ofCreature, slice.asset) && asset_in(ofObject, asset_of(1)) && !asset_in(ofPlayer, asset_of(1))
              && actor_in(ofPlayer, player.actor) && actor_in(ofCreature, creature.actor) && actor_in(ofObject, station->actor)
              && ofPlayer[2] == 0xA6 && ofPlayer[3] == 0x60,
          "the metadata state: the player slice for a player, the slice of the creature, the slice of the object; each with its actor");
    // The record of the station: metadata, Complete, the place with its rotation, the Interact state; the forge: metadata and place.
    const std::uint32_t interest = interest_of(shop.clientA, station->actor.bytes);
    std::vector<std::uint32_t> slots;
    Bytes place;
    Bytes complete;
    for (const Fragment& fragment : fragments_for(shop.clientA, interest)) {
        slots.push_back(fragment.slot);
        if (fragment.type == kPlaceType) {
            server::hub::Writer writer;
            writer.raw(field_of(fragment, 0, 0));
            writer.raw(field_of(fragment, 0, 1));
            place = Bytes{0x01, 0x03};
            const Bytes& body = writer.data();
            place.insert(place.end(), body.begin(), body.end());
        }
        if (fragment.type == kBuildableType) {
            complete = Bytes(field_of(fragment, 0, 0).begin(), field_of(fragment, 0, 0).end());
        }
    }
    const world::Entity* forge = shop.host.world.find(station->id + 0);
    std::uint32_t forgeInterest = 0;
    shop.host.world.for_each_near(kStationPlace, 0.5F, [&](world::Entity& entity) {
        if (entity.id != station->id && entity.kind == world::EntityKind::object) {
            forgeInterest = interest_of(shop.clientA, entity.actor.bytes);
        }
    });
    (void)forge;
    std::vector<std::uint32_t> forgeSlots;
    for (const Fragment& fragment : fragments_for(shop.clientA, forgeInterest)) {
        forgeSlots.push_back(fragment.slot);
    }
    check(interest != 0 && slots == std::vector<std::uint32_t>{0, 1, 2, 4} && place == expected_place(kStationPlace, kTurn)
              && complete == Bytes{5} && forgeInterest != 0 && forgeSlots == std::vector<std::uint32_t>{0, 1},
          "the record of the station: metadata, Complete (5), the place with its rotation (independent bytes), Interact; the forge: "
          "metadata and its place");
    // The stop and the re-entry of interest: 150 m south (the session stops at 120 m; the spawn rule keeps the child at 200 m).
    const std::size_t from = shop.clientA.got.size();
    shop.placeA = state::WorldPosition{100.0F, 50.0F, 30.0F};
    (void)shop.host.world.travel(kCharacterA, shop.placeA);
    for (int step = 0; step < 4; ++step) {
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(30, {&shop.clientA});
    }
    const std::vector<std::uint32_t> stops = stops_of(shop.clientA, from);
    check(std::find(stops.begin(), stops.end(), interest) != stops.end() && shop.station() == station && shop.interactions().instance_count() == 2,
          "150 m away: the session stops the station (its interest number), and the station stays in the world");
    const std::size_t back = shop.clientA.got.size();
    shop.placeA = kHere;
    (void)shop.host.world.travel(kCharacterA, shop.placeA);
    for (int step = 0; step < 4; ++step) {
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(30, {&shop.clientA});
    }
    const std::uint32_t again = interest_of(shop.clientA, station->actor.bytes, back);
    std::size_t metadata = 0;
    for (const Fragment& fragment : fragments_for(shop.clientA, again, back)) {
        metadata += fragment.slot == 0 ? 1 : 0;
    }
    check(again != 0 && metadata == 1, "back: a new record of the same station with one metadata state (no second body in one interest)");
    // The spawn rule: 400 m away no character is near: the station and its forge go away; back: a new child with a new entity.
    const world::EntityId old = station->id;
    shop.placeA = state::WorldPosition{100.0F, -250.0F, 30.0F};
    (void)shop.host.world.travel(kCharacterA, shop.placeA);
    for (int step = 0; step < 4; ++step) {
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(30, {&shop.clientA});
    }
    check(shop.station() == nullptr && shop.interactions().instance_count() == 0 && shop.host.world.find(old) == nullptr
              && shop.interactions().counts().removed == 2,
          "400 m away: the spawn rule removes the station and its forge");
    shop.placeA = kHere;
    (void)shop.host.world.travel(kCharacterA, shop.placeA);
    for (int step = 0; step < 4; ++step) {
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(30, {&shop.clientA});
    }
    check(shop.station() != nullptr && shop.station()->id != old && shop.interactions().instance_count() == 2,
          "back: the spawn rule makes the station again, with a new entity of the allocator");
}

/** ST2: the use of a station (I9, I10, I11, IT-STATION). */
void station_use(const Options&) {
    Stations shop;
    shop.enter();
    const gamedata::InteractionData& data = shop.host.interactions;
    world::Entity* station = shop.station();
    check(station != nullptr, "control: the station is in the world");
    if (station == nullptr) {
        return;
    }
    std::size_t from = shop.clientA.got.size();
    const InteractionBlock craft = child_block(data, *station, kCraft);
    shop.request(shop.clientA, craft);
    const std::optional<world::StationContext> context = shop.interactions().station(*shop.entity(kCharacterA));
    const std::uint64_t k = kStationRoot ^ station->id;
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{20U, 1U}} && context.has_value(),
          "the request of the client with the ids of the child (target = (InteractBound XOR root XOR G) + component): 3743 result 1");
    if (context) {
        check(context->assemblyKey == "Blacksmith5" && context->chargeCraftingFee && context->keyResolved
                  && context->assemblyEntity == (kFunctional ^ k) && context->assemblyTarget == (kFunctional ^ k) + kAssemblyComponent
                  && context->interactTarget == craft.target && context->optionEntity == craft.optionEntity && context->group == station->id
                  && context->actor == station->actor.bytes && context->objectKey == kStationKey && context->parentKey == kSettlementKey
                  && context->parentActor == kSettlementActor && !context->isCamp && context->objectGeneration != 0,
              "the StationContext: Blacksmith5, the fee, the Assembly reference (its instance and facet target), the parent settlement");
    }
    shop.end_block(shop.clientA);
    // A wrong target, and the forge (no bound): no object.
    InteractionBlock wrong = craft;
    wrong.target += 1;
    from = shop.clientA.got.size();
    shop.request(shop.clientA, wrong);
    shop.end_block(shop.clientA);
    world::Entity* forge = nullptr;
    shop.host.world.for_each_near(kStationPlace, 0.5F, [&](world::Entity& entity) {
        forge = entity.kind == world::EntityKind::object && entity.id != station->id ? &entity : forge;
    });
    InteractionBlock toForge = craft;
    toForge.actor = forge->actor.bytes;
    toForge.group = forge->id;
    shop.request(shop.clientA, toForge);
    shop.end_block(shop.clientA);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{20U, 2U}, {20U, 2U}} && !shop.entity(kCharacterA)->interaction,
          "a wrong target and a request at the forge (no bound): 3743 result 2");
    // A station with no Assembly: refused; a key with no row: accepted, marked.
    world::Entity* bare = shop.fixture("bare", 2.0F);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, child_block(data, *bare, kCraft));
    shop.end_block(shop.clientA);
    world::Entity* noRow = shop.fixture("noRow", 2.5F);
    shop.request(shop.clientA, child_block(data, *noRow, kCraft));
    const std::optional<world::StationContext> unresolved = shop.interactions().station(*shop.entity(kCharacterA));
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{20U, 2U}, {20U, 1U}} && unresolved
              && unresolved->assemblyKey == "Carpentry2" && !unresolved->keyResolved && unresolved->parentKey.empty(),
          "a station with no Assembly: 3743 result 2; a key with no row of the database: accepted, keyResolved false, no parent");
    shop.end_block(shop.clientA);
    // A camp: the normal action 20 and its key Camp5 with no fee; its record has the Ownership state with the rule 1.
    world::Entity* camp = shop.fixture("camp", 1.5F, -1);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, child_block(data, *camp, kCraft));
    const std::optional<world::StationContext> ofCamp = shop.interactions().station(*shop.entity(kCharacterA));
    Bytes rule;
    for (const Fragment& fragment : fragments_for(shop.clientA, interest_of(shop.clientA, camp->actor.bytes))) {
        if (fragment.slot == 4 && fragment.type == kOwnershipType) {
            rule = Bytes(field_of(fragment, 0, 4).begin(), field_of(fragment, 0, 4).end());
        }
    }
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{20U, 1U}} && ofCamp && ofCamp->isCamp
              && ofCamp->assemblyKey == "Camp5" && !ofCamp->chargeCraftingFee && rule == Bytes{1},
          "a camp: action 20 accepted, Camp5, no fee, a camp; its record has the permission rule 1 of its slice (live check L5)");
    shop.end_block(shop.clientA);
}

/** ST3: the timed use (I12, IT-TIME). */
void timed_use(const Options&) {
    Stations shop(false);
    shop.enter(false);
    const gamedata::InteractionData& data = shop.host.interactions;
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    check(keg != nullptr && keg->object.permissionRule == 1 && keg->object.ownershipSlot == 3, "control: the keg fixture, public (rule 1)");
    const InteractionBlock trigger = child_block(data, *keg, kTrigger);
    std::size_t from = shop.clientA.got.size();
    shop.report(shop.clientA, trigger);
    shop.host.steps(1, {&shop.clientA});
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{33U, 1U}} && shop.interactions().timed_count() == 1,
          "the Trigger request: 3743 result 1, one timed use");
    // The block stays in each report (a hold): no restart of the time.
    for (int step = 0; step < 48; ++step) {
        shop.report(shop.clientA, trigger);
        shop.host.steps(10, {&shop.clientA});
    }
    check(shop.interactions().counts().completions == 0 && shop.interactions().completions_of(keg->id) == 0,
          "4.8 s after the request (the same block in each report): no completion");
    for (int step = 0; step < 4; ++step) {
        shop.report(shop.clientA, trigger);
        shop.host.steps(10, {&shop.clientA});
    }
    check(shop.interactions().completions_of(keg->id) == 1 && shop.interactions().timed_count() == 0
              && shop.entity(kCharacterA)->interaction && shop.entity(kCharacterA)->interaction->completed,
          "5.2 s: one completion; the use stays until the client ends it");
    for (int step = 0; step < 30; ++step) {
        shop.report(shop.clientA, trigger);
        shop.host.steps(10, {&shop.clientA});
    }
    shop.end_block(shop.clientA);
    check(shop.interactions().completions_of(keg->id) == 1 && !shop.entity(kCharacterA)->interaction,
          "the hold goes on and ends (the release): still one completion");
    // A release at 2 s: no completion.
    shop.request(shop.clientA, trigger);
    shop.host.steps(200, {&shop.clientA});
    shop.end_block(shop.clientA);
    shop.host.steps(400, {&shop.clientA});
    check(shop.interactions().completions_of(keg->id) == 1 && shop.interactions().timed_count() == 0,
          "a release 2 s after the request: no completion after 5 s");
    // A 2473 at 1 s: no completion.
    shop.request(shop.clientA, trigger);
    shop.host.steps(100, {&shop.clientA});
    send_cancel(shop, shop.clientA, kCharacterA, trigger);
    shop.host.steps(500, {&shop.clientA});
    shop.end_block(shop.clientA);
    check(shop.interactions().completions_of(keg->id) == 1 && shop.interactions().timed_count() == 0, "a 2473 at 1 s: no completion");
    // A new use after the release passes the checks again, and completes one time.
    from = shop.clientA.got.size();
    shop.request(shop.clientA, trigger);
    shop.host.steps(520, {&shop.clientA});
    shop.end_block(shop.clientA);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{33U, 1U}} && shop.interactions().completions_of(keg->id) == 2,
          "a new press after the release: a new check (3743 result 1) and a second completion");
    // The source is disabled during the use: at the end the check fails, the client gets 3746, no completion.
    shop.request(shop.clientA, trigger);
    shop.host.steps(100, {&shop.clientA});
    from = shop.clientA.got.size();
    check(shop.interactions().set_enabled(shop.interactions().view(kCharacterA, 5.0F).objects.front().object->key, false)
              == world::ObjectError::none,
          "control: the keg is disabled during the use");
    shop.host.steps(450, {&shop.clientA});
    check(shop.interactions().completions_of(keg->id) == 2 && count_type(shop.clientA, kServerCancel, from) == 1
              && !shop.entity(kCharacterA)->interaction,
          "a disabled source at the end of the time: no completion, the use ends, the client gets 3746");
    shop.end_block(shop.clientA);
}

/** ST4: the ends of a use (I13, IT-END). */
void use_ends(const Options&) {
    Stations shop;
    shop.enter();
    const gamedata::InteractionData& data = shop.host.interactions;
    world::Entity* station = shop.station();
    // The removal of the source: the use ends, the client gets 3746, the observer B gets the stop.
    world::Entity* camp = shop.fixture("camp", 1.5F, -1);
    const InteractionBlock atCamp = child_block(data, *camp, kCraft);
    shop.request(shop.clientA, atCamp);
    const std::uint32_t campInterestB = interest_of(shop.clientB, camp->actor.bytes);
    std::size_t from = shop.clientA.got.size();
    std::size_t fromB = shop.clientB.got.size();
    const world::EntityId campId = camp->id;
    check(shop.interactions().remove_object(campId), "control: the camp is removed");
    shop.host.steps(60, {&shop.clientA, &shop.clientB});
    const std::vector<std::uint32_t> stopsB = stops_of(shop.clientB, fromB);
    check(!shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) == 1 && campInterestB != 0
              && std::find(stopsB.begin(), stopsB.end(), campInterestB) != stopsB.end() && !shop.interactions().remove_object(campId),
          "the removal of the source: the use ends, A gets 3746, B gets the stop; a second removal: no such child");
    shop.end_block(shop.clientA);
    // The loss of the range: A walks 3.5 m south; the bound is then 6.5 m away.
    shop.request(shop.clientA, child_block(data, *station, kCraft));
    from = shop.clientA.got.size();
    for (int step = 1; step <= 7; ++step) {
        shop.placeA = state::WorldPosition{100.0F, 200.0F - 0.5F * static_cast<float>(step), 30.0F};
        shop.report(shop.clientA, child_block(data, *station, kCraft));
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    check(!shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) == 1,
          "the loss of the range (an accepted position 6.5 m from the bound): the use ends, the client gets 3746");
    shop.end_block(shop.clientA);
    for (int step = 6; step >= 0; --step) {
        shop.placeA = state::WorldPosition{100.0F, 200.0F - 0.5F * static_cast<float>(step), 30.0F};
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    // Damage: a station use stays; a timed use ends; an animation (the chair, action 64) ends.
    shop.request(shop.clientA, child_block(data, *station, kCraft));
    (void)shop.host.world.health().hurt(kCharacterA, 10.0F);
    shop.host.steps(3, {&shop.clientA});
    const bool stationStays = shop.entity(kCharacterA)->interaction.has_value();
    shop.end_block(shop.clientA);
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    shop.request(shop.clientA, child_block(data, *keg, kTrigger));
    from = shop.clientA.got.size();
    (void)shop.host.world.health().hurt(kCharacterA, 10.0F);
    shop.host.steps(600, {&shop.clientA});
    const bool timedEnds = !shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) == 1
                           && shop.interactions().completions_of(keg->id) == 0;
    shop.end_block(shop.clientA);
    shop.request(shop.clientA, block_for(data, kChairObject, kChair));
    from = shop.clientA.got.size();
    (void)shop.host.world.health().hurt(kCharacterA, 10.0F);
    shop.host.steps(3, {&shop.clientA});
    check(stationStays && timedEnds && !shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) == 1,
          "damage: a station use stays; a timed use ends (3746, no completion); a chair (InteractionAnimation, IfDamaged) ends (3746)");
    shop.end_block(shop.clientA);
    (void)shop.host.world.health().heal(kCharacterA);
    // A travel in a timed use: 3746 and 6709, no completion later.
    shop.request(shop.clientA, child_block(data, *keg, kTrigger));
    from = shop.clientA.got.size();
    (void)shop.host.world.travel(kCharacterA, state::WorldPosition{101.0F, 200.5F, 30.0F});
    shop.host.steps(600, {&shop.clientA});
    check(!shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) == 1
              && count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1 && shop.interactions().completions_of(keg->id) == 0,
          "a travel in a timed use: 3746 and 6709 one time each, and no completion");
    shop.end_block(shop.clientA);
    shop.placeA = state::WorldPosition{101.0F, 200.5F, 30.0F};
    for (int step = 0; step < 3; ++step) {
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(10, {&shop.clientA});
    }
    // A conversation that the server ends (the command): 3746; the 6705 of the client (live check L7) still gets 6709.
    shop.request(shop.clientA, block_for(data, kPerson, kConversation));
    from = shop.clientA.got.size();
    check(shop.interactions().cancel(kCharacterA), "control: the cancel command");
    shop.host.steps(3, {&shop.clientA});
    world::Entity* entity = shop.entity(kCharacterA);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_call(server::rep::kTypeCutsceneEnded, entity->actor.bytes,
                                                                      entity->id + server::rep::kPlayerCutsceneComponent, entity->id));
    shop.host.steps(3, {&shop.clientA});
    check(count_type(shop.clientA, kServerCancel, from) == 1 && count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1
              && !entity->interaction,
          "a conversation that the server ends: 3746; the 6705 of the client gets 6709 (the input of the client is free)");
    shop.end_block(shop.clientA);
    // The death in a timed use: 3746, no completion.
    shop.request(shop.clientA, child_block(data, *keg, kTrigger));
    from = shop.clientA.got.size();
    (void)shop.host.world.health().hurt(kCharacterA, 1.0e9F);
    shop.host.steps(600, {&shop.clientA});
    check(!shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) >= 1 && shop.interactions().completions_of(keg->id) == 0,
          "the death in a timed use: the use ends (3746), no completion");
}

/** ST5: the service handoff (I14, IT-SERVICE). */
void service_handoff(const Options&) {
    Stations shop;
    shop.enter();
    const gamedata::InteractionData& data = shop.host.interactions;
    world::Interactions& interactions = shop.interactions();
    const InteractionBlock talk = block_for(data, kPerson, kConversation);
    shop.request(shop.clientA, talk);
    shop.request(shop.clientB, talk);
    check(interactions.grant_service(*shop.entity(kCharacterA), "shop", {11, 22}, 1)
              && interactions.grant_service(*shop.entity(kCharacterB), "shop", {33}, 1),
          "control: the person gives A the offer 11 and 22, and B the offer 33");
    send_cancel(shop, shop.clientA, kCharacterA, talk);
    shop.end_block(shop.clientA);
    check(!shop.entity(kCharacterA)->interaction && interactions.require_service(kCharacterA, "shop", 11) == world::ServiceError::none
              && interactions.require_service(kCharacterA, "shop", 22) == world::ServiceError::none,
          "the conversation of A ends (2473): its grant stays, and a later operation with no source id passes (11, 22)");
    check(interactions.require_service(kCharacterA, "shop", 33) == world::ServiceError::notOffered
              && interactions.require_service(kCharacterB, "shop", 33) == world::ServiceError::none
              && interactions.require_service(kCharacterB, "shop", 11) == world::ServiceError::notOffered
              && interactions.require_service(kCharacterA, "transmog", 11) == world::ServiceError::noGrant,
          "the offer of each character: A has no 33, B has no 11; a different service: no grant");
    // A new source ends the grant of B (a read of a page: a committed use of a different source).
    send_cancel(shop, shop.clientB, kCharacterB, talk);
    shop.end_block(shop.clientB);
    shop.request(shop.clientB, block_for(data, kPage, kReadLore));
    check(interactions.require_service(kCharacterB, "shop", 33) == world::ServiceError::noGrant
              && interactions.require_service(kCharacterA, "shop", 11) == world::ServiceError::none,
          "B uses a different source: the grant of B ends; the grant of A stays");
    shop.end_block(shop.clientB);
    // The range: A walks 6 m from the person (its bound): the grant ends.
    for (int step = 1; step <= 10; ++step) {
        shop.placeA = state::WorldPosition{100.0F + 0.5F * static_cast<float>(step), 200.0F, 30.0F};
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    check(interactions.require_service(kCharacterA, "shop", 11) == world::ServiceError::tooFar
              && interactions.require_service(kCharacterA, "shop", 11) == world::ServiceError::noGrant,
          "A 7 m from the source: tooFar, and the grant ends");
    for (int step = 9; step >= 0; --step) {
        shop.placeA = state::WorldPosition{100.0F + 0.5F * static_cast<float>(step), 200.0F, 30.0F};
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    // A server child as the source: its removal ends the grant.
    world::Entity* camp = shop.fixture("camp", 1.5F, -1);
    shop.request(shop.clientA, child_block(data, *camp, kCraft));
    check(interactions.grant_service(*shop.entity(kCharacterA), "campSkins", {7}, 1)
              && interactions.require_service(kCharacterA, "campSkins", 7) == world::ServiceError::none,
          "control: the camp gives A a service");
    check(interactions.remove_object(camp->id) && interactions.require_service(kCharacterA, "campSkins", 7) == world::ServiceError::noGrant,
          "the removal of the source: the grant ends");
    shop.end_block(shop.clientA);
    // The deadline, the death, and the leave.
    shop.request(shop.clientA, talk);
    check(interactions.grant_service(*shop.entity(kCharacterA), "shop", {11}, 2, shop.host.clock.now() + std::chrono::seconds(1)),
          "control: a grant with a deadline 1 s later");
    shop.host.steps(150, {&shop.clientA, &shop.clientB});
    check(interactions.require_service(kCharacterA, "shop", 11) == world::ServiceError::expired, "after the deadline: expired");
    (void)interactions.grant_service(*shop.entity(kCharacterA), "shop", {11}, 3);
    (void)shop.host.world.health().hurt(kCharacterA, 1.0e9F);
    shop.host.steps(3, {&shop.clientA});
    check(interactions.require_service(kCharacterA, "shop", 11) == world::ServiceError::noGrant, "the death: the grant ends");
    (void)shop.host.world.health().heal(kCharacterA);
    shop.end_block(shop.clientA);
    shop.request(shop.clientB, talk);
    (void)interactions.grant_service(*shop.entity(kCharacterB), "shop", {33}, 4);
    const world::EntityId before = shop.entity(kCharacterB)->id;
    shop.host.disconnect(shop.clientB.peer);
    shop.host.steps(60, {&shop.clientA});
    const bool gone = shop.entity(kCharacterB) == nullptr;
    FakeClient again{27002};
    shop.host.enter(again, shop.b.registration());
    shop.host.steps(30, {&shop.clientA, &again});
    note("SC5 leave: gone " + std::to_string(gone) + ", back " + std::to_string(shop.entity(kCharacterB) != nullptr));
    check(gone && shop.entity(kCharacterB) != nullptr && shop.entity(kCharacterB)->id != before && !shop.entity(kCharacterB)->serviceGrant
              && interactions.require_service(kCharacterB, "shop", 33) == world::ServiceError::noGrant,
          "B leaves and enters again: a new world entry (a new entity) has no grant");
}

/** ST6: two sessions (IT-MP). */
void two_players(const Options&) {
    Stations shop;
    shop.enter();
    const gamedata::InteractionData& data = shop.host.interactions;
    world::Entity* station = shop.station();
    const std::uint32_t atA = interest_of(shop.clientA, station->actor.bytes);
    const std::uint32_t atB = interest_of(shop.clientB, station->actor.bytes);
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, child_block(data, *station, kCraft));
    shop.request(shop.clientB, child_block(data, *station, kCraft));
    shop.host.steps(30, {&shop.clientA, &shop.clientB});
    shop.end_block(shop.clientA);
    shop.host.steps(30, {&shop.clientA, &shop.clientB});
    shop.end_block(shop.clientB);
    shop.host.steps(30, {&shop.clientA, &shop.clientB});
    const auto statesA = station_states(shop.clientA, atA, 4, fromA);
    const auto statesB = station_states(shop.clientB, atB, 4, fromB);
    const std::vector<std::pair<bool, std::uint32_t>> expected{{true, 1}, {true, 2}, {true, 1}, {true, 0}};
    check(atA != 0 && atB != 0 && statesA == expected && statesB == expected,
          "an unlimited station: each observer gets the users 1, 2, 1, 0 (its own interest number for the station)");
    check(replies(shop.clientA, fromA) == std::vector<std::pair<unsigned, unsigned>>{{20U, 1U}}
              && replies(shop.clientB, fromB) == std::vector<std::pair<unsigned, unsigned>>{{20U, 1U}},
          "each client gets only its own reply 3743");
    // The observer B gets the Interactor state of A with G of the station; B has no station context of A.
    const std::size_t from = shop.clientB.got.size();
    shop.request(shop.clientA, child_block(data, *station, kCraft));
    shop.host.steps(30, {&shop.clientA, &shop.clientB});
    bool seen = false;
    for (const Seen& item : fragments_of(shop.clientB, kInteractorSlot, kInteractorType, from)) {
        seen = seen || (item.interest != 1 && u64_of(field_of(item.fragment, 0, 0)) == station->id);
    }
    check(seen && !shop.interactions().station(*shop.entity(kCharacterB)).has_value()
              && shop.interactions().station(*shop.entity(kCharacterA)).has_value(),
          "B gets the Interactor state of A (G of the station); B has no station context");
    shop.end_block(shop.clientA);
    // A keg of one place: the second character is refused.
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    const std::size_t kegB = shop.clientB.got.size();
    shop.request(shop.clientA, child_block(data, *keg, kTrigger));
    shop.request(shop.clientB, child_block(data, *keg, kTrigger));
    check(replies(shop.clientB, kegB) == std::vector<std::pair<unsigned, unsigned>>{{33U, 2U}} && !shop.entity(kCharacterB)->interaction
              && shop.entity(kCharacterA)->interaction,
          "a keg of one place: A uses it, B is refused (full)");
    shop.end_block(shop.clientA);
    shop.end_block(shop.clientB);
}

/** ST7: the load (IT-LOAD). */
void load(const Options&) {
    const auto measure = [](std::size_t farChildren) {
        Stations shop(false);
        StationParts parts = station_parts();
        for (std::size_t index = 0; index < farChildren; ++index) {
            gamedata::SpawnedPlacement child = parts.spawned.front();
            child.key = std::string(kSettlementKey) + "/far" + std::to_string(index);
            child.position = state::WorldPosition{1000.0F + 97.0F * static_cast<float>(index % 300), 1000.0F + 97.0F * static_cast<float>(index / 300),
                                                  30.0F};
            parts.spawned.push_back(child);
        }
        if (!set_stations(shop.host.interactions, std::move(parts))) {
            throw std::runtime_error("load: " + shop.host.interactions.error());
        }
        shop.enter(false);
        const std::uint64_t scans = shop.interactions().counts().scans;
        const auto start = std::chrono::steady_clock::now();
        shop.host.steps(1000, {&shop.clientA}); // 10 s: 20 scans
        const auto spent = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
        return std::make_tuple(shop.interactions().counts().scanWork.cells, shop.interactions().counts().scanWork.bounds,
                               shop.interactions().instance_count(), spent, shop.interactions().counts().scans - scans);
    };
    const auto [cellsSmall, entriesSmall, liveSmall, timeSmall, scansSmall] = measure(1000);
    const auto [cellsLarge, entriesLarge, liveLarge, timeLarge, scansLarge] = measure(60000);
    note("SC7 work: " + std::to_string(cellsSmall) + " cells, " + std::to_string(entriesSmall) + " entries with 1,000 far children; "
         + std::to_string(cellsLarge) + " cells, " + std::to_string(entriesLarge) + " entries with 60,000; 1,000 steps: "
         + std::to_string(timeSmall.count()) + " us and " + std::to_string(timeLarge.count()) + " us; scans "
         + std::to_string(scansSmall) + " and " + std::to_string(scansLarge));
    check(cellsSmall == cellsLarge && entriesSmall == entriesLarge && liveSmall == 2 && liveLarge == 2 && scansSmall >= 19 && scansLarge >= 19
              && timeLarge.count() < 3000000,
          "60,000 far server children: the same work of the spawn rule near the character (cells, entries), the same 2 live children, "
          "and 1,000 steps in less than 3 s");
    // 150 timed uses one after the other, each with its timer: the time of the requests, the timers, and the completions.
    Stations shop(false);
    shop.enter(false);
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    const InteractionBlock trigger = child_block(shop.host.interactions, *keg, kTrigger);
    const auto start = std::chrono::steady_clock::now();
    for (int use = 0; use < 150; ++use) {
        shop.request(shop.clientA, trigger, 1);
        shop.host.advance(std::chrono::milliseconds(5100));
        shop.host.tick();
        shop.end_block(shop.clientA, 1);
    }
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    check(shop.interactions().completions_of(keg->id) == 150 && shop.interactions().timed_count() == 0 && spent.count() < 2000,
          "150 timed uses: 150 completions, no timed use left, in less than 2 s (" + std::to_string(spent.count()) + " ms)");
    // No character: the spawn rule makes no child and looks at no cell.
    Stations empty(false);
    empty.host.steps(200, {});
    check(empty.interactions().instance_count() == 0 && empty.interactions().counts().scanWork.cells == 0,
          "no character in the world: no server child, no cell of the spawn grid");
}

/** ST8: the loader of format 2 (I9). */
void loader_format2(const Options& options) {
    using nlohmann::json;
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "interaction-data-2.json";
    const auto hex8 = [](std::uint64_t value) {
        char digits[17];
        std::snprintf(digits, sizeof(digits), "%016llx", static_cast<unsigned long long>(value));
        return std::string(digits);
    };
    const json option = {{"name", "OpenCraftingOption"}, {"id", gamedata::interaction_option_id("OpenCraftingOption")}, {"action", 20},
                         {"committed", true}, {"delay", 0.0}, {"hold", 0.5}, {"instantCancel", false}, {"cooldown", 0}, {"maxUsage", 0.0},
                         {"inactive", 0.0}, {"requiredLevel", 0}, {"requiredItem", ""}, {"requiredItemCount", 0}, {"requiredCurrency", 0},
                         {"requiredAchievement", -1}, {"statusRules", false}};
    const json bound = {{"slot", 4}, {"entity", hex8(kStationBound)}, {"component", hex8(kStationInteract)}, {"position", {1.0, 0.0, 0.25}},
                        {"rangeSq", 25.0}, {"capacity", -1}, {"lineOfSight", false}, {"arcRequired", false}, {"arc", 180.0}, {"lore", -1},
                        {"options", json::array({json::array({0, hex8(kFunctional)})})}, {"requires", json::object()}};
    const json station = {{"slice", "slices/test/station.dynamicslice"}, {"asset", "a55e7501000000000000000000000002"}, {"sub", 2},
                          {"kind", "craftingStation"}, {"root", hex8(kStationRoot)},
                          {"slots", {{"position", 2}, {"buildable", 1}, {"ownership", 0}, {"vitals", 0}}}, {"buildableState", 5},
                          {"permissionRule", -1},
                          {"assembly", {{"key", "Blacksmith5"}, {"fee", true}, {"entity", hex8(kFunctional)}, {"component", hex8(kAssemblyComponent)},
                                        {"resolved", true}}},
                          {"bounds", json::array({bound})},
                          {"children", json::array({{{"template", 1}, {"position", {0.0, 0.0, 0.0}}, {"rotation", {0.0, 0.0, 0.0, 1.0}}}})}};
    const json forge = {{"slice", "slices/test/forge.dynamicslice"}, {"asset", "a55e7502000000000000000000000002"}, {"sub", 2}, {"kind", "other"},
                        {"root", hex8(0x5000)}, {"slots", {{"position", 1}, {"buildable", 0}, {"ownership", 0}, {"vitals", 0}}},
                        {"buildableState", -1}, {"permissionRule", -1}, {"assembly", nullptr}, {"bounds", json::array()},
                        {"children", json::array()}};
    const json child = {{"key", kStationKey}, {"parent", kSettlementKey}, {"parentActor", "0b5e771e5345544c0000000000000000"},
                        {"parentSlice", "slices/test/settlement.dynamicslice"}, {"spawner", "Public_Blacksmith"}, {"template", 0},
                        {"spawnOnInitialize", false}, {"region", "r_+00_+00"}, {"position", {100.0, 202.0, 30.0}},
                        {"rotation", {0.0, 0.0, 0.7071068, 0.7071068}}};
    const json good = {{"format", 2}, {"options", json::array({option})}, {"achievements", json::array()}, {"objects", json::array()},
                       {"templates", json::array({station, forge})}, {"spawned", json::array({child})}, {"fixtures", {{"station", 0}}},
                       {"counts", {{"objects", 0}, {"bounds", 0}, {"options", 1}, {"achievements", 0}, {"templates", 2}, {"spawned", 1}, {"fixtures", 1}}}};
    const auto load = [&](const json& document, gamedata::InteractionData& data) {
        std::ofstream(path) << document.dump();
        return data.load(path.string());
    };
    gamedata::InteractionData data;
    const bool loaded = load(good, data);
    check(loaded && data.loaded() && data.templates().size() == 2 && data.spawned().size() == 1 && data.fixture("station") == 0U
              && data.templates()[0].assembly && data.templates()[0].assembly->key == "Blacksmith5" && data.templates()[0].children.size() == 1
              && data.templates()[0].bounds[0].entity == kStationBound && data.spawned()[0].parent == kSettlementKey
              && data.template_spread(0) > 0.99F,
          "a complete file of format 2: the templates, the server child, the fixture (" + data.error() + ")");
    int refused = 0;
    const auto refuse = [&](const char* what, const std::function<void(json&)>& change) {
        json document = good;
        change(document);
        gamedata::InteractionData copy;
        const bool ok = load(document, copy);
        refused += ok ? 0 : 1;
        if (ok) {
            note(std::string("SC8 loaded a file with ") + what);
        }
    };
    refuse("a cycle of children", [](json& d) { d["templates"][1]["children"] = json::array({{{"template", 0}, {"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}}}); });
    refuse("a child of itself", [](json& d) { d["templates"][0]["children"][0]["template"] = 0; });
    refuse("a fixture with no bound", [](json& d) { d["fixtures"] = {{"station", 1}}; d["counts"]["fixtures"] = 1; });
    refuse("a child with no template", [](json& d) { d["spawned"][0]["template"] = 5; });
    refuse("a rotation that is not a unit", [](json& d) { d["spawned"][0]["rotation"] = {0.0, 0.0, 0.5, 0.5}; });
    refuse("no PositionInTheWorld slot", [](json& d) { d["templates"][1]["slots"]["position"] = 0; });
    refuse("a count that is not the size", [](json& d) { d["counts"]["spawned"] = 2; });
    refuse("two children with one key", [](json& d) { d["spawned"].push_back(d["spawned"][0]); d["counts"]["spawned"] = 2; });
    refuse("format 3", [](json& d) { d["format"] = 3; });
    check(refused == 9, "each incomplete or inconsistent file of format 2 is refused as a whole (" + std::to_string(refused) + " of 9)");
    std::filesystem::remove(path);
}

/** ST9: the development commands of the fixtures. */
void fixture_commands(const Options&) {
    using nlohmann::json;
    Stations shop(false);
    shop.enter(false);
    server::http::Router router;
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_dev_routes(router, services);
    shop.host.tick();
    const auto send = [&](const char* method, const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        return server::http::handle_request(router, request);
    };
    const service::HttpResponse made = send("POST", "/v1/dev/interactions/fixture", std::string("character=") + kCharacterA + "&name=powderKeg&dy=1.5");
    const json reply = json::parse(made.body, nullptr, false);
    const std::string entity = reply.is_object() && reply.contains("entity") ? reply["entity"].get<std::string>() : "";
    shop.host.steps(30, {&shop.clientA});
    const json list = json::parse(send("GET", "/v1/dev/interactions", std::string("character=") + kCharacterA + "&radius=5").body, nullptr, false);
    bool listed = false;
    for (const json& item : list["objects"]) {
        listed = listed || (item["entity"] == entity && item.contains("spawned") && item["spawned"]["fixture"] == "powderKeg");
    }
    check(made.status == 200 && entity.size() == 16 && listed && list["counts"]["serverChildren"] == 3,
          "the fixture command: 200, the entity id; the list has the keg as a server child (with the station and its forge)");
    check(send("POST", "/v1/dev/interactions/fixture", std::string("character=") + kCharacterA + "&name=noSuchFixture").status == 404
              && send("POST", "/v1/dev/interactions/remove", std::string("character=") + kCharacterA + "&entity=" + entity).status == 200
              && send("POST", "/v1/dev/interactions/remove", std::string("character=") + kCharacterA + "&entity=" + entity).status == 404,
          "an unknown fixture: 404; the removal: 200, a second removal: 404");
}

} // namespace

std::vector<Case> station_cases() {
    return {{"SC1", "stations (2.5b, I8): the object entity kind, the metadata codecs, the record, stop and re-entry, the spawn rule", object_entity},
            {"SC2", "stations (2.5b, I9 to I11, IT-STATION): the request of a child, the StationContext, no row, no Assembly, a camp", station_use},
            {"SC3", "stations (2.5b, I12, IT-TIME): the timed use: no early or double completion, the release, a new check", timed_use},
            {"SC4", "stations (2.5b, I13, IT-END): the removal of the source, the range, damage, a travel, the death, a conversation", use_ends},
            {"SC5", "stations (2.5b, I14, IT-SERVICE): the service grant after the conversation, the offers, its ends, two sessions", service_handoff},
            {"SC6", "stations (2.5b, IT-MP): two sessions at a station and a keg of one place", two_players},
            {"SC7", "stations (2.5b, IT-LOAD): far server children, 150 timed uses, no character; time limits", load},
            {"SC8", "stations (2.5b, I9): the loader of format 2 and its refusals", loader_format2},
            {"SC9", "stations (2.5b): the development commands of the fixtures", fixture_commands}};
}

} // namespace soulwarden::backend::tests
