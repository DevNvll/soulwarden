#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The basic attacks of the melee weapons (stage 3.3b-M1, protocol notes 4.32): for each tag set of a melee weapon (the column
// MannequinTag of the weapon table, "2H_Melee,GreatAxe") the fragments of its attack graph, as the client plays them from its
// animation databases and action lists. Each fragment has:
//
//   - the transitions to the next fragment: a window of the clock of the fragment (seconds), its kind (buffered: a press in the
//     window starts the next fragment at the end of the window; instant: the first step in the window whose input fits), and
//     the input that it needs (gamedata::AttackInput);
//   - the strikes: the damage clips of one damage key (body and weapon attachment, each shape) with their window and the damage
//     row of each clip with the learned nodes that the clip needs and that it must not have;
//   - the payments (a stamina or a mana cost id at a time), the dodge windows, and the length of the fragment (the end of its
//     animation; 0: not known).
//
// The data has the values of the game files and no rule: the rules of the sequence are in the world (world/attack_sequence.h).
// The data is game data: the tool tools/gamedata/weapon-actions.sh makes the file from the game copy of the user, and the file
// stays outside the repository. Without the file the backend starts and works: each melee hit uses the light or the heavy row of
// the weapon table (the rule of stage 3.2a).

namespace soulwarden::backend::gamedata {

/** File name of the weapon actions in the game data directory of the backend. */
inline constexpr const char* kWeaponActionsFile = "weapon-actions.json";

/** The input that a transition needs, at the input step of the client (the conditions of the action lists of the player). */
enum class AttackInput : std::uint8_t {
    /** No condition (the full charge of a heavy attack, Test_AlwaysTrue). */
    always,
    /** CanAttack_Heavy, CanAttack_Primary (attack_primary_virtual): the attack input or its hold; a buffered window takes a press. */
    virtualInput,
    /** CanAttack_Primary_Rapier (attack_primary): the attack input (bit 555) only. */
    press,
    /** Attack_Heavy_NotHeld: neither the attack input nor its hold. */
    notHeld,
    /** Attack_Primary_Released: the release of the attack input (bit 567). */
    released,
};

enum class TransitionKind : std::uint8_t { buffered, instant };

struct AttackTransition {
    TransitionKind kind{TransitionKind::instant};
    /** The window on the clock of the fragment (seconds). `end` below 0: to the end of the fragment. */
    float start{0.0F};
    float end{-1.0F};
    AttackInput input{AttackInput::always};
    /** The next fragment: an index into WeaponActionSet::fragments. */
    std::uint32_t to{0};
    std::string toName;
    /** The name of the condition in the action lists (for the log). */
    std::string condition;
};

/** One damage row of a strike, with the learned nodes (progression ids) that the clip needs and that it must not have. */
struct StrikeRow {
    std::string row;
    std::vector<std::uint32_t> needs;
    std::vector<std::uint32_t> forbids;
    std::vector<std::string> needNames;
    std::vector<std::string> forbidNames;
};

/** One shape of a damage clip (the parameters of the clip; the world does not test them). */
struct AttackShape {
    std::string type;
    float radius{0.0F};
    float halfHeight{0.0F};
    float box[3]{};
    float offset[3]{};
    float scale[3]{};
    std::string joint;
};

/** The damage clips of one damage key of a fragment: one hit of each target. */
struct AttackStrike {
    /** The damage key in lower case ("atk1"). */
    std::string key;
    float start{0.0F};
    float end{0.0F};
    std::vector<StrikeRow> rows;
    std::vector<AttackShape> shapes;
};

struct AttackPayment {
    float time{0.0F};
    /** True: a mana cost (CAGE-PayManaCost); false: a stamina cost (PayStaminaCost). */
    bool mana{false};
    std::string cost;
    float multiplier{1.0F};
    /** The named condition of the clip (Greatsword_Offense_Stance_Active), or empty. */
    std::string condition;
};

/** A dodge window of a fragment (CanDodgeCancel: a dodge ends the fragment at once; CanDodgeBuffer: at the end of the window). */
struct DodgeWindow {
    float start{0.0F};
    float end{-1.0F};
    bool buffered{false};
};

struct AttackFragment {
    std::string name;
    /** The tags of the variant (for the log). */
    std::string tags;
    /** The end of the animation of the fragment (seconds). 0: not known. */
    float length{0.0F};
    /** The time of the named mark CanTransition. Below 0: none. */
    float canTransition{-1.0F};
    std::vector<AttackTransition> transitions;
    std::vector<AttackStrike> strikes;
    std::vector<AttackPayment> payments;
    std::vector<DodgeWindow> dodge;
};

/** The attack graph of one tag set of a melee weapon. */
struct WeaponActionSet {
    /** The tags in the case of the tables ("2H_Melee", "GreatAxe"), and the key: the lower-case tags, sorted, joined by ','. */
    std::vector<std::string> tags;
    std::string key;
    /** The first fragment (Attack_Start_Charge): an index into `fragments`. */
    std::uint32_t start{0};
    std::vector<AttackFragment> fragments;

    /** @return The index of the fragment with this name, or -1. */
    [[nodiscard]] std::int32_t fragment_index(std::string_view name) const;
};

struct WeaponActionTables {
    std::vector<WeaponActionSet> weapons;
};

/** @return The key of a tag text of the weapon table ("2H_Melee,GreatAxe" gives "2h_melee,greataxe"). */
[[nodiscard]] std::string weapon_tag_key(std::string_view tags);

class WeaponActions {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member of a wrong form, a number that
     * is not finite, a window with its end before its start, a transition to a fragment that the weapon does not have, a
     * strike with no row, two tag sets with one key, and a member "counts" with a number that is not the size of its list.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same check. @return False, with no data, when it is not correct. */
    [[nodiscard]] bool set(WeaponActionTables tables);

    [[nodiscard]] bool loaded() const {
        return !m_tables.weapons.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const WeaponActionTables& tables() const {
        return m_tables;
    }
    /** @return The attack graph of a weapon by the tag text of its weapon row (weapon_tag_key), or null. */
    [[nodiscard]] const WeaponActionSet* of_tags(std::string_view tags) const;

private:
    WeaponActionTables m_tables;
    std::unordered_map<std::string, std::size_t> m_index;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata
