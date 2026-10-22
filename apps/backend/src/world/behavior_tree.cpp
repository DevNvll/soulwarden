#include "world/behavior_tree.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::world::tree {
namespace {

using nlohmann::json;

/**
 * One number parameter of a node: its name in the file, its range, if it must be a whole number, and for a parameter that a file can leave
 * out its value then (stage BT2: the waits of the roam; a file of stage BT1 has a roam with no parameter).
 */
struct NumberSpec {
    const char* name;
    float lowest;
    float highest;
    bool whole{false};
    bool optional{false};
    float fallback{0.0F};
};

/** How a node has children in the file: none, one ("child"), a list ("children"), or the root of a different tree ("tree"). */
enum class Children : std::uint8_t { none, one, list, tree };

struct KindSpec {
    NodeKind kind;
    const char* name;
    Children children;
    /** The number parameters, in the sequence of their values (`param`). */
    std::vector<NumberSpec> numbers;
};

// The ranges are rules of this project (a decision of an agent, stage BT1): they keep a typing error of a file out of the world.
constexpr float kLongestTime = 600.0F;
// Code review of stage BT1, finding R6: a value of the blackboard is a 32-bit float, thus an int key keeps each whole number up to 2^24 exact.
constexpr double kLargestExactInteger = 16777216.0;
constexpr float kLongestPause = 60.0F;
constexpr float kFarthest = 200.0F;
const NumberSpec kBand[] = {{"far", 0.5F, kFarthest}, {"hold", 0.5F, kFarthest}, {"near", 0.0F, kFarthest}, {"middle", 0.0F, kFarthest},
                            {"repeat", 0.0F, kLongestTime}};

const std::vector<KindSpec>& specs() {
    static const std::vector<KindSpec> table = {
        {NodeKind::selector, "selector", Children::list, {}},
        {NodeKind::sequence, "sequence", Children::list, {}},
        {NodeKind::invert, "invert", Children::one, {}},
        {NodeKind::subtree, "subtree", Children::tree, {}},
        {NodeKind::in_fight, "in_fight", Children::none, {}},
        {NodeKind::target_is, "target_is", Children::none, {}},
        {NodeKind::check, "check", Children::none, {}},
        {NodeKind::keep_target, "keep_target", Children::none, {{"lose_distance", 1.0F, 1000.0F}}},
        {NodeKind::status_action, "status_action", Children::none, {{"repeat", 0.0F, kLongestTime}, {"pause", 0.0F, kLongestPause}}},
        {NodeKind::area_attack,
         "area_attack",
         Children::none,
         {{"repeat", 0.0F, kLongestTime}, {"repeat_area_only", 0.0F, kLongestTime}, {"hold_margin", 0.0F, 50.0F}, {"pause", 0.0F, kLongestPause}}},
        {NodeKind::basic_shot, "basic_shot", Children::none, {}},
        {NodeKind::melee,
         "melee",
         Children::none,
         {{"light_before_heavy", 1.0F, 20.0F, true}, {"pause", 0.0F, kLongestPause}, {"heavy_pause", 0.0F, kLongestPause}}},
        {NodeKind::turn_to_target, "turn_to_target", Children::none, {}},
        {NodeKind::roam,
         "roam",
         Children::none,
         {{"wait_shortest", 0.0F, kLongestTime, false, true, param::kDefaultWaitShortest},
          {"wait_longest", 0.0F, kLongestTime, false, true, param::kDefaultWaitLongest},
          {"no_way_pause", 0.0F, kLongestPause, false, true, param::kDefaultNoWayPause}}},
        {NodeKind::set, "set", Children::none, {}},
    };
    return table;
}

const KindSpec* spec_of(std::string_view name) {
    for (const KindSpec& spec : specs()) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

const KindSpec& spec_of(NodeKind kind) {
    return specs()[static_cast<std::size_t>(kind)];
}

bool valid_name(std::string_view name, bool path) {
    if (name.empty() || name.size() > 64 || name.front() == '/' || name.back() == '/') {
        return false;
    }
    char last = '/';
    for (const char letter : name) {
        const bool plain = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_';
        if (!plain && !(path && letter == '/' && last != '/')) {
            return false;
        }
        last = letter;
    }
    return true;
}

std::string number_text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

/** The checks of one file: each error has the place in the file (a JSON pointer without its first '/'). */
class Checker {
public:
    Checker(const std::vector<Key>& keys) : m_keys(keys) {}

    bool node(const json& value, const std::string& at, std::size_t depth) {
        if (depth >= kMaxDepth) {
            return fail(at, "the tree is deeper than " + std::to_string(kMaxDepth) + " levels");
        }
        if (!value.is_object()) {
            return fail(at, "a node must be an object with the member \"node\"");
        }
        const auto kindName = value.find("node");
        if (kindName == value.end() || !kindName->is_string()) {
            return fail(at, "a node must have the member \"node\" (the name of its kind)");
        }
        const KindSpec* spec = spec_of(kindName->get<std::string>());
        if (spec == nullptr) {
            return fail(at + "/node", "\"" + kindName->get<std::string>() + "\" is not a node kind (tree.schema.json has the kinds)");
        }
        std::vector<std::string> allowed{"node", "name"};
        for (const NumberSpec& number : spec->numbers) {
            allowed.emplace_back(number.name);
        }
        switch (spec->children) {
        case Children::one:
            allowed.emplace_back("child");
            break;
        case Children::list:
            allowed.emplace_back("children");
            break;
        case Children::tree:
            allowed.emplace_back("tree");
            break;
        case Children::none:
            break;
        }
        if (spec->kind == NodeKind::basic_shot) {
            allowed.insert(allowed.end(), {"archer", "caster", "retreat_pause", "pause"});
        } else if (spec->kind == NodeKind::status_action || spec->kind == NodeKind::area_attack) {
            allowed.emplace_back("action");
        } else if (spec->kind == NodeKind::target_is) {
            allowed.emplace_back("kind");
        } else if (spec->kind == NodeKind::check) {
            allowed.insert(allowed.end(), {"key", "op", "value"});
        } else if (spec->kind == NodeKind::set) {
            allowed.insert(allowed.end(), {"key", "value"});
        }
        for (const auto& [member, item] : value.items()) {
            if (std::find(allowed.begin(), allowed.end(), member) == allowed.end()) {
                return fail(at + "/" + member, "the node " + std::string(spec->name) + " has no member \"" + member + "\"");
            }
        }
        if (const auto label = value.find("name"); label != value.end() && (!label->is_string() || label->get<std::string>().size() > 64)) {
            return fail(at + "/name", "the name of a node must be a text of 64 characters or less");
        }
        for (const NumberSpec& number : spec->numbers) {
            if (!numberOf(value, number, at)) {
                return false;
            }
        }
        switch (spec->kind) {
        case NodeKind::basic_shot: {
            // Stage BT2: a node has the band of an archer, of a caster, or both; it fails for a slice whose band it does not have.
            std::size_t bands = 0;
            for (const char* band : {"archer", "caster"}) {
                const auto found = value.find(band);
                if (found == value.end()) {
                    continue;
                }
                if (!found->is_object()) {
                    return fail(at + "/" + band, std::string("the band \"") + band + "\" of the node basic_shot must be an object");
                }
                ++bands;
                for (const auto& [member, item] : found->items()) {
                    if (std::none_of(std::begin(kBand), std::end(kBand), [&](const NumberSpec& spec) { return member == spec.name; })) {
                        return fail(at + "/" + band + "/" + member, "a band has no member \"" + member + "\"");
                    }
                }
                for (const NumberSpec& number : kBand) {
                    if (!numberOf(*found, number, at + "/" + band)) {
                        return false;
                    }
                }
                const float far = (*found)["far"].get<float>();
                const float hold = (*found)["hold"].get<float>();
                const float near = (*found)["near"].get<float>();
                const float middle = (*found)["middle"].get<float>();
                if (!(near <= middle && middle <= hold && hold <= far)) {
                    return fail(at + "/" + band, "the distances of a band must be near <= middle <= hold <= far");
                }
            }
            if (bands == 0) {
                return fail(at, "the node basic_shot must have a band: \"archer\", \"caster\", or both (an object)");
            }
            if (!numberOf(value, NumberSpec{"retreat_pause", 0.0F, kLongestTime}, at) || !numberOf(value, NumberSpec{"pause", 0.0F, kLongestPause}, at)) {
                return false;
            }
            break;
        }
        case NodeKind::status_action:
        case NodeKind::area_attack:
            // Stage BT2: the name of the action of the slice that the node uses (no member: each one of the slice).
            if (const auto action = value.find("action");
                action != value.end() && (!action->is_string() || !valid_name(action->get<std::string>(), false))) {
                return fail(at + "/action", "the action must be the name of an action of the slice (letters, digits, and '_', 64 characters at most)");
            }
            break;
        case NodeKind::roam:
            if (value.value("wait_shortest", static_cast<double>(param::kDefaultWaitShortest))
                > value.value("wait_longest", static_cast<double>(param::kDefaultWaitLongest))) {
                return fail(at, "the waits of the roam must be wait_shortest <= wait_longest");
            }
            break;
        case NodeKind::target_is: {
            const auto kind = value.find("kind");
            if (kind == value.end() || !kind->is_string() || (*kind != "player" && *kind != "summon")) {
                return fail(at + "/kind", "the node target_is must have \"kind\": \"player\" or \"summon\"");
            }
            break;
        }
        case NodeKind::check:
        case NodeKind::set: {
            const Key* key = keyOf(value, at);
            if (key == nullptr) {
                return false;
            }
            if (spec->kind == NodeKind::check) {
                const auto op = value.find("op");
                static const char* const kOps[] = {"eq", "ne", "lt", "le", "gt", "ge"};
                if (op == value.end() || !op->is_string()
                    || std::none_of(std::begin(kOps), std::end(kOps), [&](const char* name) { return *op == name; })) {
                    return fail(at + "/op", "the node check must have \"op\": eq, ne, lt, le, gt, or ge");
                }
            }
            if (!valueOf(value, *key, at)) {
                return false;
            }
            break;
        }
        default:
            break;
        }
        ++m_nodes;
        if (m_nodes > kMaxNodes) {
            return fail(at, "the tree has more than " + std::to_string(kMaxNodes) + " nodes");
        }
        switch (spec->children) {
        case Children::one: {
            const auto found = value.find("child");
            if (found == value.end()) {
                return fail(at, std::string("the node ") + spec->name + " must have one \"child\"");
            }
            return node(*found, at + "/child", depth + 1);
        }
        case Children::list: {
            const auto found = value.find("children");
            if (found == value.end() || !found->is_array() || found->empty()) {
                return fail(at, std::string("the node ") + spec->name + " must have \"children\" (a list of one node or more)");
            }
            for (std::size_t index = 0; index < found->size(); ++index) {
                if (!node((*found)[index], at + "/children/" + std::to_string(index), depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        case Children::tree: {
            const auto found = value.find("tree");
            if (found == value.end() || !found->is_string() || !valid_name(found->get<std::string>(), true)) {
                return fail(at + "/tree", "the node subtree must have \"tree\": the name of a tree");
            }
            m_subtrees.push_back(found->get<std::string>());
            return true;
        }
        case Children::none:
            return true;
        }
        return true;
    }

    bool fail(const std::string& at, const std::string& cause) {
        m_error = (at.empty() ? std::string("the file") : at) + ": " + cause;
        return false;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] std::vector<std::string>& subtrees() {
        return m_subtrees;
    }

private:
    bool numberOf(const json& value, const NumberSpec& spec, const std::string& at) {
        const auto found = value.find(spec.name);
        if (found == value.end() && spec.optional) {
            return true;
        }
        if (found == value.end()) {
            return fail(at, std::string("the parameter \"") + spec.name + "\" is not there");
        }
        if (!found->is_number()) {
            return fail(at + "/" + spec.name, "the parameter must be a number");
        }
        const double number = found->get<double>();
        if (!std::isfinite(number) || number < spec.lowest || number > spec.highest) {
            return fail(at + "/" + spec.name, number_text(number) + " is not in the range " + number_text(spec.lowest) + " to "
                                                  + number_text(spec.highest));
        }
        if (spec.whole && std::floor(number) != number) {
            return fail(at + "/" + spec.name, "the parameter must be a whole number");
        }
        return true;
    }
    const Key* keyOf(const json& value, const std::string& at) {
        const auto found = value.find("key");
        if (found == value.end() || !found->is_string()) {
            fail(at + "/key", "the node must have \"key\": a key of the blackboard of its file");
            return nullptr;
        }
        for (const Key& key : m_keys) {
            if (key.name == found->get<std::string>()) {
                return &key;
            }
        }
        fail(at + "/key", "\"" + found->get<std::string>() + "\" is not a declared key of the blackboard of the file");
        return nullptr;
    }
    bool valueOf(const json& value, const Key& key, const std::string& at) {
        const auto found = value.find("value");
        if (found == value.end() || !(found->is_number() || found->is_boolean())) {
            return fail(at + "/value", "the node must have \"value\": a number (or true and false for a key of the type bool)");
        }
        const double number = found->is_boolean() ? (found->get<bool>() ? 1.0 : 0.0) : found->get<double>();
        if (!std::isfinite(number) || std::fabs(number) > 1.0e9) {
            return fail(at + "/value", "the value must be a finite number");
        }
        if (key.type == KeyType::integer && std::fabs(number) > kLargestExactInteger) {
            return fail(at + "/value", "an int value must be from -16777216 to 16777216 (a value of the blackboard is a 32-bit float)");
        }
        if ((key.type == KeyType::integer && std::floor(number) != number) || (key.type == KeyType::boolean && number != 0.0 && number != 1.0)) {
            return fail(at + "/value", "the value does not fit the type of the key \"" + key.name + "\"");
        }
        return true;
    }

    const std::vector<Key>& m_keys;
    std::size_t m_nodes{0};
    std::string m_error;
    std::vector<std::string> m_subtrees;
};

float value_of(const json& value) {
    return value.is_boolean() ? (value.get<bool>() ? 1.0F : 0.0F) : value.get<float>();
}

/** The compiler of one tree: it puts the nodes of the tree and of its subtrees into one table. */
class Compiler {
public:
    Compiler(CompiledTree& out, const std::map<std::string, TreeFile, std::less<>>& files) : m_out(out), m_files(files) {}

    bool tree(const TreeFile& file, std::size_t depth) {
        if (std::find(m_stack.begin(), m_stack.end(), file.name) != m_stack.end()) {
            return fail("the subtree \"" + file.name + "\" uses itself (a cycle: " + chain() + " -> " + file.name + ")");
        }
        // The keys of the file: one slot for each name in the compiled tree (a subtree can declare a key of its parent with the same type).
        std::vector<std::uint32_t> slots;
        for (const Key& key : file.keys) {
            const auto found = std::find_if(m_out.keys.begin(), m_out.keys.end(), [&](const Key& other) { return other.name == key.name; });
            if (found != m_out.keys.end()) {
                if (found->type != key.type) {
                    return fail("the key \"" + key.name + "\" of the tree \"" + file.name + "\" has a different type in a different tree of it");
                }
                slots.push_back(static_cast<std::uint32_t>(found - m_out.keys.begin()));
                continue;
            }
            if (m_out.keys.size() >= kMaxKeys) {
                return fail("the tree has more than " + std::to_string(kMaxKeys) + " keys of the blackboard");
            }
            slots.push_back(static_cast<std::uint32_t>(m_out.keys.size()));
            m_out.keys.push_back(key);
        }
        m_stack.push_back(file.name);
        m_slots.push_back(std::move(slots));
        m_keyNames.push_back(&file.keys);
        const bool done = node(json::parse(file.root), depth);
        m_stack.pop_back();
        m_slots.pop_back();
        m_keyNames.pop_back();
        return done;
    }

    [[nodiscard]] const std::string& error() const {
        return m_error;
    }

private:
    bool node(const json& value, std::size_t depth) {
        if (depth >= kMaxDepth) {
            return fail("the tree is deeper than " + std::to_string(kMaxDepth) + " levels (with its subtrees)");
        }
        if (m_out.nodes.size() >= kMaxNodes) {
            return fail("the tree has more than " + std::to_string(kMaxNodes) + " nodes (with its subtrees)");
        }
        const KindSpec& spec = *spec_of(value.at("node").get<std::string>());
        const std::size_t index = m_out.nodes.size();
        Node node;
        node.kind = spec.kind;
        node.depth = static_cast<std::uint8_t>(depth);
        node.param = static_cast<std::uint32_t>(m_out.params.size());
        if (const auto label = value.find("name"); label != value.end()) {
            node.label = label->get<std::string>();
        }
        for (const NumberSpec& number : spec.numbers) {
            const auto found = value.find(number.name);
            m_out.params.push_back(found != value.end() ? found->get<float>() : number.fallback);
        }
        switch (spec.kind) {
        case NodeKind::basic_shot:
            for (const char* band : {"archer", "caster"}) {
                const auto found = value.find(band);
                for (const NumberSpec& number : kBand) {
                    const bool first = &number == &kBand[0];
                    m_out.params.push_back(found != value.end() ? found->at(number.name).get<float>() : first ? param::kNoBand : 0.0F);
                }
            }
            m_out.params.push_back(value.at("retreat_pause").get<float>());
            m_out.params.push_back(value.at("pause").get<float>());
            break;
        case NodeKind::target_is:
            m_out.params.push_back(value.at("kind") == "player" ? 1.0F : 0.0F);
            break;
        case NodeKind::check:
        case NodeKind::set: {
            const std::string key = value.at("key").get<std::string>();
            const std::vector<Key>& names = *m_keyNames.back();
            const auto found = std::find_if(names.begin(), names.end(), [&](const Key& item) { return item.name == key; });
            m_out.params.push_back(static_cast<float>(m_slots.back()[static_cast<std::size_t>(found - names.begin())]));
            if (spec.kind == NodeKind::check) {
                static const char* const kOps[] = {"eq", "ne", "lt", "le", "gt", "ge"};
                const auto op = std::find_if(std::begin(kOps), std::end(kOps), [&](const char* name) { return value.at("op") == name; });
                m_out.params.push_back(static_cast<float>(op - std::begin(kOps)));
            }
            m_out.params.push_back(value_of(value.at("value")));
            break;
        }
        case NodeKind::subtree:
            node.label = value.at("tree").get<std::string>();
            break;
        case NodeKind::status_action:
        case NodeKind::area_attack:
            if (const auto action = value.find("action"); action != value.end()) {
                m_out.params.push_back(static_cast<float>(m_out.actions.size()));
                m_out.actions.push_back(NamedAction{static_cast<std::uint32_t>(index), spec.kind, action->get<std::string>()});
            } else {
                m_out.params.push_back(-1.0F);
            }
            break;
        default:
            break;
        }
        std::int32_t& first = m_out.firstOfKind[static_cast<std::size_t>(spec.kind)];
        if (first < 0) {
            first = static_cast<std::int32_t>(index);
        }
        if (spec.kind == NodeKind::melee && m_out.lightBeforeHeavy == 0) {
            m_out.lightBeforeHeavy = static_cast<std::uint32_t>(value.at("light_before_heavy").get<float>());
        }
        m_out.nodes.push_back(std::move(node));
        bool done = true;
        switch (spec.children) {
        case Children::one:
            done = this->node(value.at("child"), depth + 1);
            break;
        case Children::list:
            for (const json& child : value.at("children")) {
                if (!(done = this->node(child, depth + 1))) {
                    break;
                }
            }
            break;
        case Children::tree: {
            const std::string name = value.at("tree").get<std::string>();
            const auto file = m_files.find(name);
            if (file == m_files.end()) {
                return fail("the subtree \"" + name + "\" is not a tree of the set");
            }
            done = tree(file->second, depth + 1);
            break;
        }
        case Children::none:
            break;
        }
        if (!done) {
            return false;
        }
        m_out.nodes[index].end = static_cast<std::uint16_t>(m_out.nodes.size());
        return true;
    }

    std::string chain() const {
        std::string text;
        for (const std::string& name : m_stack) {
            text += (text.empty() ? "" : " -> ") + name;
        }
        return text;
    }
    bool fail(const std::string& cause) {
        if (m_error.empty()) {
            m_error = cause;
        }
        return false;
    }

    CompiledTree& m_out;
    const std::map<std::string, TreeFile, std::less<>>& m_files;
    std::vector<std::string> m_stack;
    std::vector<std::vector<std::uint32_t>> m_slots;
    std::vector<const std::vector<Key>*> m_keyNames;
    std::string m_error;
};

} // namespace

std::string_view kind_name(NodeKind kind) {
    return spec_of(kind).name;
}

std::string_view status_name(Status status) {
    switch (status) {
    case Status::idle:
        return "idle";
    case Status::success:
        return "success";
    case Status::failure:
        return "failure";
    case Status::running:
        return "running";
    }
    return "idle";
}

std::vector<std::pair<std::string, float>> parameters(const CompiledTree& tree, std::size_t index) {
    std::vector<std::pair<std::string, float>> out;
    const Node& node = tree.nodes[index];
    const float* values = tree.values(index);
    const KindSpec& spec = spec_of(node.kind);
    std::size_t place = 0;
    for (const NumberSpec& number : spec.numbers) {
        out.emplace_back(number.name, values[place++]);
    }
    switch (node.kind) {
    case NodeKind::basic_shot:
        for (const char* band : {"archer", "caster"}) {
            const bool has = values[place] != param::kNoBand;
            for (const NumberSpec& number : kBand) {
                if (has) {
                    out.emplace_back(std::string(band) + "." + number.name, values[place]);
                }
                ++place;
            }
        }
        out.emplace_back("retreat_pause", values[place++]);
        out.emplace_back("pause", values[place++]);
        break;
    case NodeKind::target_is:
        out.emplace_back("player", values[0]);
        break;
    case NodeKind::check:
        out.emplace_back("key", values[param::kCheckKey]);
        out.emplace_back("op", values[param::kCheckOp]);
        out.emplace_back("value", values[param::kCheckValue]);
        break;
    case NodeKind::set:
        out.emplace_back("key", values[param::kSetKey]);
        out.emplace_back("value", values[param::kSetValue]);
        break;
    case NodeKind::status_action:
    case NodeKind::area_attack:
        if (values[place] >= 0.0F) {
            out.emplace_back("action", values[place]);
        }
        break;
    default:
        break;
    }
    return out;
}

bool load_tree_file(std::string_view text, const std::string& source, TreeFile& out, std::string& error) {
    json document;
    try {
        // JSON with comments (the format of the tree files); a trailing comma is an error of the library (version 3.12.0).
        document = json::parse(text.begin(), text.end(), nullptr, true, true);
    } catch (const json::exception& failure) {
        // Each error of the JSON library (a parse error, a number that is too large: code review of stage BT1, finding R1) is a refusal.
        error = std::string("not JSON with comments: ") + failure.what();
        return false;
    }
    if (!document.is_object()) {
        error = "a tree file must have one object";
        return false;
    }
    for (const auto& [member, item] : document.items()) {
        if (member != "$schema" && member != "format" && member != "tree" && member != "blackboard" && member != "root") {
            error = member + ": a tree file has no member \"" + member + "\" (format, tree, blackboard, root, $schema)";
            return false;
        }
    }
    const auto format = document.find("format");
    // The JSON number is compared with the format, not a narrowed copy (code review of stage BT1, finding R7: 4294967297 was format 1).
    if (format == document.end() || !format->is_number_integer() || *format != kFormat) {
        error = "format: the file must have \"format\": " + std::to_string(kFormat);
        return false;
    }
    const auto name = document.find("tree");
    if (name == document.end() || !name->is_string() || !valid_name(name->get<std::string>(), true)) {
        error = "tree: the file must have \"tree\": a name (letters, digits, '_', parts with '/', 64 characters at most)";
        return false;
    }
    std::vector<Key> keys;
    if (const auto board = document.find("blackboard"); board != document.end()) {
        if (!board->is_object()) {
            error = "blackboard: the blackboard must be an object of keys";
            return false;
        }
        for (const auto& [member, item] : board->items()) {
            const std::string at = "blackboard/" + member;
            if (!valid_name(member, false)) {
                error = at + ": the name of a key must be letters, digits, and '_' (64 characters at most)";
                return false;
            }
            if (keys.size() >= kMaxKeys) {
                error = at + ": a file has " + std::to_string(kMaxKeys) + " keys at most";
                return false;
            }
            const auto type = item.is_object() ? item.find("type") : item.end();
            if (!item.is_object() || type == item.end() || !type->is_string() || (*type != "int" && *type != "float" && *type != "bool")) {
                error = at + ": a key must have \"type\": int, float, or bool";
                return false;
            }
            for (const auto& [field, unused] : item.items()) {
                if (field != "type" && field != "default") {
                    error = at + "/" + field + ": a key has no member \"" + field + "\" (type, default)";
                    return false;
                }
            }
            Key key;
            key.name = member;
            key.type = *type == "int" ? KeyType::integer : *type == "bool" ? KeyType::boolean : KeyType::real;
            if (const auto initial = item.find("default"); initial != item.end()) {
                const bool fits = key.type == KeyType::boolean ? initial->is_boolean()
                                  : key.type == KeyType::integer ? initial->is_number_integer()
                                                                 : initial->is_number();
                if (!fits || (initial->is_number() && std::fabs(initial->get<double>()) > 1.0e9)
                    || (key.type == KeyType::integer && std::fabs(initial->get<double>()) > kLargestExactInteger)) {
                    error = at + "/default: the default value does not fit the type of the key (an int: -16777216 to 16777216)";
                    return false;
                }
                key.initial = value_of(*initial);
            }
            keys.push_back(std::move(key));
        }
    }
    const auto root = document.find("root");
    if (root == document.end()) {
        error = "root: the file must have \"root\": the first node of the tree";
        return false;
    }
    Checker checker(keys);
    if (!checker.node(*root, "root", 0)) {
        error = checker.error();
        return false;
    }
    out.name = name->get<std::string>();
    out.keys = std::move(keys);
    out.source = source;
    out.root = root->dump();
    out.subtrees = std::move(checker.subtrees());
    return true;
}

std::shared_ptr<const CompiledTree> compile(const TreeFile& file, const std::map<std::string, TreeFile, std::less<>>& files, std::string& error) {
    auto compiled = std::make_shared<CompiledTree>();
    compiled->name = file.name;
    compiled->source = file.source;
    compiled->firstOfKind.fill(-1);
    Compiler compiler(*compiled, files);
    if (!compiler.tree(file, 0)) {
        error = "the tree \"" + file.name + "\" does not compile: " + compiler.error();
        return nullptr;
    }
    return compiled;
}

bool TreeSet::compile_all(const std::map<std::string, TreeFile, std::less<>>& files,
                          std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>>& out, std::string& error) {
    std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>> compiled;
    for (const auto& [name, file] : files) {
        std::shared_ptr<const CompiledTree> tree = compile(file, files, error);
        if (!tree) {
            return false;
        }
        compiled.emplace(name, std::move(tree));
    }
    out = std::move(compiled);
    return true;
}

bool TreeSet::add(std::string_view text, const std::string& source, std::string& error) {
    TreeFile file;
    if (!load_tree_file(text, source, file, error)) {
        return false;
    }
    std::map<std::string, TreeFile, std::less<>> files = m_files;
    files[file.name] = file;
    std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>> compiled;
    if (!compile_all(files, compiled, error)) {
        return false;
    }
    m_files = std::move(files);
    m_compiled = std::move(compiled);
    return true;
}

bool read_text_file(const std::filesystem::path& path, std::size_t limit, std::string& text, std::string& error) {
    // At most `limit` + 1 bytes: a larger file is refused with no read of the rest (code review of stage BT1, finding R5).
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "the file cannot be read";
        return false;
    }
    text.assign(limit + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    const auto count = static_cast<std::size_t>(input.gcount());
    if (input.bad()) {
        error = "the file cannot be read";
        return false;
    }
    if (count > limit) {
        error = "the file is larger than " + std::to_string(limit / (1024 * 1024)) + " MiB";
        return false;
    }
    text.resize(count);
    return true;
}

bool TreeSet::load(const std::string& folder, LoadReport& report) {
    m_files.clear();
    m_compiled.clear();
    std::string error;
    // Each file is read and checked first, then the set compiles: a subtree can be in a file after the file of its parent (code review of
    // stage BT1, finding R2). A file of the folder replaces the tree of the program of the same name.
    std::map<std::string, TreeFile, std::less<>> program;
    for (const EmbeddedFile& file : embedded_tree_files()) {
        if (file.name == kBindingFile) {
            continue; // The binding file (stage BT2, world/tree_binding.h).
        }
        const std::string source = std::string("embedded:") + file.name;
        TreeFile tree;
        if (!load_tree_file(file.text, source, tree, error)) {
            report.refused.push_back(source + ": " + error);
            continue;
        }
        program[tree.name] = std::move(tree);
    }
    std::map<std::string, TreeFile, std::less<>> files = program;
    if (!folder.empty()) {
        std::error_code failure;
        std::vector<std::filesystem::path> paths;
        if (!std::filesystem::is_directory(folder, failure)) {
            report.refused.push_back(folder + ": not a folder (the trees of the program stay)");
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(folder, failure)) {
                if (entry.is_regular_file() && entry.path().extension() == ".jsonc" && entry.path().filename().string() != kBindingFile) {
                    paths.push_back(entry.path());
                }
            }
        }
        std::sort(paths.begin(), paths.end());
        for (const std::filesystem::path& path : paths) {
            const std::string source = path.filename().string();
            std::string text;
            TreeFile tree;
            if (!read_text_file(path, kMaxFileBytes, text, error) || !load_tree_file(text, source, tree, error)) {
                report.refused.push_back(source + ": " + error);
                continue;
            }
            files[tree.name] = std::move(tree);
        }
    }
    // The compile. A tree that does not compile by a fault of its own file (not by a subtree that does not compile) is refused: the tree
    // of the program of its name comes back (or no tree), and the set compiles again. A cycle of trees that each fail refuses each one.
    for (std::size_t round = 0; round <= 2 * files.size() + 1; ++round) {
        std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>> compiled;
        std::map<std::string, std::string, std::less<>> failed;
        for (const auto& [name, file] : files) {
            std::shared_ptr<const CompiledTree> tree = compile(file, files, error);
            if (tree) {
                compiled.emplace(name, std::move(tree));
            } else {
                failed.emplace(name, error);
            }
        }
        if (failed.empty()) {
            m_files = std::move(files);
            m_compiled = std::move(compiled);
            break;
        }
        std::vector<std::string> own;
        for (const auto& [name, cause] : failed) {
            const std::vector<std::string>& subtrees = files.at(name).subtrees;
            if (std::none_of(subtrees.begin(), subtrees.end(), [&](const std::string& subtree) { return failed.count(subtree) != 0; })) {
                own.push_back(name);
            }
        }
        if (own.empty()) {
            for (const auto& [name, cause] : failed) {
                own.push_back(name);
            }
        }
        for (const std::string& name : own) {
            const TreeFile& file = files.at(name);
            report.refused.push_back(file.source + ": " + failed.at(name));
            const auto original = program.find(name);
            if (original != program.end() && original->second.source != file.source) {
                files[name] = original->second;
            } else {
                files.erase(name);
            }
        }
    }
    for (const auto& [name, tree] : m_compiled) {
        report.loaded.push_back(name + " (" + tree->source + ", " + std::to_string(tree->nodes.size()) + " nodes)");
    }
    return default_tree() != nullptr;
}

std::shared_ptr<const CompiledTree> TreeSet::find(std::string_view name) const {
    const auto found = m_compiled.find(name);
    return found == m_compiled.end() ? nullptr : found->second;
}

} // namespace soulwarden::backend::world::tree
