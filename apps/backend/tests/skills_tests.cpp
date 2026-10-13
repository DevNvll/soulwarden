// Cases for the weapon mastery, the ability bar, and the trade skills (stage 1.6): the wire
// form of the three skill states, the rank and the experience, the requests of a client with a
// correct and with a wrong body, two sessions, the persistence, a tree in more than one
// fragment, and the development routes.

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "gamedata/progression_tables.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/progress_requests.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using gamedata::ProgressionKind;
using server::rep::View;
using world::ProgressError;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterNone = "dddddddd-0000-4000-8000-00000000000d";
constexpr float kHeight = 50.0F;
/** Largest number of data bytes of one carrier message (protocol notes 3.2). */
constexpr std::size_t kCarrierMessage = 1024;
/** Ids of the examples of the research (skills.md 3.4): two nodes of the sword tree of the game. */
constexpr std::uint32_t kWhirlingBlade = 0x0ED6B966;
constexpr std::uint32_t kDamageOnHeavy = 0xF6E3B771;

struct TwoPlayers {
    Player alpha;
    Player beta;
    explicit TwoPlayers(state::Store& store)
        : alpha(add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight})),
          beta(add_player(store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight})) {}
};

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** @return The nodes of the test tables. */
const std::vector<gamedata::AbilityNode>& test_nodes() {
    static const std::vector<gamedata::AbilityNode> nodes = test_progression().nodes;
    return nodes;
}

Bytes from_hex(const std::string& text) {
    Bytes data;
    for (std::size_t index = 0; index + 1 < text.size(); index += 2) {
        data.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(index, 2), nullptr, 16)));
    }
    return data;
}

/** Reads a state with one group as a fragment. `ok` is false when the decoder does not read it to its last byte. */
Fragment decode_state(std::uint32_t slot, std::uint32_t type, const Bytes& state, bool& ok) {
    server::rep::RecordWriter record(1);
    record.fragment(slot, type, state);
    server::hub::Writer body;
    const Bytes payload = record.finish();
    body.u8(1);
    body.size64(1);
    body.u8(1);
    body.u8(0);
    body.u8(0);
    body.u8(0);
    body.size(static_cast<std::uint32_t>(payload.size()));
    body.raw(payload);
    const Bundle bundle = read_bundle(body.data());
    ok = bundle.complete && bundle.records.size() == 1 && bundle.records[0].fragments.size() == 1;
    return ok ? bundle.records[0].fragments[0] : Fragment{};
}

/** @return The skill rows of a character as text. */
std::string rows_text(state::Store& store, const std::string& characterId) {
    const state::SkillRows rows = store.list_skills(characterId);
    std::string text;
    for (const auto& rank : rows.ranks) {
        text += rank.name + ":" + std::to_string(rank.rank) + ":" + std::to_string(rank.points) + " ";
    }
    text += "|";
    // The rows are in the sequence of the node ids: the text has the names in the sequence of the letters.
    std::vector<std::string> names;
    for (const auto& node : rows.nodes) {
        names.push_back(node.name);
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        text += " " + name;
    }
    text += " |";
    for (const auto& bar : rows.bars) {
        text += " " + std::to_string(bar.slot) + ":" + bar.name;
    }
    return text;
}

/**
 * The last ranks that a client got for its own character, and its ability bars. The client
 * gives an ability state to its facet only when it is different from the last state of the
 * server (same_bars): the model does the same. `bars` is the bar that the client has: the
 * last state that its facet applied, or the bar that a test sets as the own change of the
 * client before a request.
 */
struct ClientSkills {
    std::map<std::uint32_t, WireRank> ranks;
    WireBars bars;
    /** The last ability state of the server. */
    WireBars serverBars;
    unsigned rankFragments{0};
    unsigned barFragments{0};
    /** Ability states that the facet applied. */
    unsigned barApplied{0};
    bool fault{false};
};

void follow(ClientSkills& state, const FakeClient& client, std::size_t from) {
    const SlotUpdates ranks = slot_updates(client, kCategoricalSlot, from);
    for (const Fragment& fragment : ranks.fragments) {
        const WireRanks wire = ranks_of(fragment);
        state.fault = state.fault || !wire.ok;
        // The client makes its map again from each fragment.
        state.ranks.clear();
        for (const WireRank& entry : wire.entries) {
            state.ranks[entry.id] = entry;
        }
        ++state.rankFragments;
    }
    const SlotUpdates bars = slot_updates(client, kAbilitySlot, from);
    for (const Fragment& fragment : bars.fragments) {
        const WireBars wire = bars_of(fragment);
        state.fault = state.fault || !wire.ok;
        ++state.barFragments;
        if (same_bars(wire, state.serverBars)) {
            continue; // The state merge: no change, thus no call of the facet.
        }
        state.serverBars = wire;
        state.bars = wire;
        ++state.barApplied;
    }
    state.fault = state.fault || !ranks.complete || !bars.complete;
}

/** @return The abilities of the bar of a weapon in the ability state of a client, as text. */
std::string bar_text(const ClientSkills& state, std::uint16_t tableIndex, std::uint32_t table) {
    if (state.bars.tables.size() != gamedata::kAbilityTableOrder.size() || state.bars.tables[tableIndex].table != table
        || state.bars.tables[tableIndex].abilities.size() != 3) {
        return "no bar";
    }
    std::string text;
    for (const std::uint32_t ability : state.bars.tables[tableIndex].abilities) {
        const gamedata::AbilityNode* node = nullptr;
        for (const gamedata::AbilityNode& candidate : test_nodes()) {
            if (node_id(candidate.id.c_str()) == ability) {
                node = &candidate;
            }
        }
        text += (ability == 0 ? std::string("-") : node != nullptr ? node->id.substr(node->id.rfind('_') + 1) : std::string("?")) + " ";
    }
    return text;
}

/** K1: the wire bytes of the three skill states. */
void wire_bytes(const Options&) {
    using namespace server::rep;
    ManualClock clock;
    bool ok = false;

    // skills.md 9.1, fragment A: sword mastery rank 4 and mining rank 50. The backend writes the
    // entries in the sequence of their ids, thus mining (25DD6A56) is before the sword (E8EC5716).
    world::SkillSet skills;
    skills.progressions[kSword] = {4, 0};
    skills.progressions[kMining] = {50, 0};
    const Bytes ranks = encode_categorical(skills, 1);
    const std::string exampleA = "010700010102e8ec571625dd6a56000101020004003200010102000000000000" "00000000000000000000";
    const WireRanks mine = ranks_of(decode_state(kCategoricalSlot, kTypeCategoricalState, ranks, ok));
    bool exampleOk = false;
    const WireRanks research = ranks_of(decode_state(kCategoricalSlot, kTypeCategoricalState, from_hex(exampleA), exampleOk));
    check(hex_text(ranks) == "0107" "00010102" "25dd6a56e8ec5716" "00010102" "00320004" "00010102" "0000000000000000" "0000000000000000"
              && ranks.size() == 42 && ok && exampleOk && mine.ok && research.ok && mine.sequence == 1 && mine.entries.size() == 2
              && mine.entries[1] == research.entries[0] && mine.entries[0] == research.entries[1]
              && mine.entries[1] == WireRank{kSword, 4, 0} && mine.entries[0] == WireRank{kMining, 50, 0},
          "slot 65: the state for sword mastery rank 4 and mining rank 50 has the 42 bytes of the example of the research "
          "with its two entries in the other sequence (the client keeps them in a map), and three lists of the same length");
    skills.progressions[kSword] = {9, 0};
    skills.progressions[kMining] = {100, 0};
    check(hex_text(encode_categorical(skills, 2))
              == "0107" "00010202" "25dd6a56e8ec5716" "00010202" "00640009" "00010202" "0000000000000000" "0000000000000000",
          "an update has the sequence number of its change (2) in each list: the example U1 of the research");
    skills.progressions[kMining] = {0, 0};
    skills.progressions[kSword] = {0, 7};
    const WireRanks partial = ranks_of(decode_state(kCategoricalSlot, kTypeCategoricalState, encode_categorical(skills, 3), ok));
    check(ok && partial.entries.size() == 1 && partial.entries[0] == WireRank{kSword, 0, 7},
          "an entry at the start (rank 0, no experience) is not in the lists; an entry with experience is");

    // skills.md 9.2, fragment B: two nodes of the sword tree, and 2 free points at rank 4.
    world::SkillSet tree;
    tree.progressions[kSword] = {4, 0};
    tree.pools[kSword][kWhirlingBlade].level = 1;
    tree.pools[kSword][kDamageOnHeavy].level = 1;
    Bytes points = encode_points(tree, 1, 0);
    check(hex_text(points) == "010f00010101e8ec5716000101010002000101010100010102e8ec57160ed6b9" "660001e8ec5716f6e3b7710001"
              && points.size() == 45 && tree.free_points(kSword) == 2,
          "slot 67: the state for two nodes of the sword tree with 2 free points is the 45 bytes of the example of the "
          "research (the pool, its free points, the category 1, and the two nodes with the level 1)");
    tree.progressions[kSword] = {9, 0};
    points = encode_points(tree, 2, 1);
    check(hex_text(points) == "010f00010201e8ec5716000102010007000102010100010202e8ec57160ed6b9" "660001e8ec5716f6e3b7710001",
          "rank 9 gives 7 free points: the example U2 of the research");
    // A reset at rank 4: the nodes go out one time with the level 0.
    tree.progressions[kSword] = {4, 0};
    tree.pools[kSword][kWhirlingBlade] = {0, 5};
    tree.pools[kSword][kDamageOnHeavy] = {0, 5};
    points = encode_points(tree, 3, 4);
    const Bytes later = encode_points(tree, 3, 5);
    const Bytes fresh = encode_points(tree, 3, 0);
    check(hex_text(points) == "010f00010301e8ec5716000103010004000103010100010302e8ec57160ed6b9" "660000e8ec5716f6e3b7710000"
              && hex_text(later) == "010f00010301e8ec57160001030100040001030101" "00010300" && fresh == later,
          "after a reset (the example R of the research): a session that had the nodes gets them with the level 0 and "
          "the 4 free points; a session whose last record is newer, and a session with no record, get no node");

    // skills.md 9.3, fragment C in its correct form: one element for each ability table of the client.
    world::SkillSet bars;
    bars.bars[kSword] = {kWhirlingBlade, 0, 0};
    const Bytes abilities = encode_ability_bars(bars);
    const WireBars wire = bars_of(decode_state(kAbilitySlot, kTypeAbilityState, abilities, ok));
    const std::string element = hex_text(Bytes(abilities.begin() + 3 + 27 * 29, abilities.begin() + 3 + 28 * 29));
    bool order = wire.tables.size() == gamedata::kAbilityTableOrder.size();
    for (std::size_t index = 0; order && index < wire.tables.size(); ++index) {
        order = wire.tables[index].table == gamedata::kAbilityTableOrder[index] && wire.tables[index].abilities.size() == 3
                && wire.tables[index].buttons == std::vector<std::uint32_t>{0x192, 0x73, 0x8B}
                && (index == kSwordIndex || wire.tables[index].abilities == std::vector<std::uint32_t>{0, 0, 0});
    }
    check(abilities.size() == 845 && hex_text(abilities).substr(0, 6) == "01011d" && abilities.back() == 0 && ok && wire.ok && order
              && wire.versions == 0 && element == "e8ec5716" "03" "0ed6b966" "00000192" "00000000" "00000073" "00000000" "0000008b"
              && hex_text(Bytes(abilities.begin() + 3, abilities.begin() + 3 + 29))
                     == "2915fb5d" "03" "00000000" "00000192" "00000000" "00000073" "00000000" "0000008b",
          "slot 46: 29 elements, one for each ability table of the client in the sequence of the client, each one with "
          "its table id, three places, and the three input numbers of the client; the sword is element 27 and has the "
          "ability in its first place; no table version (845 bytes)");

    // The three states are for the owner only.
    world::Entity entity;
    entity.skills = tree;
    entity.skills.bars = bars.bars;
    PartsRequest request;
    request.budget = 900;
    check(encode_categorical_state(entity, View::observer, clock).empty() && encode_points_state(entity, View::observer, request).empty()
              && encode_ability_state(entity, View::observer, request).empty()
              && !encode_categorical_state(entity, View::owner, clock).empty() && encode_points_state(entity, View::owner, request).size() == 1
              && encode_ability_state(entity, View::owner, request).size() == 1,
          "observer view: no fragment of the three skill states (the remote player slice has no facet for them)");
    entity.skills.bars.clear();
    request.since = 0;
    const bool none = encode_ability_state(entity, View::owner, request).empty();
    request.since = 7;
    check(none && encode_ability_state(entity, View::owner, request).size() == 1,
          "a character with no ability on a bar: no ability state in the first record of a session (the client has its "
          "own empty bars), and the full state with 29 elements for a later change");
}

/** K2: the rank and the experience of a weapon mastery and of a trade skill. */
void ranks_and_experience(const Options&) {
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Skills& skills = host.world.skills();
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    ClientSkills client;
    ClientTrees trees;
    std::size_t from = 0;
    follow(client, a, from);
    follow_trees(trees, a, from);
    check(client.rankFragments == 1 && client.ranks.empty() && trees.pools.size() == 2 && trees.free(kSword) == 0 && trees.free(kBow) == 0
              && client.barFragments == 0 && !client.fault && !trees.fault,
          "a character with no skill row: no entry in the mastery lists, one pool with no point for each weapon of the "
          "tables, and no ability state");

    world::SkillResult result = skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 4, 25);
    host.steps(2, {&a});
    follow(client, a, from);
    follow_trees(trees, a, from);
    check(result.ok() && result.inWorld && result.rank == 4 && result.points == 25 && result.freePoints == 4
              && client.ranks.at(kSword) == WireRank{kSword, 4, 25} && trees.free(kSword) == 4 && trees.free(kBow) == 0
              && rows_text(host.store, kCharacterA) == "SwordAbilityTable:4:25 | |",
          "sword mastery rank 4 (level 5 of the screen) with 25 experience: the client gets the entry and 4 free points "
          "in the pool of the sword (one point for each rank)");
    result = skills.add_experience(kCharacterA, "swordabilitytable", 30);
    host.steps(2, {&a});
    follow(client, a, from);
    follow_trees(trees, a, from);
    check(result.ok() && result.rank == 5 && result.points == 5 && result.freePoints == 5 && client.ranks.at(kSword) == WireRank{kSword, 5, 5}
              && trees.free(kSword) == 5,
          "30 experience at rank 4 with 25 of 50: the rank goes up, the rest (5) is experience of the new rank, and the "
          "weapon has one more point");
    result = skills.add_experience(kCharacterA, "SwordAbilityTable", 100000);
    check(result.ok() && result.rank == 14 && result.points == 0 && result.freePoints == 14,
          "experience above the last limit: the largest rank (14), with no experience in it");
    result = skills.set_rank(kCharacterA, "Mining", ProgressionKind::tradeSkill, 3, 50);
    const world::SkillResult gained = skills.add_experience(kCharacterA, "Mining", 250);
    host.steps(2, {&a});
    follow(client, a, from);
    check(result.ok() && result.rank == 3 && result.freePoints == 0 && gained.ok() && gained.rank == 5 && gained.points == 0
              && client.ranks.at(kMining) == WireRank{kMining, 5, 0} && client.ranks.size() == 2,
          "mining rank 3 with 50 experience, then 250 experience: two ranks more to the largest rank of the trade skill "
          "(5); a trade skill has no points");

    // A lower rank with fewer points than the character spent: the tree is reset.
    skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 5);
    for (const char* node : {"Test_Sword_Slash", "Test_Sword_P0a", "Test_Sword_Stab"}) {
        skills.assign(kCharacterA, kSword, node_id(node), 1);
    }
    skills.set_bar(kCharacterA, kSword, 0, node_id("Test_Sword_Slash"));
    host.steps(2, {&a});
    follow(client, a, from);
    follow_trees(trees, a, from);
    const bool spent = trees.count(kSword) == 3 && trees.free(kSword) == 2 && bar_text(client, kSwordIndex, kSword) == "Slash - - ";
    result = skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 2);
    host.steps(2, {&a});
    follow(client, a, from);
    follow_trees(trees, a, from);
    check(spent && result.ok() && result.freePoints == 2 && alpha->skills.spent_points(kSword) == 0 && trees.count(kSword) == 0
              && trees.free(kSword) == 2 && bar_text(client, kSwordIndex, kSword) == "- - - "
              && rows_text(host.store, kCharacterA) == "Mining:5:0 SwordAbilityTable:2:0 | |",
          "a weapon with 3 spent points gets rank 2: its tree is reset (the client gets each node with the level 0, 2 "
          "free points, and an empty bar), and the rows have no node");

    const std::string before = rows_text(host.store, kCharacterA);
    const world::Revision revision = host.world.revision();
    check(skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 15).error == ProgressError::badRank
              && skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 4, 50).error == ProgressError::badRank
              && skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 14, 1).error == ProgressError::badRank
              && skills.set_rank(kCharacterA, "Riding", ProgressionKind::tradeSkill, 1).error == ProgressError::badRank
              && skills.set_rank(kCharacterA, "NoSuchWeapon", ProgressionKind::weapon, 1).error == ProgressError::unknownProgression
              && skills.set_rank(kCharacterA, "Mining", ProgressionKind::weapon, 1).error == ProgressError::wrongKind
              && skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::tradeSkill, 1).error == ProgressError::wrongKind
              && skills.add_experience(kCharacterA, "NoSuchSkill", 10).error == ProgressError::unknownProgression
              && skills.add_experience(kCharacterA, "Mining", 0).error == ProgressError::badAmount
              && skills.set_rank(kCharacterNone, "Mining", ProgressionKind::tradeSkill, 1).error == ProgressError::unknownCharacter
              && rows_text(host.store, kCharacterA) == before && host.world.revision() == revision,
          "refused, with no change of a row or of a state: a rank above the largest one, experience that is not below "
          "the experience of the rank, a trade skill whose largest rank is 0, an id that is not known, a trade skill id "
          "for a weapon command and a weapon id for a trade skill command, an amount of 0, and a character that is not "
          "known");

    ServerHost bare;
    const TwoPlayers others(bare.store);
    check(bare.world.skills().set_rank(kCharacterA, "Mining", ProgressionKind::tradeSkill, 1).error == ProgressError::noTables
              && bare.world.skills().assign(kCharacterA, kSword, node_id("Test_Sword_Slash"), 1).error == ProgressError::noTables,
          "with no progression tables the backend refuses a rank and a mastery point");
}

/** K3: the requests of a client for its weapon trees and its ability bars. */
void client_requests(const Options&) {
    using namespace server::rep;
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    world::Skills& skills = host.world.skills();
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 9);
    skills.set_rank(kCharacterB, "SwordAbilityTable", ProgressionKind::weapon, 9);
    host.steps(2, {&a, &b});
    ClientSkills client;
    ClientTrees trees;
    std::size_t from = 0;
    const auto send = [&](FakeClient& sender, const world::Entity* route, std::uint32_t type, const Bytes& body) {
        host.datagram(sender.peer, sender.component_message(type, route->actor.bytes, body));
        host.steps(2, {&a, &b});
    };
    const auto update = [&] {
        follow(client, a, from);
        follow_trees(trees, a, from);
    };
    const auto assign = [&](const char* node, std::uint32_t pool = kSword, std::uint32_t count = 1) {
        send(a, alpha, kTypeAssignPoolPoints, assign_body(alpha->id, pool, node_id(node), count));
        update();
    };
    const auto last_log = [](const std::string& text) { return log_count(text) != 0; };
    update();

    const std::uint32_t liveNode = 0x6C2E4693;
    check(assign_body(alpha->id, kSword, liveNode).size() == 28 && hex_text(assign_body(alpha->id, kSword, liveNode)).substr(32) == "e8ec5716" "6c2e4693" "00000001"
              && bar_body(alpha->id, kSwordIndex, 0, liveNode).size() == 26 && hex_text(bar_body(alpha->id, kSwordIndex, 0, liveNode)).substr(32) == "001b" "00000000" "6c2e4693"
              && plain_body(alpha->id, kAbilitySlot).size() == 16 && tree_reset_body(alpha->id, kSwordIndex).size() == 19
              && read_assign_request(assign_body(alpha->id, kSword, liveNode))->node == liveNode
              && read_bar_request(bar_body(alpha->id, kSwordIndex, 0, liveNode))->tableIndex == 27
              && read_tree_reset_request(tree_reset_body(alpha->id, kSwordIndex))->tableIndex == 27,
          "the bodies of the tests are the bodies of the live client: a mastery point (28 bytes: pool, node, count), an "
          "ability for a place of the bar (26 bytes: table index 27 for the sword, place, ability), apply (16 bytes), and "
          "a tree reset (19 bytes)");

    // A mastery point (type 1112) with the conditions of a node.
    assign("Test_Sword_P0a");
    check(trees.has(kSword, node_id("Test_Sword_P0a")) && trees.free(kSword) == 8 && alpha->skills.free_points(kSword) == 8
              && rows_text(host.store, kCharacterA) == "SwordAbilityTable:9:0 | Test_Sword_P0a |" && !trees.fault
              && last_log("rep: mastery point request (type 1112, pool E8EC5716, node " )
              && log_count("1 point(s)): done, 8 free point(s) [s1 aaaaaaaa]") == 1,
          "mastery point request with a correct body: the node is in the tree (row and client), 8 of the 9 points are "
          "free, and the log has the line");
    unsigned fragments = trees.fragments;
    assign("Test_Sword_SlashUp");
    check(!trees.has(kSword, node_id("Test_Sword_SlashUp")) && trees.free(kSword) == 8 && trees.fragments == fragments + 1
              && alpha->skills.spent_points(kSword) == 1 && log_count("a condition of the node is not met, 8 free point(s)") == 1,
          "a node whose needed node the character does not have (the upgrade of an ability with no ability): refused, and "
          "the client gets the node with the level 0 and the free points of the server again");
    assign("Test_Sword_Slash");
    assign("Test_Sword_SlashUp");
    check(trees.has(kSword, node_id("Test_Sword_Slash")) && trees.has(kSword, node_id("Test_Sword_SlashUp")) && trees.free(kSword) == 6,
          "with the ability, its upgrade in the next row is permitted");
    assign("Test_Sword_D1a");
    assign("Test_Sword_P3a");
    assign("Test_Sword_Ultimate");
    check(trees.count(kSword) == 3 && trees.free(kSword) == 6 && log_count("a condition of the node is not met, 6 free point(s)") == 3,
          "refused: a node of row 1 of the second tree with no point in row 0 of that tree, a node of row 3 with no "
          "point in row 2, and the node of the last row with fewer than 10 points in its tree");
    assign("Test_Sword_Slash");
    assign("Test_Bow_Shot");
    assign("Test_Bow_Shot", kBow);
    assign("Test_Sword_P0b", kSword, 2);
    send(a, alpha, kTypeAssignPoolPoints, assign_body(alpha->id, kSword, 0x12345678));
    send(a, alpha, kTypeAssignPoolPoints, assign_body(alpha->id, 0xDEADBEEF, node_id("Test_Sword_P0b")));
    send(a, alpha, kTypeAssignPoolPoints, assign_body(alpha->id, kMining, node_id("Test_Sword_P0b")));
    update();
    check(trees.count(kSword) == 3 && trees.free(kSword) == 6 && trees.count(kBow) == 0 && trees.free(kBow) == 0 && !trees.fault
              && log_count("the character has the node already") == 2 && log_count("the node is not a node of the tree of this weapon") == 4
              && log_count("the request spends more points than the character has, 0 free point(s)") == 1
              && log_count("the amount is not correct, 6 free point(s)") == 1
              && log_count("the game data has no weapon mastery and no trade skill with this id") == 2
              && log_count("the id is not of the kind that the request needs") == 2
              && rows_text(host.store, kCharacterA) == "SwordAbilityTable:9:0 | Test_Sword_P0a Test_Sword_Slash Test_Sword_SlashUp |",
          "refused: a node that the character has, a node of the bow in the pool of the sword, a node of a weapon with no "
          "free point, 2 points for one node, a node id that no tree has, a pool that is not known, and the id of a trade "
          "skill as the pool; the rows have the three nodes");
    Bytes manyPoints = assign_body(alpha->id, kSword, node_id("Test_Sword_P0b"), 4097);
    Bytes longAssign = assign_body(alpha->id, kSword, node_id("Test_Sword_P0b"));
    longAssign.push_back(0);
    fragments = trees.fragments;
    send(a, alpha, kTypeAssignPoolPoints, manyPoints);
    send(a, alpha, kTypeAssignPoolPoints, longAssign);
    send(a, alpha, kTypeAssignPoolPoints, assign_body(beta->id, kSword, node_id("Test_Sword_P0b")));
    update();
    check(trees.fragments == fragments && alpha->skills.spent_points(kSword) == 3 && beta->skills.spent_points(kSword) == 0
              && log_count("rep: mastery point request (type 1112, 28 bytes) does not have the form of the request: no change") == 1
              && log_count("rep: mastery point request (type 1112, 29 bytes) does not have the form of the request: no change") == 1
              && log_count("rep: mastery point request (type 1112) names the entity") == 1,
          "a body with a count above 4,096 or with one byte more is not the request, and a request that names the entity "
          "of B changes nothing for A and for B: no state goes out");

    // The ability bar (type 2701) and the end of a selection (type 2857).
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 0, node_id("Test_Sword_Slash")));
    send(a, alpha, kTypeApplyAbilityPoints, plain_body(alpha->id, kAbilitySlot));
    update();
    check(bar_text(client, kSwordIndex, kSword) == "Slash - - " && client.barFragments == 1 && !client.fault
              && rows_text(host.store, kCharacterA) == "SwordAbilityTable:9:0 | Test_Sword_P0a Test_Sword_Slash Test_Sword_SlashUp | 0:Test_Sword_Slash"
              && log_count("rep: ability bar request (type 2701, table index 27, place 0, ability ") == 1
              && log_count("rep: apply ability points request (type 2857): the points of the selection are in the tree [s1 aaaaaaaa]") == 1,
          "ability bar request with a correct body: the ability is in the first place of the bar of the sword (row and "
          "client: the ability state has 29 elements), and the apply request has its log line");
    assign("Test_Sword_Stab");
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 1, node_id("Test_Sword_Stab")));
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 2, node_id("Test_Sword_Slash")));
    update();
    check(bar_text(client, kSwordIndex, kSword) == "- Stab Slash ", "an ability that goes to a different place leaves the place that it had");
    unsigned barFragments = client.barFragments;
    const unsigned barApplied = client.barApplied;
    // The client changes its own bar before it sends a request: the model has that bar, and
    // only a state that the facet applies puts the bar of the server back (review finding 3).
    client.bars.tables[kSwordIndex].abilities[0] = node_id("Test_Sword_P0a");
    const bool predicted = bar_text(client, kSwordIndex, kSword) == "P0a Stab Slash ";
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 0, node_id("Test_Sword_P0a")));
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 0, node_id("Test_Sword_Leap")));
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 0, node_id("Test_Bow_Shot")));
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, 29, 0, node_id("Test_Sword_Slash")));
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, 0, 0, node_id("Test_Sword_Slash")));
    update();
    check(predicted && bar_text(client, kSwordIndex, kSword) == "- Stab Slash " && client.barFragments == barFragments + 8
              && client.barApplied == barApplied + 8 && same_bars(client.bars, client.serverBars)
              && client.bars.tables[kSwordIndex].buttons == std::vector<std::uint32_t>{0x192, 0x73, 0x8B} && alpha->skills.barsRefused != 0
              && log_count("the ability is not an active ability of the tree of this weapon") == 4
              && log_count("the character does not have the node of the ability") == 2
              && log_count("rep: ability bar request (type 2701, table index 29, place 0") == 1
              && log_count("rep: ability bar request (type 2701, table index 0, place 0") == 1,
          "refused: a node that is not an active ability, an active ability that the character does not have, an ability "
          "of the bow for the bar of the sword and the index of a table that is not a weapon of the game data, and a "
          "table index that the list of the client does not have. After each refusal the client gets the bar of the "
          "server in two states, first with the place numbers as the button numbers and then with the numbers of the "
          "client: each one is different from the state before it, thus the facet of the client applies it and the own "
          "bar of the client goes away");
    Bytes badSlot = bar_body(alpha->id, kSwordIndex, 3, node_id("Test_Sword_Slash"));
    send(a, alpha, kTypeChangeMappedAbility, badSlot);
    send(a, alpha, kTypeChangeMappedAbility, bar_body(beta->id, kSwordIndex, 0, node_id("Test_Sword_Slash")));
    send(a, alpha, kTypeChangeMappedAbility, bar_body(alpha->id, kSwordIndex, 2, 0));
    update();
    check(bar_text(client, kSwordIndex, kSword) == "- Stab - " && beta->skills.bars.empty()
              && log_count("rep: ability bar request (type 2701, 26 bytes) does not have the form of the request: no change") == 1
              && log_count("rep: ability bar request (type 2701) names the entity") == 1,
          "a body with the place 3 is not the request, a request that names the entity of B changes nothing, and a "
          "request with the ability 0 makes a place empty");

    // The tree reset (type 186) and its answer (type 2858).
    std::size_t replies = messages_of(a, kTypeMasteryTreeRespecResponse).size();
    send(a, alpha, kTypeMasteryTreeRespec, tree_reset_body(alpha->id, kSwordIndex));
    update();
    std::vector<Bytes> answers = messages_of(a, kTypeMasteryTreeRespecResponse);
    server::hub::Writer expected;
    expected.u64(target_of(alpha->id, kAbilitySlot));
    expected.u64(alpha->id);
    check(answers.size() == replies + 1 && answers.back().size() == 21 && hex_text(answers.back()) == hex_text(expected.data()) + "01" "0000001b"
              && trees.count(kSword) == 0 && trees.free(kSword) == 9 && bar_text(client, kSwordIndex, kSword) == "- - - "
              && rows_text(host.store, kCharacterA) == "SwordAbilityTable:9:0 | |"
              && log_count("rep: tree reset request (type 186, table index 27): done: message \"mastery tree respec response\" (type 2858, success 1) sent") == 1,
          "tree reset request with a correct body: the client gets the answer (type 2858: the target and the entity of "
          "the request, success 1, the table index 27 as 32 bits), then each node with the level 0, all 9 points, and an "
          "empty bar; the rows have no node and no bar");
    assign("Test_Sword_P0a");
    skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 10);
    host.steps(2, {&a, &b});
    update();
    replies = messages_of(a, kTypeMasteryTreeRespecResponse).size();
    send(a, alpha, kTypeMasteryTreeRespec, tree_reset_body(alpha->id, kSwordIndex));
    send(a, alpha, kTypeMasteryTreeRespec, tree_reset_body(alpha->id, 29));
    update();
    answers = messages_of(a, kTypeMasteryTreeRespecResponse);
    check(answers.size() == replies + 2 && hex_text(answers[replies]).substr(32) == "00" "0000001b" && hex_text(answers.back()).substr(32) == "00" "0000001d"
              && trees.has(kSword, node_id("Test_Sword_P0a")) && trees.free(kSword) == 9
              && log_count("the reset has a cost in a currency that the backend does not have") == 2,
          "a reset at rank 10, which has a cost in a currency that the backend does not have, and a reset with a table "
          "index that is not known: the answer has success 0 and the table index of the request, and nothing changes");
    replies = answers.size();
    send(a, alpha, kTypeMasteryTreeRespec, tree_reset_body(alpha->id, kSwordIndex, 2));
    send(a, alpha, kTypeMasteryTreeRespec, tree_reset_body(beta->id, kSwordIndex));
    Bytes longApply = plain_body(alpha->id, kAbilitySlot);
    longApply.push_back(0);
    send(a, alpha, kTypeApplyAbilityPoints, longApply);
    check(messages_of(a, kTypeMasteryTreeRespecResponse).size() == replies && messages_of(b, kTypeMasteryTreeRespecResponse).empty()
              && alpha->skills.spent_points(kSword) == 1
              && log_count("rep: tree reset request (type 186, 19 bytes) does not have the form of the request: no change") == 1
              && log_count("rep: tree reset request (type 186) names the entity") == 1
              && log_count("rep: apply ability points request (type 2857, 17 bytes) does not have the form of the request") == 1,
          "a reset body with a flag byte that is not 0 or 1 is not the request, and a reset that names the entity of B "
          "gets no answer; an apply body with one byte more is not the request");
}

/** K4: two sessions. */
void two_sessions(const Options&) {
    using namespace server::rep;
    ServerHost host(observing());
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
    host.datagram(b.peer, b.input_at(1, 102.0F, 200.0F, kHeight));
    host.steps(60, {&a, &b});
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    check(host.session(1).observer().known().size() == 2 && host.session(2).observer().known().size() == 2,
          "each of the two sessions observes the entity of the other character");
    const std::size_t fromA = a.got.size();
    const std::size_t fromB = b.got.size();
    const std::string rowsB = rows_text(host.store, kCharacterB);
    host.world.skills().set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 6);
    host.world.skills().set_rank(kCharacterA, "Mining", ProgressionKind::tradeSkill, 4);
    host.steps(2, {&a, &b});
    for (const Bytes& body : {assign_body(alpha->id, kSword, node_id("Test_Sword_Slash")),
                              bar_body(alpha->id, kSwordIndex, 0, node_id("Test_Sword_Slash"))}) {
        host.datagram(a.peer, a.component_message(body.size() == 28 ? kTypeAssignPoolPoints : kTypeChangeMappedAbility, alpha->actor.bytes, body));
        host.steps(2, {&a, &b});
    }
    ClientSkills own;
    ClientTrees trees;
    std::size_t from = fromA;
    follow(own, a, from);
    follow_trees(trees, a, from);
    check(own.ranks.at(kSword).rank == 6 && own.ranks.at(kMining).rank == 4 && trees.has(kSword, node_id("Test_Sword_Slash"))
              && trees.free(kSword) == 5 && bar_text(own, kSwordIndex, kSword) == "Slash - - " && !own.fault && !trees.fault,
          "A gets a mastery rank and a trade skill rank, unlocks a node, and puts it on its bar: its own session gets the "
          "three skill states");
    bool nothing = true;
    for (const std::uint32_t slot : {kCategoricalSlot, kPointsSlot, kAbilitySlot}) {
        nothing = nothing && slot_updates(b, slot, 0, 2).fragments.empty() && slot_updates(b, slot, fromB, 1).fragments.empty();
    }
    check(nothing && beta->skills.progressions.empty() && beta->skills.spent_points(kSword) == 0 && beta->skills.bars.empty()
              && rows_text(host.store, kCharacterB) == rowsB && slot_updates(b, kProgressionSlot, 0, 2).complete,
          "session B gets no fragment of the three skill states of A (owner only), and the change of A changes nothing "
          "for B: its entity, its rows, and its own states stay");
}

struct Sink : world::SessionSink {
    void character_lost(world::LostCause) override {}
};

/** K5: the persistence. */
void persistence(const Options& options) {
    {
        ServerHost host;
        set_progression(host);
        const TwoPlayers players(host.store);
        FakeClient a(27000);
        host.enter(a, players.alpha.registration());
        world::Skills& skills = host.world.skills();
        skills.set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 4, 25);
        skills.set_rank(kCharacterA, "Mining", ProgressionKind::tradeSkill, 3, 10);
        skills.assign(kCharacterA, kSword, node_id("Test_Sword_Bash"), 1);
        skills.assign(kCharacterA, kSword, node_id("Test_Sword_Slash"), 1);
        skills.set_bar(kCharacterA, kSword, 1, node_id("Test_Sword_Bash"));
        // A command for a character that is not in the world changes its rows.
        const world::SkillResult stored = skills.set_rank(kCharacterB, "BowAbilityTable", ProgressionKind::weapon, 2);
        host.steps(2, {&a});
        host.datagram(a.peer, a.carrier_close());
        host.steps(2, {&a});
        check(host.world.entities().empty()
                  && rows_text(host.store, kCharacterA) == "Mining:3:10 SwordAbilityTable:4:25 | Test_Sword_Bash Test_Sword_Slash | 1:Test_Sword_Bash"
                  && stored.ok() && !stored.inWorld && rows_text(host.store, kCharacterB) == "BowAbilityTable:2:0 | |",
              "the character leaves the world: its rows have the two ranks with their experience, the two nodes, and the "
              "bar; a command for a character that is not in the world changed only its rows");

        FakeClient again(27002);
        host.enter(again, players.alpha.registration());
        ClientSkills client;
        ClientTrees trees;
        std::size_t from = 0;
        follow(client, again, from);
        follow_trees(trees, again, from);
        const SlotUpdates bars = slot_updates(again, kAbilitySlot);
        check(client.ranks.at(kSword) == WireRank{kSword, 4, 25} && client.ranks.at(kMining) == WireRank{kMining, 3, 10}
                  && trees.count(kSword) == 2 && trees.has(kSword, node_id("Test_Sword_Bash")) && trees.free(kSword) == 2
                  && bar_text(client, kSwordIndex, kSword) == "- Bash - " && client.barFragments == 1 && !client.fault && !trees.fault
                  && bars.largestFrame <= kCarrierMessage
                  && log_count("has 2 weapon masteries and trade skills above the start, 2 node(s) of the weapon trees, and 1 ability bar(s)") == 1,
              "a new session of the character gets the stored ranks, nodes, and bar in its first record (the ability "
              "state is in a bundle of its own, below the size of one carrier message), and the log has the counts");

        ManualClock clock;
        world::World second(clock, host.store, host.tables, ServerHost::with_game_data(host.catalog, host.progression));
        Sink sink;
        const world::EnterResult entered = second.enter(1, sink, kCharacterB, players.beta.account.personaId);
        check(entered.error == world::EnterError::none && entered.entity->skills.rank(kBow) == 2 && entered.entity->skills.free_points(kBow) == 2
                  && entered.entity->skills.pools.size() == 2,
              "a new world object on the same database has the rank that the command gave to the character that was not "
              "in the world");
    }

    // The rows of a change are one transaction: a write that fails changes nothing.
    const std::filesystem::path path = options.workDirectory / "skills-rollback.sqlite3";
    std::filesystem::remove(path);
    state::Store store;
    const bool opened = store.open(path.string());
    const Player player = add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    ManualClock clock;
    gamedata::AppearanceTables tables;
    gamedata::ProgressionTables progression;
    progression.set(test_progression());
    world::WorldOptions worldOptions;
    worldOptions.progressionTables = &progression;
    world::World world(clock, store, tables, worldOptions);
    Sink sink;
    const bool entered = world.enter(1, sink, kCharacterA, player.account.personaId).error == world::EnterError::none;
    const bool ranked = world.skills().set_rank(kCharacterA, "SwordAbilityTable", ProgressionKind::weapon, 5).ok()
                        && world.skills().assign(kCharacterA, kSword, node_id("Test_Sword_Slash"), 1).ok()
                        && world.skills().set_bar(kCharacterA, kSword, 0, node_id("Test_Sword_Slash")).ok();
    sqlite3* connection = nullptr;
    sqlite3_open(path.string().c_str(), &connection);
    const std::string sql = "CREATE TRIGGER refuse_node BEFORE INSERT ON ability_nodes WHEN NEW.node_id="
                            + std::to_string(node_id("Test_Sword_P0a")) + " BEGIN SELECT RAISE(FAIL, 'test write failure'); END";
    const bool trigger = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    check(opened && entered && ranked && trigger, "a database file with a character that has sword rank 5, one node, and one ability on its bar");
    const world::Entity* entity = world.find_character(kCharacterA);
    const world::Revision before = world.revision();
    const std::string rows = rows_text(store, kCharacterA);
    const world::SkillResult failed = world.skills().assign(kCharacterA, kSword, node_id("Test_Sword_P0a"), 1);
    check(failed.error == ProgressError::storeFailed && rows_text(store, kCharacterA) == rows
              && rows == "SwordAbilityTable:5:0 | Test_Sword_Slash | 0:Test_Sword_Slash" && entity->skills.spent_points(kSword) == 1
              && entity->skills.free_points(kSword) == 4,
          "a mastery point whose write fails: the rows of the rank, of the node, and of the bar stay as they were, and "
          "the entity keeps its points");
    check(world.revision() > before && world.skills().assign(kCharacterA, kSword, node_id("Test_Sword_P0b"), 1).ok()
              && rows_text(store, kCharacterA) == "SwordAbilityTable:5:0 | Test_Sword_P0b Test_Sword_Slash | 0:Test_Sword_Slash",
          "the session gets the state of the server again after the refusal, and a later change that the database accepts works");
}

/** K6: a large weapon tree is one fragment (review finding 2 of stage 1.6). */
void large_tree(const Options&) {
    using namespace server::rep;
    // 16 weapons with 19 nodes each: the largest tree set of a character of the game (304 nodes).
    world::SkillSet skills;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> wanted;
    for (std::uint32_t weapon = 0; weapon < 16; ++weapon) {
        const std::uint32_t pool = 0x10000000 + weapon;
        skills.progressions[pool] = {19, 0};
        for (std::uint32_t node = 0; node < 19; ++node) {
            skills.pools[pool][0x20000000 + weapon * 64 + node].level = 1;
            wanted.emplace_back(pool, 0x20000000 + weapon * 64 + node);
        }
    }
    const Bytes state = encode_points(skills, 9, 0);
    bool ok = false;
    const Fragment fragment = decode_state(kPointsSlot, kTypePointsState, state, ok);
    const WirePools wire = pools_of(fragment);
    ClientTrees trees;
    trees.apply(fragment);
    bool each = true;
    for (const auto& [pool, node] : wanted) {
        each = each && trees.has(pool, node);
    }
    check(ok && wire.ok && wire.pools.size() == 16 && wire.nodes.size() == 304 && state.size() > 3000 && each && !trees.fault
              && trees.refused == 0 && wire.entryStamp == 9 && wire.poolStamp == 9,
          "16 weapons with 19 nodes each: one state of more than 3,000 bytes with each pool and each of the 304 nodes; a "
          "client that applies it has each node");
    // The model of the client replaces its node list with the list of a state, as the live
    // client does. A state with a part of the nodes makes the other nodes go away: this is why
    // the encoder does not cut the state.
    world::SkillSet part = skills;
    for (auto& [pool, nodes] : part.pools) {
        if (pool != 0x10000000) {
            nodes.clear();
        }
    }
    ClientTrees cut = trees;
    cut.apply(decode_state(kPointsSlot, kTypePointsState, encode_points(part, 10, 0), ok));
    ClientTrees same = trees;
    same.apply(decode_state(kPointsSlot, kTypePointsState, encode_points(part, 9, 0), ok));
    check(cut.count(0x10000000) == 19 && cut.count(0x10000001) == 0 && cut.pools.size() == 16 && cut.refused == 0
              && same.refused == 1 && same.count(0x10000001) == 19,
          "the client model: a later state with the nodes of one pool only leaves the client with only these nodes (the "
          "list of a state replaces the list of the client), and a state whose stamp is not larger is not used");
    check(encode_points(skills, 9, 0) == state && skills.free_points(0x10000000) == 0,
          "the free points are the rank minus the spent points and not below 0");

    // In a session: a character with each node of the test tree and a bar.
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    state::SkillRows rows;
    rows.ranks.push_back({kSword, "SwordAbilityTable", 14, 0});
    for (const gamedata::AbilityNode& node : test_nodes()) {
        if (node.table == kSword) {
            rows.nodes.push_back({kSword, node_id(node.id.c_str()), node.id, 1});
        }
    }
    rows.bars.push_back({kSword, 0, node_id("Test_Sword_Slash"), "Test_Sword_Slash"});
    const bool written = host.store.write_skills(kCharacterA, rows);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    ClientTrees entered;
    std::size_t from = 0;
    follow_trees(entered, a, from);
    std::size_t largest = 0;
    bool complete = true;
    for (const Sent& sent : a.got) {
        if (sent.type == 8) {
            largest = std::max(largest, sent.frame.size());
            complete = complete && read_bundle(sent.body).complete;
        }
    }
    check(written && entered.count(kSword) == 19 && !entered.fault && entered.refused == 0 && complete
              && largest <= kCarrierMessage && slot_updates(a, kAbilitySlot).fragments.size() == 1
              && slot_updates(a, kPointsSlot).fragments.size() == 1,
          "the first record of a character with 19 nodes and a bar: one fragment of the point state, and each bundle "
          "decodes to its last byte and is below the size of one carrier message");

    // A character with more nodes than one carrier message holds: the point state is one
    // fragment in a bundle of its own, above one carrier message and below the limit of the client.
    ServerHost large;
    gamedata::ProgressionData data = test_progression();
    state::SkillRows many;
    for (std::uint32_t weapon = 0; weapon < 6; ++weapon) {
        const std::string id = "Test" + std::to_string(weapon) + "AbilityTable";
        data.progressions.push_back({id, 0, gamedata::ProgressionKind::weapon, 19, data.progressions[0].ranks});
        data.progressions.back().ranks.experience.assign(20, 10);
        data.progressions.back().ranks.resetCost.assign(20, 0);
        many.ranks.push_back({gamedata::progression_id(id), id, 19, 0});
        for (std::uint32_t node = 0; node < 19; ++node) {
            gamedata::AbilityNode entry;
            entry.id = "Test" + std::to_string(weapon) + "_Node" + std::to_string(node);
            entry.table = gamedata::progression_id(id);
            entry.row = node;
            data.nodes.push_back(entry);
            many.nodes.push_back({gamedata::progression_id(id), gamedata::progression_id(entry.id), entry.id, 1});
        }
    }
    large.progression.set(std::move(data));
    const TwoPlayers others(large.store);
    const bool manyWritten = large.store.write_skills(kCharacterA, many);
    FakeClient c(27000);
    large.enter(c, others.alpha.registration());
    ClientTrees big;
    std::size_t start = 0;
    follow_trees(big, c, start);
    const SlotUpdates pointUpdates = slot_updates(c, kPointsSlot);
    std::size_t nodeCount = 0;
    for (const auto& [pool, value] : big.pools) {
        nodeCount += value.nodes.size();
    }
    bool decodes = true;
    for (const Sent& sent : c.got) {
        if (sent.type == 8) {
            decodes = decodes && read_bundle(sent.body).complete;
        }
    }
    check(manyWritten && pointUpdates.fragments.size() == 1 && nodeCount == 114 && !big.fault && big.refused == 0 && decodes
              && pointUpdates.largestFrame > kCarrierMessage && pointUpdates.largestFrame < 256000,
          "a character with 114 nodes (6 weapons with 19 nodes): the session sends the point state as one fragment in "
          "one bundle that is larger than one carrier message, and the client has the 114 nodes");
}

/** K7: the development routes. */
void skill_routes(const Options&) {
    using nlohmann::json;
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_dev_routes(router, services);
    const auto send = [&](const char* method, const std::string& path, const std::string& query, const std::string& serviceHost = {}) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        request.host = serviceHost;
        std::string route;
        return router.dispatch(request, route);
    };
    const std::string forA = std::string("character=") + kCharacterA;
    const std::string forB = std::string("character=") + kCharacterB;

    service::HttpResponse reply = send("POST", "/v1/dev/progress/mastery", forA + "&weapon=sword&level=5&experience=20");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "world" && json::parse(reply.body)["rank"] == 4
              && json::parse(reply.body)["experience"] == 20 && json::parse(reply.body)["freePoints"] == 4
              && rows_text(host.store, kCharacterA) == "SwordAbilityTable:4:20 | |",
          "mastery route with the short name of a weapon: level 5 of the screen is rank 4, with its experience and 4 free "
          "points");
    reply = send("POST", "/v1/dev/progress/tradeskill", forA + "&skill=Mining&level=3");
    check(reply.status == 200 && json::parse(reply.body)["rank"] == 3
              && send("POST", "/v1/dev/progress/skill-experience", forA + "&id=Mining&amount=120").status == 200
              && send("POST", "/v1/dev/progress/skill-experience", forA + "&id=bow&amount=35").status == 200
              && rows_text(host.store, kCharacterA) == "Mining:4:20 BowAbilityTable:2:5 SwordAbilityTable:4:20 | |",
          "trade skill route and experience route: mining level 3 and 120 experience give level 4, and 35 experience give "
          "the bow rank 2");
    reply = send("POST", "/v1/dev/progress/mastery", forB + "&weapon=BowAbilityTable&level=3");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "stored" && rows_text(host.store, kCharacterB) == "BowAbilityTable:2:0 | |",
          "mastery route with the id of the game table, for a character that is not in the world: applied to the rows");
    host.world.skills().assign(kCharacterA, kSword, node_id("Test_Sword_Slash"), 1);
    host.world.skills().set_bar(kCharacterA, kSword, 2, node_id("Test_Sword_Slash"));
    reply = send("GET", "/v1/dev/progress", forA);
    const json list = json::parse(reply.body);
    const json sword = list["weapons"][0];
    const json mining = list["tradeSkills"][0];
    check(reply.status == 200 && sword["id"] == "SwordAbilityTable" && sword["level"] == 5 && sword["experience"] == 20
              && sword["nextLevel"] == 50 && sword["freePoints"] == 3 && sword["spentPoints"] == 1
              && sword["nodes"] == json::array({"Test_Sword_Slash"}) && sword["bar"] == json::array({"", "", "Test_Sword_Slash"})
              && list["weapons"][1]["id"] == "BowAbilityTable" && list["weapons"][1]["level"] == 3 && mining["id"] == "Mining"
              && mining["level"] == 4 && mining["experience"] == 20 && mining["nextLevel"] == 100 && mining["maxLevel"] == 5
              && list["tradeSkills"][1]["maxLevel"] == 0,
          "the list route gives each weapon (the level of the screen, the experience and its limit, the free and the "
          "spent points, the nodes, the bar) and each trade skill");
    const std::string before = rows_text(host.store, kCharacterA);
    check(send("POST", "/v1/dev/progress/mastery", forA + "&weapon=sword").status == 400
              && send("POST", "/v1/dev/progress/mastery", forA + "&weapon=sword&level=0").status == 400
              && send("POST", "/v1/dev/progress/mastery", forA + "&weapon=sword&level=16").status == 400
              && send("POST", "/v1/dev/progress/mastery", forA + "&weapon=sword&level=5&experience=50").status == 400
              && send("POST", "/v1/dev/progress/mastery", forA + "&weapon=axe&level=2").status == 400
              && send("POST", "/v1/dev/progress/mastery", forA + "&weapon=Mining&level=2").status == 400
              && send("POST", "/v1/dev/progress/tradeskill", forA + "&skill=sword&level=2").status == 400
              && send("POST", "/v1/dev/progress/tradeskill", forA + "&skill=Mining&level=6").status == 400
              && send("POST", "/v1/dev/progress/tradeskill", forA + "&skill=Riding&level=1").status == 400
              && send("POST", "/v1/dev/progress/skill-experience", forA + "&id=Mining&amount=0").status == 400
              && send("POST", "/v1/dev/progress/skill-experience", forA + "&id=Cooking&amount=5").status == 400,
          "the routes refuse a request with no level, the level 0 of a weapon, a level above the largest one, experience "
          "that is not below the limit of the level, a weapon or a skill id that is not known, an id of the other kind, "
          "and an amount of 0 (reply 400)");
    check(send("POST", "/v1/dev/progress/mastery", std::string("character=") + kCharacterNone + "&weapon=sword&level=2").status == 404
              && send("POST", "/v1/dev/progress/mastery", forA + "&weapon=sword&level=2", "gateway.test").status == 403
              && send("POST", "/v1/dev/progress/tradeskill", forA + "&skill=Mining&level=2", "gateway.test").status == 403
              && send("POST", "/v1/dev/progress/skill-experience", forA + "&id=Mining&amount=5", "gateway.test").status == 403
              && rows_text(host.store, kCharacterA) == before,
          "an unknown character gives the reply 404, a request with a service host the reply 403, and no request that "
          "was refused changed a row");
}

} // namespace

std::vector<Case> skill_cases() {
    return {
        {"K1", "wire bytes: the three skill states against the examples of the research; no fragment for an observer", wire_bytes},
        {"K2", "weapon mastery and trade skill: the rank, the experience, the mastery points, a lower rank", ranks_and_experience},
        {"K3", "client requests: mastery point (1112), apply (2857), ability bar (2701), tree reset (186) with its answer (2858)", client_requests},
        {"K4", "two sessions: the skills of A do not change B, and B gets no skill state of A", two_sessions},
        {"K5", "persistence: the skills after a new entry, in a new world object, and a write that fails", persistence},
        {"K6", "a large weapon tree set is one fragment: the client replaces its node list (review finding 2 of stage 1.6)", large_tree},
        {"K7", "development routes for the weapon mastery, the trade skills, and their experience", skill_routes},
    };
}

} // namespace soulwarden::backend::tests
