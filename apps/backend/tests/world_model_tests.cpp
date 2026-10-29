// Cases for the world model (stage 0.6b): the entity table, the encoders with the owner view
// and the observer view, the registration checks, one session for each character, the ids,
// the interest table, an entity with no session, and the delete of a character that is in the
// world.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"
#include "gamedata/slice_ids.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "world/ids.h"
#include "world/position_index.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using server::rep::View;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterNone = "cccccccc-0000-4000-8000-00000000000c";
constexpr float kHeight = 50.0F;
/** Frames of KickClientMsg (type 271) with the reasons 1 and 2: reason:u32, u64 0, empty text. */
constexpr const char* kKickDuplicateFrame = "1100018f0400000001000000000000000000";
constexpr const char* kKickDeactivatedFrame = "1100018f0400000002000000000000000000";

struct TwoPlayers {
    Player alpha;
    Player beta;
    explicit TwoPlayers(ServerHost& host, state::WorldPosition a = {100.0F, 200.0F, kHeight},
                        std::optional<state::WorldPosition> b = std::nullopt)
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", a)),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", b)) {}
};

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** @return Each state bundle (type 8) that a client got, from the message `from` on. */
std::vector<Bundle> bundles_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<Bundle> bundles;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == 8) {
            bundles.push_back(read_bundle(client.got[index].body));
        }
    }
    return bundles;
}

/** @return The records of an interest number in the bundles, in sequence. */
std::vector<Record> records_of(const std::vector<Bundle>& bundles, std::uint32_t interest) {
    std::vector<Record> records;
    for (const Bundle& bundle : bundles) {
        for (const Record& record : bundle.records) {
            if (record.interest == interest) {
                records.push_back(record);
            }
        }
    }
    return records;
}

/** @return The stop numbers of the bundles, in sequence. */
std::vector<std::uint32_t> stops_of(const std::vector<Bundle>& bundles) {
    std::vector<std::uint32_t> stops;
    for (const Bundle& bundle : bundles) {
        stops.insert(stops.end(), bundle.stops.begin(), bundle.stops.end());
    }
    return stops;
}

/** @return True when each bundle decodes to its last byte and the sequences are first, first + 1, ... */
bool sequences_have_no_gap(const std::vector<Bundle>& bundles, std::uint64_t first = 1) {
    for (std::size_t index = 0; index < bundles.size(); ++index) {
        if (!bundles[index].complete || bundles[index].sequence != first + index) {
            return false;
        }
    }
    return !bundles.empty();
}

std::string fields_text(const Fragment& fragment) {
    std::string text;
    for (const auto& [key, value] : fragment.fields) {
        text += "(" + std::to_string(key.first) + "," + std::to_string(key.second) + ") ";
    }
    return text;
}

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

std::uint64_t group_of(const Record& record) {
    const Fragment* metadata = fragment_of(record, 0);
    if (metadata == nullptr || metadata->fields.count({0, 1}) == 0) {
        return 0;
    }
    world::ActorId actor;
    std::copy_n(metadata->fields.at({0, 1}).begin(), 16, actor.bytes.begin());
    return world::entity_group_id(actor);
}

bool default_distances() {
    const world::WorldOptions options;
    return options.interestEnter == 100.0F && options.interestLeave == 120.0F;
}

bool end_cause(const std::string& cause) {
    return log_count("rep: session ended, cause: " + cause) == 1;
}

/** N1: the entity table of the world: enter, leave, one entity for each character. */
void entity_table(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    check(host.world.entities().empty(), "a world with no session has no entity");

    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    check(host.world.entities().size() == 1 && alpha != nullptr, "the session of A enters: the world has one entity, of the character of A");
    if (alpha == nullptr) {
        return;
    }
    check(alpha->owner == 1 && host.world.entity_of(1) == alpha && alpha->name == "Alpha"
              && alpha->personaId == players.alpha.account.personaId,
          "the entity has the session 1 as its owner, and the name and the account of the character");
    check(alpha->position.x == 100.0F && alpha->position.y == 200.0F && alpha->position.z == kHeight && alpha->positionEstimated,
          "the position of the entity is the position of the row (100, 200, 50)");
    check(alpha->id == world::entity_group_id(alpha->actor) && host.world.find(alpha->id) == alpha,
          "the key of the entity is its entity group id: the first 8 bytes of its actor id");
    const world::ActorId firstActor = alpha->actor;

    FakeClient b(27001);
    host.enter(b, players.beta.registration());
    const world::Entity* beta = host.world.find_character(kCharacterB);
    check(host.world.entities().size() == 2 && beta != nullptr && beta->owner == 2 && beta != alpha,
          "the session of B enters: the world has two entities, one for each character");
    check(beta != nullptr && beta->position.x == 8880.0F && beta->position.y == 4250.0F && !beta->positionEstimated,
          "a character with no saved position gets the default spawn position, which is exact");

    // One path for a change (rule MP-2): a system changes the entity and marks the state. The
    // replication step of each session that observes the entity sends it.
    const std::size_t aBefore = a.got.size();
    const std::size_t bBefore = b.got.size();
    world::Entity* changed = host.world.find(alpha->id);
    changed->vitals.health = 500.0F;
    host.world.touch(changed->id, world::ComponentKey::vitals);
    host.steps(1, {&a, &b});
    const std::vector<Record> updates = records_of(bundles_of(a, aBefore), 1);
    const Fragment* vitals = updates.empty() ? nullptr : fragment_of(updates[0], 41);
    check(updates.size() == 1 && vitals != nullptr && float_at(vitals->fields.at({0, 0}), 0) == 500.0F
              && fragment_of(updates[0], 0) == nullptr,
          "a change of the vitals of the entity of A goes to session A as one record with the vitals state (health 500)");
    check(records_of(bundles_of(b, bBefore), 2).empty() && !b.received_text("Alpha"),
          "session B does not observe the entity of A: it gets nothing of it");

    host.datagram(a.peer, a.carrier_close());
    check(host.world.entities().size() == 1 && host.world.find_character(kCharacterA) == nullptr
              && host.world.entity_of(1) == nullptr,
          "session A ends: the world takes its entity out of the table");
    host.steps(2, {&b});

    FakeClient again(27002);
    host.enter(again, players.alpha.registration());
    const world::Entity* second = host.world.find_character(kCharacterA);
    check(second != nullptr && host.world.entities().size() == 2 && second->owner == 3 && second->actor != firstActor,
          "the character of A enters again: a new entity with a new actor id (an id is not used again)");
    std::set<std::string> characters;
    for (const auto& [id, entity] : host.world.entities()) {
        characters.insert(entity.characterId);
    }
    check(characters.size() == host.world.entities().size(), "each character has one entity at most");
    host.server->stop();
    check(host.world.entities().empty(), "the stop of the game server ends each session: the world has no entity");
}

/** T3: the character of session A registers in session C. The newer session wins. */
void character_in_a_second_session(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    // A walks: the row has (120, 200), and the entity is at (123, 200).
    host.datagram(a.peer, a.input_at(1, 120.0F, 200.0F, kHeight));
    host.steps(100, {&a});
    host.datagram(a.peer, a.input_at(31, 123.0F, 200.0F, kHeight));
    host.steps(1, {&a});
    const auto row = saved_position(host.store, kCharacterA);
    const world::Entity* before = host.world.find_character(kCharacterA);
    check(row && std::abs(row->x - 120.0F) < 0.05F && before != nullptr && std::abs(before->position.x - 123.0F) < 0.05F,
          "before the second session: the row has (120, 200), and the entity is at (123, 200)");
    const world::EntityId entityId = before != nullptr ? before->id : 0;
    const world::ActorId actor = before != nullptr ? before->actor : world::ActorId{};

    FakeClient c(27001);
    host.enter(c, players.alpha.registration());
    a.read(host.take(a.peer));
    const auto kicks = std::count_if(a.got.begin(), a.got.end(), [](const Sent& sent) { return sent.type == 271; });
    check(kicks == 1 && a.got.back().type == 271 && hex_text(a.got.back().frame) == kKickDuplicateFrame,
          "A gets the message type 271 with reason 1 (frame 11 00 01 8F 04 00 00 00 01, 8 zero bytes, 00) as its last message");
    const std::size_t aMessages = a.got.size();
    const world::Entity* after = host.world.find_character(kCharacterA);
    check(host.world.entities().size() == 1 && after != nullptr && after->id == entityId && after->actor == actor
              && after->owner == 2,
          "the world has one entity: the entity of the character stays, with its ids, and session C is its owner");
    check(c.types() == "3 1628 1635 1617 8 8 ", "C gets the 6 messages of a registration");
    const std::vector<Record> records = records_of(bundles_of(c), 1);
    const Fragment* movement = records.empty() ? nullptr : fragment_of(records[0], 16);
    check(movement != nullptr && float_at(movement->fields.at({0, 27}), 0) == 123.0F
              && float_at(movement->fields.at({0, 27}), 4) == 200.0F,
          "C gets the position of the entity (123, 200), not the position that the row had (120, 200)");
    check(!records.empty() && group_of(records[0]) == entityId, "the record of C has the actor id of the entity");
    check(log_count("rep: position 123.00 200.00 50.00 saved") == 1 && log_count("saved") == 2,
          "one position write for the change of the owner: the row gets (123, 200)");

    // A is alive until its client closes the connection, or for 1 s. It gets no state after the
    // kick. Its client does not acknowledge here: the carrier sends the kick again.
    const std::uint64_t aBytes = host.transport.bytes[a.peer];
    host.datagram(a.peer, a.input_at(60, 700.0F, 800.0F, kHeight));
    host.steps(97, {&c});
    check(log_count("rep: session ended") == 0 && host.server->session_count() == 2,
          "0.99 s after the kick, session A is alive: the carrier gives the message to the client");
    host.steps(1, {&c});
    check(end_cause("replaced by a newer session of the character"),
          "1 s after the kick, session A ends with the cause 'replaced by a newer session of the character'");
    a.read(host.take(a.peer));
    check(a.got.size() == aMessages && host.transport.bytes[a.peer] > aBytes
              && std::abs(host.world.find_character(kCharacterA)->position.x - 123.0F) < 0.05F,
          "after the kick, A gets no new message (the carrier sends the kick again), and an input of A does not move "
          "the entity");
    check(host.world.entities().size() == 1 && host.world.find_character(kCharacterA)->owner == 2
              && log_count("saved") == 2,
          "the end of A changes no entity and writes no position");

    // The client of A closes its connection at once: the end has the same cause.
    FakeClient d(27002);
    host.enter(d, players.alpha.registration());
    c.read(host.take(c.peer));
    host.datagram(c.peer, c.carrier_close());
    check(log_count("rep: session ended, cause: replaced by a newer session of the character") == 2,
          "a client that closes its connection after the kick: its session ends with the same cause at once");
    check(host.world.entities().size() == 1 && host.world.find_character(kCharacterA)->owner == 3,
          "the world has one entity, and the third session is its owner");
}

/** T4: a registration with a character id that the store does not have, and with a character of a different account. */
void registration_refused(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);

    FakeClient unknown(27000);
    Registration fields = players.alpha.registration();
    fields.characterId = kCharacterNone;
    host.enter(unknown, fields);
    host.steps(50, {&unknown});
    check(unknown.types() == "3 " && hex_text(unknown.got.at(0).body).rfind("ffffff60", 0) == 0,
          "a character id that the store does not have: the client gets only the reply (type 3) with the result -160 (FF FF FF 60)");
    check(host.world.entities().empty(), "the world has no entity");
    check(log_count("refused (the store does not have the character)") == 1, "the log has the cause of the refusal");
    host.datagram(unknown.peer, unknown.carrier_close());
    check(end_cause("registration refused"), "the client closes its connection: the session ends with the cause 'registration refused'");

    FakeClient other(27001);
    fields = players.beta.registration();
    fields.characterId = kCharacterA; // The account of B asks for the character of A.
    host.enter(other, fields);
    host.steps(50, {&other});
    check(other.types() == "3 " && hex_text(other.got.at(0).body).rfind("ffffff60", 0) == 0,
          "a character of a different account: the client gets only the reply with the result -160");
    check(host.world.entities().empty() && log_count("refused (the character is of a different account)") == 1,
          "the world has no entity, and the log has the cause");
    // This client does not close its connection: the session ends 1 s after the reply.
    host.steps(60, {&other});
    check(log_count("rep: session ended, cause: registration refused") == 2 && other.types() == "3 ",
          "a client that stays: its session ends 1 s after the reply, and it gets no other message");

    FakeClient empty(27002);
    fields = players.alpha.registration();
    fields.characterId.clear();
    host.enter(empty, fields);
    check(empty.types() == "3 " && hex_text(empty.got.at(0).body).rfind("ffffff60", 0) == 0 && host.world.entities().empty(),
          "a registration with no character id: the same refusal");

    FakeClient correct(27003);
    host.enter(correct, players.alpha.registration());
    check(correct.types() == "3 1628 1635 1617 8 8 " && host.world.entities().size() == 1
              && hex_text(correct.got.at(0).body).rfind("00000000", 0) == 0,
          "after the refusals, the account of A registers its character: result 0 and the 6 messages");
    check(!saved_position(host.store, kCharacterB).has_value() && log_count("saved") == 0, "no refusal wrote a row");
}

/** V1: each encoder with the owner view and the observer view; the record of a remote player. */
void encoder_views(const Options&) {
    using namespace server::rep;
    // The time anchor of the example of the research: 1,000,000,000 microseconds.
    ManualClock clock(std::chrono::seconds(1000));
    world::Entity entity;
    entity.actor.bytes = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61};
    entity.id = world::entity_group_id(entity.actor);
    entity.characterId = "00000000-0000-4000-8000-000000000002";
    entity.name = "Remote";
    entity.position = state::WorldPosition{8882.0F, 4252.0F, 138.5F};

    const auto fields = [&](const StateCodec& codec, View view) {
        return fields_text(decode_state(codec.type, codec.encode(entity, view, clock)));
    };
    const std::span<const StateCodec> codecs = player_codecs();
    check(codecs.size() == 33 && codecs[0].slot == 0 && codecs[0].type == 10 && codecs[1].slot == 9 && codecs[1].type == 3935
              && codecs[2].slot == 16 && codecs[2].type == 11 && codecs[3].slot == 41 && codecs[3].type == 15
              && codecs[4].slot == 47 && codecs[4].type == 1652 && codecs[5].slot == 12 && codecs[5].type == 1195
              && codecs[6].slot == 19 && codecs[6].type == 3133 && codecs[7].slot == 66 && codecs[7].type == 3210
              && codecs[8].slot == 70 && codecs[8].type == 899 && codecs[9].slot == 27 && codecs[9].type == 129
              && codecs[10].slot == 62 && codecs[10].type == 3183 && codecs[11].slot == 60 && codecs[11].type == 3786
              && codecs[12].slot == 61 && codecs[12].type == 1755 && codecs[13].slot == 65 && codecs[13].type == 911
              && codecs[14].slot == 67 && codecs[14].type == 3681 && codecs[15].slot == 46 && codecs[15].type == 185
              && codecs[16].slot == 45 && codecs[16].type == 2932 && codecs[17].slot == 48 && codecs[17].type == 5257
              && codecs[18].slot == 44 && codecs[18].type == 4236 && codecs[19].slot == 43 && codecs[19].type == 1525
              && codecs[20].slot == 42 && codecs[20].type == 4297 && codecs[20].key == world::ComponentKey::stamina && codecs[20].ownerOnly
              && codecs[21].slot == 16 && codecs[21].type == 11 && codecs[21].key == world::ComponentKey::motion
              && codecs[22].slot == 26 && codecs[22].type == 3752 && !codecs[22].ownerOnly
              && codecs[23].slot == 33 && codecs[23].type == 982 && codecs[23].key == world::ComponentKey::lootTracker && codecs[23].ownerOnly
              && codecs[24].slot == 36 && codecs[24].type == 81 && codecs[24].key == world::ComponentKey::gathering && codecs[24].ownerOnly
              && codecs[25].slot == 76 && codecs[25].type == 1755 && codecs[25].key == world::ComponentKey::selectedContainer
              && codecs[25].ownerOnly && codecs[26].slot == 77 && codecs[26].type == 2938
              && codecs[26].key == world::ComponentKey::globalStorage && codecs[26].ownerOnly && codecs[27].slot == 63
              && codecs[27].type == 205 && codecs[27].key == world::ComponentKey::crafting && codecs[27].ownerOnly
              && codecs[28].slot == 73 && codecs[28].type == 5606 && codecs[28].key == world::ComponentKey::seasons && codecs[28].ownerOnly
              && codecs[28].afterObjectivesEnable && codecs[29].slot == 22 && codecs[29].type == 3857
              && codecs[29].key == world::ComponentKey::objectives && codecs[29].ownerOnly && codecs[29].afterObjectivesEnable
              && codecs[30].slot == 49 && codecs[30].type == 3152 && codecs[30].key == world::ComponentKey::faction && !codecs[30].ownerOnly
              && !codecs[30].afterObjectivesEnable && codecs[31].slot == 55 && codecs[31].type == 4176
              && codecs[31].key == world::ComponentKey::social && codecs[31].ownerOnly && codecs[31].afterObjectivesEnable
              && codecs[32].slot == 10 && codecs[32].type == 3829 && codecs[32].key == world::ComponentKey::objectiveInteractor
              && codecs[32].ownerOnly && !codecs[32].afterObjectivesEnable
              && std::count_if(codecs.begin(), codecs.end(), [](const StateCodec& codec) { return codec.afterObjectivesEnable; }) == 3,
          "one encoder for each component state: metadata (0, 10), player (9, 3935), movement (16, 11), vitals (41, 15), "
          "mana (47, 1652, user test 1), appearance (12, 1195), from stage 1.1b the entitlements (19, 3133) and the "
          "achievements (66, 3210) before the level, from stage 1.3 progression (70, 899), currency (60, 3786), "
          "and container (61, 1755), from stage 1.4 paperdoll (62, 3183), from stage 1.5 attribute (27, 129), from "
          "stage 1.6 categorical progression (65, 911), progression points (67, 3681), and ability (46, 185), from "
          "stage 3.3a the cooldowns (45, 2932), from stage 1.3b the heart rune charge (48, 5257), from stage 3.3b-S1 the "
          "status effects (44, 4236), from stage 3.3b-S2a the speed table (43, 1525), from stage 3.4f-B the stamina (42, 4297, "
          "owner only), from stage 3.3b-S4 the movement "
          "of a dash for the observers (16, 11), from stage 2.5a the Interactor (26, 3752, owner and observers), and from stage 3.5b.1 "
          "the LootTracker (33, 982) and the Gathering state (36, 81), owner only, and from stage 3.5b.2 the selected Container (76, "
          "1755) and the GlobalStorage map (77, 2938), owner only, and from stage 3.7b the Crafting state (63, 205), owner only, and from "
          "stage 1.8a the seasons readiness (73, 5606) and the Objectives state (22, 3857), owner only, after the first 2709, and from "
          "stage QG7 the Faction state (49, 3152, owner and observers) and the empty Social state (55, 4176, owner only, after the first "
          "2709), and from stage QG6a the ObjectiveInteractor state (10, 3829, owner only)");
    check(!codecs[0].ownerOnly && !codecs[1].ownerOnly && !codecs[2].ownerOnly && !codecs[3].ownerOnly
              && codecs[4].ownerOnly && !codecs[5].ownerOnly && codecs[6].ownerOnly && codecs[7].ownerOnly && !codecs[8].ownerOnly
              && codecs[9].ownerOnly && !codecs[10].ownerOnly && codecs[11].ownerOnly && codecs[12].ownerOnly && codecs[13].ownerOnly
              && codecs[14].ownerOnly && codecs[15].ownerOnly && codecs[16].ownerOnly && codecs[17].ownerOnly && !codecs[18].ownerOnly
              && codecs[19].ownerOnly,
          "the five states of stage 0.6b, the level, the paperdoll, and the status effects (each view has its fields) are for each "
          "observer; the mana, the entitlements, the achievements, the attributes, the coin, the bag, the three skill states, the "
          "cooldowns, the charge, and the speed table go to the owner only");
    check(fields(codecs[4], View::owner) == "(0,0) (0,1) (0,2) (0,3) "
              && hex_text(encode_mana_state(entity, View::owner, clock)) == "010f42c8000042c800000000000000000000"
              && encode_mana_state(entity, View::observer, clock).empty(),
          "mana, owner view: current 100, maximum 100, regeneration delay 0 and rate 0 (the values of the vitals state); "
          "the observer view has no fragment");

    const Bytes metadataOwner = encode_metadata_state(entity, View::owner, clock);
    const Bytes metadataObserver = encode_metadata_state(entity, View::observer, clock);
    check(fields(codecs[0], View::owner) == "(0,0) (0,1) " && metadataOwner == metadataObserver
              && hex_text(metadataObserver)
                     == "0103a660eeebebc75cb7be6bab11eb8317310000000260606060606060606161616161616161",
          "metadata: the two views have the asset id of the player slice with the sub id 2, and the actor id of the entity");

    check(fields(codecs[1], View::owner) == "(1,6) (1,7) (2,0) (2,1) (2,3) " && fields(codecs[1], View::observer) == "(2,0) (2,1) (2,3) ",
          "player: the two views have the fields characterId, characterName, playerConnected of group 2; the owner also has the "
          "wall clock pair of group 1 (fields 6 and 7, stage 3.3b-S1), and an observer no field of group 1");

    const Fragment movementOwner = decode_state(11, encode_movement_state(entity, View::owner, clock));
    const Fragment movementObserver = decode_state(11, encode_movement_state(entity, View::observer, clock));
    check(fields_text(movementOwner) == "(0,11) (0,12) (0,27) (0,31) (0,40) (0,41) (0,42) "
              && hex_text(movementOwner.fields.at({0, 41})) == "c0" && hex_text(movementOwner.fields.at({0, 42})) == "03",
          "movement, owner view: rotation, lookDir, worldPosAbs, teleport id, timeAnchor, and the two permission fields "
          "(dataBits C0, moreDataBits 03)");
    check(fields_text(movementObserver) == "(0,11) (0,12) (0,13) (0,14) (0,15) (0,27) (0,31) (0,40) (0,47) ",
          "movement, observer view: no permission field (dataBits, moreDataBits), and the action of layer 0 "
          "(state id, start count, sequence id, layer count)");
    check(hex_text(movementObserver.fields.at({0, 13})) == "1f" && hex_text(movementObserver.fields.at({0, 14})) == "0001"
              && hex_text(movementObserver.fields.at({0, 15})) == "b910" && hex_text(movementObserver.fields.at({0, 47})) == "04",
          "movement, observer view: the idle action of a player that stands (state 30 and sequence 1080 go out plus 1, "
          "start count 1, 4 layers)");
    check(hex_text(movementObserver.fields.at({0, 27})) == "460ac8004584e0003781"
              && hex_text(movementObserver.fields.at({0, 40})) == "000000003b9aca00",
          "movement: the position (8882.0, 4252.0, 138.5) and the time anchor of the world clock");
    check(fields_text(decode_state(11, encode_movement_heartbeat(clock, 0))) == "(0,0) (0,25) (0,40) ",
          "movement, heartbeat state for the owner: the fields idRel, idAbs, and timeAnchor only");
    world::Entity estimated = entity;
    estimated.positionEstimated = true;
    check(hex_text(decode_state(11, encode_movement_state(estimated, View::owner, clock)).fields.at({0, 27}))
              == "460ac8004584e000379f",
          "movement: an estimated position goes out 0.5 m higher (138.5 becomes 139.0)");

    const Fragment vitalsOwner = decode_state(15, encode_vitals_state(entity, View::owner, clock));
    const Fragment vitalsObserver = decode_state(15, encode_vitals_state(entity, View::observer, clock));
    check(fields_text(vitalsOwner) == "(0,0) (0,1) (0,4) (0,5) (0,6) (0,7) (0,8) (0,13) (1,0) (1,3) (1,4) ",
          "vitals, owner view: health and stamina (group 0) and mana (group 1)");
    check(fields_text(vitalsObserver) == "(0,0) (0,1) (0,4) (0,5) (0,6) (0,7) (0,8) (0,13) ",
          "vitals, observer view: group 0 only, no mana field");

    check(fields(codecs[5], View::owner) == "(0,0) (0,1) (0,2) (0,3) (0,4) (0,5) (0,6) (0,7) (0,8) (0,9) (0,10) (0,11) "
              && encode_appearance_state(entity, View::owner, clock) == encode_appearance_state(entity, View::observer, clock),
          "appearance: the two views have the 12 fields");

    // The record of a remote player that stands: interest 2, and the five states in the observer
    // view, here in the sequence of their slots. It is the record of the research (entities.md
    // 5.3, 172 bytes) with the four action fields in the movement state, which the live client
    // needs to show the body (7 bytes more).
    RecordWriter record(2);
    record.fragment(kMetadataSlot, kTypeGdeMetadataState, metadataObserver);
    record.fragment(kPlayerComponentSlot, kTypePlayerComponentState, encode_player_state(entity, View::observer, clock));
    record.fragment(kAppearanceSlot, kTypeAppearanceState, encode_appearance_state(entity, View::observer, clock));
    record.fragment(kMovementSlot, kTypeMovementState, encode_movement_state(entity, View::observer, clock));
    record.fragment(kVitalsSlot, kTypeVitalsState, encode_vitals_state(entity, View::observer, clock));
    const std::string expected =
        "0205"
        "000a0103a660eeebebc75cb7be6bab11eb8317310000000260606060606060606161616161616161"
        "099f3d040b002430303030303030302d303030302d343030302d383030302d3030303030303030303030320652656d6f746501"
        "0cab1201ff000003010000001f0000000000"
        "100b01fc007c00448040" "6e3a" "1f" "0001" "b910" "460ac8004584e0003781" "01" "000000003b9aca00" "04"
        "290f01f3447a000042c80000447a0000000042c80000" "43000098197a6503e8";
    const Bytes bytes = record.finish();
    note("record of a remote player: " + std::to_string(bytes.size()) + " bytes");
    check(bytes.size() == 179 && hex_text(bytes) == expected,
          "the observer view of the five states gives the record of a remote player (179 bytes)");
}

/** T9: the ids of two entities, and the check of the client messages against the id rule. */
void ids_and_targets(const Options&) {
    {
        world::IdAllocator ids;
        const world::ActorId first = ids.next(world::ActorKind::playerCharacter);
        const world::ActorId second = ids.next(world::ActorKind::playerCharacter);
        check(world::actor_text(first) == "02000000534f554c10000000534f554c"
                  && world::actor_text(second) == "04000000534f554c10000000534f554c"
                  && world::entity_group_id(first) == 0x4C554F5300000002ULL,
              "the first two player actor ids are 02000000 534F554C 10000000 534F554C and 04000000 ...; the entity group "
              "id of the first one is 4C554F5300000002");
        check(world::actor_text(ids.next(world::ActorKind::session)) == "02000020534f554c02000000534f554c"
                  && world::actor_text(ids.player_manager()) == "00000020534f554c01000000534f554c"
                  && world::actor_text(ids.next(world::ActorKind::otherEntity)) == "00000040534f554c20000000534f554c",
              "the first session actor, the player manager, and the first entity that is not a player have their own "
              "serial ranges");
        std::set<world::EntityId> groups{world::entity_group_id(first), world::entity_group_id(second)};
        bool rule = true;
        for (int index = 0; index < 1000; ++index) {
            const world::ActorId actor = ids.next(index % 2 == 0 ? world::ActorKind::playerCharacter : world::ActorKind::otherEntity);
            rule = rule && (actor.bytes[0] & 1) == 0 && actor.bytes[4] == 'S' && actor.bytes[5] == 'O' && actor.bytes[6] == 'U'
                   && actor.bytes[7] == 'L' && groups.insert(world::entity_group_id(actor)).second;
        }
        check(rule && groups.size() == 1002,
              "1,002 ids: the first byte is even, bytes 4 to 7 are equal in all ids and not zero, and no two ids have "
              "equal first 8 bytes");
    }

    // The ids of a slice: made-up values. target = ((entity XOR root XOR G) + component) mod 2^64.
    gamedata::SliceIds slice;
    const std::uint64_t root = 0x1111000000000001ULL;
    const std::uint64_t otherEntity = 0x2222000000000002ULL;
    slice.set(root, {{root, 0x10, "RootComponent"}, {otherEntity, 0xFFFFFFFFFFFFFFF0ULL, "OtherComponent"}});
    const std::uint64_t groupA = 0x4C554F5300000002ULL;
    const std::uint64_t groupB = 0x4C554F5300000004ULL;
    const std::uint64_t targetOther = (otherEntity ^ root ^ groupA) + 0xFFFFFFFFFFFFFFF0ULL;
    check(slice.find(groupA, groupA + 0x10) != nullptr && slice.find(groupA, groupA + 0x10)->name == "RootComponent"
              && slice.find(groupA, targetOther) != nullptr && slice.find(groupA, targetOther)->name == "OtherComponent",
          "id rule: the root entity of an instance has the entity group id, and a target is the instance entity id plus "
          "the component id (with a wrap at 64 bits)");
    check(slice.find(groupB, groupA + 0x10) == nullptr && slice.find(groupB, groupB + 0x10) != nullptr,
          "a target of one entity is not a target of a different entity");

    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.playerSlice = &slice;
    ServerHost host(options);
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    const std::vector<Record> recordsA = records_of(bundles_of(a), 1);
    const std::vector<Record> recordsB = records_of(bundles_of(b), 1);
    check(recordsA.size() == 1 && recordsB.size() == 1 && records_of(bundles_of(a), 2).empty(),
          "each session uses the interest number 1 for its own entity, and no other number");
    if (recordsA.size() != 1 || recordsB.size() != 1) {
        return;
    }
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    check(group_of(recordsA[0]) == groupA && group_of(recordsB[0]) == groupB && alpha->id == groupA && beta->id == groupB,
          "the two entities have different ids: the record of A has the actor id of serial 1, the record of B of serial 2");
    check(hex_text(a.got.at(1).body).substr(2, 32) != hex_text(b.got.at(1).body).substr(2, 32),
          "the two sessions have different session actor ids in their self identification");

    // Client messages of A: the route is the actor id of the receiver.
    const world::ActorId manager = host.world.player_manager();
    host.datagram(a.peer, a.component_call(1094, alpha->actor.bytes, groupA + 0x10, groupA));
    host.datagram(a.peer, a.component_call(1094, alpha->actor.bytes, targetOther, groupA));
    host.datagram(a.peer, a.component_call(2515, alpha->actor.bytes, groupA + 0x99, groupA));
    host.datagram(a.peer, a.component_call(2556, alpha->actor.bytes, groupA + 0x10, groupB));
    host.datagram(a.peer, a.component_call(4248, beta->actor.bytes, groupB + 0x10, groupB));
    host.datagram(a.peer, a.component_call(5000, manager.bytes, 0, 0));
    host.datagram(a.peer, a.other_message(4242));
    const server::rep::MessageCounts& counts = host.session(1).message_counts();
    check(counts.componentCalls == 4 && counts.knownTargets == 2,
          "two component calls of A have a target of the id rule for the entity of A");
    check(counts.unknownTargets == 1 && log_count("the target is not a facet of the player slice for this entity") == 1,
          "a target that the slice does not have for the entity is counted and goes to the log");
    check(counts.wrongEntity == 1 && log_count("the entity field is not the entity group id of the route") == 1,
          "a call whose entity field is not the entity of the route is counted and goes to the log");
    check(counts.unknownRoutes == 2 && log_count("the route is not the player manager and not an entity of this session") == 2,
          "a route that is not known is counted: the actor id of an entity that the session does not observe, and a "
          "made-up id");
    check(counts.playerManager == 1, "a message with the route of the player manager is counted as such");
    const server::rep::MessageCounts& countsB = host.session(2).message_counts();
    check(countsB.componentCalls == 0 && countsB.unknownRoutes == 0, "the messages of A change no count of session B");
    check(log_count("rep: message type 1094: target 4c554f5300000012 is the facet of component 0000000000000010 "
                    "(RootComponent) of entity 4c554f5300000002 [s1 aaaaaaaa]")
              == 1,
          "the log names the component of the first message of each type");
    host.datagram(a.peer, a.carrier_close());
    check(log_count("rep: client messages of the session: 4 component calls (2 with a target of the id rule, 1 with a "
                    "target that is not known, 1 with a wrong entity, 0 with no check), 1 for the player manager, 2 with "
                    "a route that is not known [s1 aaaaaaaa]")
              == 1,
          "the end of the session writes the counts of its client messages to the log");

    // With no ids of the slice, the backend does not check a target.
    ServerHost plain;
    const TwoPlayers plainPlayers(plain);
    FakeClient c(27000);
    plain.enter(c, plainPlayers.alpha.registration());
    const world::Entity* entity = plain.world.find_character(kCharacterA);
    plain.datagram(c.peer, c.component_call(1094, entity->actor.bytes, 0x1234, entity->id));
    check(plain.session(1).message_counts().uncheckedTargets == 1 && plain.session(1).message_counts().unknownTargets == 0,
          "with no ids of the player slice, the target of a component call has no check");
}

/** T10: the interest table of a session with the two distances, and the stop of an entity. */
void interest_table(const Options&) {
    check(default_distances(), "the interest distances of the world are 100 m (enter) and 120 m (leave)");
    ServerHost host(observing());
    const TwoPlayers players(host, state::WorldPosition{1000.0F, 1000.0F, kHeight}, state::WorldPosition{1080.0F, 1000.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    host.steps(60, {&a, &b});
    const world::EntityId idA = host.world.find_character(kCharacterA)->id;
    const world::EntityId idB = host.world.find_character(kCharacterB)->id;
    const auto known = [&](std::uint32_t session, world::EntityId id) -> std::uint32_t {
        const auto& table = host.session(session).observer().known();
        const auto found = table.find(id);
        return found == table.end() ? 0 : found->second.interest;
    };
    check(known(1, idA) == 1 && known(1, idB) == 0 && records_of(bundles_of(a), 2).empty(),
          "B is 80 m from A, but the client of A sent no camera position: A observes only its own entity");

    // The first input report with a camera: the loading screen of the client of A is gone.
    host.datagram(a.peer, a.input_at(1, 1000.0F, 1000.0F, kHeight));
    host.steps(60, {&a, &b});
    check(known(1, idB) == 2 && host.session(1).observer().own() == idA,
          "B at 80 m from A: the interest table of A has B with the number 2");
    std::vector<Record> records = records_of(bundles_of(a), 2);
    check(records.size() == 1, "A gets one record with the interest number 2");
    if (records.size() != 1) {
        return;
    }
    const Record& remote = records[0];
    check(order_of(remote) == "(0,10) (9,3935) (16,11) (41,15) (12,1195) (70,899) " && group_of(remote) == idB
              && text_of(fragment_of(remote, 9)->fields.at({2, 0}), 1) == kCharacterB
              && text_of(fragment_of(remote, 9)->fields.at({2, 1}), 0) == "Beta",
          "the record has the metadata state and the five other states of the entity of B (the last one is its level): "
          "its actor id, character id, and name");
    check(fields_text(*fragment_of(remote, 16)) == "(0,11) (0,12) (0,13) (0,14) (0,15) (0,27) (0,31) (0,40) (0,47) "
              && fields_text(*fragment_of(remote, 41)) == "(0,0) (0,1) (0,4) (0,5) (0,6) (0,7) (0,8) (0,13) "
              && float_at(fragment_of(remote, 16)->fields.at({0, 27}), 0) == 1080.0F,
          "the record is the observer view: no movement permissions, no mana, and the position of B (1080, 1000)");
    check(known(2, idA) == 0 && !b.received_text("Alpha"),
          "the client of B sent no camera position: B observes only its own entity and gets nothing of A");

    const auto move_b = [&](float x) {
        host.world.set_position(idB, state::WorldPosition{x, 1000.0F, kHeight}, false);
        host.steps(60, {&a, &b});
    };
    std::size_t from = a.got.size();
    move_b(1200.0F);
    check(known(1, idB) == 0 && stops_of(bundles_of(a, from)) == std::vector<std::uint32_t>{2}
              && records_of(bundles_of(a, from), 2).empty(),
          "B at 200 m: A gets one bundle with the stop of the number 2, and the table of A has no entry for B");
    from = a.got.size();
    move_b(1110.0F);
    check(known(1, idB) == 0 && records_of(bundles_of(a, from), 2).empty() && stops_of(bundles_of(a, from)).empty(),
          "B back at 110 m (between the two distances): no entry and no record");
    move_b(1090.0F);
    records = records_of(bundles_of(a, from), 2);
    check(known(1, idB) == 2 && records.size() == 1 && order_of(records[0]).rfind("(0,10) ", 0) == 0
              && records[0].fragments.size() == 6,
          "B at 90 m: the entry is back with the number 2 (the lowest free number), and A gets the full record again");
    from = a.got.size();
    move_b(1110.0F);
    check(known(1, idB) == 2 && stops_of(bundles_of(a, from)).empty() && records_of(bundles_of(a, from), 2).empty(),
          "B at 110 m (between the two distances): the entry stays, with no stop and no new record");
    check(sequences_have_no_gap(bundles_of(a)),
          "each bundle of A decodes to its last byte, and the sequences are 1, 2, 3, ... with no gap");

    // The client of B sends its camera: B observes A (rule MP-10: two sessions, two characters).
    host.datagram(b.peer, b.input_at(1, 1110.0F, 1000.0F, kHeight));
    move_b(1050.0F);
    const std::vector<Record> ofA = records_of(bundles_of(b), 2);
    check(known(2, idA) == 2 && ofA.size() == 1 && group_of(ofA[0]) == idA
              && text_of(fragment_of(ofA[0], 9)->fields.at({2, 1}), 0) == "Alpha",
          "B gets the entity of A with its own number 2, in the observer view");
    std::size_t bundleOfOwn = 0;
    std::size_t bundleOfOther = 0;
    const std::vector<Bundle> allB = bundles_of(b);
    for (std::size_t index = 0; index < allB.size(); ++index) {
        for (const Record& record : allB[index].records) {
            if (record.interest == 1 && fragment_of(record, 0) != nullptr) {
                bundleOfOwn = index + 1;
            }
            if (record.interest == 2 && bundleOfOther == 0) {
                bundleOfOther = index + 1;
            }
        }
    }
    check(bundleOfOwn != 0 && bundleOfOwn < bundleOfOther, "the record of the own character is in an earlier bundle than the record of a different player");

    // A leaves the world: its entity goes away, and B gets the stop of its number of A.
    from = b.got.size();
    host.datagram(a.peer, a.carrier_close());
    host.steps(60, {&b});
    check(host.world.find_character(kCharacterA) == nullptr && known(2, idA) == 0
              && stops_of(bundles_of(b, from)) == std::vector<std::uint32_t>{2} && records_of(bundles_of(b, from), 2).empty(),
          "A leaves the world: B gets one stop of the number 2, and no fragment for that number after it");
    check(sequences_have_no_gap(bundles_of(b)), "the bundle sequences of B have no gap");
}

/** S1: an entity with no session (the development command for a remote player). */
void entity_with_no_session(const Options&) {
    ServerHost host(observing());
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
    host.steps(60, {&a});
    const state::WorldPosition place{102.0F, 202.0F, 50.25F};
    check(host.world.spawn_character(kCharacterNone, place) == world::SpawnError::unknownCharacter,
          "a character id that the store does not have gets no entity");
    check(host.world.spawn_character(kCharacterA, place) == world::SpawnError::inWorld,
          "a character that is in the world gets no second entity");
    std::size_t from = a.got.size();
    check(host.world.spawn_character(kCharacterB, place) == world::SpawnError::none,
          "a character of the database goes into the world as an entity with no session");
    const world::Entity* remote = host.world.find_character(kCharacterB);
    check(remote != nullptr && remote->owner == 0 && !remote->positionEstimated && host.world.entities().size() == 2,
          "the entity has no owner, and its position is exact");
    if (remote == nullptr) {
        return;
    }
    const world::EntityId id = remote->id;
    host.steps(60, {&a});
    std::vector<Record> records = records_of(bundles_of(a, from), 2);
    check(records.size() == 1 && group_of(records[0]) == id && records[0].fragments.size() == 6,
          "the session that observes it gets its full record with the interest number 2");
    if (records.size() == 1) {
        const Bytes& position = fragment_of(records[0], 16)->fields.at({0, 27});
        // Height on the wire: (50.25 + 100) / 1100 * 65535 = 8951.
        check(float_at(position, 0) == 102.0F && float_at(position, 4) == 202.0F && position[8] == 0x22 && position[9] == 0xF7
                  && text_of(fragment_of(records[0], 9)->fields.at({2, 1}), 0) == "Beta",
              "the record has the set position with no change of the height (102, 202, 50.25), and the name of the character");
    }

    from = a.got.size();
    host.world.touch_all(id);
    host.steps(1, {&a});
    records = records_of(bundles_of(a, from), 2);
    check(records.size() == 1 && records[0].fragments.size() == 6 && fragment_of(records[0], 0) != nullptr,
          "a mark on each component state: the observer gets the full record again, with the same number");

    from = a.got.size();
    check(host.world.remove_character(kCharacterB, world::LostCause::deleted) && host.world.entities().size() == 1,
          "the entity is removed from the world");
    host.steps(1, {&a});
    check(stops_of(bundles_of(a, from)) == std::vector<std::uint32_t>{2} && host.session(1).observer().known().size() == 1,
          "the observer gets the stop of the number 2 in the next tick");
    from = a.got.size();
    check(host.world.spawn_character(kCharacterB, place) == world::SpawnError::none, "the character goes into the world again");
    host.steps(60, {&a});
    records = records_of(bundles_of(a, from), 2);
    check(records.size() == 1 && group_of(records[0]) != id && group_of(records[0]) != 0,
          "the observer gets a new record with the number 2 again, and the new entity has a new actor id");
    check(!saved_position(host.store, kCharacterB).has_value(), "an entity with no session writes no position row");

    // The client of the character registers: its session takes the entity. No session loses it.
    FakeClient b(27001);
    host.enter(b, players.beta.registration());
    const world::Entity* taken = host.world.find_character(kCharacterB);
    check(taken != nullptr && taken->owner == 2 && host.world.entities().size() == 2 && log_count("kicked") == 0,
          "a session of the character takes the entity that has no session, and no session gets the message 'kicked'");
    check(sequences_have_no_gap(bundles_of(a)), "the bundle sequences of the observer have no gap");
}

/** D1: the delete route for a character that is in the world. */
void delete_in_world(const Options&) {
    using nlohmann::json;
    ServerHost host;
    const TwoPlayers players(host);
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_gateway_routes(router, services);
    const auto request_delete = [&](const Player& player, const std::string& characterId) {
        service::HttpRequest request;
        request.method = "DELETE";
        request.host = "gateway.test";
        request.path = "/prod/game/characters/" + characterId;
        request.target = request.path;
        request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", player.account.personaId}}.dump()));
        std::string route;
        return router.dispatch(request, route);
    };

    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    host.datagram(a.peer, a.input_at(1, 120.0F, 200.0F, kHeight));
    host.steps(100, {&a});
    host.datagram(a.peer, a.input_at(31, 123.0F, 200.0F, kHeight));
    host.steps(1, {&a});
    const std::size_t saves = log_count("saved");

    check(request_delete(players.beta, kCharacterA).status == 404 && host.world.entities().size() == 1
              && host.store.find_character(kCharacterA).has_value() && !host.session(1).closing(),
          "a delete request of a different account: reply 404, and the character, its entity, and its session stay");

    // The route runs in an HTTP thread: it gives the world a task and waits. The task runs in a
    // tick of the world thread, which is the thread of this case.
    std::atomic<bool> done{false};
    int status = 0;
    std::thread http([&] {
        status = request_delete(players.alpha, kCharacterA).status;
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    check(!done && host.world.entities().size() == 1, "the route waits for the world thread: no change before a tick");
    for (int attempt = 0; attempt < 2000 && !done; ++attempt) {
        host.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    http.join();
    check(done && status == 200, "the delete request of the account of the character: reply 200");
    check(host.world.entities().empty() && !host.store.find_character(kCharacterA).has_value(),
          "the world has no entity of the character, and the store has no row");
    check(log_count("saved") == saves, "the delete writes no position row");
    a.read(host.take(a.peer));
    const auto kick = std::find_if(a.got.begin(), a.got.end(), [](const Sent& sent) { return sent.type == 271; });
    check(kick != a.got.end() && hex_text(kick->frame) == kKickDeactivatedFrame,
          "the session of the character gets the message 'kicked' (type 271) with reason 2 before the delete");
    check(log_count("gateway: character " + std::string(kCharacterA) + " was in the world") == 1
              && log_count("gateway: deleted character Alpha") == 1,
          "the log has the removal from the world and the delete");
    const std::size_t aMessages = a.got.size();
    host.datagram(a.peer, a.input_at(60, 700.0F, 800.0F, kHeight));
    host.steps(10, {&a});
    host.datagram(a.peer, a.carrier_close());
    check(end_cause("character deleted") && a.got.size() == aMessages && log_count("saved") == saves,
          "the session gets no state after the kick, and it ends with the cause 'character deleted' with no position write");

    check(request_delete(players.alpha, kCharacterA).status == 404, "a second delete of the same character: reply 404");
    check(request_delete(players.beta, kCharacterB).status == 200 && !host.store.find_character(kCharacterB).has_value(),
          "the delete of a character that is not in the world: reply 200, and the row goes away");
    FakeClient again(27001);
    host.enter(again, players.alpha.registration());
    check(again.types() == "3 " && host.world.entities().empty(),
          "a registration with the deleted character is refused (result -160)");
}

/** P1: the position index. */
void position_index(const Options&) {
    world::PositionIndex index(64.0F);
    index.insert(1, state::WorldPosition{10.0F, 10.0F, 0.0F});
    index.insert(2, state::WorldPosition{100.0F, 10.0F, 0.0F});
    index.insert(3, state::WorldPosition{-200.0F, -200.0F, 0.0F});
    index.insert(4, state::WorldPosition{10.0F, 500.0F, 0.0F});
    const auto near = [&](float x, float y, float range) {
        std::set<world::EntityId> ids;
        index.for_each_candidate(state::WorldPosition{x, y, 0.0F}, range, [&](world::EntityId id) { ids.insert(id); });
        return ids;
    };
    check(index.cell_count() == 4 && near(0.0F, 0.0F, 30.0F) == std::set<world::EntityId>{1},
          "a search of 30 m at the origin reads only the cells that the circle touches: one entity");
    check(near(0.0F, 0.0F, 120.0F) == (std::set<world::EntityId>{1, 2}), "a search of 120 m finds the entity at 100 m also");
    check(near(0.0F, 0.0F, 600.0F).size() == 4, "a search of 600 m finds the four entities, also at negative coordinates");
    index.move(2, state::WorldPosition{100.0F, 10.0F, 0.0F}, state::WorldPosition{1000.0F, 10.0F, 0.0F});
    check(near(0.0F, 0.0F, 120.0F) == std::set<world::EntityId>{1} && near(1000.0F, 0.0F, 30.0F) == std::set<world::EntityId>{2},
          "an entity that moves goes to the cell of its new position");
    index.erase(1, state::WorldPosition{10.0F, 10.0F, 0.0F});
    index.erase(2, state::WorldPosition{1000.0F, 10.0F, 0.0F});
    check(index.cell_count() == 2 && near(0.0F, 0.0F, 120.0F).empty(), "an entity that is erased is in no cell, and an empty cell goes away");

    ServerHost host;
    const TwoPlayers players(host, state::WorldPosition{1000.0F, 1000.0F, kHeight}, state::WorldPosition{1080.0F, 1060.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    const auto count = [&](float range) {
        int found = 0;
        host.world.for_each_near(state::WorldPosition{1000.0F, 1000.0F, 900.0F}, range, [&](world::Entity&) { ++found; });
        return found;
    };
    check(count(99.0F) == 1 && count(100.0F) == 2,
          "the world compares the exact horizontal distance: the entity at 100 m is in a search of 100 m and not of 99 m, "
          "and the height is not a part of the distance");
}

} // namespace

std::vector<Case> world_model_cases() {
    return {
        {"N1", "entity table: enter, leave, one entity for each character, one path for a state change", entity_table},
        {"T3", "the character of session A registers in session C: A gets 'kicked' and ends, one entity, one position write", character_in_a_second_session},
        {"T4", "registration refused: unknown character id, character of a different account (result -160, no other message)", registration_refused},
        {"V1", "encoders: the owner view and the observer view of each component state; the record of a remote player", encoder_views},
        {"T9", "ids: different ids for two entities, interest 1 for the own entity, client messages against the id rule", ids_and_targets},
        {"T10", "interest table: the two distances, the observer view, the stop of an entity, two sessions that observe", interest_table},
        {"S1", "entity with no session: record for the observer, full record again, stop, the session of the character takes it", entity_with_no_session},
        {"D1", "delete route: a character that is in the world loses its session and its entity before the delete", delete_in_world},
        {"P1", "position index: cells, search, move, erase, exact distance", position_index},
    };
}

} // namespace soulwarden::backend::tests
