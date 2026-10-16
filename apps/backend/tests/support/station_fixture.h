#pragma once

// The world of the cases of stage 2.5b (the server children, the timed use, the service handoff; tests/station_tests.cpp) and of the
// corrections of its code review (tests/review_stations_tests.cpp): the station templates, the keg, the camp, a station child of a
// made-up settlement, and the helpers of the records. All keys, ids, and positions are made up.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "server/rep/lock_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/interaction_fixture.h"

namespace soulwarden::backend::tests::stations {

using namespace interaction;

/** The option rows of stage 2.5b after the rows of the fixture of 2.5a (kReadLore to kPet). */
enum StationOption : std::uint32_t { kCraft = 6, kTrigger = 7, kBind = 8 };
/** The templates of the cases. */
enum TemplateIndex : std::uint32_t { kStationTemplate, kForgeTemplate, kKegTemplate, kCampTemplate, kBareTemplate, kNoRowTemplate };

inline constexpr std::uint64_t kStationRoot = 0x3000;
inline constexpr std::uint64_t kFunctional = 0x3001;
inline constexpr std::uint64_t kStationBound = 0x3002;
inline constexpr std::uint64_t kAssemblyComponent = 0x698F02D45D873F1AULL;
inline constexpr std::uint64_t kStationInteract = 0x6001;
inline constexpr std::uint64_t kKegRoot = 0x4000;
inline constexpr std::uint64_t kKegEntity = 0x4001;
inline constexpr std::uint64_t kKegInteract = 0x7000;
inline constexpr const char* kSettlementKey = "5e771e00-0000-4000-8000-000000000001";
inline const std::string kStationKey = std::string(kSettlementKey) + "/0000000000002001";
inline const std::string kFarStationKey = std::string(kSettlementKey) + "/0000000000002002";
inline constexpr std::array<std::uint8_t, 16> kSettlementActor{0x0B, 0x5E, 0x77, 0x1E, 'S', 'E', 'T', 'L', 0, 0, 0, 0, 0, 0, 0, 0};
/** The station 2 m north of the place of A, turned by 90 degrees: its bound (1, 0, 0.25 in the slice) is 3 m north of A. */
inline constexpr state::WorldPosition kStationPlace{100.0F, 202.0F, 30.0F};
inline const gamedata::Rotation kTurn{0.0F, 0.0F, std::sqrt(0.5F), std::sqrt(0.5F)};
inline constexpr std::uint32_t kPlaceType = 13;
inline constexpr std::uint32_t kBuildableType = 1594;
inline constexpr std::uint32_t kOwnershipType = 3217;

inline std::array<std::uint8_t, 16> asset_of(std::uint8_t tag) {
    return {0xA5, 0x5E, 0x75, tag, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
}

inline gamedata::InteractionBound template_bound(std::uint32_t slot, std::uint64_t entity, std::uint64_t component, state::WorldPosition local,
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

inline StationParts station_parts() {
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

inline bool set_stations(gamedata::InteractionData& data, StationParts parts = station_parts()) {
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
inline InteractionBlock child_block(const gamedata::InteractionData& data, const world::Entity& entity, std::uint32_t option) {
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
inline void send_cancel(Shop& shop, FakeClient& client, const char* character, const InteractionBlock& block) {
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
inline std::uint32_t interest_of(const FakeClient& client, const std::array<std::uint8_t, 16>& actor, std::size_t from = 0) {
    std::uint32_t interest = 0;
    for (const Seen& seen : fragments_of(client, 0, 10, from)) {
        const auto ref = field_of(seen.fragment, 0, 1);
        if (ref.size() == 16 && std::equal(ref.begin(), ref.end(), actor.begin())) {
            interest = seen.interest;
        }
    }
    return interest;
}

inline std::vector<Fragment> fragments_for(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
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

inline std::vector<std::uint32_t> stops_of(const FakeClient& client, std::size_t from = 0) {
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
inline std::vector<std::pair<bool, std::uint32_t>> station_states(const FakeClient& client, std::uint32_t interest, std::uint32_t slot = 4,
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
inline Bytes expected_place(const state::WorldPosition& place, const gamedata::Rotation& q) {
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


} // namespace soulwarden::backend::tests::stations
